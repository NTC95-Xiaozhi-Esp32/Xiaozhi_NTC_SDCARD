#include "weather_widget.h"

#if !defined(CONFIG_USE_EMOTE_MESSAGE_STYLE)

#include "application.h"
#include "board.h"
#include "display.h"

#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <type_traits>
#include <utility>

#include <font_awesome.h>

// Fonts used by this project (declared in the font component)
LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_30_4);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_ds_digitb_48);

#define TAG "WeatherWidget"

// --- Colors (VIVID HIGH-CONTRAST THEME) ---
#define COLOR_BG            lv_color_hex(0x000000) // Pure Black (tối ưu độ tương phản)
#define COLOR_WHITE         lv_color_hex(0xFFFFFF) // Trắng tinh
#define COLOR_SILVER        lv_color_hex(0xB0B0B0) // Bạc (text phụ)
#define COLOR_DIM           lv_color_hex(0x606060) // Xám tối (placeholder)

// Accent Colors (Màu tươi sáng, nổi bật trên nền đen)
#define COLOR_VIVID_CYAN    lv_color_hex(0x00FFFF) // Cyan (Mưa, Gió)
#define COLOR_VIVID_YELLOW  lv_color_hex(0xFFD700) // Gold (Nắng, Nhiệt độ chính)
#define COLOR_VIVID_PINK    lv_color_hex(0xFF1493) // Deep Pink (Sấm sét, Áp suất)
#define COLOR_VIVID_BLUE    lv_color_hex(0x1E90FF) // Dodger Blue (Mây, Biển báo)
#define COLOR_VIVID_GREEN   lv_color_hex(0x00FF7F) // Spring Green (Độ ẩm, trong lành)
#define COLOR_VIVID_ORANGE  lv_color_hex(0xFF8C00) // Dark Orange

// Gradient Bars (Purple -> Blue)
#define GRAD_START          lv_color_hex(0x8A2BE2) // BlueViolet
#define GRAD_END            lv_color_hex(0x4169E1) // RoyalBlue

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

// Hàm vẽ dãy 9 chấm bi (trên/dưới)
static lv_obj_t* CreateDotsRow(lv_obj_t* parent, int screen_width, float w_ratio) {
    int gap_dots = static_cast<int>(4 * w_ratio);
    if (gap_dots < 1) {
        gap_dots = 1;
    }

    lv_obj_t* dots_cont = lv_obj_create(parent);
    lv_obj_set_size(dots_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dots_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots_cont, 0, 0);
    lv_obj_set_style_pad_all(dots_cont, 0, 0);
    lv_obj_set_flex_flow(dots_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(dots_cont, gap_dots, 0);

    // Nhỏ -> lớn -> nhỏ
    int dot_sizes[] = {2, 3, 4, 5, 6, 5, 4, 3, 2};

    for (int i = 0; i < 9; ++i) {
        lv_obj_t* dot = lv_obj_create(dots_cont);
        int s = static_cast<int>(dot_sizes[i] * w_ratio);
        if (s < 2) {
            s = 2;
        }
        lv_obj_set_size(dot, s, s);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        // Dùng màu trắng mờ để trang trí tinh tế
        lv_obj_set_style_bg_color(dot, COLOR_WHITE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_30, 0); 
        lv_obj_set_style_border_width(dot, 0, 0);
    }
    (void)screen_width;
    return dots_cont;
}

static int ClampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

static int MapFloatToPercent(float value, float min_v, float max_v) {
    if (max_v <= min_v) {
        return 0;
    }
    float t = (value - min_v) / (max_v - min_v);
    if (t < 0.f) {
        t = 0.f;
    } else if (t > 1.f) {
        t = 1.f;
    }
    return static_cast<int>(t * 100.f + 0.5f);
}

static bool ParseOpenWeatherIconCode(const std::string& s, int* kind_out, bool* night_out) {
    if (kind_out) {
        *kind_out = 0;
    }
    if (night_out) {
        *night_out = false;
    }
    if (s.size() != 3) {
        return false;
    }
    const unsigned char c0 = static_cast<unsigned char>(s[0]);
    const unsigned char c1 = static_cast<unsigned char>(s[1]);
    const unsigned char c2 = static_cast<unsigned char>(s[2]);
    if (!std::isdigit(c0) || !std::isdigit(c1) || (c2 != 'd' && c2 != 'n')) {
        return false;
    }
    const int kind = (s[0] - '0') * 10 + (s[1] - '0');
    const bool night = (s[2] == 'n');
    if (kind_out) {
        *kind_out = kind;
    }
    if (night_out) {
        *night_out = night;
    }
    return true;
}

static lv_color_t WeatherIconColor(const std::string& code_or_name) {
    int kind = 0;
    bool night = false;
    if (ParseOpenWeatherIconCode(code_or_name, &kind, &night)) {
        (void)night; 
        if (kind == 1) {   // clear
            return COLOR_VIVID_YELLOW; // Nắng vàng rực
        }
        if (kind == 2) {   // few clouds
            return COLOR_VIVID_ORANGE; // Mây nắng cam
        }
        if (kind == 3 || kind == 4) { // clouds
            return COLOR_VIVID_BLUE;   // Mây xanh dương
        }
        if (kind == 9 || kind == 10) { // rain
            return COLOR_VIVID_CYAN;   // Mưa xanh lơ
        }
        if (kind == 11) {  // thunder
            return COLOR_VIVID_PINK;   // Sấm hồng tím
        }
        if (kind == 13) {  // snow
            return COLOR_WHITE;        // Tuyết trắng
        }
        if (kind == 50) {  // mist/wind
            return COLOR_VIVID_GREEN;  // Sương mù xanh nhạt
        }
        return COLOR_SILVER;
    }

    std::string lower = code_or_name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.find("thunder") != std::string::npos || lower.find("storm") != std::string::npos) {
        return COLOR_VIVID_PINK;
    }
    if (lower.find("rain") != std::string::npos || lower.find("drizzle") != std::string::npos) {
        return COLOR_VIVID_CYAN;
    }
    if (lower.find("snow") != std::string::npos) {
        return COLOR_WHITE;
    }
    if (lower.find("wind") != std::string::npos || lower.find("mist") != std::string::npos ||
        lower.find("fog") != std::string::npos || lower.find("haze") != std::string::npos) {
        return COLOR_VIVID_GREEN;
    }
    if (lower == "sun" || lower.find("clear") != std::string::npos) {
        return COLOR_VIVID_YELLOW;
    }
    if (lower == "cloud_sun") {
        return COLOR_VIVID_ORANGE;
    }
    if (lower.find("cloud") != std::string::npos) {
        return COLOR_VIVID_BLUE;
    }
    return COLOR_SILVER;
}

