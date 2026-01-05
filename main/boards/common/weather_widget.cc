#include "weather_widget.h"

#if !defined(CONFIG_USE_EMOTE_MESSAGE_STYLE)

#include "application.h"
#include "board.h"
#include "display.h"

#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sys/time.h>

#include <font_awesome.h>

// Fonts used by this project (declared in the font component)
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

#define TAG "WeatherWidget"

namespace {

static void ReplaceAllUtf8InPlace(std::string& str, const char* from, const char* to) {
    if (!from || !*from) {
        return;
    }
    const std::string from_s(from);
    const std::string to_s = to ? std::string(to) : std::string();
    std::size_t pos = 0;
    while ((pos = str.find(from_s, pos)) != std::string::npos) {
        str.replace(pos, from_s.size(), to_s);
        pos += to_s.size();
    }
}

static std::string FormatHmFromLocalEpochShifted(int64_t local_epoch_shifted) {
    if (local_epoch_shifted <= 0) {
        return "--:--";
    }
    time_t t = static_cast<time_t>(local_epoch_shifted);
    tm tm_local{};
    // sunrise_local/sunset_local đã cộng offset -> dùng gmtime để lấy HH:MM địa phương.
    gmtime_r(&t, &tm_local);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm_local.tm_hour, tm_local.tm_min);
    return std::string(buf);
}

}  // namespace

WeatherWidget::WeatherWidget(lv_obj_t* parent, WeatherService* weather_service)
    : weather_service_(weather_service), parent_(parent) {
    CreateUI();

    esp_timer_create_args_t timer_args = {
        .callback = ClockTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "weather_clock",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &clock_timer_));
}

WeatherWidget::~WeatherWidget() {
    if (clock_timer_) {
        esp_timer_stop(clock_timer_);
        esp_timer_delete(clock_timer_);
        clock_timer_ = nullptr;
    }
    if (container_) {
        lv_obj_del(container_);
        container_ = nullptr;
    }
}

