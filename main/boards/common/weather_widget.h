#ifndef WEATHER_WIDGET_H
#define WEATHER_WIDGET_H

#include "weather_service.h"

#include <array>
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

// Widget thời tiết toàn màn hình (LVGL v9)
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
    // Services
    WeatherService* weather_service_ = nullptr;

    // Root
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* main_col_ = nullptr;

    // Screen size (để scale layout)
    int screen_width_ = 240;
    int screen_height_ = 320;

    // Header
    lv_obj_t* label_wifi_icon_ = nullptr;
    lv_obj_t* label_bat_icon_ = nullptr;
    lv_obj_t* label_full_date_ = nullptr;

    // Clock (8 glyphs: HH:MM:SS)
    lv_obj_t* cont_clock_ = nullptr;
    std::array<lv_obj_t*, 8> lbl_clock_digits_{};

    // City
    lv_obj_t* label_city_ = nullptr;

    // Main weather
    lv_obj_t* group_weather_ = nullptr;
    lv_obj_t* label_main_temp_ = nullptr;
    lv_obj_t* label_main_icon_ = nullptr;
    lv_obj_t* label_main_desc_ = nullptr;


    // Sunrise / Sunset (overlay, ignore flex layout)
    lv_obj_t* label_sunrise_sunset_ = nullptr;
    // Details
    lv_obj_t* group_details_ = nullptr;
    lv_obj_t* arc_humid_ = nullptr;
    lv_obj_t* label_humid_val_ = nullptr;
    lv_obj_t* arc_press_ = nullptr;
    lv_obj_t* label_press_val_ = nullptr;
    lv_obj_t* arc_wind_ = nullptr;
    lv_obj_t* label_wind_val_ = nullptr;

    // Forecast
    lv_obj_t* obj_forecast_cont_ = nullptr;
    std::array<lv_obj_t*, 5> forecast_day_label_{};
    std::array<lv_obj_t*, 5> forecast_icon_label_{};
    std::array<lv_obj_t*, 5> forecast_temp_label_{};

    // Loading
    lv_obj_t* loading_spinner_ = nullptr;

    // Timer
    esp_timer_handle_t clock_timer_ = nullptr;
    bool visible_ = false;

    // Cached weather (để vẽ lại khi Show)
    WeatherData last_weather_{};
    bool have_last_weather_ = false;

    void CreateUI();
    void UpdateUI(const WeatherData& weather);
    void UpdateHeaderAndClock();

    static void ClockTimerCallback(void* arg);

    // Helpers
    static void CreateGradientBars(lv_obj_t* parent, int screen_width, int screen_height);
    static void CreateDetailArc(lv_obj_t* parent,
                                int screen_width,
                                lv_obj_t** arc_out,
                                lv_obj_t** label_out,
                                lv_color_t color);

    static const char* GetWifiIconGlyph(int rssi);
    static const char* GetBatteryIconGlyph(int level, bool charging);
    static const char* GetWeatherIconGlyph(const std::string& code_or_name);

    static std::string GetThuDayDu(int wday); // "Thứ Hai"...
    static std::string GetThuNgan(int wday);  // T2..CN

    static std::string NormalizeVietnameseForFont(const std::string& input);
};

#endif

#endif // WEATHER_WIDGET_H
