#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include "system_reset.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include "esp_lcd_ili9341.h"

#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <esp_timer.h>

#define TAG "FreenoveESP32S3Display"

/* =========================
 * FT6x36 / FT6336 Touch
 * (muma-style, polling)
 * ========================= */
class Ft6336Touch {
public:
    struct Point {
        int num = 0;
        int x = -1;
        int y = -1;
    };

    Ft6336Touch(i2c_master_bus_handle_t bus, uint8_t addr,
                gpio_num_t rst, gpio_num_t irq)
        : bus_(bus), addr_(addr), rst_(rst), irq_(irq) {}

    void Init() {
        // Reset nếu có
        if (rst_ != GPIO_NUM_NC) {
            gpio_set_direction(rst_, GPIO_MODE_OUTPUT);
            gpio_set_level(rst_, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(rst_, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        // IRQ chỉ để check idle, không dùng ISR
        if (irq_ != GPIO_NUM_NC) {
            gpio_set_direction(irq_, GPIO_MODE_INPUT);
        }

        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = addr_,
            .scl_speed_hz    = 400000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &cfg, &dev_));
    }

    void Update() {
        uint8_t reg = 0x02;
        uint8_t buf[5] = {};
        if (i2c_master_transmit_receive(dev_, &reg, 1, buf, 5, 50) != ESP_OK) {
            pt_ = {};
            return;
        }

        pt_.num = buf[0] & 0x0F;
        if (pt_.num > 0) {
            pt_.x = ((buf[1] & 0x0F) << 8) | buf[2];
            pt_.y = ((buf[3] & 0x0F) << 8) | buf[4];
        }
    }

    const Point& Get() const { return pt_; }

private:
    i2c_master_bus_handle_t bus_;
    i2c_master_dev_handle_t dev_{};
    uint8_t addr_;
    gpio_num_t rst_;
    gpio_num_t irq_;
    Point pt_;
};

/* =========================
 * Board
 * ========================= */
class FreenoveESP32S3Display : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_{nullptr};

    // I2C dùng chung: Audio + Touch
    i2c_master_bus_handle_t i2c_bus_{nullptr};

    Ft6336Touch* touch_{nullptr};
    esp_timer_handle_t touch_timer_{nullptr};

    /* ---------- I2C ---------- */
    void InitializeI2c() {
        i2c_master_bus_config_t cfg = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = { .enable_internal_pullup = 1 },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &i2c_bus_));
    }

    /* ---------- SPI ---------- */
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MIS0_PIN;
        buscfg.sclk_io_num = DISPLAY_SCK_PIN;
        buscfg.max_transfer_sz =
            DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);

        ESP_ERROR_CHECK(
            spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    /* ---------- LCD ---------- */
    void InitializeLcd() {
        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_cfg = {};
        io_cfg.cs_gpio_num = DISPLAY_CS_PIN;
        io_cfg.dc_gpio_num = DISPLAY_DC_PIN;
        io_cfg.spi_mode = DISPLAY_SPI_MODE;
        io_cfg.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_cfg.trans_queue_depth = 10;
        io_cfg.lcd_cmd_bits = 8;
        io_cfg.lcd_param_bits = 8;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &io));

        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = DISPLAY_RST_PIN;
        panel_cfg.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_cfg.bits_per_pixel = 16;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel,
                             DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(
            io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY);
    }

    /* ---------- Gesture (muma-style) ---------- */
    static void TouchTimerCb(void*) {
        auto& board =
            (FreenoveESP32S3Display&)Board::GetInstance();
        auto tp = board.touch_;

        static bool was_pressed = false;
        static int sx = 0, sy = 0;
        static int64_t t0 = 0;
        static bool is_swipe = false;
        static int brightness = 100;

        tp->Update();
        auto p = tp->Get();

        bool pressed = p.num > 0;
        int64_t now = esp_timer_get_time() / 1000;

        if (pressed && !was_pressed) {
            sx = p.x; sy = p.y;
            t0 = now;
            is_swipe = false;
        }
        else if (pressed && was_pressed && !is_swipe) {
            int dx = p.x - sx;
            int dy = p.y - sy;

            auto codec = board.GetAudioCodec();
            auto backlight = board.GetBacklight();

            if (abs(dx) > 50 && abs(dx) > abs(dy) * 3 / 2) {
                is_swipe = true;
                int v = codec->output_volume();
                codec->SetOutputVolume(dx > 0 ? v + 10 : v - 10);
            }
            else if (abs(dy) > 50 && abs(dy) > abs(dx) * 3 / 2) {
                is_swipe = true;
                brightness += (dy < 0 ? 10 : -10);
                brightness = std::max(10, std::min(100, brightness));
                backlight->SetBrightness(brightness);
            }
        }
        else if (!pressed && was_pressed) {
            if (!is_swipe && (now - t0) < 500) {
                Application::GetInstance().ToggleChatState();
            }
        }

        was_pressed = pressed;
    }

    void InitializeTouch() {
        touch_ = new Ft6336Touch(
            i2c_bus_, TP_I2C_ADDR, TP_RST_PIN, TP_INT_PIN);
        touch_->Init();

        esp_timer_create_args_t args = {};
        args.callback = TouchTimerCb;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "touch_timer";

        ESP_ERROR_CHECK(esp_timer_create(&args, &touch_timer_));
        ESP_ERROR_CHECK(
            esp_timer_start_periodic(touch_timer_, 10 * 1000));
    }

    /* ---------- Button ---------- */
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
    FreenoveESP32S3Display()
        : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeLcd();
        InitializeTouch();
        InitializeButtons();
        GetBacklight()->SetBrightness(100);
    }

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec codec(
            i2c_bus_, AUDIO_CODEC_I2C_NUM,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            true, true);
        return &codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
        static PwmBacklight bl(
            DISPLAY_BACKLIGHT_PIN,
            DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &bl;
    }
};

DECLARE_BOARD(FreenoveESP32S3Display);
