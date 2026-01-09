// lunar_widget.cc
#include "lunar_widget.h"

#if !defined(CONFIG_USE_EMOTE_MESSAGE_STYLE)

#include "application.h"
#include "board.h"
#include "display.h"
#include "lunar_calendar.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <vector>
#include <esp_log.h>

#define TAG "LunarWidget"

// Fonts Declaration
LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_ds_digitb_48); // Font số to

// --- COLOR PALETTE DEFINITION ---
// Common Colors
#define CLR_ACCENT          lv_color_hex(0x3366FF) // Xanh dương chủ đạo
#define CLR_HIGHLIGHT       lv_color_hex(0x00D97E) // Xanh lá nhấn (Hoàng đạo)
#define CLR_BAD             lv_color_hex(0xE63946) // Đỏ (Hắc đạo/CN)

// Day Theme (Light)
#define CLR_DAY_BG          lv_color_hex(0xF0F4F8) // Nền sáng
#define CLR_DAY_CARD        lv_color_hex(0xFFFFFF) // Thẻ trắng
#define CLR_DAY_TXT_MAIN    lv_color_hex(0x102A43) // Chữ đậm
#define CLR_DAY_TXT_SUB     lv_color_hex(0x627D98) // Chữ mờ

// Night Theme (Dark)
#define CLR_NIGHT_BG        lv_color_hex(0x000000) // Nền đen
#define CLR_NIGHT_CARD      lv_color_hex(0x1A1A1A) // Thẻ xám đậm
#define CLR_NIGHT_TXT_MAIN  lv_color_hex(0xFFFFFF) // Chữ trắng
#define CLR_NIGHT_TXT_SUB   lv_color_hex(0xA0A0A0) // Chữ xám sáng


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

