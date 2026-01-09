// lunar_widget.h
#ifndef LUNAR_WIDGET_H
#define LUNAR_WIDGET_H

#include <esp_timer.h>
#include <array>
#include <cstdint>
#include <string>

#if defined(CONFIG_USE_EMOTE_MESSAGE_STYLE)
// Stub class if LVGL is disabled
class LunarWidget {
public:
    LunarWidget(void*) {}
    ~LunarWidget() = default;
    void Show() {}
    void Hide() {}
    void UpdateClock() {}
};
#else

#include <lvgl.h>

class LunarWidget {
public:
    explicit LunarWidget(lv_obj_t* parent);
    ~LunarWidget();

    void Show();
    void Hide();
    bool IsVisible() const { return visible_; }
    void UpdateClock();

private:
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* root_ = nullptr;

    // --- UI Elements ---
    
    // 1. Top Strip: Weekly calendar (Lịch tuần)
    lv_obj_t* container_week_strip_ = nullptr;
    struct DayStripItem {
        lv_obj_t* container;
        lv_obj_t* lbl_wday;      // T2, T3...
        lv_obj_t* lbl_solar_day; // 07
    };
    std::array<DayStripItem, 7> week_items_;

    // 2. Main Center: Big Solar Date (Ngày dương to)
    lv_obj_t* container_solar_ = nullptr;
    lv_obj_t* lbl_wday_title_ = nullptr;   // "THỨ TƯ"
    lv_obj_t* lbl_year_ = nullptr;        // "Năm 2026 · Âm 2025 (Ất Tỵ)"
    lv_obj_t* lbl_solar_big_ = nullptr;    // "07"
    lv_obj_t* lbl_digital_time_ = nullptr; // "18:36:08"

    // 3. Bottom Card: Lunar Info (Thông tin âm lịch)
    lv_obj_t* container_lunar_ = nullptr;
    lv_obj_t* lbl_lunar_date_ = nullptr;   // "19/11 (Âm)"
    lv_obj_t* lbl_canchi_day_ = nullptr;   // "Ngày Tân Tỵ"
    lv_obj_t* lbl_day_quality_ = nullptr;  // "Hoàng Đạo"
    lv_obj_t* lbl_solar_term_ = nullptr;   // "Tiết khí: Lập Xuân"

    esp_timer_handle_t timer_ = nullptr;
    bool visible_ = false;
    uint32_t ui_tick_ = 0; // UI rotate tick (seconds)

    // --- Internal Logic ---
    void CreateUI();
    void BuildWeekStrip(lv_obj_t* parent);
    void BuildSolarMain(lv_obj_t* parent);
    void BuildLunarCard(lv_obj_t* parent);
    void UpdateAll();
    
    static void TimerCallback(void* arg);
    static std::string NormalizeVietnameseForFont(const std::string& input);
    static std::string GetWDayString(int wday);
};

#endif
#endif // LUNAR_WIDGET_H