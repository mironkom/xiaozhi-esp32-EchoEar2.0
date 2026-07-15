#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st77916.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "adc_battery_monitor.h"
#include "application.h"
#include "backlight.h"
#include "button.h"
#include "codecs/box_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "display/emote_display.h"
#include "i2c_device.h"
#include "led/gpio_led.h"
#include "mcp_server.h"
#include "power_save_timer.h"
#include "wifi_board.h"

#define TAG "OstbEchoEar2st"

namespace {

std::string UrlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

const char* WmoToRussian(int code) {
    switch (code) {
        case 0: return "ясно";
        case 1: return "преимущественно ясно";
        case 2: return "переменная облачность";
        case 3: return "пасмурно";
        case 45: case 48: return "туман";
        case 51: case 53: case 55: return "морось";
        case 56: case 57: return "ледяная морось";
        case 61: return "небольшой дождь";
        case 63: return "дождь";
        case 65: return "сильный дождь";
        case 66: case 67: return "ледяной дождь";
        case 71: return "небольшой снег";
        case 73: return "снег";
        case 75: return "сильный снег";
        case 77: return "снежная крупа";
        case 80: case 81: case 82: return "ливень";
        case 85: case 86: return "снегопад";
        case 95: return "гроза";
        case 96: case 99: return "гроза с градом";
        default: return "без осадков";
    }
}

}  // namespace