static void ApplyWeatherIconColor(lv_obj_t* label, const std::string& icon) {
    if (!label) {
        return;
    }
    lv_obj_set_style_text_color(label, WeatherIconColor(icon), 0);
}

template <typename T, typename = void>
struct HasSunriseLocalMember : std::false_type {};

template <typename T>
struct HasSunriseLocalMember<T, std::void_t<decltype(std::declval<T>().sunrise_local)>> : std::true_type {};

template <typename T, typename = void>
struct HasSunsetLocalMember : std::false_type {};

template <typename T>
struct HasSunsetLocalMember<T, std::void_t<decltype(std::declval<T>().sunset_local)>> : std::true_type {};

template <typename V>
static bool TryGetUnixSeconds(const V& v, int64_t* out) {
    if (!out) {
        return false;
    }
    if constexpr (std::is_integral_v<V>) {
        *out = static_cast<int64_t>(v);
        return true;
    } else if constexpr (std::is_floating_point_v<V>) {
        *out = static_cast<int64_t>(v + 0.5);
        return true;
    } else {
        return false;
    }
}

static int64_t NormalizeUnixSeconds(int64_t t) {
    // Nếu value đang ở milliseconds, đổi về seconds.
    if (t > 2000000000000LL) {
        return t / 1000LL;
    }
    return t;
}

static std::string FormatHHMMFromUnix(int64_t unix_seconds) {
    time_t tt = static_cast<time_t>(NormalizeUnixSeconds(unix_seconds));
    tm tm_utc {};
    if (!gmtime_r(&tt, &tm_utc)) {
        return "--:--";
    }
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(tm_utc.tm_hour),
             static_cast<unsigned>(tm_utc.tm_min));
    return std::string(buf);
}

} // namespace

