#include "sd_mount.h"

#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_defs.h>
#include <sdmmc_cmd.h>
#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <cstring>
#include <nvs.h>
#include <nvs_flash.h>

#define TAG "SDMOUNT"

// Lưu card để unmount
static sdmmc_card_t* s_card = nullptr;
// Để biết lần mount gần nhất dùng SPI hay SDMMC
static bool s_last_mount_spi = false;
// Lưu host.slot để spi_bus_free
static int s_spi_host_slot = -1;

// ========================================================
// SdMount class
// ========================================================

SdMount::SdMount()
    : mounted_(false),
      last_detect_state_(false),
      mount_point_("/sdcard"),
      card_name_("") {
    info_ = {};
}

SdMount::~SdMount() {
    Deinit();
}

SdMount& SdMount::GetInstance() {
    static SdMount instance;
    return instance;
}

// --------------------------------------------------------
// Đọc cấu hình SD từ NVS ("wifi")
//  - Nếu không có NVS → giữ defaults trong class
//  - Chỉ ghi đè những key tồn tại, phần còn lại giữ default
// --------------------------------------------------------
esp_err_t SdMount::LoadConfigFromNvs() {
    
    sd_mode_ = 0;          // 0 = 1-bit, 1 = 4-bit, 2 = SPI
    sd_clk_  = -1;
    sd_cmd_  = -1;
    sd_d0_   = -1;
    sd_d1_   = -1;
    sd_d2_   = -1;
    sd_d3_   = -1;

    spi_sck_  = -1;
    spi_miso_ = -1;
    spi_mosi_ = -1;
    spi_cs_   = -1;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG,
                 "No SD config in NVS, using defaults: mode=%d CLK=%d CMD=%d D0=%d D3=%d",
                 sd_mode_, sd_clk_, sd_cmd_, sd_d0_, sd_d3_);
        return err;
    }

    int32_t v = 0;
    if (nvs_get_i32(nvs, "sd_mode", &v) == ESP_OK) sd_mode_ = v;

    if (nvs_get_i32(nvs, "sd_clk", &v) == ESP_OK)  sd_clk_  = v;
    if (nvs_get_i32(nvs, "sd_cmd", &v) == ESP_OK)  sd_cmd_  = v;
    if (nvs_get_i32(nvs, "sd_d0",  &v) == ESP_OK)  sd_d0_   = v;
    if (nvs_get_i32(nvs, "sd_d1",  &v) == ESP_OK)  sd_d1_   = v;
    if (nvs_get_i32(nvs, "sd_d2",  &v) == ESP_OK)  sd_d2_   = v;
    if (nvs_get_i32(nvs, "sd_d3",  &v) == ESP_OK)  sd_d3_   = v;

    if (nvs_get_i32(nvs, "spi_sck",  &v) == ESP_OK) spi_sck_  = v;
    if (nvs_get_i32(nvs, "spi_miso", &v) == ESP_OK) spi_miso_ = v;
    if (nvs_get_i32(nvs, "spi_mosi", &v) == ESP_OK) spi_mosi_ = v;
    if (nvs_get_i32(nvs, "spi_cs",   &v) == ESP_OK) spi_cs_   = v;

    nvs_close(nvs);

    ESP_LOGI(TAG,
             "SD config from NVS: mode=%d "
             "SDMMC(CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d) "
             "SPI(SCK=%d MISO=%d MOSI=%d CS=%d)",
             sd_mode_,
             sd_clk_, sd_cmd_, sd_d0_, sd_d1_, sd_d2_, sd_d3_,
             spi_sck_, spi_miso_, spi_mosi_, spi_cs_);

    return ESP_OK;
}

esp_err_t SdMount::Init() {
    ESP_LOGI(TAG, "Init SD...");

    LoadConfigFromNvs();

    // Config detect pin nếu có
    if (sd_d3_ >= 0) {
        gpio_set_direction((gpio_num_t)sd_d3_, GPIO_MODE_INPUT);
        gpio_pullup_en((gpio_num_t)sd_d3_);
    }
    last_detect_state_ = false;

    // Thử mount ngay lần đầu
    Loop();
    return ESP_OK;
}

esp_err_t SdMount::ReinitFromNvs() {
    ESP_LOGI(TAG, "Re-init SD from NVS...");

    // Tháo SD hiện tại + tắt host
    Deinit();

    // Load lại config
    LoadConfigFromNvs();

    // Config detect pin
    if (sd_d3_ >= 0) {
        gpio_set_direction((gpio_num_t)sd_d3_, GPIO_MODE_INPUT);
        gpio_pullup_en((gpio_num_t)sd_d3_);
    }
    last_detect_state_ = false;

    // Thử mount
    Loop();

    return mounted_ ? ESP_OK : ESP_FAIL;
}