std::string LunarWidget::NormalizeVietnameseForFont(const std::string& input) {
    // Một số font UI có thể thiếu glyph IN HOA có dấu. Hạ về chữ thường để tránh ô vuông.
    std::string out = input;

    // ===== A / Ă / Â =====
    ReplaceAllUtf8InPlace(out, "Á", "á");
    ReplaceAllUtf8InPlace(out, "À", "à");
    ReplaceAllUtf8InPlace(out, "Ả", "ả");
    ReplaceAllUtf8InPlace(out, "Ã", "ã");
    ReplaceAllUtf8InPlace(out, "Ạ", "ạ");

    ReplaceAllUtf8InPlace(out, "Ă", "ă");
    ReplaceAllUtf8InPlace(out, "Ắ", "ắ");
    ReplaceAllUtf8InPlace(out, "Ằ", "ằ");
    ReplaceAllUtf8InPlace(out, "Ẳ", "ẳ");
    ReplaceAllUtf8InPlace(out, "Ẵ", "ẵ");
    ReplaceAllUtf8InPlace(out, "Ặ", "ặ");

    ReplaceAllUtf8InPlace(out, "Â", "â");
    ReplaceAllUtf8InPlace(out, "Ấ", "ấ");
    ReplaceAllUtf8InPlace(out, "Ầ", "ầ");
    ReplaceAllUtf8InPlace(out, "Ẩ", "ẩ");
    ReplaceAllUtf8InPlace(out, "Ẫ", "ẫ");
    ReplaceAllUtf8InPlace(out, "Ậ", "ậ");

    // ===== D (Đ) =====
    ReplaceAllUtf8InPlace(out, "Đ", "đ");

    // ===== E / Ê =====
    ReplaceAllUtf8InPlace(out, "É", "é");
    ReplaceAllUtf8InPlace(out, "È", "è");
    ReplaceAllUtf8InPlace(out, "Ẻ", "ẻ");
    ReplaceAllUtf8InPlace(out, "Ẽ", "ẽ");
    ReplaceAllUtf8InPlace(out, "Ẹ", "ẹ");

    ReplaceAllUtf8InPlace(out, "Ê", "ê");
    ReplaceAllUtf8InPlace(out, "Ế", "ế");
    ReplaceAllUtf8InPlace(out, "Ề", "ề");
    ReplaceAllUtf8InPlace(out, "Ể", "ể");
    ReplaceAllUtf8InPlace(out, "Ễ", "ễ");
    ReplaceAllUtf8InPlace(out, "Ệ", "ệ");

    // ===== I =====
    ReplaceAllUtf8InPlace(out, "Í", "í");
    ReplaceAllUtf8InPlace(out, "Ì", "ì");
    ReplaceAllUtf8InPlace(out, "Ỉ", "ỉ");
    ReplaceAllUtf8InPlace(out, "Ĩ", "ĩ");
    ReplaceAllUtf8InPlace(out, "Ị", "ị");

    // ===== O / Ô / Ơ =====
    ReplaceAllUtf8InPlace(out, "Ó", "ó");
    ReplaceAllUtf8InPlace(out, "Ò", "ò");
    ReplaceAllUtf8InPlace(out, "Ỏ", "ỏ");
    ReplaceAllUtf8InPlace(out, "Õ", "õ");
    ReplaceAllUtf8InPlace(out, "Ọ", "ọ");

    ReplaceAllUtf8InPlace(out, "Ô", "ô");
    ReplaceAllUtf8InPlace(out, "Ố", "ố");
    ReplaceAllUtf8InPlace(out, "Ồ", "ồ");
    ReplaceAllUtf8InPlace(out, "Ổ", "ổ");
    ReplaceAllUtf8InPlace(out, "Ỗ", "ỗ");
    ReplaceAllUtf8InPlace(out, "Ộ", "ộ");

    ReplaceAllUtf8InPlace(out, "Ơ", "ơ");
    ReplaceAllUtf8InPlace(out, "Ớ", "ớ");
    ReplaceAllUtf8InPlace(out, "Ờ", "ờ");
    ReplaceAllUtf8InPlace(out, "Ở", "ở");
    ReplaceAllUtf8InPlace(out, "Ỡ", "ỡ");
    ReplaceAllUtf8InPlace(out, "Ợ", "ợ");

    // ===== U / Ư =====
    ReplaceAllUtf8InPlace(out, "Ú", "ú");
    ReplaceAllUtf8InPlace(out, "Ù", "ù");
    ReplaceAllUtf8InPlace(out, "Ủ", "ủ");
    ReplaceAllUtf8InPlace(out, "Ũ", "ũ");
    ReplaceAllUtf8InPlace(out, "Ụ", "ụ");

    ReplaceAllUtf8InPlace(out, "Ư", "ư");
    ReplaceAllUtf8InPlace(out, "Ứ", "ứ");
    ReplaceAllUtf8InPlace(out, "Ừ", "ừ");
    ReplaceAllUtf8InPlace(out, "Ử", "ử");
    ReplaceAllUtf8InPlace(out, "Ữ", "ữ");
    ReplaceAllUtf8InPlace(out, "Ự", "ự");

    // ===== Y =====
    ReplaceAllUtf8InPlace(out, "Ý", "ý");
    ReplaceAllUtf8InPlace(out, "Ỳ", "ỳ");
    ReplaceAllUtf8InPlace(out, "Ỷ", "ỷ");
    ReplaceAllUtf8InPlace(out, "Ỹ", "ỹ");
    ReplaceAllUtf8InPlace(out, "Ỵ", "ỵ");

    return out;
}

std::string LunarWidget::GetWDayString(int wday) {
    const char* days[] = {"CHỦ NHẬT", "THỨ HAI", "THỨ BA", "THỨ TƯ", "THỨ NĂM", "THỨ SÁU", "THỨ BẢY"};
    if (wday >= 0 && wday <= 6) {
        return NormalizeVietnameseForFont(days[wday]);
    }
    return "";
}

LunarWidget::LunarWidget(lv_obj_t* parent) : parent_(parent) {
    CreateUI();
}

LunarWidget::~LunarWidget() {
    if (timer_) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_del(root_);
    }
}