WeatherWidget::WeatherWidget(lv_obj_t* parent, WeatherService* weather_service)
    : weather_service_(weather_service), parent_(parent) {
    // Mặc định 240x320 (có thể thay đổi nếu parent là root với kích thước khác)
    if (parent_) {
        const int w = static_cast<int>(lv_obj_get_width(parent_));
        const int h = static_cast<int>(lv_obj_get_height(parent_));
        if (w > 0 && h > 0) {
            screen_width_ = w;
            screen_height_ = h;
        }
    }

    for (auto& p : lbl_clock_digits_) {
        p = nullptr;
    }
    for (auto& p : forecast_day_label_) {
        p = nullptr;
    }
    for (auto& p : forecast_icon_label_) {
        p = nullptr;
    }
    for (auto& p : forecast_temp_label_) {
        p = nullptr;
    }

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

void WeatherWidget::CreateGradientBars(lv_obj_t* parent, int screen_width, int screen_height) {
    int bar_thick = static_cast<int>(screen_width * 0.030f);
    if (bar_thick < 2) {
        bar_thick = 2;
    }
    int h_3 = screen_height / 3;
    if (h_3 < 1) {
        h_3 = 1;
    }

    static lv_style_t style_grad;
    static bool inited = false;
    if (!inited) {
        lv_style_init(&style_grad);
        lv_style_set_bg_opa(&style_grad, LV_OPA_COVER);
        // Gradient dọc: Tím -> Xanh (Trendy)
        lv_style_set_bg_grad_color(&style_grad, GRAD_END);
        lv_style_set_bg_color(&style_grad, GRAD_START);
        lv_style_set_radius(&style_grad, 0);
        inited = true;
    }

    // Left
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, bar_thick, h_3);
    lv_obj_align(line, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_style(line, &style_grad, 0);
    lv_obj_set_style_bg_grad_dir(line, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);

    // Right
    line = lv_obj_create(parent);
    lv_obj_set_size(line, bar_thick, h_3);
    lv_obj_align(line, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_style(line, &style_grad, 0);
    lv_obj_set_style_bg_grad_dir(line, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
}

void WeatherWidget::CreateDetailArc(lv_obj_t* parent,
                                   int screen_width,
                                   lv_obj_t** arc_out,
                                   lv_obj_t** label_out,
                                   lv_color_t color) {
    int box_size = static_cast<int>(screen_width / 6.5f);
    if (box_size < 34) {
        box_size = 34;
    }

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, box_size, box_size);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);

    lv_obj_t* arc = lv_arc_create(wrap);
    lv_obj_set_size(arc, box_size - 4, box_size - 4);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, std::max(2, box_size / 10), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, std::max(2, box_size / 10), LV_PART_INDICATOR);
    
    // Nền Arc màu xám đậm (Dark Gray) để nổi màu chính
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    *arc_out = arc;

    lv_obj_t* lbl = lv_label_create(arc);
    lv_obj_set_style_text_font(lbl, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(lbl, COLOR_WHITE, 0);
    lv_label_set_text(lbl, "-");
    lv_obj_center(lbl);
    *label_out = lbl;
}

const char* WeatherWidget::GetWifiIconGlyph(int /*rssi*/) {
    // Font Awesome: WiFi
    return "\uf1eb";
}

const char* WeatherWidget::GetBatteryIconGlyph(int level, bool charging) {
    // Font Awesome: bolt / battery-full / battery-half / battery-empty
    if (charging) {
        return "\uf0e7";
    }
    if (level >= 80) {
        return "\uf240";
    }
    if (level >= 40) {
        return "\uf242";
    }
    return "\uf244";
}

const char* WeatherWidget::GetWeatherIconGlyph(const std::string& code_or_name) {
    // Hỗ trợ cả dạng OpenWeather (01d/01n/...) lẫn dạng "sun", "cloud_sun"... từ service.
    if (code_or_name.size() == 3 && std::isdigit(static_cast<unsigned char>(code_or_name[0])) &&
        std::isdigit(static_cast<unsigned char>(code_or_name[1])) &&
        (code_or_name[2] == 'd' || code_or_name[2] == 'n')) {
        const int kind = (code_or_name[0] - '0') * 10 + (code_or_name[1] - '0');
        const bool night = (code_or_name[2] == 'n');
        if (kind == 1) {
            return night ? FONT_AWESOME_MOON : FONT_AWESOME_SUN;
        }
        if (kind == 2) {
            return night ? FONT_AWESOME_CLOUD_MOON : FONT_AWESOME_CLOUD_SUN;
        }
        if (kind == 3 || kind == 4) {
            return FONT_AWESOME_CLOUD;
        }
        if (kind == 9 || kind == 10) {
            // Rain/Drizzle -> droplet
            return "\uf043";
        }
        if (kind == 11) {
            // Thunder -> bolt
            return "\uf0e7";
        }
        if (kind == 13) {
            return FONT_AWESOME_SNOWFLAKE;
        }
        if (kind == 50) {
            return FONT_AWESOME_WIND;
        }
        return FONT_AWESOME_CLOUD;
    }

    if (code_or_name == "sun") {
        // Theo giờ local: 18:00..05:59 => night
        bool is_night = false;
        time_t now = time(nullptr);
        tm* tm_local = localtime(&now);
        if (tm_local) {
            const int hour = tm_local->tm_hour;
            is_night = (hour >= 18 || hour < 6);
        }
        return is_night ? FONT_AWESOME_MOON : FONT_AWESOME_SUN;
    }
    if (code_or_name == "cloud_sun") {
        bool is_night = false;
        time_t now = time(nullptr);
        tm* tm_local = localtime(&now);
        if (tm_local) {
            const int hour = tm_local->tm_hour;
            is_night = (hour >= 18 || hour < 6);
        }
        return is_night ? FONT_AWESOME_CLOUD_MOON : FONT_AWESOME_CLOUD_SUN;
    }
    if (code_or_name == "rain") {
        return "\uf043";
    }
    if (code_or_name == "thunder") {
        return "\uf0e7";
    }
    if (code_or_name == "mist") {
        return FONT_AWESOME_WIND;
    }
    if (code_or_name == "wind") {
        return FONT_AWESOME_WIND;
    }
    if (code_or_name == "snowflake") {
        return FONT_AWESOME_SNOWFLAKE;
    }
    return FONT_AWESOME_CLOUD;
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

std::string WeatherWidget::NormalizeVietnameseForFont(const std::string& input) {
    // Một số font UI có thể thiếu glyph IN HOA có dấu. Hạ về chữ thường để tránh ô vuông.
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

void WeatherWidget::CreateUI() {
    // ===== Visual theme (High Contrast OLED) =====
    const lv_color_t COL_BG            = COLOR_BG;
    const lv_color_t COL_TEXT_PRIMARY  = COLOR_WHITE;
    const lv_color_t COL_TEXT_MUTED    = COLOR_SILVER;
    const lv_color_t COL_TEXT_DIM      = COLOR_DIM;

    // Specific Accent Colors for Data types
    const lv_color_t COL_ACCENT_HUMID  = COLOR_VIVID_GREEN;
    const lv_color_t COL_ACCENT_PRESS  = COLOR_VIVID_PINK;
    const lv_color_t COL_ACCENT_WIND   = COLOR_VIVID_CYAN;
    const lv_color_t COL_ACCENT_SUN    = COLOR_VIVID_YELLOW;

    const lv_color_t COL_ICON_TOP      = COLOR_SILVER;

    // Tỷ lệ scale
    const float h_ratio = static_cast<float>(screen_height_) / 280.0f;
    const float w_ratio = static_cast<float>(screen_width_) / 240.0f;
    int zoom_std = static_cast<int>(256 * w_ratio);
    if (zoom_std < 160) {
        zoom_std = 160;
    }
    int safe_pad_text = static_cast<int>(4 * h_ratio);
    if (safe_pad_text < 3) {
        safe_pad_text = 3;
    }

    // --- KHOẢNG CÁCH MỚI CHO CÁC HÀNG CUỐI ---
    const int spacing_rows = static_cast<int>(12 * h_ratio); 

    // Icon & Component zoom
    const int zoom_icon_top    = static_cast<int>(zoom_std * 0.60f);  
    
    // --- GIẢM KÍCH THƯỚC ĐỒNG HỒ  ---
    const int zoom_clock       = static_cast<int>(zoom_std * 0.60f);  // Giảm từ 0.70 xuống 0.60
    
    const int zoom_icon_main   = static_cast<int>(zoom_std * 1.20f);  
    const int zoom_icon_forecast = static_cast<int>(zoom_std * 0.70f); 

    // Root container
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, screen_width_, screen_height_);
    lv_obj_center(container_);
    lv_obj_set_style_bg_color(container_, COL_BG, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    CreateGradientBars(container_, screen_width_, screen_height_);

    // Main column
    main_col_ = lv_obj_create(container_);
    lv_obj_set_size(main_col_, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(main_col_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(main_col_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_col_, 0, 0);
    lv_obj_set_style_pad_all(main_col_, 0, 0);
    lv_obj_set_flex_flow(main_col_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_col_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Margin trên cùng tối thiểu
    const int margin_v = static_cast<int>(screen_height_ * 0.01f);
    lv_obj_set_style_pad_ver(main_col_, margin_v, 0);
    lv_obj_set_style_pad_hor(main_col_, 0, 0);
    lv_obj_set_style_pad_row(main_col_, 0, 0);

    // Row 0: dots top
    CreateDotsRow(main_col_, screen_width_, w_ratio);

    // Row 1: Header
    lv_obj_t* row_header = lv_obj_create(main_col_);
    lv_obj_set_width(row_header, lv_pct(100));
    lv_obj_set_height(row_header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_header, 0, 0);
    lv_obj_set_style_pad_all(row_header, 0, 0);
    lv_obj_set_style_pad_ver(row_header, 0, 0); // Sát lề
    lv_obj_clear_flag(row_header, LV_OBJ_FLAG_SCROLLABLE);

    const int icon_pad = static_cast<int>(10 * w_ratio);

    label_wifi_icon_ = lv_label_create(row_header);
    lv_obj_set_style_text_font(label_wifi_icon_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(label_wifi_icon_, COL_ICON_TOP, 0);
    lv_label_set_text(label_wifi_icon_, GetWifiIconGlyph(-50));
    lv_obj_align(label_wifi_icon_, LV_ALIGN_LEFT_MID, icon_pad, 0);
    lv_obj_set_style_transform_zoom(label_wifi_icon_, zoom_icon_top, 0);

    label_bat_icon_ = lv_label_create(row_header);
    lv_obj_set_style_text_font(label_bat_icon_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(label_bat_icon_, COL_ICON_TOP, 0);
    lv_label_set_text(label_bat_icon_, GetBatteryIconGlyph(80, false));
    lv_obj_align(label_bat_icon_, LV_ALIGN_RIGHT_MID, -icon_pad, 0);
    lv_obj_set_style_transform_zoom(label_bat_icon_, zoom_icon_top, 0);

    label_full_date_ = lv_label_create(row_header);
    lv_obj_set_width(label_full_date_, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label_full_date_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(label_full_date_, COL_TEXT_MUTED, 0);
    lv_label_set_text(label_full_date_, "...");
    lv_obj_align(label_full_date_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_transform_zoom(label_full_date_, zoom_std, 0);

    // Row 2: Clock
    cont_clock_ = lv_obj_create(main_col_);
    lv_obj_set_width(cont_clock_, lv_pct(100));
    lv_obj_set_height(cont_clock_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont_clock_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_clock_, 0, 0);
    lv_obj_set_style_pad_all(cont_clock_, 0, 0);
    lv_obj_set_style_pad_ver(cont_clock_, safe_pad_text, 0);
    lv_obj_set_flex_flow(cont_clock_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_clock_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cont_clock_, 0, 0);

    // --- THU HẸP CHIỀU RỘNG HƠN NỮA ĐỂ SỐ KHÔNG BỊ RỜI RẠC ---
    // Giảm hệ số chiều rộng từ 0.75f xuống 0.65f
    const int digit_w = std::max(10, static_cast<int>((screen_width_ / 9) * 0.65f)); 
    const int colon_w = std::max(6, digit_w / 2);
     
    for (int i = 0; i < 8; ++i) {
        lbl_clock_digits_[i] = lv_label_create(cont_clock_);
        lv_obj_set_style_text_font(lbl_clock_digits_[i], &lv_font_ds_digitb_48, 0);
        lv_obj_set_style_text_color(lbl_clock_digits_[i], COL_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_align(lbl_clock_digits_[i], LV_TEXT_ALIGN_CENTER, 0);
        
        // Apply zoom nhỏ (0.60)
        lv_obj_set_style_transform_zoom(lbl_clock_digits_[i], zoom_clock, 0);

        if (i == 2 || i == 5) {
            lv_obj_set_width(lbl_clock_digits_[i], colon_w);
            lv_label_set_text(lbl_clock_digits_[i], ":");
            lv_obj_set_style_text_color(lbl_clock_digits_[i], COL_TEXT_DIM, 0);
        } else {
            lv_obj_set_width(lbl_clock_digits_[i], digit_w);
            lv_label_set_text(lbl_clock_digits_[i], "0");
        }
    }

    // Row 3: City
    label_city_ = lv_label_create(main_col_);
    lv_obj_set_width(label_city_, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label_city_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(label_city_, COL_TEXT_PRIMARY, 0);
    lv_label_set_text(label_city_, "---");
    lv_obj_set_style_transform_zoom(label_city_, zoom_std, 0);
    lv_obj_set_style_pad_ver(label_city_, safe_pad_text, 0);

    // Row 4: Weather group
    group_weather_ = lv_obj_create(main_col_);
    lv_obj_set_width(group_weather_, LV_SIZE_CONTENT);
    lv_obj_set_height(group_weather_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group_weather_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group_weather_, 0, 0);
    lv_obj_set_style_pad_all(group_weather_, 0, 0);
    lv_obj_set_style_pad_ver(group_weather_, safe_pad_text + 2, 0);
    lv_obj_set_flex_flow(group_weather_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group_weather_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(group_weather_, static_cast<int>(30 * w_ratio), 0);

    label_main_icon_ = lv_label_create(group_weather_);
    lv_obj_set_style_text_font(label_main_icon_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(label_main_icon_, COLOR_VIVID_YELLOW, 0);
    lv_label_set_text(label_main_icon_, FONT_AWESOME_SUN);
    lv_obj_set_style_transform_zoom(label_main_icon_, zoom_icon_main, 0);
    lv_obj_set_style_pad_right(label_main_icon_, 5, 0);

    lv_obj_t* weather_text_col = lv_obj_create(group_weather_);
    lv_obj_set_size(weather_text_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(weather_text_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_text_col, 0, 0);
    lv_obj_set_style_pad_all(weather_text_col, 0, 0);
    lv_obj_set_flex_flow(weather_text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(weather_text_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    label_main_temp_ = lv_label_create(weather_text_col);
    lv_obj_set_style_text_font(label_main_temp_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_main_temp_, COLOR_VIVID_YELLOW, 0);
    lv_label_set_text(label_main_temp_, "--°C");
    lv_obj_set_style_transform_zoom(label_main_temp_, zoom_std, 0);

    label_main_desc_ = lv_label_create(weather_text_col);
    lv_obj_set_width(label_main_desc_, static_cast<int>(120 * w_ratio));
    lv_obj_set_style_pad_ver(label_main_desc_, safe_pad_text, 0);
    lv_label_set_long_mode(label_main_desc_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(label_main_desc_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(label_main_desc_, COL_TEXT_MUTED, 0);
    lv_label_set_text(label_main_desc_, "---");
    lv_obj_set_style_transform_zoom(label_main_desc_, zoom_std, 0);

    // Row 5: Details arcs
    group_details_ = lv_obj_create(main_col_);
    lv_obj_set_width(group_details_, lv_pct(100));
    lv_obj_set_height(group_details_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group_details_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group_details_, 0, 0);
    lv_obj_set_style_pad_all(group_details_, 0, 0);
    lv_obj_set_flex_flow(group_details_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group_details_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(group_details_, static_cast<int>(10 * w_ratio), 0);

    CreateDetailArc(group_details_, screen_width_, &arc_humid_, &label_humid_val_, COL_ACCENT_HUMID);
    CreateDetailArc(group_details_, screen_width_, &arc_press_, &label_press_val_, COL_ACCENT_PRESS);
    CreateDetailArc(group_details_, screen_width_, &arc_wind_, &label_wind_val_, COL_ACCENT_WIND);

    // Row 6: Sunrise / Sunset (NO BACKGROUND - CLEAN)
    lv_obj_t* row_sun = lv_obj_create(main_col_);
    lv_obj_set_width(row_sun, LV_SIZE_CONTENT);
    lv_obj_set_height(row_sun, LV_SIZE_CONTENT);
    
    // === Bỏ nền ===
    lv_obj_set_style_bg_opa(row_sun, LV_OPA_TRANSP, 0); 
    lv_obj_set_style_border_width(row_sun, 0, 0);
    lv_obj_set_style_pad_all(row_sun, 0, 0); 
    
    // === THÊM KHOẢNG TRẮNG GIỮA ROW 5 và 6 ===
    lv_obj_set_style_pad_top(row_sun, spacing_rows, 0);
    
    lv_obj_set_flex_flow(row_sun, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_sun,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    label_sunrise_sunset_ = lv_label_create(row_sun);
    lv_obj_set_style_text_font(label_sunrise_sunset_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(label_sunrise_sunset_, COL_ACCENT_SUN, 0);
    lv_label_set_text(label_sunrise_sunset_, "M: --:--   L: --:--");
    lv_obj_set_style_transform_zoom(label_sunrise_sunset_, zoom_std, 0);

    // giữ nguyên cơ chế ẩn/hiện ở UpdateUI()
    lv_obj_add_flag(label_sunrise_sunset_, LV_OBJ_FLAG_HIDDEN);

    // Row 7: Forecast
    obj_forecast_cont_ = lv_obj_create(main_col_);
    lv_obj_set_width(obj_forecast_cont_, lv_pct(100));
    lv_obj_set_height(obj_forecast_cont_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(obj_forecast_cont_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj_forecast_cont_, 0, 0);
    lv_obj_set_style_pad_all(obj_forecast_cont_, 0, 0);

    // === THÊM KHOẢNG TRẮNG GIỮA ROW 6 và 7 ===
    lv_obj_set_style_pad_top(obj_forecast_cont_, spacing_rows, 0);

    lv_obj_set_flex_flow(obj_forecast_cont_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj_forecast_cont_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(obj_forecast_cont_, static_cast<int>(2 * w_ratio), 0);

    // === THAY ĐỔI: Gap = 0 để Thứ/Icon/Nhiệt độ dính sát nhau ===
    const int gap_day_icon = 0; 

    for (int i = 0; i < 5; ++i) {
        lv_obj_t* day_wrap = lv_obj_create(obj_forecast_cont_);
        lv_obj_set_size(day_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_min_width(day_wrap, static_cast<int>(28 * w_ratio), 0);
        lv_obj_set_style_bg_opa(day_wrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(day_wrap, 0, 0);
        lv_obj_set_style_pad_all(day_wrap, 0, 0);
        lv_obj_set_style_pad_gap(day_wrap, 0, 0);
        lv_obj_set_flex_flow(day_wrap, LV_FLEX_FLOW_COLUMN);
        
        lv_obj_set_style_pad_gap(day_wrap, gap_day_icon, 0);
        
        lv_obj_set_flex_align(day_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // 1. CHỮ NGÀY (Mon, Tue...)
        lv_obj_t* lbl_d = lv_label_create(day_wrap);
        lv_obj_set_style_text_font(lbl_d, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_d, COL_TEXT_MUTED, 0);
        lv_label_set_text(lbl_d, "-");
        
        // === SỬA TẠI ĐÂY: Đổi zoom_std thành zoom_icon_forecast ===
        lv_obj_set_style_transform_zoom(lbl_d, zoom_icon_forecast, 0); 
        
        forecast_day_label_[i] = lbl_d;


        // 2. ICON (Mây, Mưa...)
        lv_obj_t* lbl_icon = lv_label_create(day_wrap);
        lv_obj_set_style_text_font(lbl_icon, &font_awesome_30_4, 0);
        lv_obj_set_style_text_color(lbl_icon, COLOR_VIVID_BLUE, 0);
        lv_label_set_text(lbl_icon, FONT_AWESOME_CLOUD);
        
        // Icon đã dùng zoom_icon_forecast rồi
        lv_obj_set_style_transform_zoom(lbl_icon, zoom_icon_forecast, 0); 
        
        forecast_icon_label_[i] = lbl_icon;


        // 3. CHỮ NHIỆT ĐỘ (--°C)
        lv_obj_t* lbl_t = lv_label_create(day_wrap);
        lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_t, COL_TEXT_PRIMARY, 0);
        lv_label_set_text(lbl_t, "--°C");
        
        // === SỬA TẠI ĐÂY: Đổi zoom_std thành zoom_icon_forecast ===
        // Việc này sẽ làm chữ nhỏ đi còn khoảng 50% so với ban đầu
        lv_obj_set_style_transform_zoom(lbl_t, zoom_icon_forecast, 0); 
        
        forecast_temp_label_[i] = lbl_t;
    }

    // Row 8: dots bottom
    CreateDotsRow(main_col_, screen_width_, w_ratio);

    // Loading spinner (overlay)
    loading_spinner_ = lv_arc_create(container_);
    lv_obj_set_size(loading_spinner_, 54, 54);
    lv_obj_center(loading_spinner_);
    lv_arc_set_range(loading_spinner_, 0, 100);
    lv_arc_set_value(loading_spinner_, 75);
    lv_arc_set_bg_angles(loading_spinner_, 0, 360);
    lv_arc_set_rotation(loading_spinner_, 270);
    lv_obj_set_style_arc_color(loading_spinner_, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(loading_spinner_, COLOR_VIVID_CYAN, LV_PART_INDICATOR);
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
        esp_timer_start_periodic(clock_timer_, 1000000);
    }

    UpdateHeaderAndClock();

    if (have_last_weather_) {
        UpdateUI(last_weather_);
    } else if (weather_service_) {
        WeatherData w = weather_service_->GetCurrentWeather();
        if (w.valid) {
            last_weather_ = w;
            have_last_weather_ = true;
        }
        UpdateUI(w);
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
    last_weather_ = weather;
    have_last_weather_ = true;
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
        if (!obj) {
            return;
        }
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    };

    set_hidden(loading_spinner_, !show);
    set_hidden(main_col_, show);
}

void WeatherWidget::UpdateClock() {
    UpdateHeaderAndClock();
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

void WeatherWidget::UpdateHeaderAndClock() {
    if (!label_full_date_ || !lbl_clock_digits_[0]) {
        return;
    }

    // Mặc định: placeholders (có thể nối thêm battery/wifi thực tế sau)
    const int wifi_rssi = -50;
    const int battery_level = 80;
    const bool charging = false;
    lv_label_set_text(label_wifi_icon_, GetWifiIconGlyph(wifi_rssi));
    lv_label_set_text(label_bat_icon_, GetBatteryIconGlyph(battery_level, charging));

    time_t now = time(nullptr);
    tm* tm_local = localtime(&now);

    if (!tm_local || tm_local->tm_year < 2024 - 1900) {
        lv_label_set_text(label_full_date_, "...");
        const char* fallback = "00:00:00";
        for (int i = 0; i < 8; ++i) {
            char c[2] = {fallback[i], '\0'};
            lv_label_set_text(lbl_clock_digits_[i], c);
        }
        return;
    }

    const int wday = (tm_local->tm_wday >= 0 && tm_local->tm_wday <= 6) ? tm_local->tm_wday : 0;
    char date_buf[16];
    snprintf(date_buf, sizeof(date_buf), "%02u/%02u", static_cast<unsigned>(tm_local->tm_mday),
             static_cast<unsigned>(tm_local->tm_mon + 1));

    std::string full_date = GetThuDayDu(wday) + ", " + date_buf;
    full_date = NormalizeVietnameseForFont(full_date);
    lv_label_set_text(label_full_date_, full_date.c_str());

    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02u:%02u:%02u", static_cast<unsigned>(tm_local->tm_hour),
             static_cast<unsigned>(tm_local->tm_min), static_cast<unsigned>(tm_local->tm_sec));

    for (int i = 0; i < 8; ++i) {
        char c[2] = {time_buf[i], '\0'};
        lv_label_set_text(lbl_clock_digits_[i], c);
    }
}

void WeatherWidget::UpdateUI(const WeatherData& weather) {
    // City
    std::string city = "---";
    if (weather_service_) {
        GeoLocation loc = weather_service_->GetCurrentLocation();
        if (!loc.city.empty()) {
            city = loc.city;
        }
    }
    city = NormalizeVietnameseForFont(city);
    if (label_city_) {
        lv_label_set_text(label_city_, city.c_str());
    }

    if (!weather.valid) {
        if (label_main_temp_) lv_label_set_text(label_main_temp_, "--°C");
        if (label_main_desc_) lv_label_set_text(label_main_desc_, "Không có dữ liệu");
        if (label_main_icon_) {
            lv_label_set_text(label_main_icon_, FONT_AWESOME_CLOUD);
            lv_obj_set_style_text_color(label_main_icon_, COLOR_DIM, 0);
        }
        if (label_sunrise_sunset_) {
            lv_obj_add_flag(label_sunrise_sunset_, LV_OBJ_FLAG_HIDDEN);
        }

        if (arc_humid_) lv_arc_set_value(arc_humid_, 0);
        if (label_humid_val_) lv_label_set_text(label_humid_val_, "-");
        if (arc_press_) lv_arc_set_value(arc_press_, 0);
        if (label_press_val_) lv_label_set_text(label_press_val_, "-");
        if (arc_wind_) lv_arc_set_value(arc_wind_, 0);
        if (label_wind_val_) lv_label_set_text(label_wind_val_, "-");

        for (int i = 0; i < 5; ++i) {
            if (forecast_day_label_[i]) lv_label_set_text(forecast_day_label_[i], "-");
            if (forecast_icon_label_[i]) {
                lv_label_set_text(forecast_icon_label_[i], FONT_AWESOME_CLOUD);
                lv_obj_set_style_text_color(forecast_icon_label_[i], COLOR_DIM, 0);
            }
            if (forecast_temp_label_[i]) lv_label_set_text(forecast_temp_label_[i], "--°C");
        }
        return;
    }

    // Current
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f°C", weather.temperature);
        if (label_main_temp_) lv_label_set_text(label_main_temp_, buf);
    }

    if (label_main_desc_) {
        std::string desc = NormalizeVietnameseForFont(weather.description);
        lv_label_set_text(label_main_desc_, desc.c_str());
    }
    if (label_main_icon_) {
        lv_label_set_text(label_main_icon_, GetWeatherIconGlyph(weather.icon));
        ApplyWeatherIconColor(label_main_icon_, weather.icon);
    }

    // Sunrise / Sunset (nếu WeatherData có fields sunrise/sunset)
    if (label_sunrise_sunset_) {
        bool shown = false;
        if constexpr (HasSunriseLocalMember<WeatherData>::value && HasSunsetLocalMember<WeatherData>::value) {
            int64_t sr = 0;
            int64_t ss = 0;
            const bool ok_sr = TryGetUnixSeconds(weather.sunrise_local, &sr);
            const bool ok_ss = TryGetUnixSeconds(weather.sunset_local, &ss);

            if (ok_sr && ok_ss && sr > 0 && ss > 0) {
                std::string txt = std::string("M: ") + FormatHHMMFromUnix(sr) + "  L: " + FormatHHMMFromUnix(ss);
                lv_label_set_text(label_sunrise_sunset_, txt.c_str());
                lv_obj_clear_flag(label_sunrise_sunset_, LV_OBJ_FLAG_HIDDEN);
                shown = true;
            }
        }
        if (!shown) {
            lv_obj_add_flag(label_sunrise_sunset_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Details
    if (arc_humid_ && label_humid_val_) {
        const int h = ClampInt(static_cast<int>(weather.humidity + 0.5f), 0, 100);
        lv_arc_set_value(arc_humid_, h);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", h);
        lv_label_set_text(label_humid_val_, buf);
    }

    if (arc_press_ && label_press_val_) {
        // Scale 950..1050 hPa -> 0..100
        const int pct = MapFloatToPercent(static_cast<float>(weather.pressure), 950.f, 1050.f);
        lv_arc_set_value(arc_press_, pct);

        // Chuyển từ hPa → kPa và lấy 2 chữ số đầu
        int kpa = static_cast<int>(weather.pressure / 10.0f + 0.5f);  // hPa -> kPa, làm tròn
        int k2 = (kpa / 10) % 100;  // lấy 2 chữ số đầu
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d", k2);  // ví dụ: "10kPa"
        lv_label_set_text(label_press_val_, buf);
    }

    if (arc_wind_ && label_wind_val_) {
        // Scale 0..20 m/s -> 0..100
        const int pct = MapFloatToPercent(static_cast<float>(weather.wind_speed), 0.f, 20.f);
        lv_arc_set_value(arc_wind_, pct);
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f", weather.wind_speed);
        lv_label_set_text(label_wind_val_, buf);
    }

    // Forecast (5 days)
    for (int i = 0; i < 5; ++i) {
        if (!forecast_day_label_[i] || !forecast_icon_label_[i] || !forecast_temp_label_[i]) {
            continue;
        }

        if (i < weather.forecast_count && weather.forecast[i].valid) {
            lv_label_set_text(forecast_day_label_[i], GetThuNgan(weather.forecast[i].wday).c_str());
            lv_label_set_text(forecast_icon_label_[i], GetWeatherIconGlyph(weather.forecast[i].icon));
            ApplyWeatherIconColor(forecast_icon_label_[i], weather.forecast[i].icon);
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f°C", weather.forecast[i].temp_min);
            lv_label_set_text(forecast_temp_label_[i], buf);
        } else {
            lv_label_set_text(forecast_day_label_[i], "-");
            lv_label_set_text(forecast_icon_label_[i], FONT_AWESOME_CLOUD);
            lv_obj_set_style_text_color(forecast_icon_label_[i], COLOR_DIM, 0);
            lv_label_set_text(forecast_temp_label_[i], "--°C");
        }
    }
}

#endif // !CONFIG_USE_EMOTE_MESSAGE_STYLE