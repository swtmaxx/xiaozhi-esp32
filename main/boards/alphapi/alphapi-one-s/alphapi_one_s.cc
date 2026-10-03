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

// ===========================================================================
// ST7735 风格调参常量 —— 全部从 FW1 固件二进制逆向还原（DROM 0x24cb8~0x24cf3）
// 硬件为 ST7789 兼容屏，厂商在标准 ST7789 驱动之上叠加了这套调参序列。
// 缺少这套序列时屏幕能亮，但 gamma / 电源 / VCOM 参数不对，色彩会失真。
// 详见《屏幕驱动定案报告.md》
// ===========================================================================
static const uint8_t kFrameRate[]        = {0x01, 0x2C, 0x2D};   // 0xB1 / 0xB2
static const uint8_t kFrameRate3[]       = {0x01, 0x2C, 0x2D};   // 0xB3
static const uint8_t kInversionControl[] = {0x07};               // 0xB4（注意不是 0xB5）
static const uint8_t kPowerControl1[]    = {0xA2, 0x02, 0x84};   // 0xC0
static const uint8_t kPowerControl2[]    = {0xC5};               // 0xC1
static const uint8_t kPowerControl3[]    = {0x00, 0xC5};         // 0xC2
static const uint8_t kPowerControl4[]    = {0x2A, 0x0A};         // 0xC3
static const uint8_t kPowerControl5[]    = {0xEE, 0x8A};         // 0xC4
static const uint8_t kVcomControl[]      = {0x0E, 0x8A};         // 0xC5
static const uint8_t kInitialMadctl[]    = {0xC8};               // 0x36 MY=1,MX=1,MV=0,BGR=1
static const uint8_t kColorMode[]        = {0x05};               // 0x3A RGB565 16bit
static const uint8_t kColumnRange[]      = {0x00, 0x00, 0x00, 0x9F}; // 0x2A 列 0~159
static const uint8_t kRowRange[]         = {0x00, 0x00, 0x00, 0x7F}; // 0x2B 行 0~127
static const uint8_t kPositiveGamma[]    = {0x0F,0x1B,0x0F,0x17,0x33,0x2C,0x29,0x2E,
                                            0x30,0x30,0x39,0x3F,0x00,0x07,0x03,0x10}; // 0xE0
static const uint8_t kNegativeGamma[]    = {0x0F,0x1A,0x0F,0x18,0x2F,0x28,0x20,0x22,
                                            0x1F,0x1B,0x23,0x37,0x00,0x07,0x02,0x10}; // 0xE1

// 自定义 LCD 命令码。
// ESP-IDF 6 dropped the generic LCD_CMD_* definitions that shipped with the
// legacy panel headers, so define every command this board needs locally.
#define LCD_CMD_SWRESET 0x01
#define LCD_CMD_SLPOUT  0x11
#define LCD_CMD_NORON   0x13
#define LCD_CMD_INVOFF  0x20
#define LCD_CMD_DISPON  0x29
#define LCD_CMD_CASET   0x2A
#define LCD_CMD_RASET   0x2B
#define LCD_CMD_MADCTL  0x36
#define LCD_CMD_COLMOD  0x3A
#define LCD_CMD_FRMCTR1 0xB1
#define LCD_CMD_FRMCTR2 0xB2
#define LCD_CMD_FRMCTR3 0xB3
#define LCD_CMD_INVCTR  0xB4
#define LCD_CMD_PWCTR1  0xC0
#define LCD_CMD_PWCTR2  0xC1
#define LCD_CMD_PWCTR3  0xC2
#define LCD_CMD_PWCTR4  0xC3
#define LCD_CMD_PWCTR5  0xC4
#define LCD_CMD_VMCTR1  0xC5
#define LCD_CMD_GMCTRP1 0xE0
#define LCD_CMD_GMCTRN1 0xE1

// ---------------------------------------------------------------------------
// ST7735 风格调参序列（严格按 FW1 固件还原的 21 条 tx_param 顺序）
// 必须在 esp_lcd_new_panel_st7789() + esp_lcd_panel_init() 之后调用。
// 注意：MADCTL(0x36) 在此按固件原值 0xC8 下发，因此不能用
// esp_lcd_panel_mirror()/swap_xy()/invert_color() 覆盖——它们会重发 MADCTL
// 并丢掉 BGR 位。
// ---------------------------------------------------------------------------
static void InitializeSt7735Panel(esp_lcd_panel_io_handle_t io) {
    esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_panel_io_tx_param(io, LCD_CMD_FRMCTR1, kFrameRate, sizeof(kFrameRate));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_FRMCTR2, kFrameRate, sizeof(kFrameRate));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_FRMCTR3, kFrameRate3, sizeof(kFrameRate3));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_INVCTR, kInversionControl, sizeof(kInversionControl));

    esp_lcd_panel_io_tx_param(io, LCD_CMD_PWCTR1, kPowerControl1, sizeof(kPowerControl1));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_PWCTR2, kPowerControl2, sizeof(kPowerControl2));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_PWCTR3, kPowerControl3, sizeof(kPowerControl3));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_PWCTR4, kPowerControl4, sizeof(kPowerControl4));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_PWCTR5, kPowerControl5, sizeof(kPowerControl5));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_VMCTR1, kVcomControl, sizeof(kVcomControl));

    esp_lcd_panel_io_tx_param(io, LCD_CMD_INVOFF, NULL, 0);

    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, kInitialMadctl, sizeof(kInitialMadctl));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, kColorMode, sizeof(kColorMode));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, kColumnRange, sizeof(kColumnRange));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, kRowRange, sizeof(kRowRange));

    esp_lcd_panel_io_tx_param(io, LCD_CMD_GMCTRP1, kPositiveGamma, sizeof(kPositiveGamma));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_GMCTRN1, kNegativeGamma, sizeof(kNegativeGamma));

    esp_lcd_panel_io_tx_param(io, LCD_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_lcd_panel_io_tx_param(io, LCD_CMD_NORON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
}

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

        // 追加厂商自定义 ST7735 风格调参序列（从固件逆向还原）。
        // 该序列已包含 MADCTL(0x36)=0xC8 与 INVOFF(0x20)，因此不再调用
        // esp_lcd_panel_invert_color()/swap_xy()/mirror()——它们内部会重发
        // MADCTL 并把 BGR 位覆盖掉，导致红蓝通道对调。
        InitializeSt7735Panel(panel_io_);

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
