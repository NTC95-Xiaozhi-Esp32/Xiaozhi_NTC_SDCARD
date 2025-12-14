#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// ES3C28P 2.8" (ESP32-S3 + ILI9341 + CTP FT6x36/FT6336 + ES8311 + microSD) configuration

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <hal/lcd_types.h>

/* =========================
 * Audio
 * ========================= */
#define AUDIO_INPUT_SAMPLE_RATE   24000
#define AUDIO_OUTPUT_SAMPLE_RATE  24000

// I2S (ES8311) - phổ biến trên board 2.8"
#define AUDIO_I2S_GPIO_MCLK       GPIO_NUM_4   // MCLK
#define AUDIO_I2S_GPIO_BCLK       GPIO_NUM_5   // SCLK/BCLK
#define AUDIO_I2S_GPIO_WS         GPIO_NUM_7   // LRCK/WS
#define AUDIO_I2S_GPIO_DOUT       GPIO_NUM_6   // DOUT (ESP32 -> Codec)
#define AUDIO_I2S_GPIO_DIN        GPIO_NUM_8   // DIN  (Codec -> ESP32)

// PA enable (thường low=enable)
#define AUDIO_CODEC_PA_PIN        GPIO_NUM_1

// I2C (share bus with Touch)
#define AUDIO_CODEC_I2C_NUM       I2C_NUM_0
#define AUDIO_CODEC_I2C_SDA_PIN   GPIO_NUM_16
#define AUDIO_CODEC_I2C_SCL_PIN   GPIO_NUM_15

#ifndef ES8311_CODEC_DEFAULT_ADDR
#define ES8311_CODEC_DEFAULT_ADDR 0x18
#endif
#define AUDIO_CODEC_ES8311_ADDR   ES8311_CODEC_DEFAULT_ADDR

/* =========================
 * LED / Button
 * ========================= */
#define BUILTIN_LED_GPIO          GPIO_NUM_42
#define BOOT_BUTTON_GPIO          GPIO_NUM_0

/* =========================
 * Display (ILI9341 SPI)
 * ========================= */
#define DISPLAY_WIDTH             320
#define DISPLAY_HEIGHT            240
#define DISPLAY_MIRROR_X          false
#define DISPLAY_MIRROR_Y          false
#define DISPLAY_SWAP_XY           true

#define DISPLAY_OFFSET_X          0
#define DISPLAY_OFFSET_Y          0

#define DISPLAY_BACKLIGHT_PIN     GPIO_NUM_45
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// SPI pins (the names match your board .cc usage)
#define DISPLAY_SCK_PIN           GPIO_NUM_12
#define DISPLAY_MOSI_PIN          GPIO_NUM_11
#define DISPLAY_MIS0_PIN          GPIO_NUM_13
#define DISPLAY_CS_PIN            GPIO_NUM_10
#define DISPLAY_DC_PIN            GPIO_NUM_46
#define DISPLAY_RST_PIN           GPIO_NUM_NC

#define DISPLAY_RGB_ORDER         LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_INVERT_COLOR      true
#define DISPLAY_SPI_MODE          0
#define DISPLAY_SPI_SCLK_HZ       (20 * 1000 * 1000)

// LCD SPI host (keep consistent with your .cc)
#define LCD_SPI_HOST              SPI2_HOST

#define LCD_TYPE_ILI9341_SERIAL

/* =========================
 * microSD (SDMMC + optional SDSPI mapping)
 * ========================= */
// SDMMC (SDIO 4-bit)
#define CARD_SDMMC_CLK_GPIO       GPIO_NUM_38
#define CARD_SDMMC_CMD_GPIO       GPIO_NUM_40
#define CARD_SDMMC_D0_GPIO        GPIO_NUM_39
#define CARD_SDMMC_D1_GPIO        GPIO_NUM_41
#define CARD_SDMMC_D2_GPIO        GPIO_NUM_48
#define CARD_SDMMC_D3_GPIO        GPIO_NUM_47

// Optional: SDSPI mapping from the same signals (CS usually uses D3/DAT3)
#define CARD_SDSPI_HOST           SPI3_HOST
#define CARD_SDSPI_SCK_GPIO       CARD_SDMMC_CLK_GPIO
#define CARD_SDSPI_MOSI_GPIO      CARD_SDMMC_CMD_GPIO
#define CARD_SDSPI_MISO_GPIO      CARD_SDMMC_D0_GPIO
#define CARD_SDSPI_CS_GPIO        CARD_SDMMC_D3_GPIO

/* =========================
 * Touch (CTP - FT6x36/FT6336)
 * ========================= */
// Touch uses the same I2C bus (IO15/IO16) on your PCB
#define TP_I2C_ADDR               0x38

#define TP_PIN_NUM_TP_SDA         GPIO_NUM_16
#define TP_PIN_NUM_TP_SCL         GPIO_NUM_15
#define TP_PIN_NUM_TP_RST         GPIO_NUM_18   // nếu không nối -> GPIO_NUM_NC
#define TP_PIN_NUM_TP_INT         GPIO_NUM_17   // nếu không nối -> GPIO_NUM_NC

// Compatibility aliases used by your .cc
#define TP_SDA_PIN                TP_PIN_NUM_TP_SDA
#define TP_SCL_PIN                TP_PIN_NUM_TP_SCL
#define TP_RST_PIN                TP_PIN_NUM_TP_RST
#define TP_INT_PIN                TP_PIN_NUM_TP_INT

/* =========================
 * Power / Battery (optional)
 * ========================= */
#define POWER_CHARGE_LED_PIN      GPIO_NUM_NC
#define POWER_CHARGE_DETECT_PIN   GPIO_NUM_NC
#define POWER_ADC_UNIT            ADC_UNIT_1
#define POWER_ADC_CHANNEL         ADC_CHANNEL_0
#define BAT_ADC_GPIO              GPIO_NUM_9

/* =========================
 * UART header (optional)
 * ========================= */
#define UART0_RXD_GPIO            GPIO_NUM_43
#define UART0_TXD_GPIO            GPIO_NUM_44

/* =========================
 * Expansion IO (optional)
 * ========================= */
#define EXP_IO0_GPIO2             GPIO_NUM_2
#define EXP_IO1_GPIO3             GPIO_NUM_3
#define EXP_IO2_GPIO14            GPIO_NUM_14
#define EXP_IO3_GPIO21            GPIO_NUM_21

#endif // _BOARD_CONFIG_H_