void LunarWidget::CreateUI() {
    if (!parent_) return;

    // Root Container - Vertical Flex
    root_ = lv_obj_create(parent_);
    lv_obj_set_size(root_, 240, 320); // Fixed size
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    
    // Căn giữa các phần tử con theo chiều ngang
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Màu nền mặc định (sẽ được cập nhật ngay trong UpdateAll)
    lv_obj_set_style_bg_color(root_, CLR_DAY_BG, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Build Top Week Strip
    BuildWeekStrip(root_);
    
    // 2. Build Main Center
    BuildSolarMain(root_);
    
    // 3. Build Bottom Lunar Info
    BuildLunarCard(root_);

    // Timer
    const esp_timer_create_args_t targs = {
        .callback = &LunarWidget::TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lunar_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&targs, &timer_);
}

void LunarWidget::BuildWeekStrip(lv_obj_t* parent) {
    container_week_strip_ = lv_obj_create(parent);
    lv_obj_set_size(container_week_strip_, LV_PCT(100), 55); 
    lv_obj_set_style_bg_opa(container_week_strip_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_week_strip_, 0, 0);
    lv_obj_set_style_pad_all(container_week_strip_, 4, 0);
    lv_obj_set_style_pad_gap(container_week_strip_, 2, 0);
    lv_obj_set_flex_flow(container_week_strip_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_week_strip_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container_week_strip_, LV_OBJ_FLAG_SCROLLABLE);

    const char* short_days[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};

    for (int i = 0; i < 7; ++i) {
        lv_obj_t* item = lv_obj_create(container_week_strip_);
        lv_obj_set_size(item, 30, 45);
        lv_obj_set_style_bg_color(item, CLR_DAY_CARD, 0); // Default Day
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0); 
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 6, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        // Weekday Name
        lv_obj_t* l_day = lv_label_create(item);
        lv_label_set_text(l_day, NormalizeVietnameseForFont(short_days[i]).c_str());
        lv_obj_set_style_text_font(l_day, &font_puhui_14_1, 0);
        lv_obj_set_style_text_color(l_day, CLR_DAY_TXT_SUB, 0);

        // Solar Day
        lv_obj_t* l_solar = lv_label_create(item);
        lv_label_set_text(l_solar, "00");
        lv_obj_set_style_text_font(l_solar, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l_solar, CLR_DAY_TXT_MAIN, 0);

        week_items_[i] = {item, l_day, l_solar};
    }
}

