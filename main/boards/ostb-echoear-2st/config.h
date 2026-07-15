#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/* Pin assignments taken from the OSTB N16-R8 schematic (SpotPear wiki,
 * N16-R8-Schematic.pdf) and cross-checked against I2C probing of the board.
 * This clone is rerouted relative to the original ESP-VoCat. */

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_12
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_11
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_15
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_16
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_13
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_14
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_18

/* ST77916 QSPI display, 360x360 round LCD. Touch (same I2C bus as the
 * codecs, via 0R links) shares its reset line with the display on GPIO9. */
#define QSPI_PIN_NUM_LCD_CS      GPIO_NUM_3
#define QSPI_PIN_NUM_LCD_PCLK    GPIO_NUM_8
#define QSPI_PIN_NUM_LCD_DATA0   GPIO_NUM_4
#define QSPI_PIN_NUM_LCD_DATA1   GPIO_NUM_5
#define QSPI_PIN_NUM_LCD_DATA2   GPIO_NUM_6
#define QSPI_PIN_NUM_LCD_DATA3   GPIO_NUM_7
#define QSPI_PIN_NUM_LCD_RST     GPIO_NUM_9
#define QSPI_PIN_NUM_LCD_BL      GPIO_NUM_41
#define TP_PIN_NUM_INT           GPIO_NUM_42

#define QSPI_LCD_HOST            SPI2_HOST
#define QSPI_LCD_H_RES           (360)
#define QSPI_LCD_V_RES           (360)
#define QSPI_LCD_BIT_PER_PIXEL   (16)

#define DISPLAY_WIDTH            360
#define DISPLAY_HEIGHT           360
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0

#define DISPLAY_BACKLIGHT_PIN           QSPI_PIN_NUM_LCD_BL
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define BAT_DET_PIN              GPIO_NUM_17
#define SYSTEM_LED_GPIO          GPIO_NUM_46

#define BUILTIN_LED_GPIO         GPIO_NUM_NC
#define BOOT_BUTTON_GPIO         GPIO_NUM_0

#endif // _BOARD_CONFIG_H_