void WeatherWidget::CreateUI() {
    // Full-screen container: 240x320
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, 240, 320);
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);

    // Header title
    title_label_ = lv_label_create(container_);
    lv_obj_set_width(title_label_, 236);
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(title_label_, "---");

    // Current icon
    current_icon_label_ = lv_label_create(container_);
    lv_obj_set_style_text_font(current_icon_label_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(current_icon_label_, lv_color_hex(0xFFDD00), 0);
    lv_obj_align(current_icon_label_, LV_ALIGN_TOP_LEFT, 16, 52);
    lv_obj_set_style_transform_zoom(current_icon_label_, 420, 0);
    lv_label_set_text(current_icon_label_, FONT_AWESOME_CLOUD);

    // Temperature big
    temp_label_ = lv_label_create(container_);
    lv_obj_set_style_text_color(temp_label_, lv_color_hex(0xFF6600), 0);
    lv_obj_align(temp_label_, LV_ALIGN_TOP_LEFT, 88, 46);
    lv_obj_set_style_transform_zoom(temp_label_, 520, 0);
    lv_label_set_text(temp_label_, "--°C");

    // Description
    desc_label_ = lv_label_create(container_);
    lv_obj_set_width(desc_label_, 140);
    lv_label_set_long_mode(desc_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(desc_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(desc_label_, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(desc_label_, LV_ALIGN_TOP_LEFT, 88, 98);
    lv_label_set_text(desc_label_, "Đang tải...");

    // Separator
    lv_obj_t* sep1 = lv_obj_create(container_);
    lv_obj_remove_style_all(sep1);
    lv_obj_set_size(sep1, 220, 1);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_50, 0);
    lv_obj_align(sep1, LV_ALIGN_TOP_MID, 0, 132);

    // Stats container
    lv_obj_t* stats = lv_obj_create(container_);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, 240, 56);
    lv_obj_align(stats, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(stats, 4, 0);

    auto make_stat_box = [&](lv_obj_t** out_label, const char* init_text, lv_color_t color) {
        lv_obj_t* box = lv_obj_create(stats);
        lv_obj_set_size(box, 74, 48);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x101010), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(box, 10, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_pad_all(box, 4, 0);

        lv_obj_t* lbl = lv_label_create(box);
        lv_obj_set_width(lbl, 66);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, color, 0);
        lv_obj_center(lbl);
        lv_label_set_text(lbl, init_text);
        *out_label = lbl;
    };

    make_stat_box(&wind_label_, "Gió\n-- m/s", lv_color_hex(0x66AADD));
    make_stat_box(&pressure_label_, "Áp suất\n---- hPa", lv_color_hex(0xCCCCCC));
    make_stat_box(&humidity_label_, "Độ ẩm\n--%", lv_color_hex(0x00BFFF));

    // Sunrise / Sunset
    sun_label_ = lv_label_create(container_);
    lv_obj_set_width(sun_label_, 236);
    lv_obj_set_style_text_align(sun_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sun_label_, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(sun_label_, LV_ALIGN_TOP_MID, 0, 200);
    lv_label_set_text(sun_label_, "🌅 --:--    🌇 --:--");

    // Separator
    lv_obj_t* sep2 = lv_obj_create(container_);
    lv_obj_remove_style_all(sep2);
    lv_obj_set_size(sep2, 220, 1);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_50, 0);
    lv_obj_align(sep2, LV_ALIGN_TOP_MID, 0, 222);

    // Forecast container (bottom)
    forecast_container_ = lv_obj_create(container_);
    lv_obj_remove_style_all(forecast_container_);
    lv_obj_set_size(forecast_container_, 240, 92);
    lv_obj_align(forecast_container_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(forecast_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(forecast_container_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(forecast_container_, 4, 0);

    for (int i = 0; i < 5; ++i) {
        lv_obj_t* cell = lv_obj_create(forecast_container_);
        lv_obj_set_size(cell, 44, 88);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x0B0B0B), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(cell, 10, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);

        forecast_day_label_[i] = lv_label_create(cell);
        lv_obj_set_width(forecast_day_label_[i], 44);
        lv_obj_set_style_text_align(forecast_day_label_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(forecast_day_label_[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(forecast_day_label_[i], LV_ALIGN_TOP_MID, 0, 6);
        lv_label_set_text(forecast_day_label_[i], "--");

        forecast_icon_label_[i] = lv_label_create(cell);
        lv_obj_set_style_text_font(forecast_icon_label_[i], &font_awesome_30_4, 0);
        lv_obj_set_style_text_color(forecast_icon_label_[i], lv_color_hex(0xAAAAAA), 0);
        lv_obj_align(forecast_icon_label_[i], LV_ALIGN_TOP_MID, 0, 26);
        lv_obj_set_style_transform_zoom(forecast_icon_label_[i], 320, 0);
        lv_label_set_text(forecast_icon_label_[i], FONT_AWESOME_CLOUD);

        forecast_temp_label_[i] = lv_label_create(cell);
        lv_obj_set_width(forecast_temp_label_[i], 44);
        lv_obj_set_style_text_align(forecast_temp_label_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(forecast_temp_label_[i], lv_color_hex(0xFFAA66), 0);
        lv_obj_align(forecast_temp_label_[i], LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_label_set_text(forecast_temp_label_[i], "--/--");
    }

    // Loading spinner
    loading_spinner_ = lv_arc_create(container_);
    lv_obj_set_size(loading_spinner_, 54, 54);
    lv_obj_center(loading_spinner_);
    lv_arc_set_range(loading_spinner_, 0, 100);
    lv_arc_set_value(loading_spinner_, 75);
    lv_arc_set_bg_angles(loading_spinner_, 0, 360);
    lv_arc_set_rotation(loading_spinner_, 270);
    lv_obj_set_style_arc_color(loading_spinner_, lv_color_hex(0x1062), LV_PART_MAIN);
    lv_obj_set_style_arc_color(loading_spinner_, lv_color_hex(0x4DA6FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(loading_spinner_, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(loading_spinner_, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(loading_spinner_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_add_flag(loading_spinner_, LV_OBJ_FLAG_HIDDEN);

    // Start hidden
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void WeatherWidget::Show() {
    if (!container_) {
        return;
    }

    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;

    if (clock_timer_) {
        // Chỉ cần cập nhật header khi đổi ngày; 1s vẫn an toàn (scheduler lock display)
        esp_timer_start_periodic(clock_timer_, 1000000);
    }

    UpdateClock();

    if (weather_service_) {
        WeatherData weather = weather_service_->GetCurrentWeather();
        if (weather.valid) {
            UpdateUI(weather);
        }
    }
}

void WeatherWidget::Hide() {
    if (!container_) {
        return;
    }
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    visible_ = false;

    if (clock_timer_) {
        esp_timer_stop(clock_timer_);
    }
}

void WeatherWidget::UpdateWeather(const WeatherData& weather) {
    if (!visible_) {
        return;
    }
    UpdateUI(weather);
}

void WeatherWidget::ShowLoading(bool show) {
    if (!container_) {
        return;
    }

    auto set_hidden = [&](lv_obj_t* obj, bool hidden) {
        if (!obj) return;
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    };

    set_hidden(loading_spinner_, !show);

    // Hide/show main parts
    set_hidden(title_label_, show);
    set_hidden(current_icon_label_, show);
    set_hidden(temp_label_, show);
    set_hidden(desc_label_, show);
    set_hidden(wind_label_ ? lv_obj_get_parent(wind_label_) : nullptr, show);      // box
    set_hidden(pressure_label_ ? lv_obj_get_parent(pressure_label_) : nullptr, show);
    set_hidden(humidity_label_ ? lv_obj_get_parent(humidity_label_) : nullptr, show);
    set_hidden(sun_label_, show);
    set_hidden(forecast_container_, show);
}

void WeatherWidget::UpdateClock() {
    UpdateHeader();
}

void WeatherWidget::ClockTimerCallback(void* arg) {
    auto* widget = static_cast<WeatherWidget*>(arg);
    if (!widget || !widget->visible_) {
        return;
    }

    // Không chạm LVGL trực tiếp từ esp_timer.
    Application::GetInstance().Schedule([widget]() {
        if (!widget->visible_) {
            return;
        }

        Display* display = Board::GetInstance().GetDisplay();
        if (!display) {
            return;
        }
        DisplayLockGuard lock(display);
        widget->UpdateClock();
    });
}

void WeatherWidget::UpdateHeader() {
    if (!title_label_) {
        return;
    }

    time_t now = time(nullptr);
    tm* tm_local = localtime(&now);

    // Nếu chưa sync SNTP -> hiển thị tối giản
    if (!tm_local || tm_local->tm_year < 2024 - 1900) {
        std::string city = "---";
        if (weather_service_) {
            GeoLocation loc = weather_service_->GetCurrentLocation();
            if (!loc.city.empty()) {
                city = loc.city;
            }
        }
        lv_label_set_text(title_label_, NormalizeVietnameseForFont(city).c_str());
        return;
    }

    const int wday = (tm_local->tm_wday >= 0 && tm_local->tm_wday <= 6) ? tm_local->tm_wday : 0;

    // Chỉ cập nhật khi đổi ngày để giảm cập nhật text
    if (tm_local->tm_mday == last_day_) {
        return;
    }
    last_day_ = tm_local->tm_mday;

    std::string city = "---";
    if (weather_service_) {
        GeoLocation loc = weather_service_->GetCurrentLocation();
        if (!loc.city.empty()) {
            city = loc.city;
        }
    }

    char date_buf[16];
	snprintf(date_buf, sizeof(date_buf), "%02u/%02u",
			 static_cast<unsigned>(tm_local->tm_mday),
			 static_cast<unsigned>(tm_local->tm_mon + 1));

    std::string title = city + " — " + GetThuDayDu(wday) + " " + date_buf;
    title = NormalizeVietnameseForFont(title);
    lv_label_set_text(title_label_, title.c_str());
}

void WeatherWidget::UpdateUI(const WeatherData& weather) {
    if (!weather.valid) {
        lv_label_set_text(temp_label_, "--°C");
        lv_label_set_text(desc_label_, "Không có dữ liệu");
        lv_label_set_text(sun_label_, "🌅 --:--    🌇 --:--");
        for (int i = 0; i < 5; ++i) {
            lv_label_set_text(forecast_day_label_[i], "--");
            lv_label_set_text(forecast_icon_label_[i], FONT_AWESOME_CLOUD);
            lv_label_set_text(forecast_temp_label_[i], "--/--");
        }
        return;
    }

    // Header có city + thứ/ngày
    UpdateHeader();

    // Icon
    const std::string icon_symbol = GetWeatherIconSymbol(weather.icon);
    lv_label_set_text(current_icon_label_, icon_symbol.c_str());

    if (weather.icon == "sun") {
        lv_obj_set_style_text_color(current_icon_label_, lv_color_hex(0xFFBB00), 0);
    } else if (weather.icon == "cloud_sun") {
        lv_obj_set_style_text_color(current_icon_label_, lv_color_hex(0xFF9900), 0);
    } else if (weather.icon == "cloud") {
        lv_obj_set_style_text_color(current_icon_label_, lv_color_hex(0xAAAAAA), 0);
    } else if (weather.icon == "wind") {
        lv_obj_set_style_text_color(current_icon_label_, lv_color_hex(0x66AADD), 0);
    } else if (weather.icon == "snowflake") {
        lv_obj_set_style_text_color(current_icon_label_, lv_color_hex(0xDDDDDD), 0);
    }

    // Temperature (làm tròn cho dễ nhìn)
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.0f°C", weather.temperature);
    lv_label_set_text(temp_label_, temp_str);

    // Description
    std::string desc = NormalizeVietnameseForFont(weather.description);
    lv_label_set_text(desc_label_, desc.c_str());

    // Stats
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Gió\n%.1f m/s", weather.wind_speed);
        lv_label_set_text(wind_label_, buf);
    }
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Áp suất\n%.0f hPa", weather.pressure);
        lv_label_set_text(pressure_label_, buf);
    }
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "Độ ẩm\n%.0f%%", weather.humidity);
        lv_label_set_text(humidity_label_, buf);
    }

    // Sunrise / Sunset
    const std::string rise = FormatHmFromLocalEpochShifted(weather.sunrise_local);
    const std::string set = FormatHmFromLocalEpochShifted(weather.sunset_local);
    std::string sun = "🌅 " + rise + "    🌇 " + set;
    lv_label_set_text(sun_label_, sun.c_str());

    // Forecast 5 days
    for (int i = 0; i < 5; ++i) {
        if (i < weather.forecast_count && weather.forecast[i].valid) {
            lv_label_set_text(forecast_day_label_[i], GetThuNgan(weather.forecast[i].wday).c_str());
            lv_label_set_text(forecast_icon_label_[i], GetWeatherIconSymbol(weather.forecast[i].icon).c_str());
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f/%.0f", weather.forecast[i].temp_max, weather.forecast[i].temp_min);
            lv_label_set_text(forecast_temp_label_[i], buf);
        } else {
            lv_label_set_text(forecast_day_label_[i], "--");
            lv_label_set_text(forecast_icon_label_[i], FONT_AWESOME_CLOUD);
            lv_label_set_text(forecast_temp_label_[i], "--/--");
        }
    }
}

std::string WeatherWidget::GetWeatherIconSymbol(const std::string& icon) {
    // Day/night: 18:00..05:59 => night
    bool is_night = false;
    time_t now = time(nullptr);
    tm* tm_local = localtime(&now);
    if (tm_local) {
        const int hour = tm_local->tm_hour;
        is_night = (hour >= 18 || hour < 6);
    }

    if (icon == "sun") {
        return is_night ? FONT_AWESOME_MOON : FONT_AWESOME_SUN;
    }
    if (icon == "cloud_sun") {
        return is_night ? FONT_AWESOME_CLOUD_MOON : FONT_AWESOME_CLOUD_SUN;
    }
    if (icon == "wind") {
        return FONT_AWESOME_WIND;
    }
    if (icon == "snowflake") {
        return FONT_AWESOME_SNOWFLAKE;
    }
    return FONT_AWESOME_CLOUD;
}

std::string WeatherWidget::GetThuNgan(int wday) {
    // 0=CN, 1=T2, ..., 6=T7
    switch (wday) {
        case 0: return "CN";
        case 1: return "T2";
        case 2: return "T3";
        case 3: return "T4";
        case 4: return "T5";
        case 5: return "T6";
        case 6: return "T7";
        default: return "--";
    }
}

std::string WeatherWidget::GetThuDayDu(int wday) {
    // 0=CN
    switch (wday) {
        case 0: return "Chủ nhật";
        case 1: return "Thứ Hai";
        case 2: return "Thứ Ba";
        case 3: return "Thứ Tư";
        case 4: return "Thứ Năm";
        case 5: return "Thứ Sáu";
        case 6: return "Thứ Bảy";
        default: return "---";
    }
}

std::string WeatherWidget::NormalizeVietnameseForFont(const std::string& input) {
    // Một số font UI có thể thiếu glyph in hoa dấu. Hạ về chữ thường để tránh ô vuông.
    std::string out = input;

    // A / Ă / Â
    ReplaceAllUtf8InPlace(out, "Ạ", "ạ");
    ReplaceAllUtf8InPlace(out, "Ả", "ả");
    ReplaceAllUtf8InPlace(out, "Ấ", "ấ");
    ReplaceAllUtf8InPlace(out, "Ầ", "ầ");
    ReplaceAllUtf8InPlace(out, "Ẩ", "ẩ");
    ReplaceAllUtf8InPlace(out, "Ẫ", "ẫ");
    ReplaceAllUtf8InPlace(out, "Ậ", "ậ");
    ReplaceAllUtf8InPlace(out, "Ắ", "ắ");
    ReplaceAllUtf8InPlace(out, "Ằ", "ằ");
    ReplaceAllUtf8InPlace(out, "Ẳ", "ẳ");
    ReplaceAllUtf8InPlace(out, "Ẵ", "ẵ");
    ReplaceAllUtf8InPlace(out, "Ặ", "ặ");

    // E / Ê
    ReplaceAllUtf8InPlace(out, "Ẹ", "ẹ");
    ReplaceAllUtf8InPlace(out, "Ẻ", "ẻ");
    ReplaceAllUtf8InPlace(out, "Ẽ", "ẽ");
    ReplaceAllUtf8InPlace(out, "Ế", "ế");
    ReplaceAllUtf8InPlace(out, "Ề", "ề");
    ReplaceAllUtf8InPlace(out, "Ể", "ể");
    ReplaceAllUtf8InPlace(out, "Ễ", "ễ");
    ReplaceAllUtf8InPlace(out, "Ệ", "ệ");

    // I
    ReplaceAllUtf8InPlace(out, "Ỉ", "ỉ");
    ReplaceAllUtf8InPlace(out, "Ị", "ị");

    // O / Ô / Ơ
    ReplaceAllUtf8InPlace(out, "Ọ", "ọ");
    ReplaceAllUtf8InPlace(out, "Ỏ", "ỏ");
    ReplaceAllUtf8InPlace(out, "Ố", "ố");
    ReplaceAllUtf8InPlace(out, "Ồ", "ồ");
    ReplaceAllUtf8InPlace(out, "Ổ", "ổ");
    ReplaceAllUtf8InPlace(out, "Ỗ", "ỗ");
    ReplaceAllUtf8InPlace(out, "Ộ", "ộ");
    ReplaceAllUtf8InPlace(out, "Ớ", "ớ");
    ReplaceAllUtf8InPlace(out, "Ờ", "ờ");
    ReplaceAllUtf8InPlace(out, "Ở", "ở");
    ReplaceAllUtf8InPlace(out, "Ỡ", "ỡ");
    ReplaceAllUtf8InPlace(out, "Ợ", "ợ");

    // U / Ư
    ReplaceAllUtf8InPlace(out, "Ụ", "ụ");
    ReplaceAllUtf8InPlace(out, "Ủ", "ủ");
    ReplaceAllUtf8InPlace(out, "Ứ", "ứ");
    ReplaceAllUtf8InPlace(out, "Ừ", "ừ");
    ReplaceAllUtf8InPlace(out, "Ử", "ử");
    ReplaceAllUtf8InPlace(out, "Ữ", "ữ");
    ReplaceAllUtf8InPlace(out, "Ự", "ự");

    // Y
    ReplaceAllUtf8InPlace(out, "Ỳ", "ỳ");
    ReplaceAllUtf8InPlace(out, "Ỵ", "ỵ");
    ReplaceAllUtf8InPlace(out, "Ỷ", "ỷ");
    ReplaceAllUtf8InPlace(out, "Ỹ", "ỹ");

    // base Ơ/Ư
    ReplaceAllUtf8InPlace(out, "Ơ", "ơ");
    ReplaceAllUtf8InPlace(out, "Ư", "ư");

    return out;
}

#endif // !CONFIG_USE_EMOTE_MESSAGE_STYLE