void LunarWidget::BuildSolarMain(lv_obj_t* parent) {
    container_solar_ = lv_obj_create(parent);
    lv_obj_set_size(container_solar_, LV_PCT(100), 130);
    lv_obj_set_style_bg_opa(container_solar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_solar_, 0, 0);
    lv_obj_set_flex_flow(container_solar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_solar_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container_solar_, LV_OBJ_FLAG_SCROLLABLE);

    // Thứ...
    lbl_wday_title_ = lv_label_create(container_solar_);
    lv_obj_set_style_text_font(lbl_wday_title_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(lbl_wday_title_, CLR_ACCENT, 0);

    // Năm
    lbl_year_ = lv_label_create(container_solar_);
    lv_obj_set_style_text_font(lbl_year_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(lbl_year_, CLR_DAY_TXT_SUB, 0);

    // Ngày To
    lbl_solar_big_ = lv_label_create(container_solar_);
    lv_obj_set_style_text_font(lbl_solar_big_, &lv_font_ds_digitb_48, 0);
    lv_obj_set_style_text_color(lbl_solar_big_, CLR_DAY_TXT_MAIN, 0);
    lv_obj_set_style_text_line_space(lbl_solar_big_, -5, 0);

    // Giờ Digital
    lbl_digital_time_ = lv_label_create(container_solar_);
    lv_obj_set_style_text_font(lbl_digital_time_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_digital_time_, CLR_DAY_TXT_SUB, 0);
    lv_obj_set_style_pad_top(lbl_digital_time_, 6, 0);
}

void LunarWidget::BuildLunarCard(lv_obj_t* parent) {
    container_lunar_ = lv_obj_create(parent);
    lv_obj_set_width(container_lunar_, LV_PCT(100)); // Full width
    lv_obj_set_flex_grow(container_lunar_, 1); 
    lv_obj_set_style_bg_color(container_lunar_, CLR_DAY_CARD, 0);
    lv_obj_set_style_radius(container_lunar_, 12, 0);
    lv_obj_set_style_border_width(container_lunar_, 0, 0);
    lv_obj_set_style_pad_all(container_lunar_, 10, 0);
    lv_obj_set_style_pad_gap(container_lunar_, 5, 0);

    // Shadow
    lv_obj_set_style_shadow_width(container_lunar_, 16, 0);
    lv_obj_set_style_shadow_opa(container_lunar_, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(container_lunar_, 4, 0);
    lv_obj_set_style_shadow_color(container_lunar_, lv_color_hex(0x000000), 0);

    lv_obj_set_flex_flow(container_lunar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_lunar_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container_lunar_, LV_OBJ_FLAG_SCROLLABLE);

    // Lunar Date Row
    lv_obj_t* row1 = lv_obj_create(container_lunar_);
    lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Icon/Text
    lv_obj_t* lbl_icon = lv_label_create(row1);
    lv_label_set_text(lbl_icon, NormalizeVietnameseForFont("Âm lịch:").c_str());
    lv_obj_set_style_text_font(lbl_icon, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(lbl_icon, CLR_DAY_TXT_SUB, 0);
    lv_obj_set_style_pad_right(lbl_icon, 5, 0);

    lbl_lunar_date_ = lv_label_create(row1);
    lv_obj_set_style_text_font(lbl_lunar_date_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_lunar_date_, CLR_ACCENT, 0);

    // Can Chi Day
    lbl_canchi_day_ = lv_label_create(container_lunar_);
    lv_obj_set_style_text_font(lbl_canchi_day_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(lbl_canchi_day_, CLR_DAY_TXT_MAIN, 0);
    lv_label_set_long_mode(lbl_canchi_day_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_canchi_day_, LV_PCT(100));
    lv_obj_set_style_text_align(lbl_canchi_day_, LV_TEXT_ALIGN_CENTER, 0);   

    // Separator
    lv_obj_t* line = lv_obj_create(container_lunar_);
    lv_obj_set_size(line, LV_PCT(60), 1);
    lv_obj_set_style_bg_color(line, CLR_DAY_TXT_SUB, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_50, 0);

    // Good/Bad Day
    lbl_day_quality_ = lv_label_create(container_lunar_);
    lv_obj_set_style_text_font(lbl_day_quality_, &font_puhui_14_1, 0);
    
    // Solar Term
    lbl_solar_term_ = lv_label_create(container_lunar_);
    lv_obj_set_style_text_font(lbl_solar_term_, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(lbl_solar_term_, CLR_DAY_TXT_SUB, 0);
    lv_label_set_long_mode(lbl_solar_term_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_solar_term_, LV_PCT(100));
    lv_obj_set_style_text_align(lbl_solar_term_, LV_TEXT_ALIGN_CENTER, 0);
}

void LunarWidget::TimerCallback(void* arg) {
    auto* self = static_cast<LunarWidget*>(arg);
    if (!self || !self->IsVisible()) return;

    Application::GetInstance().Schedule([self]() {
        if (!self->IsVisible()) return;
        Display* display = Board::GetInstance().GetDisplay();
        if (!display) return;
        DisplayLockGuard lock(display);
        self->UpdateClock();
    });
}

void LunarWidget::UpdateClock() {
    UpdateAll();
}

void LunarWidget::UpdateAll() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    ui_tick_++;

    // ==========================================
    // LOGIC CHUYỂN MÀU NỀN TỰ ĐỘNG (DAY/NIGHT)
    // ==========================================
    // Ban đêm: từ 18h đến 6h sáng
    bool is_night = (t.tm_hour >= 18 || t.tm_hour < 6);
    static int prev_night_mode = -1; // -1 để ép cập nhật lần đầu

    if (prev_night_mode != (int)is_night) {
        prev_night_mode = is_night;
        
        // Chọn bộ màu
        lv_color_t c_bg   = is_night ? CLR_NIGHT_BG : CLR_DAY_BG;
        lv_color_t c_card = is_night ? CLR_NIGHT_CARD : CLR_DAY_CARD;
        lv_color_t c_main = is_night ? CLR_NIGHT_TXT_MAIN : CLR_DAY_TXT_MAIN;
        lv_color_t c_sub  = is_night ? CLR_NIGHT_TXT_SUB : CLR_DAY_TXT_SUB;

        // Apply Global
        lv_obj_set_style_bg_color(root_, c_bg, 0);

        // Apply Week Strip
        for(int i=0; i<7; ++i) {
            lv_obj_set_style_bg_color(week_items_[i].container, c_card, 0);
            lv_obj_set_style_text_color(week_items_[i].lbl_wday, c_sub, 0);
            // Solar day sẽ được cập nhật màu ở logic active/inactive bên dưới
        }

        // Apply Solar Main
        lv_obj_set_style_text_color(lbl_year_, c_sub, 0);
        lv_obj_set_style_text_color(lbl_solar_big_, c_main, 0);
        lv_obj_set_style_text_color(lbl_digital_time_, c_sub, 0);

        // Apply Lunar Card
        lv_obj_set_style_bg_color(container_lunar_, c_card, 0);    
        lv_obj_set_style_text_color(lbl_canchi_day_, c_main, 0);
    }

    // ==========================================
    // DATA UPDATE LOGIC
    // ==========================================

    int dd = t.tm_mday;
    int mm = t.tm_mon + 1;
    int yy = t.tm_year + 1900;
    int wday = t.tm_wday; // 0=Sun

    // Precompute lunar date once per tick
    LunarDate lunar = SolarToLunar(dd, mm, yy, LUNAR_TZ_HOURS);

    // --- 1. Top Week Strip ---
    int current_idx = (wday == 0) ? 6 : (wday - 1);
    time_t t_monday = now - (current_idx * 86400); 

    // Màu text hiện tại (dựa vào night mode)
    lv_color_t c_txt_main = is_night ? CLR_NIGHT_TXT_MAIN : CLR_DAY_TXT_MAIN;
    lv_color_t c_txt_sub  = is_night ? CLR_NIGHT_TXT_SUB : CLR_DAY_TXT_SUB;

    for (int i = 0; i < 7; ++i) {
        time_t t_item = t_monday + (i * 86400);
        struct tm ti;
        localtime_r(&t_item, &ti);
        
        lv_label_set_text_fmt(week_items_[i].lbl_solar_day, "%02d", ti.tm_mday);

        // Highlight Active Day
        if (i == current_idx) {
            lv_obj_set_style_bg_color(week_items_[i].container, CLR_ACCENT, 0);
            lv_obj_set_style_bg_opa(week_items_[i].container, LV_OPA_COVER, 0);

            lv_obj_set_style_shadow_width(week_items_[i].container, 10, 0);
            lv_obj_set_style_shadow_opa(week_items_[i].container, LV_OPA_30, 0);
            lv_obj_set_style_shadow_offset_y(week_items_[i].container, 2, 0);
            lv_obj_set_style_shadow_color(week_items_[i].container, CLR_ACCENT, 0);

            lv_obj_set_style_text_color(week_items_[i].lbl_wday, lv_color_white(), 0);
            lv_obj_set_style_text_color(week_items_[i].lbl_solar_day, lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_opa(week_items_[i].container, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(week_items_[i].container, 0, 0);
            
            // Sunday Color
            if (i == 6) {
                lv_obj_set_style_text_color(week_items_[i].lbl_wday, CLR_BAD, 0);
                lv_obj_set_style_text_color(week_items_[i].lbl_solar_day, CLR_BAD, 0);
            } else {
                lv_obj_set_style_text_color(week_items_[i].lbl_wday, c_txt_sub, 0);
                lv_obj_set_style_text_color(week_items_[i].lbl_solar_day, c_txt_main, 0);
            }
        }
    }

    // --- 2. Main Center ---
    lv_label_set_text(lbl_wday_title_, GetWDayString(wday).c_str());

    {
        std::string s_year = "Dương " + std::to_string(yy) + " · Âm " + std::to_string(lunar.year) +
                             " (" + CanChiYear(lunar.year) + ")";
        lv_label_set_text(lbl_year_, NormalizeVietnameseForFont(s_year).c_str());
    }

    lv_label_set_text_fmt(lbl_solar_big_, "%d", dd);
    lv_label_set_text_fmt(lbl_digital_time_, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);

    // --- 3. Lunar Info ---
    char buf_lunar[32];
    snprintf(buf_lunar, sizeof(buf_lunar), "%02d / %02d%s", lunar.day, lunar.month, lunar.is_leap ? " N" : "");
    lv_label_set_text(lbl_lunar_date_, buf_lunar);

    const int day_branch_idx = DayBranchIndex(dd, mm, yy);
    const DayQuality q = DayQualityByLunarMonth(lunar.month, day_branch_idx);

    {
        const uint32_t kRotateSec = 3; 
        const int mode = (ui_tick_ / kRotateSec) % 6;

        std::string s;
        switch (mode) {
            case 0: {
                std::string lbl = LunarDayLabel(lunar.day);
                s = "Âm: " + lbl + " " + std::to_string(lunar.day) + "/" + std::to_string(lunar.month);
                if (lunar.is_leap) s += " (Nhuận)";
                break;
            }
            case 1:
                s = "Ngày " + CanChiDay(dd, mm, yy, LUNAR_TZ_HOURS);
                break;
            case 2:
                s = "Tháng " + CanChiMonth(lunar.month, lunar.year, lunar.is_leap);
                break;
            case 3:
                s = "Năm " + CanChiYear(lunar.year);
                break;
            case 4:
                s = "Giờ " + CanChiHour(t.tm_hour, dd, mm, yy);
                break;
            case 5:
            default:
                s = "Chi ngày: " + BranchName(day_branch_idx);
                break;
        }
        lv_label_set_text(lbl_canchi_day_, NormalizeVietnameseForFont(s).c_str());
    }

    // Set quality text & color
    if (q == DayQuality::kGood) {
        lv_label_set_text(lbl_day_quality_, NormalizeVietnameseForFont("★ Ngày Hoàng Đạo").c_str());
        lv_obj_set_style_text_color(lbl_day_quality_, CLR_HIGHLIGHT, 0);
    } else if (q == DayQuality::kBad) {
        lv_label_set_text(lbl_day_quality_, NormalizeVietnameseForFont("● Ngày Hắc Đạo").c_str());
        lv_obj_set_style_text_color(lbl_day_quality_, CLR_BAD, 0);
    } else {
        lv_label_set_text(lbl_day_quality_, NormalizeVietnameseForFont("Ngày Bình Thường").c_str());
        lv_obj_set_style_text_color(lbl_day_quality_, c_txt_sub, 0); // Màu text phụ theo theme
    }

    // Footer Info
    {
        const std::string fest = LunarFestivalName(lunar.day, lunar.month);
        const std::string term = SolarTermName(dd, mm, yy, LUNAR_TZ_HOURS);

        bool showing_hours = ((ui_tick_ / 6) % 2) == 1;
        if (showing_hours) {
            std::string hrs = AuspiciousHours(dd, mm, yy);
            if (!hrs.empty()) {
                std::string s = "Giờ hoàng đạo: " + hrs;
                lv_label_set_text(lbl_solar_term_, NormalizeVietnameseForFont(s).c_str());
                lv_obj_set_style_text_color(lbl_solar_term_, CLR_HIGHLIGHT, 0);
            } else {
                showing_hours = false;
            }
        }

        if (!showing_hours) {
            if (!fest.empty()) {
                lv_label_set_text(lbl_solar_term_, NormalizeVietnameseForFont(fest).c_str());
                lv_obj_set_style_text_color(lbl_solar_term_, CLR_BAD, 0); // Highlight festival
            } else {
                lv_label_set_text(lbl_solar_term_, NormalizeVietnameseForFont(term).c_str());
                lv_obj_set_style_text_color(lbl_solar_term_, c_txt_sub, 0); // Màu text phụ theo theme
            }
        }
    }
}

void LunarWidget::Show() {
    if (root_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        visible_ = true;
        UpdateAll();
        if (timer_) esp_timer_start_periodic(timer_, 1000000); // 1s
    }
}

void LunarWidget::Hide() {
    if (root_) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        visible_ = false;
        if (timer_) esp_timer_stop(timer_);
    }
}

#endif