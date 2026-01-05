#ifndef WEATHER_WIDGET_H
#define WEATHER_WIDGET_H

#include "weather_service.h"

#include <string>

#include <esp_timer.h>

// Khi project build không có LVGL (ví dụ style emote-message), cung cấp stub no-op.
#if defined(CONFIG_USE_EMOTE_MESSAGE_STYLE)

class WeatherWidget {
public:
    WeatherWidget(void* /*parent*/, WeatherService* /*weather_service*/) {}
    ~WeatherWidget() = default;

    void Show() {}
    void Hide() {}
    void UpdateWeather(const WeatherData& /*weather*/) {}
    bool IsVisible() const { return false; }
    void ShowLoading(bool /*show*/) {}
    void UpdateClock() {}

private:
};

#else

#include <lvgl.h>

// Widget thời tiết toàn màn hình 240x320 (LVGL v9)
class WeatherWidget {
public:
    WeatherWidget(lv_obj_t* parent, WeatherService* weather_service);
    ~WeatherWidget();

    void Show();
    void Hide();

    // Update từ service callback. Không làm gì nếu widget đang ẩn.
    void UpdateWeather(const WeatherData& weather);

    bool IsVisible() const { return visible_; }

    void ShowLoading(bool show);
    void UpdateClock();

private:
    WeatherService* weather_service_ = nullptr;
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* container_ = nullptr;

    // Header
    lv_obj_t* title_label_ = nullptr; // "Thành phố — Thứ ... dd/mm"

    // Hiện tại
    lv_obj_t* current_icon_label_ = nullptr;
    lv_obj_t* temp_label_ = nullptr;
    lv_obj_t* desc_label_ = nullptr;

    // Thông tin phụ
    lv_obj_t* wind_label_ = nullptr;
    lv_obj_t* pressure_label_ = nullptr;
    lv_obj_t* humidity_label_ = nullptr;

    // Mặt trời mọc/lặn
    lv_obj_t* sun_label_ = nullptr;

    // Dự báo 5 ngày
    lv_obj_t* forecast_container_ = nullptr;
    lv_obj_t* forecast_day_label_[5]{};
    lv_obj_t* forecast_icon_label_[5]{};
    lv_obj_t* forecast_temp_label_[5]{};

    // Loading
    lv_obj_t* loading_spinner_ = nullptr;

    // Clock
    esp_timer_handle_t clock_timer_ = nullptr;
    int last_day_ = -1;
    bool visible_ = false;

    void CreateUI();
    void UpdateUI(const WeatherData& weather);
    void UpdateHeader();

    static void ClockTimerCallback(void* arg);

    static std::string GetWeatherIconSymbol(const std::string& icon);
    static std::string GetThuNgan(int wday);       // T2..CN
    static std::string GetThuDayDu(int wday);      // "Thứ Hai"...
    static std::string NormalizeVietnameseForFont(const std::string& input);
};

#endif

#endif // WEATHER_WIDGET_H