// Panel init sequence carried over from ESP-VoCat (same 1.85" 360x360
// ST77916 round LCD module family).
static const st77916_lcd_init_cmd_t vendor_specific_init_yysj[] = {
    {0xF0, (uint8_t []){0x28}, 1, 0},
    {0xF2, (uint8_t []){0x28}, 1, 0},
    {0x73, (uint8_t []){0xF0}, 1, 0},
    {0x7C, (uint8_t []){0xD1}, 1, 0},
    {0x83, (uint8_t []){0xE0}, 1, 0},
    {0x84, (uint8_t []){0x61}, 1, 0},
    {0xF2, (uint8_t []){0x82}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x01}, 1, 0},
    {0xF1, (uint8_t []){0x01}, 1, 0},
    {0xB0, (uint8_t []){0x56}, 1, 0},
    {0xB1, (uint8_t []){0x4D}, 1, 0},
    {0xB2, (uint8_t []){0x24}, 1, 0},
    {0xB4, (uint8_t []){0x87}, 1, 0},
    {0xB5, (uint8_t []){0x44}, 1, 0},
    {0xB6, (uint8_t []){0x8B}, 1, 0},
    {0xB7, (uint8_t []){0x40}, 1, 0},
    {0xB8, (uint8_t []){0x86}, 1, 0},
    {0xBA, (uint8_t []){0x00}, 1, 0},
    {0xBB, (uint8_t []){0x08}, 1, 0},
    {0xBC, (uint8_t []){0x08}, 1, 0},
    {0xBD, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x80}, 1, 0},
    {0xC1, (uint8_t []){0x10}, 1, 0},
    {0xC2, (uint8_t []){0x37}, 1, 0},
    {0xC3, (uint8_t []){0x80}, 1, 0},
    {0xC4, (uint8_t []){0x10}, 1, 0},
    {0xC5, (uint8_t []){0x37}, 1, 0},
    {0xC6, (uint8_t []){0xA9}, 1, 0},
    {0xC7, (uint8_t []){0x41}, 1, 0},
    {0xC8, (uint8_t []){0x01}, 1, 0},
    {0xC9, (uint8_t []){0xA9}, 1, 0},
    {0xCA, (uint8_t []){0x41}, 1, 0},
    {0xCB, (uint8_t []){0x01}, 1, 0},
    {0xD0, (uint8_t []){0x91}, 1, 0},
    {0xD1, (uint8_t []){0x68}, 1, 0},
    {0xD2, (uint8_t []){0x68}, 1, 0},
    {0xF5, (uint8_t []){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t []){0x4F}, 1, 0},
    {0xDE, (uint8_t []){0x4F}, 1, 0},
    {0xF1, (uint8_t []){0x10}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t []){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t []){0x10}, 1, 0},
    {0xF3, (uint8_t []){0x10}, 1, 0},
    {0xE0, (uint8_t []){0x07}, 1, 0},
    {0xE1, (uint8_t []){0x00}, 1, 0},
    {0xE2, (uint8_t []){0x00}, 1, 0},
    {0xE3, (uint8_t []){0x00}, 1, 0},
    {0xE4, (uint8_t []){0xE0}, 1, 0},
    {0xE5, (uint8_t []){0x06}, 1, 0},
    {0xE6, (uint8_t []){0x21}, 1, 0},
    {0xE7, (uint8_t []){0x01}, 1, 0},
    {0xE8, (uint8_t []){0x05}, 1, 0},
    {0xE9, (uint8_t []){0x02}, 1, 0},
    {0xEA, (uint8_t []){0xDA}, 1, 0},
    {0xEB, (uint8_t []){0x00}, 1, 0},
    {0xEC, (uint8_t []){0x00}, 1, 0},
    {0xED, (uint8_t []){0x0F}, 1, 0},
    {0xEE, (uint8_t []){0x00}, 1, 0},
    {0xEF, (uint8_t []){0x00}, 1, 0},
    {0xF8, (uint8_t []){0x00}, 1, 0},
    {0xF9, (uint8_t []){0x00}, 1, 0},
    {0xFA, (uint8_t []){0x00}, 1, 0},
    {0xFB, (uint8_t []){0x00}, 1, 0},
    {0xFC, (uint8_t []){0x00}, 1, 0},
    {0xFD, (uint8_t []){0x00}, 1, 0},
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xFF, (uint8_t []){0x00}, 1, 0},
    {0x60, (uint8_t []){0x40}, 1, 0},
    {0x61, (uint8_t []){0x04}, 1, 0},
    {0x62, (uint8_t []){0x00}, 1, 0},
    {0x63, (uint8_t []){0x42}, 1, 0},
    {0x64, (uint8_t []){0xD9}, 1, 0},
    {0x65, (uint8_t []){0x00}, 1, 0},
    {0x66, (uint8_t []){0x00}, 1, 0},
    {0x67, (uint8_t []){0x00}, 1, 0},
    {0x68, (uint8_t []){0x00}, 1, 0},
    {0x69, (uint8_t []){0x00}, 1, 0},
    {0x6A, (uint8_t []){0x00}, 1, 0},
    {0x6B, (uint8_t []){0x00}, 1, 0},
    {0x70, (uint8_t []){0x40}, 1, 0},
    {0x71, (uint8_t []){0x03}, 1, 0},
    {0x72, (uint8_t []){0x00}, 1, 0},
    {0x73, (uint8_t []){0x42}, 1, 0},
    {0x74, (uint8_t []){0xD8}, 1, 0},
    {0x75, (uint8_t []){0x00}, 1, 0},
    {0x76, (uint8_t []){0x00}, 1, 0},
    {0x77, (uint8_t []){0x00}, 1, 0},
    {0x78, (uint8_t []){0x00}, 1, 0},
    {0x79, (uint8_t []){0x00}, 1, 0},
    {0x7A, (uint8_t []){0x00}, 1, 0},
    {0x7B, (uint8_t []){0x00}, 1, 0},
    {0x80, (uint8_t []){0x48}, 1, 0},
    {0x81, (uint8_t []){0x00}, 1, 0},
    {0x82, (uint8_t []){0x06}, 1, 0},
    {0x83, (uint8_t []){0x02}, 1, 0},
    {0x84, (uint8_t []){0xD6}, 1, 0},
    {0x85, (uint8_t []){0x04}, 1, 0},
    {0x86, (uint8_t []){0x00}, 1, 0},
    {0x87, (uint8_t []){0x00}, 1, 0},
    {0x88, (uint8_t []){0x48}, 1, 0},
    {0x89, (uint8_t []){0x00}, 1, 0},
    {0x8A, (uint8_t []){0x08}, 1, 0},
    {0x8B, (uint8_t []){0x02}, 1, 0},
    {0x8C, (uint8_t []){0xD8}, 1, 0},
    {0x8D, (uint8_t []){0x04}, 1, 0},
    {0x8E, (uint8_t []){0x00}, 1, 0},
    {0x8F, (uint8_t []){0x00}, 1, 0},
    {0x90, (uint8_t []){0x48}, 1, 0},
    {0x91, (uint8_t []){0x00}, 1, 0},
    {0x92, (uint8_t []){0x0A}, 1, 0},
    {0x93, (uint8_t []){0x02}, 1, 0},
    {0x94, (uint8_t []){0xDA}, 1, 0},
    {0x95, (uint8_t []){0x04}, 1, 0},
    {0x96, (uint8_t []){0x00}, 1, 0},
    {0x97, (uint8_t []){0x00}, 1, 0},
    {0x98, (uint8_t []){0x48}, 1, 0},
    {0x99, (uint8_t []){0x00}, 1, 0},
    {0x9A, (uint8_t []){0x0C}, 1, 0},
    {0x9B, (uint8_t []){0x02}, 1, 0},
    {0x9C, (uint8_t []){0xDC}, 1, 0},
    {0x9D, (uint8_t []){0x04}, 1, 0},
    {0x9E, (uint8_t []){0x00}, 1, 0},
    {0x9F, (uint8_t []){0x00}, 1, 0},
    {0xA0, (uint8_t []){0x48}, 1, 0},
    {0xA1, (uint8_t []){0x00}, 1, 0},
    {0xA2, (uint8_t []){0x05}, 1, 0},
    {0xA3, (uint8_t []){0x02}, 1, 0},
    {0xA4, (uint8_t []){0xD5}, 1, 0},
    {0xA5, (uint8_t []){0x04}, 1, 0},
    {0xA6, (uint8_t []){0x00}, 1, 0},
    {0xA7, (uint8_t []){0x00}, 1, 0},
    {0xA8, (uint8_t []){0x48}, 1, 0},
    {0xA9, (uint8_t []){0x00}, 1, 0},
    {0xAA, (uint8_t []){0x07}, 1, 0},
    {0xAB, (uint8_t []){0x02}, 1, 0},
    {0xAC, (uint8_t []){0xD7}, 1, 0},
    {0xAD, (uint8_t []){0x04}, 1, 0},
    {0xAE, (uint8_t []){0x00}, 1, 0},
    {0xAF, (uint8_t []){0x00}, 1, 0},
    {0xB0, (uint8_t []){0x48}, 1, 0},
    {0xB1, (uint8_t []){0x00}, 1, 0},
    {0xB2, (uint8_t []){0x09}, 1, 0},
    {0xB3, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0xD9}, 1, 0},
    {0xB5, (uint8_t []){0x04}, 1, 0},
    {0xB6, (uint8_t []){0x00}, 1, 0},
    {0xB7, (uint8_t []){0x00}, 1, 0},
    {0xB8, (uint8_t []){0x48}, 1, 0},
    {0xB9, (uint8_t []){0x00}, 1, 0},
    {0xBA, (uint8_t []){0x0B}, 1, 0},
    {0xBB, (uint8_t []){0x02}, 1, 0},
    {0xBC, (uint8_t []){0xDB}, 1, 0},
    {0xBD, (uint8_t []){0x04}, 1, 0},
    {0xBE, (uint8_t []){0x00}, 1, 0},
    {0xBF, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x10}, 1, 0},
    {0xC1, (uint8_t []){0x47}, 1, 0},
    {0xC2, (uint8_t []){0x56}, 1, 0},
    {0xC3, (uint8_t []){0x65}, 1, 0},
    {0xC4, (uint8_t []){0x74}, 1, 0},
    {0xC5, (uint8_t []){0x88}, 1, 0},
    {0xC6, (uint8_t []){0x99}, 1, 0},
    {0xC7, (uint8_t []){0x01}, 1, 0},
    {0xC8, (uint8_t []){0xBB}, 1, 0},
    {0xC9, (uint8_t []){0xAA}, 1, 0},
    {0xD0, (uint8_t []){0x10}, 1, 0},
    {0xD1, (uint8_t []){0x47}, 1, 0},
    {0xD2, (uint8_t []){0x56}, 1, 0},
    {0xD3, (uint8_t []){0x65}, 1, 0},
    {0xD4, (uint8_t []){0x74}, 1, 0},
    {0xD5, (uint8_t []){0x88}, 1, 0},
    {0xD6, (uint8_t []){0x99}, 1, 0},
    {0xD7, (uint8_t []){0x01}, 1, 0},
    {0xD8, (uint8_t []){0xBB}, 1, 0},
    {0xD9, (uint8_t []){0xAA}, 1, 0},
    {0xF3, (uint8_t []){0x01}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0x21, (uint8_t []){}, 0, 0},
    {0x11, (uint8_t []){}, 0, 0},
    {0x00, (uint8_t []){}, 0, 120},
};

