#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

//音频iis部分
#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_4  //MCLK
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_5  //SCK
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_6  //DIN
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_7  //LRC
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_8  //DOUT
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_1  //PA

//音频iic部分
#define AUDIO_CODEC_I2C_NUM      I2C_NUM_0
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_15
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_16
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

//boot引脚
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BUILTIN_LED_GPIO GPIO_NUM_42

//屏幕显示部分
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_45

#define DISPLAY_RST_PIN       GPIO_NUM_NC
#define DISPLAY_SCK_PIN       GPIO_NUM_12
#define DISPLAY_DC_PIN        GPIO_NUM_46
#define DISPLAY_CS_PIN        GPIO_NUM_10
#define DISPLAY_MOSI_PIN      GPIO_NUM_11
#define DISPLAY_MIS0_PIN      GPIO_NUM_13
#define DISPLAY_SPI_SCLK_HZ   (20 * 1000 * 1000)

#define LCD_SPI_HOST          SPI3_HOST

#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH         240
#define DISPLAY_HEIGHT        320
#define DISPLAY_MIRROR_X      true
#define DISPLAY_MIRROR_Y      false
#define DISPLAY_SWAP_XY       false
#define DISPLAY_INVERT_COLOR  true
#define DISPLAY_RGB_ORDER     LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X      0
#define DISPLAY_OFFSET_Y      0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE      0

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



#endif  // _BOARD_CONFIG_H_
