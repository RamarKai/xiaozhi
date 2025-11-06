#include "dual_network_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "emotion_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#define TAG "hzx_dev_oled"

class hzx_dev_oled : public DualNetworkBoard
{
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display *display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    void InitializeDisplayI2c()
    {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display()
    {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new EmotionDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons()
    {
        boot_button_.OnClick([this]()
                             {
            auto& app = Application::GetInstance();
            if (GetNetworkType() == NetworkType::WIFI) {
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    // cast to WifiBoard
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }
            app.ToggleChatState(); });
        boot_button_.OnDoubleClick([this]()
                                   {
            auto& app = Application::GetInstance();
            // 允许在启动、WiFi配置、空闲状态下切换网络模式
            if (app.GetDeviceState() == kDeviceStateStarting || 
                app.GetDeviceState() == kDeviceStateWifiConfiguring || 
                app.GetDeviceState() == kDeviceStateIdle) {
                SwitchNetworkType();
            } });

        touch_button_.OnPressDown([this]()
                                  { Application::GetInstance().StartListening(); });
        touch_button_.OnPressUp([this]()
                                { Application::GetInstance().StopListening(); });

        volume_up_button_.OnClick([this]()
                                  {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume)); });

        volume_up_button_.OnLongPress([this]()
                                      {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME); });

        volume_down_button_.OnClick([this]()
                                    {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume)); });

        volume_down_button_.OnLongPress([this]()
                                        {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
            
            // 添加4G模块调试信息
            if (GetNetworkType() == NetworkType::ML307) {
                auto display = GetDisplay();
                display->ShowNotification("Checking 4G status...");
                
                ESP_LOGI(TAG, "Network type: ML307");
                ESP_LOGI(TAG, "Attempting to get modem status...");
                
                vTaskDelay(pdMS_TO_TICKS(2000));
                display->ShowNotification("4G debug check done");
            } });
    }

    void InitializeTools()
    {
        static LampController lamp(LAMP_GPIO);
    }

    void InitializeSdCard()
    {
#if SDCARD_SDMMC_ENABLED
        // SDMMC模式初始化
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

        // 配置引脚
        slot_config.clk = SDCARD_SDMMC_CLK_PIN;
        slot_config.cmd = SDCARD_SDMMC_CMD_PIN;
        slot_config.d0 = SDCARD_SDMMC_D0_PIN;
        slot_config.width = SDCARD_SDMMC_BUS_WIDTH;

        if (SDCARD_SDMMC_BUS_WIDTH == 4)
        {
            slot_config.d1 = SDCARD_SDMMC_D1_PIN;
            slot_config.d2 = SDCARD_SDMMC_D2_PIN;
            slot_config.d3 = SDCARD_SDMMC_D3_PIN;
        }

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = true,
        };

        sdmmc_card_t *card;
        esp_err_t ret = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

        if (ret == ESP_OK)
        {
            sdmmc_card_print_info(stdout, card);
            ESP_LOGI(TAG, "SD card mounted at %s (SDMMC)", SDCARD_MOUNT_POINT);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to mount SD card (SDMMC): %s", esp_err_to_name(ret));
        }

#elif SDCARD_SDSPI_ENABLED
        // SDSPI模式初始化
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = SDCARD_SPI_MOSI,
            .miso_io_num = SDCARD_SPI_MISO,
            .sclk_io_num = SDCARD_SPI_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
        };

        ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_initialize((spi_host_device_t)SDCARD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = SDCARD_SPI_CS;
        slot_config.host_id = (spi_host_device_t)SDCARD_SPI_HOST;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = true,
        };

        sdmmc_card_t *card;
        esp_err_t ret = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

        if (ret == ESP_OK)
        {
            sdmmc_card_print_info(stdout, card);
            ESP_LOGI(TAG, "SD card mounted at %s (SDSPI)", SDCARD_MOUNT_POINT);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to mount SD card (SDSPI): %s", esp_err_to_name(ret));
        }
#else
        ESP_LOGI(TAG, "SD card disabled (enable SDCARD_SDMMC_ENABLED or SDCARD_SDSPI_ENABLED)");
#endif
    }

public:
    hzx_dev_oled() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, GPIO_NUM_NC),
                     boot_button_(BOOT_BUTTON_GPIO),
                     touch_button_(TOUCH_BUTTON_GPIO),
                     volume_up_button_(VOLUME_UP_BUTTON_GPIO),
                     volume_down_button_(VOLUME_DOWN_BUTTON_GPIO)
    {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
        InitializeSdCard();
    }

    virtual Led *GetLed() override
    {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec *GetAudioCodec() override
    {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                              AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display *GetDisplay() override
    {
        return display_;
    }
};

DECLARE_BOARD(hzx_dev_oled);
