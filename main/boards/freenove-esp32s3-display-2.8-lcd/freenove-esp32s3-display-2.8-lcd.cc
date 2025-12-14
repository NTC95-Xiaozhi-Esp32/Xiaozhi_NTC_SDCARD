#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include "system_reset.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include "esp_lcd_ili9341.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <driver/spi_common.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "FreenoveESP32S3Display"

// ---- Touch config fallbacks ----
#ifndef TP_I2C_ADDR
#define TP_I2C_ADDR 0x38
#endif

#ifndef TP_RST_PIN
  #ifdef TP_PIN_NUM_TP_RST
    #define TP_RST_PIN TP_PIN_NUM_TP_RST
  #else
    #define TP_RST_PIN GPIO_NUM_NC
  #endif
#endif

#ifndef TP_INT_PIN
  #ifdef TP_PIN_NUM_TP_INT
    #define TP_INT_PIN TP_PIN_NUM_TP_INT
  #else
    #define TP_INT_PIN GPIO_NUM_NC
  #endif
#endif

static inline int clamp_i(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

/* =========================
 * Touch (FT6x36/FT6336 I2C)
 * ========================= */
class Ft6336Touch {
public:
  struct TouchPoint {
    int num = 0;
    int x = -1;
    int y = -1;
  };

  Ft6336Touch(i2c_master_bus_handle_t bus, uint8_t addr, gpio_num_t rst_pin, gpio_num_t int_pin)
      : bus_(bus), addr_(addr), rst_pin_(rst_pin), int_pin_(int_pin) {}

  void Init() {
    // RST (optional)
    if (rst_pin_ != GPIO_NUM_NC) {
      gpio_config_t io = {};
      io.pin_bit_mask = 1ULL << rst_pin_;
      io.mode = GPIO_MODE_OUTPUT;
      ESP_ERROR_CHECK(gpio_config(&io));

      gpio_set_level(rst_pin_, 0);
      vTaskDelay(pdMS_TO_TICKS(10));
      gpio_set_level(rst_pin_, 1);
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    // INT (optional) - dùng polling, không dùng ISR
    if (int_pin_ != GPIO_NUM_NC) {
      gpio_config_t io = {};
      io.pin_bit_mask = 1ULL << int_pin_;
      io.mode = GPIO_MODE_INPUT;
      io.pull_up_en = GPIO_PULLUP_ENABLE;
      io.pull_down_en = GPIO_PULLDOWN_DISABLE;
      ESP_ERROR_CHECK(gpio_config(&io));
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.device_address = addr_;
    dev_cfg.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &dev_cfg, &dev_));
  }

  void UpdateTouchPoint() {
    // FT6x36/FT6336:
    // 0x02 TD_STATUS
    // 0x03..0x06 Point1 (XH, XL, YH, YL)
    uint8_t reg = 0x02;
    uint8_t buf[5] = {0};

    esp_err_t err = i2c_master_transmit_receive(dev_, &reg, 1, buf, sizeof(buf), 50);
    if (err != ESP_OK) {
      tp_ = {};
      return;
    }

    int td = buf[0] & 0x0F;
    tp_.num = td;

    if (td <= 0) {
      tp_.x = -1;
      tp_.y = -1;
      return;
    }

    tp_.x = ((buf[1] & 0x0F) << 8) | buf[2];
    tp_.y = ((buf[3] & 0x0F) << 8) | buf[4];
  }

  const TouchPoint& GetTouchPoint() const { return tp_; }

private:
  i2c_master_bus_handle_t bus_;
  i2c_master_dev_handle_t dev_ = nullptr;
  uint8_t addr_;
  gpio_num_t rst_pin_;
  gpio_num_t int_pin_;
  TouchPoint tp_;
};

/* =========================
 * Board
 * ========================= */
class FreenoveESP32S3Display : public WifiBoard {
private:
  Button boot_button_;
  LcdDisplay* display_ = nullptr;

  // One I2C bus shared: codec + touch
  i2c_master_bus_handle_t i2c_bus_ = nullptr;

  Ft6336Touch* touch_ = nullptr;
  esp_timer_handle_t touch_timer_ = nullptr;

  void InitializeI2c() {
    i2c_master_bus_config_t cfg = {
        .i2c_port = AUDIO_CODEC_I2C_NUM,
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &i2c_bus_));
  }

  void InitializeSpi() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
    buscfg.miso_io_num = DISPLAY_MIS0_PIN;
    buscfg.sclk_io_num = DISPLAY_SCK_PIN;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
  }

  void InitializeLcdDisplay() {
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_CS_PIN;
    io_config.dc_gpio_num = DISPLAY_DC_PIN;
    io_config.spi_mode = DISPLAY_SPI_MODE;
    io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io));

    ESP_LOGI(TAG, "Install LCD driver ILI9341");
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RST_PIN;
    panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
    panel_config.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

    display_ = new SpiLcdDisplay(panel_io, panel,
                                 DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                 DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                 DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                 DISPLAY_SWAP_XY);
  }

	// Tích hợp gesture: vuốt ngang (volume), vuốt dọc (độ sáng), chạm ngắn (toggle chat như cũ)
	static void touchpad_timer_callback(void* arg) {
		auto& board = (FreenoveESP32S3Display&)Board::GetInstance();
		auto touchpad = board.GetTouchpad();

		static bool     was_touched         = false;
		static int64_t  touch_start_time_ms = 0;
		static int16_t  touch_start_x       = -1;
		static int16_t  touch_start_y       = -1;
		static bool     is_swiping          = false;
		static int      current_brightness  = 100;

		const int64_t TOUCH_THRESHOLD_MS     = 500;
		const int64_t SWIPE_MAX_DURATION_MS  = 1000;
		const int16_t SWIPE_THRESHOLD_PX     = 50;

		touchpad->UpdateTouchPoint();
		auto touch_point = touchpad->GetTouchPoint();

		bool    is_pressed = (touch_point.num > 0);
		int16_t current_x  = touch_point.x;
		int16_t current_y  = touch_point.y;

		int64_t now_ms = esp_timer_get_time() / 1000;

		// 🟢 Bắt đầu chạm
		if (is_pressed && !was_touched) {
			was_touched         = true;
			touch_start_time_ms = now_ms;
			touch_start_x       = current_x;
			touch_start_y       = current_y;
			is_swiping          = false;
		}

		// 🟡 Đang giữ tay: kiểm tra vuốt nếu chưa gán là swipe
		else if (is_pressed && was_touched && !is_swiping &&
				 touch_start_x >= 0 && touch_start_y >= 0 &&
				 current_x    >= 0 && current_y    >= 0) {

			int16_t dx = current_x - touch_start_x;
			int16_t dy = current_y - touch_start_y;
			int16_t adx = dx >= 0 ? dx : -dx;
			int16_t ady = dy >= 0 ? dy : -dy;
			int64_t duration_ms = now_ms - touch_start_time_ms;

			if (duration_ms < SWIPE_MAX_DURATION_MS) {
				auto& codec     = *board.GetAudioCodec();
				auto  display   = board.GetDisplay();
				auto  backlight = board.GetBacklight();

				// ✅ Vuốt ngang: điều chỉnh âm lượng
				if (adx > SWIPE_THRESHOLD_PX && adx > (ady * 3 / 2)) {
					is_swiping = true;

					int current_volume = codec.output_volume();
					int new_volume = current_volume;

					if (dx > 0) {
						// Vuốt sang phải → tăng volume
						new_volume = current_volume + 10;
						if (new_volume > 100) new_volume = 100;
						ESP_LOGI(TAG, "➡️ Vuốt PHẢI - Âm lượng: %d → %d", current_volume, new_volume);
					} else {
						// Vuốt sang trái → giảm volume
						new_volume = current_volume - 10;
						if (new_volume < 0) new_volume = 0;
						ESP_LOGI(TAG, "⬅️ Vuốt TRÁI - Âm lượng: %d → %d", current_volume, new_volume);
					}

					codec.SetOutputVolume(new_volume);
					if (display) {
						display->ShowNotification("Âm thanh: " + std::to_string(new_volume));
					}
				}

				// ✅ Vuốt dọc: điều chỉnh độ sáng
				else if (ady > SWIPE_THRESHOLD_PX && ady > (adx * 3 / 2)) {
					is_swiping = true;

					int new_brightness = current_brightness;
					if (dy < 0) {
						// Vuốt lên → tăng sáng
						new_brightness = current_brightness + 10;
						if (new_brightness > 100) new_brightness = 100;
						ESP_LOGI(TAG, "🔼 Vuốt LÊN - Độ sáng: %d → %d", current_brightness, new_brightness);
					} else {
						// Vuốt xuống → giảm sáng
						new_brightness = current_brightness - 10;
						if (new_brightness < 10) new_brightness = 10;
						ESP_LOGI(TAG, "🔽 Vuốt XUỐNG - Độ sáng: %d → %d", current_brightness, new_brightness);
					}

					backlight->SetBrightness(new_brightness);
					current_brightness = new_brightness;

					if (display) {
						display->ShowNotification("Độ sáng: " + std::to_string(new_brightness));
					}
				}
			}
		}

		// 🔴 Thả tay ra khỏi màn hình
		else if (!is_pressed && was_touched) {
			was_touched = false;
			int64_t touch_duration_ms = now_ms - touch_start_time_ms;

			if (!is_swiping && touch_duration_ms < TOUCH_THRESHOLD_MS) {
				auto& app = Application::GetInstance();
				if (app.GetDeviceState() == kDeviceStateStarting &&
					!WifiStation::GetInstance().IsConnected()) {
					board.ResetWifiConfiguration();
				}
				app.ToggleChatState();
			} else if (is_swiping) {
				ESP_LOGI(TAG, "Đã vuốt xong, không xử lý tap.");
			}

			is_swiping    = false;
			touch_start_x = -1;
			touch_start_y = -1;
		}
	}

  void InitializeTouch() {
    // Nếu có INT pin, một số board idle-high; nếu idle không đúng thì bỏ init để tránh treo I2C
    if (TP_INT_PIN != GPIO_NUM_NC) {
      gpio_set_direction(TP_INT_PIN, GPIO_MODE_INPUT);
      int level = gpio_get_level(TP_INT_PIN);
      if (level != 1) {
        ESP_LOGW(TAG, "Touch INT not idle-high (level=%d), skip init.", level);
        return;
      }
    }

    ESP_LOGI(TAG, "Init touch: addr=0x%02X, RST=%d, INT=%d", TP_I2C_ADDR, (int)TP_RST_PIN, (int)TP_INT_PIN);
    touch_ = new Ft6336Touch(i2c_bus_, (uint8_t)TP_I2C_ADDR, TP_RST_PIN, TP_INT_PIN);
    touch_->Init();

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = touchpad_timer_callback;
    timer_args.arg = nullptr;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "touch_timer";

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touch_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(touch_timer_, 10 * 1000)); // 10ms
  }

  void InitializeButtons() {
    boot_button_.OnClick([this]() {
      auto& app = Application::GetInstance();
      if (app.GetDeviceState() == kDeviceStateStarting &&
          !WifiStation::GetInstance().IsConnected()) {
        ResetWifiConfiguration();
      }
      app.ToggleChatState();
    });
  }

public:
  FreenoveESP32S3Display() : boot_button_(BOOT_BUTTON_GPIO) {
    InitializeI2c();
    InitializeSpi();
    InitializeLcdDisplay();
    InitializeTouch();
    InitializeButtons();
    GetBacklight()->SetBrightness(100);
  }

  virtual Led* GetLed() override {
    static SingleLed led(BUILTIN_LED_GPIO);
    return &led;
  }

  virtual AudioCodec* GetAudioCodec() override {
    static Es8311AudioCodec audio_codec(i2c_bus_, AUDIO_CODEC_I2C_NUM,
                                        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                        AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
                                        AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                                        AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR,
                                        true, true);
    return &audio_codec;
  }

  virtual Display* GetDisplay() override { return display_; }

  virtual Backlight* GetBacklight() override {
    static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    return &backlight;
  }

  Ft6336Touch* GetTouchpad() { return touch_; }
};

DECLARE_BOARD(FreenoveESP32S3Display);
