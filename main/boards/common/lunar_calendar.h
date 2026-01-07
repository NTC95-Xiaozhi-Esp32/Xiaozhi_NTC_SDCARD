// lunar_calendar.h
#ifndef LUNAR_CALENDAR_H
#define LUNAR_CALENDAR_H

#include <cstdint>
#include <string>

// Default timezone used for lunar conversion.
#ifndef LUNAR_TZ_HOURS
#define LUNAR_TZ_HOURS 7
#endif

struct LunarDate {
    int day = 0;
    int month = 0;
    int year = 0;
    bool is_leap = false;
};

// Đánh giá ngày theo lịch can chi (mang tính tham khảo / lịch vạn niên).
enum class DayQuality {
    kGood,
    kBad,
    kNeutral,
};

LunarDate SolarToLunar(int dd, int mm, int yy, int tz_hours = LUNAR_TZ_HOURS);

void LunarToSolar(int lday, int lmonth, int lyear, bool is_leap, int tz_hours,
                  int& dd, int& mm, int& yy);

// --- Can-Chi Helpers ---
std::string CanChiYear(int lunar_year);
std::string CanChiMonth(int lunar_month, int lunar_year, bool is_leap = false);
std::string CanChiDayFromJulian(int jd);
std::string CanChiDay(int dd, int mm, int yy, int tz_hours = LUNAR_TZ_HOURS);

// MỚI: Tính Can Chi của giờ hiện tại (ví dụ: Giờ Đinh Dậu)
std::string CanChiHour(int hour, int dd, int mm, int yy);

// --- Nâng cấp: thông tin lịch vạn niên phổ biến ---

// Chi của ngày (0=Tý, 1=Sửu, ..., 11=Hợi).
int DayBranchIndex(int dd, int mm, int yy);

// Tên Chi theo index.
std::string BranchName(int branch_idx);

// Giờ hoàng đạo trong ngày (danh sách Chi, ví dụ: "Sửu, Thìn, Ngọ, Mùi, Tuất, Hợi").
std::string AuspiciousHours(int dd, int mm, int yy);

// Hoàng đạo / Hắc đạo theo tháng âm (bảng phổ biến trong lịch vạn niên).
DayQuality DayQualityByLunarMonth(int lunar_month, int day_branch_idx);
std::string DayQualityText(DayQuality q);

// Tiết khí hiện tại (xấp xỉ theo kinh độ Mặt Trời).
std::string SolarTermName(int dd, int mm, int yy, int tz_hours = LUNAR_TZ_HOURS);

// Ngày lễ âm lịch phổ biến (trả về chuỗi rỗng nếu không có).
std::string LunarFestivalName(int lunar_day, int lunar_month);

// Nhãn ngắn cho ngày âm ("Mùng 1", "Rằm", ...).
std::string LunarDayLabel(int lunar_day);

#endif // LUNAR_CALENDAR_H