// CST816S touch controller, carried over from ESP-VoCat. Interrupt-driven:
// the ISR releases a semaphore, the task reads the touch point over I2C.
class Cst816s : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };

    enum TouchEvent {
        TOUCH_NONE,
        TOUCH_PRESS,
        TOUCH_RELEASE,
        TOUCH_HOLD
    };

    Cst816s(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[6];
        touch_isr_mux_ = xSemaphoreCreateBinary();
        if (touch_isr_mux_ == NULL) {
            ESP_LOGE(TAG, "Failed to create touch semaphore");
        }
    }

    ~Cst816s() {
        delete[] read_buffer_;
        if (touch_isr_mux_ != NULL) {
            vSemaphoreDelete(touch_isr_mux_);
            touch_isr_mux_ = NULL;
        }
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    TouchEvent CheckTouchEvent() {
        bool is_touched = (tp_.num > 0);
        TouchEvent event = TOUCH_NONE;
        if (is_touched && !was_touched_) {
            event = TOUCH_PRESS;
            ESP_LOGI(TAG, "TOUCH PRESS - x: %d, y: %d", tp_.x, tp_.y);
        } else if (!is_touched && was_touched_) {
            event = TOUCH_RELEASE;
        } else if (is_touched && was_touched_) {
            event = TOUCH_HOLD;
        }
        was_touched_ = is_touched;
        return event;
    }

    bool WaitForTouchEvent(TickType_t timeout = portMAX_DELAY) {
        if (touch_isr_mux_ != NULL) {
            return xSemaphoreTake(touch_isr_mux_, timeout) == pdTRUE;
        }
        return false;
    }

    void NotifyTouchEvent() {
        if (touch_isr_mux_ != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(touch_isr_mux_, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
    bool was_touched_ = false;
    SemaphoreHandle_t touch_isr_mux_ = nullptr;
};

// OSTB EchoEar 2.0 — third-party redesign of the Espressif ESP-VoCat.
// The PCB is rerouted relative to the original (pin map from the vendor
// N16-R8 schematic), so this board skips ESP-VoCat's PCB version detection.
// Touch reset is wired to the display reset line (GPIO9); the touch
// controller only appears on the I2C bus after the panel is brought up.
class OstbEchoEar2st : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Display* display_ = nullptr;
    Backlight* backlight_ = nullptr;
    Cst816s* cst816s_ = nullptr;
    TaskHandle_t touch_task_handle_ = nullptr;
    AdcBatteryMonitor* battery_monitor_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;
    Button boot_button_;

    static void touch_isr_callback(void* arg) {
        Cst816s* touchpad = static_cast<Cst816s*>(arg);
        if (touchpad != nullptr) {
            touchpad->NotifyTouchEvent();
        }
    }

    static void touch_event_task(void* arg) {
        Cst816s* touchpad = static_cast<Cst816s*>(arg);
        if (touchpad == nullptr) {
            vTaskDelete(NULL);
            return;
        }
        while (true) {
            if (touchpad->WaitForTouchEvent()) {
                auto &app = Application::GetInstance();
                auto &board = (OstbEchoEar2st &)Board::GetInstance();
                touchpad->UpdateTouchPoint();
                if (touchpad->CheckTouchEvent() == Cst816s::TOUCH_RELEASE) {
                    if (app.GetDeviceState() == kDeviceStateStarting) {
                        board.EnterWifiConfigMode();
                    } else {
                        app.ToggleChatState();
                    }
                }
            }
        }
    }

    void InitializeCst816sTouchPad() {
        esp_err_t probe = i2c_master_probe(i2c_bus_, 0x15, 200);
        ESP_LOGI(TAG, "I2C probe: CST816S@0x15=%s", esp_err_to_name(probe));

        cst816s_ = new Cst816s(i2c_bus_, 0x15);
        xTaskCreatePinnedToCore(touch_event_task, "touch_task", 4 * 1024, cst816s_, 5, &touch_task_handle_, 1);

        const gpio_config_t int_gpio_config = {
            .pin_bit_mask = (1ULL << TP_PIN_NUM_INT),
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_ANYEDGE
        };
        gpio_config(&int_gpio_config);
        gpio_install_isr_service(0);
        gpio_intr_enable(TP_PIN_NUM_INT);
        gpio_isr_handler_add(TP_PIN_NUM_INT, touch_isr_callback, cst816s_);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Non-fatal codec probe: confirms the I2C wiring on the serial log
        // before BoxAudioCodec init, whose own errors are less specific.
        esp_err_t es8311 = i2c_master_probe(i2c_bus_, AUDIO_CODEC_ES8311_ADDR >> 1, 200);
        esp_err_t es7210 = i2c_master_probe(i2c_bus_, AUDIO_CODEC_ES7210_ADDR >> 1, 200);
        ESP_LOGI(TAG, "I2C probe: ES8311@0x18=%s ES7210@0x40=%s",
                 esp_err_to_name(es8311), esp_err_to_name(es7210));
    }

    void InitializeSpi() {
        const spi_bus_config_t bus_config = {
            .data0_io_num = QSPI_PIN_NUM_LCD_DATA0,
            .data1_io_num = QSPI_PIN_NUM_LCD_DATA1,
            .sclk_io_num = QSPI_PIN_NUM_LCD_PCLK,
            .data2_io_num = QSPI_PIN_NUM_LCD_DATA2,
            .data3_io_num = QSPI_PIN_NUM_LCD_DATA3,
            .max_transfer_sz = QSPI_LCD_H_RES * 80 * sizeof(uint16_t),
        };
        ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeSt77916Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));
        st77916_vendor_config_t vendor_config = {
            .init_cmds = vendor_specific_init_yysj,
            .init_cmds_size = sizeof(vendor_specific_init_yysj) / sizeof(st77916_lcd_init_cmd_t),
            .flags = {
                .use_qspi_interface = 1,
            },
        };
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

#if CONFIG_USE_EMOTE_MESSAGE_STYLE
        display_ = new emote::EmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#else
        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#endif
        backlight_ = new PwmBacklight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        backlight_->RestoreBrightness();
    }

    std::string WikiApiGet(const std::string& params) {
        auto http = GetNetwork()->CreateHttp(0);
        http->SetTimeout(8000);
        http->SetHeader("User-Agent", "xiaozhi-esp32/2.2.6 (ostb-echoear-2st)");
        std::string url = "https://ru.wikipedia.org/w/api.php?format=json&utf8=1&" + params;
        if (!http->Open("GET", url)) {
            http->Close();
            return "";
        }
        int status = http->GetStatusCode();
        std::string body = http->ReadAll();
        http->Close();
        return status == 200 ? body : "";
    }

    std::string DoWikipediaSearch(const std::string& query, int max_results) {
        std::string body = WikiApiGet("action=query&list=search&srlimit="
                                      + std::to_string(max_results) + "&srsearch=" + UrlEncode(query));
        if (body.empty()) {
            return "Ошибка: Википедия сейчас недоступна.";
        }

        cJSON* root = cJSON_Parse(body.c_str());
        if (root == nullptr) return "Ошибка: некорректный ответ Википедии.";
        cJSON* q = cJSON_GetObjectItem(root, "query");
        cJSON* search = q ? cJSON_GetObjectItem(q, "search") : nullptr;
        int n = search ? cJSON_GetArraySize(search) : 0;
        if (n == 0) {
            cJSON_Delete(root);
            return "В Википедии по запросу «" + query + "» ничего не найдено.";
        }

        int top_pageid = 0;
        std::string top_title;
        std::string others;
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(search, i);
            cJSON* title = cJSON_GetObjectItem(item, "title");
            if (!cJSON_IsString(title)) continue;
            if (i == 0) {
                top_title = title->valuestring;
                top_pageid = cJSON_GetObjectItem(item, "pageid")->valueint;
            } else {
                if (!others.empty()) others += "; ";
                others += title->valuestring;
            }
        }
        cJSON_Delete(root);

        // Pull the intro of the best match so the assistant has real content.
        std::string extract;
        body = WikiApiGet("action=query&prop=extracts&exintro=1&explaintext=1&redirects=1&pageids="
                          + std::to_string(top_pageid));
        if (!body.empty()) {
            root = cJSON_Parse(body.c_str());
            if (root != nullptr) {
                cJSON* pages = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "query"), "pages");
                cJSON* page = pages ? pages->child : nullptr;
                cJSON* ext = page ? cJSON_GetObjectItem(page, "extract") : nullptr;
                if (cJSON_IsString(ext)) extract = ext->valuestring;
                cJSON_Delete(root);
            }
        }
        constexpr size_t kMaxExtract = 1500;
        if (extract.size() > kMaxExtract) {
            size_t cut = kMaxExtract;
            while (cut > 0 && (extract[cut] & 0xC0) == 0x80) cut--;  // keep UTF-8 intact
            extract.resize(cut);
            extract += "…";
        }

        std::string out = "Статья: " + top_title + "\n";
        out += extract.empty() ? "(краткое описание недоступно)" : extract;
        if (!others.empty()) {
            out += "\n\nДругие статьи по теме: " + others;
        }
        return out;
    }

    std::string DoGetWeather(const std::string& city, int days) {
        // Geocode the city name first (Open-Meteo, free, no API key).
        auto http = GetNetwork()->CreateHttp(0);
        http->SetTimeout(8000);
        std::string geo_url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=ru&format=json&name="
                              + UrlEncode(city);
        if (!http->Open("GET", geo_url)) {
            http->Close();
            return "Ошибка: сервис геокодирования недоступен.";
        }
        std::string geo_body = http->ReadAll();
        http->Close();

        double lat = 0, lon = 0;
        std::string place = city;
        {
            cJSON* root = cJSON_Parse(geo_body.c_str());
            if (root == nullptr) return "Ошибка: некорректный ответ геокодирования.";
            cJSON* results = cJSON_GetObjectItem(root, "results");
            cJSON* first = results ? cJSON_GetArrayItem(results, 0) : nullptr;
            if (first == nullptr) {
                cJSON_Delete(root);
                return "Не нашёл город «" + city + "». Уточни название.";
            }
            lat = cJSON_GetObjectItem(first, "latitude")->valuedouble;
            lon = cJSON_GetObjectItem(first, "longitude")->valuedouble;
            cJSON* name = cJSON_GetObjectItem(first, "name");
            if (cJSON_IsString(name)) place = name->valuestring;
            cJSON_Delete(root);
        }

        char fc_url[512];
        snprintf(fc_url, sizeof(fc_url),
                 "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                 "&timezone=auto&wind_speed_unit=ms&forecast_days=%d", lat, lon, days);
        http = GetNetwork()->CreateHttp(0);
        http->SetTimeout(8000);
        if (!http->Open("GET", fc_url)) {
            http->Close();
            return "Ошибка: сервис погоды недоступен.";
        }
        std::string fc_body = http->ReadAll();
        http->Close();

        cJSON* root = cJSON_Parse(fc_body.c_str());
        if (root == nullptr) return "Ошибка: некорректный ответ сервиса погоды.";

        char line[192];
        std::string out = "Погода, " + place + ". ";
        cJSON* cur = cJSON_GetObjectItem(root, "current");
        if (cur != nullptr) {
            snprintf(line, sizeof(line),
                     "Сейчас %.0f°C (ощущается как %.0f°C), %s, ветер %.0f м/с, влажность %.0f%%. ",
                     cJSON_GetObjectItem(cur, "temperature_2m")->valuedouble,
                     cJSON_GetObjectItem(cur, "apparent_temperature")->valuedouble,
                     WmoToRussian(cJSON_GetObjectItem(cur, "weather_code")->valueint),
                     cJSON_GetObjectItem(cur, "wind_speed_10m")->valuedouble,
                     cJSON_GetObjectItem(cur, "relative_humidity_2m")->valuedouble);
            out += line;
        }
        cJSON* daily = cJSON_GetObjectItem(root, "daily");
        if (daily != nullptr) {
            cJSON* dates = cJSON_GetObjectItem(daily, "time");
            cJSON* codes = cJSON_GetObjectItem(daily, "weather_code");
            cJSON* tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
            cJSON* tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
            int n = dates ? cJSON_GetArraySize(dates) : 0;
            for (int i = 0; i < n; i++) {
                snprintf(line, sizeof(line), "%s: от %.0f до %.0f°C, %s. ",
                         cJSON_GetArrayItem(dates, i)->valuestring,
                         cJSON_GetArrayItem(tmin, i)->valuedouble,
                         cJSON_GetArrayItem(tmax, i)->valuedouble,
                         WmoToRussian(cJSON_GetArrayItem(codes, i)->valueint));
                out += line;
            }
        }
        cJSON_Delete(root);
        return out;
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        // Wikipedia only — the device has no general web search. If the user
        // asks to "search the internet", tell them you can't, but offer to
        // look it up in Wikipedia instead; don't silently substitute.
        mcp_server.AddTool("self.wikipedia_search",
            "Look something up in Russian Wikipedia: facts, definitions, people, history, "
            "science, places, technology, concepts. This is NOT a general web search — it "
            "only covers Wikipedia. If the user explicitly wants a general internet search or "
            "current news, tell them the device can't do that, but offer Wikipedia instead. "
            "Returns the intro of the best-matching article plus related article titles. "
            "Query works best in Russian.",
            PropertyList({
                Property("query", kPropertyTypeString),
                Property("max_results", kPropertyTypeInteger, 5, 1, 8)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                return DoWikipediaSearch(properties["query"].value<std::string>(),
                                         properties["max_results"].value<int>());
            });

        mcp_server.AddTool("self.get_weather",
            "Get current weather and forecast for a city (worldwide, city name in any language). "
            "Returns temperature, conditions, wind, humidity and a daily forecast, in Russian.",
            PropertyList({
                Property("city", kPropertyTypeString),
                Property("days", kPropertyTypeInteger, 2, 1, 7)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                return DoGetWeather(properties["city"].value<std::string>(),
                                    properties["days"].value<int>());
            });
    }

    void InitializePowerSaveTimer() {
        // Visual-only sleep after 2 min idle: the cat closes its eyes and the
        // backlight dims. cpu_max_freq=-1 keeps wake word detection running,
        // so "小智" (or a tap) still wakes it; the app then raises the power
        // save level and SetPowerSaveLevel() below exits sleep.
        power_save_timer_ = new PowerSaveTimer(-1, 120);
        power_save_timer_->OnEnterSleepMode([this]() {
            if (display_ != nullptr) {
                display_->SetEmotion("sleepy");
            }
            if (backlight_ != nullptr) {
                backlight_->SetBrightness(5);
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            if (display_ != nullptr) {
                // EmoteDisplay remaps neutral to the dozing idle loop in standby
                display_->SetEmotion("neutral");
            }
            if (backlight_ != nullptr) {
                backlight_->RestoreBrightness();
            }
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                ESP_LOGI(TAG, "Boot button pressed, enter WiFi configuration mode");
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    OstbEchoEar2st() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        // Touch shares the display reset line, so the panel must be brought
        // up first — only then does the CST816S respond on the bus.
        InitializeSt77916Display();
        InitializeCst816sTouchPad();
        InitializeButtons();
        InitializeTools();
        InitializePowerSaveTimer();
        // VBAT via a 100K/100K divider on GPIO17 (ADC2_CH6); the charge
        // detect net from the schematic is not mapped to a GPIO yet.
        battery_monitor_ = new AdcBatteryMonitor(ADC_UNIT_2, ADC_CHANNEL_6, 100000, 100000, GPIO_NUM_NC);
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        // The app raises the level on any activity (wake word, touch chat,
        // server events) — treat that as "wake the sleeping cat".
        if (level != PowerSaveLevel::LOW_POWER && power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (battery_monitor_ == nullptr) {
            return false;
        }
        level = battery_monitor_->GetBatteryLevel();
        charging = battery_monitor_->IsCharging();
        discharging = battery_monitor_->IsDischarging();
        return true;
    }

    virtual Led* GetLed() override {
        // SYSTEM_LED drives the green LED through an NPN transistor (active high).
        static GpioLed led(SYSTEM_LED_GPIO);
        return &led;
    }
};

DECLARE_BOARD(OstbEchoEar2st);
