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

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <esp_lcd_panel_vendor.h>
#include <esp_io_expander_tca9554.h>
#include <driver/spi_common.h>
#include "i2c_device.h"
#include <esp_timer.h>
#include "power_manager.h"
#include "power_save_timer.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#define TAG "Spotpear_esp32_s3_lcd_1_54"

class Cst816d : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    Cst816d(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        uint8_t chip_id = ReadReg(0xA3);
        ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        read_buffer_ = new uint8_t[6];
    }

    ~Cst816d() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
};

class Spotpear_esp32_s3_lcd_1_54 : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Display* display_;
    esp_timer_handle_t touchpad_timer_;
    Cst816d* cst816d_;
    esp_io_expander_handle_t io_expander_ = NULL;
    esp_lcd_panel_handle_t panel_ = nullptr;

    PowerManager* power_manager_;
    PowerSaveTimer* power_save_timer_;
    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_41);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_3);
        rtc_gpio_set_direction(GPIO_NUM_3, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_3, 1);

        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            rtc_gpio_set_level(GPIO_NUM_3, 0);
            // 启用保持功能，确保睡眠期间电平不变
            rtc_gpio_hold_en(GPIO_NUM_3);
            esp_lcd_panel_disp_on_off(panel_, false); //关闭显示
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
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
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeCodecI2c_Touch() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = TP_PIN_NUM_TP_SDA,
            .scl_io_num = TP_PIN_NUM_TP_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

	// Tích hợp gesture: vuốt ngang (volume), vuốt dọc (độ sáng), chạm ngắn (toggle chat như cũ)
	static void touchpad_timer_callback(void* arg) {
		auto& board = (Spotpear_esp32_s3_lcd_1_54&)Board::GetInstance();
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

    void InitializeCst816DTouchPad() {
        ESP_LOGI(TAG, "Init Cst816D");
        cst816d_ = new Cst816d(i2c_bus_, 0x15);

        // 创建定时器，10ms 间隔
        esp_timer_create_args_t timer_args = {
            .callback = touchpad_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };

        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 10 * 1000)); // 10ms = 10000us
    }

    void EnableLcdCs() {
        if(io_expander_ != NULL) {
            esp_io_expander_set_level(io_expander_, DISPLAY_SPI_CS_PIN, 0);// 置低 LCD CS
        }
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_SPI_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 60 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        EnableLcdCs();
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));

        // uint8_t data_0xBB[] = { 0x3F };
        // esp_lcd_panel_io_tx_param(panel_io, 0xBB, data_0xBB, sizeof(data_0xBB));

        uint8_t data_0xBB[] = { 0x38 };
        esp_lcd_panel_io_tx_param(panel_io, 0xBB, data_0xBB, sizeof(data_0xBB));

        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

public:

    Spotpear_esp32_s3_lcd_1_54() :boot_button_(BOOT_BUTTON_GPIO){
        gpio_set_direction(TP_PIN_NUM_TP_INT, GPIO_MODE_INPUT);
        int level = gpio_get_level(TP_PIN_NUM_TP_INT);
        if (level == 1) {
            InitializeCodecI2c_Touch();
            InitializeCst816DTouchPad();
        }
        InitializePowerSaveTimer();
        InitializeCodecI2c();
        InitializeSpi();
        InitializePowerManager();
        InitializeSt7789Display();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();

    }

    virtual Led* GetLed() override {
        static SingleLed led_strip(BUILTIN_LED_GPIO);
        return &led_strip;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Cst816d* GetTouchpad() {
        return cst816d_;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(Spotpear_esp32_s3_lcd_1_54);
