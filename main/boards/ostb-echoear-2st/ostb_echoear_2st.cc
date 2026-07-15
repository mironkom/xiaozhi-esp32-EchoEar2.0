#include <driver/i2c_master.h>
#include <esp_log.h>

#include "application.h"
#include "button.h"
#include "codecs/box_audio_codec.h"
#include "config.h"
#include "wifi_board.h"

#define TAG "OstbEchoEar2st"

// OSTB EchoEar 2.0 — third-party redesign of the Espressif ESP-VoCat.
// The PCB is rerouted relative to the original (I2C moved to GPIO12/11),
// so this board skips ESP-VoCat's PCB version detection entirely.
// First bring-up iteration: WiFi + audio only; display, touch, IMU and
// charge chip are not initialized until their pins are reverse engineered.
class OstbEchoEar2st : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;

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
        InitializeButtons();
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
};

DECLARE_BOARD(OstbEchoEar2st);
