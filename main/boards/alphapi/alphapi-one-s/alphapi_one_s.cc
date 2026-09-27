#include "alphapi_one_audio_codec.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "config.h"
#include "display/lcd_display.h"
#include "esp_lcd_panel_vendor.h"
#include "wifi_board.h"

#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <algorithm>

#define TAG "AlphaPiOneS"

class AlphaPiOneS : public WifiBoard {
private:
    Button button_a_;
    Button button_b_;
    Button button_c_;
    Button button_d_;
    LcdDisplay* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializeSpi() {
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = DISPLAY_MOSI_PIN;
        bus_config.miso_io_num = DISPLAY_MISO_PIN;
        bus_config.sclk_io_num = DISPLAY_CLK_PIN;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RESET_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new SpiLcdDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                     DISPLAY_SWAP_XY);
    }

    void ShowVolume(int volume) {
        volume = std::clamp(volume, 0, 100);
        GetAudioCodec()->SetOutputVolume(volume);
        display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume / 10));
    }

    void InitializeButtons() {
        button_a_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        button_a_.OnLongPress([this]() { EnterWifiConfigMode(); });

        button_b_.OnClick([this]() {
            ShowVolume(GetAudioCodec()->output_volume() - 10);
        });
        button_b_.OnLongPress([this]() { ShowVolume(0); });

        button_c_.OnClick([this]() {
            ShowVolume(GetAudioCodec()->output_volume() + 10);
        });
        button_c_.OnLongPress([this]() { ShowVolume(100); });

        button_d_.OnClick([]() {
            Application::GetInstance().WakeWordInvoke("你好小智");
        });
    }

public:
    AlphaPiOneS()
        : button_a_(BUTTON_A_GPIO),
          button_b_(BUTTON_B_GPIO),
          button_c_(BUTTON_C_GPIO),
          button_d_(BUTTON_D_GPIO) {
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
        ESP_LOGI(TAG, "AlphaPi One S board initialized");
    }

    AudioCodec* GetAudioCodec() override {
        static AlphaPiOneAudioCodec codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_CODEC_UART_PORT, AUDIO_CODEC_UART_BAUD_RATE,
            AUDIO_CODEC_UART_TX_PIN, AUDIO_CODEC_UART_RX_PIN);
        return &codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN,
                                      DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(AlphaPiOneS);
