#pragma once

#include <string>
#include <esp_err.h>

/*
    SdMount hỗ trợ 3 mode:
      0 = SDMMC 1-bit
      1 = SDMMC 4-bit
      2 = SPI

    Cấu hình lấy từ NVS namespace "wifi":
      - sd_mode
      - sd_clk, sd_cmd, sd_d0, sd_d1, sd_d2, sd_d3
      - spi_sck, spi_miso, spi_mosi, spi_cs
*/

class SdMount {
public:
    struct SdCardInfo {
        uint32_t capacityMB = 0;
        uint32_t speedKBps  = 0;   // reserved
    };

public:
    // Singleton
    static SdMount& GetInstance();

    // Khởi tạo:
    //  - Load config từ NVS nếu có
    //  - Config detect pin
    //  - Thử mount một lần
    esp_err_t Init();

    // Re-init sau khi đã ghi config SD mới vào NVS
    esp_err_t ReinitFromNvs();

    // Gọi định kỳ để auto-mount khi cắm thẻ
    void Loop();

    // Unmount + tắt host/bus
    void Deinit();

    // Trạng thái
    bool IsMounted() const { return mounted_; }

    // Thông tin
    std::string GetMountPoint() const { return mount_point_; }
    std::string GetCardName()  const { return card_name_; }
    SdCardInfo  GetCardInfo()  const { return info_; }

private:
    SdMount();
    ~SdMount();

    // Load cấu hình từ NVS
    esp_err_t LoadConfigFromNvs();

    // Detect có thẻ hay không (D3)
    bool DetectInserted();

    // Mount theo mode
    esp_err_t MountSdmmc1Bit();
    esp_err_t MountSdmmc4Bit();
    esp_err_t MountSpi();

private:
    bool mounted_ = false;
    bool last_detect_state_ = false;

    std::string mount_point_ = "/sdcard";
    std::string card_name_;

    SdCardInfo info_;

    // ---------- SDMMC config ----------
    // 0 = 1-bit, 1 = 4-bit, 2 = SPI
    int sd_mode_ = 0;

    int sd_clk_ = 17;
    int sd_cmd_ = 18;
    int sd_d0_  = 21;
    int sd_d1_  = -1;
    int sd_d2_  = -1;
    int sd_d3_  = 13;

    // ---------- SPI config ----------
    int spi_sck_  = -1;
    int spi_miso_ = -1;
    int spi_mosi_ = -1;
    int spi_cs_   = -1;
};