// --------------------------------------------------------
// DetectInserted()
//  - Nếu không cấu hình D3 (sd_d3_ < 0) → luôn coi như CÓ thẻ
//    (không auto-unmount khi rút)
//  - Nếu có D3     → đọc mức logic
//    (board cũ: level=1 khi CÓ thẻ)
// --------------------------------------------------------
bool SdMount::DetectInserted() {
    if (sd_d3_ < 0) {
        // Không có detect pin → luôn thử mount một lần
        if (!last_detect_state_) {
            ESP_LOGI(TAG, "No SD detect pin configured → assume INSERTED once.");
            last_detect_state_ = true;
        }
        return true;
    }

    int level = gpio_get_level((gpio_num_t)sd_d3_);
    bool inserted = (level == 1);

    if (inserted != last_detect_state_) {
        ESP_LOGI(TAG, "SD detect change: level=%d → %s",
                 level, inserted ? "INSERTED" : "REMOVED");
        last_detect_state_ = inserted;
    }

    return inserted;
}

// --------------------------------------------------------
// Mount SDMMC 1-bit
// --------------------------------------------------------
esp_err_t SdMount::MountSdmmc1Bit() {
    ESP_LOGI(TAG, "Mount SDMMC 1-bit...");

    if (sd_clk_ < 0 || sd_cmd_ < 0 || sd_d0_ < 0) {
        ESP_LOGW(TAG,
                 "SDMMC 1-bit pins invalid (CLK=%d CMD=%d D0=%d) → skip",
                 sd_clk_, sd_cmd_, sd_d0_);
        return ESP_ERR_INVALID_ARG;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk   = (gpio_num_t)sd_clk_;
    slot.cmd   = (gpio_num_t)sd_cmd_;
    slot.d0    = (gpio_num_t)sd_d0_;
    slot.d1    = (gpio_num_t)sd_d1_;
    slot.d2    = (gpio_num_t)sd_d2_;
    slot.d3    = (gpio_num_t)sd_d3_;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card = nullptr;
    esp_err_t ret;

    ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init fail: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init_slot fail: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    ret = esp_vfs_fat_sdmmc_mount(mount_point_.c_str(), &host, &slot, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdmmc_mount fail: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    mounted_ = true;
    s_card   = card;
    s_last_mount_spi = false;

    card_name_ = std::string(card->cid.name, 5);
    info_.capacityMB = (card->csd.capacity) / (1024 * 1024);

    const sdmmc_cid_t& c = card->cid;

    ESP_LOGI(TAG, "===== SD CID (SDMMC 1-bit) =====");
    ESP_LOGI(TAG, "MID: 0x%02X", c.mfg_id);
    char oem0 = (c.oem_id >> 8) & 0xFF;
    char oem1 = (c.oem_id >> 0) & 0xFF;
    ESP_LOGI(TAG, "OEM: %c%c", oem0, oem1);
    char pnm[6]; memcpy(pnm, c.name, 5); pnm[5] = 0;
    ESP_LOGI(TAG, "Product: %s", pnm);
    ESP_LOGI(TAG, "Revision: %d.%d",
             (c.revision >> 4) & 0x0F,
             c.revision & 0x0F);
    ESP_LOGI(TAG, "Serial: 0x%08X", c.serial);
    int month = c.date & 0x0F;
    int year  = 2000 + ((c.date >> 4) & 0xFF);
    ESP_LOGI(TAG, "Date: %02d/%04d", month, year);

    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "SD mounted OK (SDMMC 1-bit, %s)", card_name_.c_str());
    return ESP_OK;
}

// --------------------------------------------------------
// Mount SDMMC 4-bit
// --------------------------------------------------------
esp_err_t SdMount::MountSdmmc4Bit() {
    ESP_LOGI(TAG, "Mount SDMMC 4-bit...");

    if (sd_clk_ < 0 || sd_cmd_ < 0 ||
        sd_d0_  < 0 || sd_d1_  < 0 ||
        sd_d2_  < 0 || sd_d3_  < 0) {
        ESP_LOGW(TAG,
                 "SDMMC 4-bit pins invalid "
                 "(CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d) → skip",
                 sd_clk_, sd_cmd_, sd_d0_, sd_d1_, sd_d2_, sd_d3_);
        return ESP_ERR_INVALID_ARG;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk   = (gpio_num_t)sd_clk_;
    slot.cmd   = (gpio_num_t)sd_cmd_;
    slot.d0    = (gpio_num_t)sd_d0_;
    slot.d1    = (gpio_num_t)sd_d1_;
    slot.d2    = (gpio_num_t)sd_d2_;
    slot.d3    = (gpio_num_t)sd_d3_;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card = nullptr;
    esp_err_t ret;

    ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init fail: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init_slot fail: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    ret = esp_vfs_fat_sdmmc_mount(mount_point_.c_str(), &host, &slot, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdmmc_mount fail: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    mounted_ = true;
    s_card   = card;
    s_last_mount_spi = false;

    card_name_ = std::string(card->cid.name, 5);
    info_.capacityMB = (card->csd.capacity) / (1024 * 1024);

    const sdmmc_cid_t& c = card->cid;

    ESP_LOGI(TAG, "===== SD CID (SDMMC 4-bit) =====");
    ESP_LOGI(TAG, "MID: 0x%02X", c.mfg_id);
    char oem0 = (c.oem_id >> 8) & 0xFF;
    char oem1 = (c.oem_id >> 0) & 0xFF;
    ESP_LOGI(TAG, "OEM: %c%c", oem0, oem1);
    char pnm[6]; memcpy(pnm, c.name, 5); pnm[5] = 0;
    ESP_LOGI(TAG, "Product: %s", pnm);
    ESP_LOGI(TAG, "Revision: %d.%d",
             (c.revision >> 4) & 0x0F,
             c.revision & 0x0F);
    ESP_LOGI(TAG, "Serial: 0x%08X", c.serial);
    int month = c.date & 0x0F;
    int year  = 2000 + ((c.date >> 4) & 0xFF);
    ESP_LOGI(TAG, "Date: %02d/%04d", month, year);

    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "SD mounted OK (SDMMC 4-bit, %s)", card_name_.c_str());
    return ESP_OK;
}

// --------------------------------------------------------
// Mount SPI mode
// --------------------------------------------------------
esp_err_t SdMount::MountSpi() {
    ESP_LOGI(TAG, "Mount SD over SPI...");

    if (spi_sck_ < 0 || spi_miso_ < 0 ||
        spi_mosi_ < 0 || spi_cs_   < 0) {
        ESP_LOGW(TAG,
                 "SPI pins invalid (SCK=%d MISO=%d MOSI=%d CS=%d) → skip",
                 spi_sck_, spi_miso_, spi_mosi_, spi_cs_);
        return ESP_ERR_INVALID_ARG;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num     = spi_mosi_;
    bus_cfg.miso_io_num     = spi_miso_;
    bus_cfg.sclk_io_num     = spi_sck_;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 4000;

    // ==== FIX QUAN TRỌNG ====
    spi_host_device_t host_id =
        static_cast<spi_host_device_t>(host.slot);

    esp_err_t ret = spi_bus_initialize(host_id, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize fail: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024
    };

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)spi_cs_;
    slot_config.host_id = host_id;     // FIX

    sdmmc_card_t* card = nullptr;

    ret = esp_vfs_fat_sdspi_mount(
        mount_point_.c_str(), &host, &slot_config, &mount_cfg, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount fail: %s", esp_err_to_name(ret));
        spi_bus_free(host_id);    // FIX
        return ret;
    }

    mounted_ = true;
    s_card   = card;
    s_last_mount_spi = true;
    s_spi_host_slot  = static_cast<int>(host_id);

    card_name_ = std::string(card->cid.name, 5);
    info_.capacityMB = (card->csd.capacity) / (1024 * 1024);

    const sdmmc_cid_t& c = card->cid;

    ESP_LOGI(TAG, "===== SD CID (SPI) =====");
    ESP_LOGI(TAG, "MID: 0x%02X", c.mfg_id);
    char oem0 = (c.oem_id >> 8) & 0xFF;
    char oem1 = (c.oem_id >> 0) & 0xFF;
    ESP_LOGI(TAG, "OEM: %c%c", oem0, oem1);
    char pnm[6]; memcpy(pnm, c.name, 5); pnm[5] = 0;
    ESP_LOGI(TAG, "Product: %s", pnm);
    ESP_LOGI(TAG, "Revision: %d.%d",
             (c.revision >> 4) & 0x0F,
             c.revision & 0x0F);
    ESP_LOGI(TAG, "Serial: 0x%08X", c.serial);
    int month = c.date & 0x0F;
    int year  = 2000 + ((c.date >> 4) & 0xFF);
    ESP_LOGI(TAG, "Date: %02d/%04d", month, year);

    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "SD mounted OK (SPI, %s)", card_name_.c_str());
    return ESP_OK;
}

// --------------------------------------------------------
// Loop() — auto mount theo sd_mode
// --------------------------------------------------------
void SdMount::Loop() {
    if (mounted_) return;

    if (!DetectInserted()) {
        return;
    }

    ESP_LOGI(TAG, "Try mount SD: mode=%d", sd_mode_);

    esp_err_t ret = ESP_FAIL;
    switch (sd_mode_) {
    case 1:
        ret = MountSdmmc4Bit();
        break;
    case 2:
        ret = MountSpi();
        break;
    case 0:
    default:
        ret = MountSdmmc1Bit();
        break;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Mount failed in mode %d: %s",
                 sd_mode_, esp_err_to_name(ret));
    }
}

// --------------------------------------------------------
// Deinit() — unmount + tắt host
// --------------------------------------------------------
void SdMount::Deinit() {
    if (!mounted_) {
        return;
    }

    esp_vfs_fat_sdcard_unmount(mount_point_.c_str(), s_card);
    ESP_LOGI(TAG, "SD unmounted.");
    mounted_ = false;
    s_card   = nullptr;

    if (s_last_mount_spi) {
        if (s_spi_host_slot >= 0) {
            spi_bus_free(static_cast<spi_host_device_t>(s_spi_host_slot));
            s_spi_host_slot = -1;
        }
    } else {
        sdmmc_host_deinit();
    }
}

