// lunar_calendar.cc
#include "lunar_calendar.h"
#include <cmath>
#include <vector>

namespace {

static constexpr double PI = 3.14159265358979323846;
static const char* STEMS[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
static const char* BRANCHES[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

// 24 Tiết khí, định nghĩa theo kinh độ Mặt Trời, lấy gốc 0° là Xuân phân.
// Thứ tự: 0°, 15°, 30°, ... 345°.
static const char* SOLAR_TERMS_VI[24] = {
    "Xuân phân", "Thanh minh", "Cốc vũ", "Lập hạ", "Tiểu mãn", "Mang chủng",
    "Hạ chí", "Tiểu thử", "Đại thử", "Lập thu", "Xử thử", "Bạch lộ",
    "Thu phân", "Hàn lộ", "Sương giáng", "Lập đông", "Tiểu tuyết", "Đại tuyết",
    "Đông chí", "Tiểu hàn", "Đại hàn", "Lập xuân", "Vũ thủy", "Kinh trập",
};

static int INT(double d) {
    return static_cast<int>(std::floor(d));
}

static int JulianDayFromDate(int dd, int mm, int yy) {
    int a = (14 - mm) / 12;
    int y = yy + 4800 - a;
    int m = mm + 12 * a - 3;
    int jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    if (jd < 2299161) {
        jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
    }
    return jd;
}

static void DateFromJulianDay(int jd, int& dd, int& mm, int& yy) {
    int a, b, c;
    if (jd > 2299160) {
        a = jd + 32044;
        b = (4 * a + 3) / 146097;
        c = a - (b * 146097) / 4;
    } else {
        b = 0;
        c = jd + 32082;
    }
    int d = (4 * c + 3) / 1461;
    int e = c - (1461 * d) / 4;
    int m = (5 * e + 2) / 153;
    dd = e - (153 * m + 2) / 5 + 1;
    mm = m + 3 - 12 * (m / 10);
    yy = b * 100 + d - 4800 + (m / 10);
}

// Astronomical algorithms helpers (simplified logic kept same as original)
static double NewMoon(int k) {
    double T = k / 1236.85;
    double T2 = T * T;
    double T3 = T2 * T;
    double dr = PI / 180.0;
    double Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
    Jd1 += 0.00033 * std::sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
    double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
    double C1 = (0.1734 - 0.000393 * T) * std::sin(M * dr)
              + 0.0021 * std::sin(2 * M * dr)
              - 0.4068 * std::sin(Mpr * dr)
              + 0.0161 * std::sin(2 * Mpr * dr)
              - 0.0004 * std::sin(3 * Mpr * dr)
              + 0.0104 * std::sin(2 * F * dr)
              - 0.0051 * std::sin((M + Mpr) * dr)
              - 0.0074 * std::sin((M - Mpr) * dr)
              + 0.0004 * std::sin((2 * F + M) * dr)
              - 0.0004 * std::sin((2 * F - M) * dr)
              - 0.0006 * std::sin((2 * F + Mpr) * dr)
              + 0.0010 * std::sin((2 * F - Mpr) * dr)
              + 0.0005 * std::sin((2 * Mpr + M) * dr);
    double deltaT = (T < -11) ? (0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3) : (-0.000278 + 0.000265 * T + 0.000262 * T2);
    return Jd1 + C1 - deltaT;
}

static double SunLongitude(double jdn) {
    double T = (jdn - 2451545.0) / 36525.0;
    double T2 = T * T;
    double dr = PI / 180.0;
    double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(dr * M)
              + (0.019993 - 0.000101 * T) * std::sin(dr * 2 * M)
              + 0.000290 * std::sin(dr * 3 * M);
    double L = (L0 + DL) * dr;
    return L - PI * 2.0 * std::floor(L / (PI * 2.0));
}

static int GetNewMoonDay(int k, int timeZone) {
    return INT(NewMoon(k) + 0.5 + static_cast<double>(timeZone) / 24.0);
}

static int GetSunLongitude(int dayNumber, int timeZone) {
    return INT(SunLongitude(dayNumber - 0.5 - static_cast<double>(timeZone) / 24.0) / PI * 6);
}

static int GetLunarMonth11(int yy, int timeZone) {
    int off = JulianDayFromDate(31, 12, yy) - 2415021;
    int k = INT(off / 29.530588853);
    int nm = GetNewMoonDay(k, timeZone);
    if (GetSunLongitude(nm, timeZone) >= 9) nm = GetNewMoonDay(k - 1, timeZone);
    return nm;
}

static int GetLeapMonthOffset(int a11, int timeZone) {
    int k = INT(0.5 + (a11 - 2415021.076998695) / 29.530588853);
    int last = 0, i = 1;
    int arc = GetSunLongitude(GetNewMoonDay(k + i, timeZone), timeZone);
    do {
        last = arc;
        i++;
        arc = GetSunLongitude(GetNewMoonDay(k + i, timeZone), timeZone);
    } while (arc != last && i < 14);
    return i - 1;
}

static std::string StemBranch(int stem_idx, int branch_idx) {
    stem_idx = (stem_idx % 10 + 10) % 10;
    branch_idx = (branch_idx % 12 + 12) % 12;
    return std::string(STEMS[stem_idx]) + " " + BRANCHES[branch_idx];
}

static std::string JoinBranches(const int* idxs, int n) {
    std::string out;
    out.reserve(64);
    for (int i = 0; i < n; ++i) {
        int idx = (idxs[i] % 12 + 12) % 12;
        if (i) out += ", ";
        out += BRANCHES[idx];
    }
    return out;
}

static bool ContainsBranch(const int* idxs, int n, int branch_idx) {
    branch_idx = (branch_idx % 12 + 12) % 12;
    for (int i = 0; i < n; ++i) {
        if (((idxs[i] % 12) + 12) % 12 == branch_idx) return true;
    }
    return false;
}

static int DayBranchIndexFromJulian(int jd) {
    return (jd + 1) % 12;
}

static double SunLongitudeDeg(double jdn) {
    return SunLongitude(jdn) * 180.0 / PI;
}

} // namespace

LunarDate SolarToLunar(int dd, int mm, int yy, int tz_hours) {
    LunarDate out;
    int dayNumber = JulianDayFromDate(dd, mm, yy);
    int k = INT((dayNumber - 2415021.076998695) / 29.530588853);
    int monthStart = GetNewMoonDay(k + 1, tz_hours);
    if (monthStart > dayNumber) monthStart = GetNewMoonDay(k, tz_hours);

    int a11 = GetLunarMonth11(yy, tz_hours);
    int b11 = a11;
    int lunarYear;

    if (a11 >= monthStart) {
        lunarYear = yy;
        a11 = GetLunarMonth11(yy - 1, tz_hours);
    } else {
        lunarYear = yy + 1;
        b11 = GetLunarMonth11(yy + 1, tz_hours);
    }

    int lunarDay = dayNumber - monthStart + 1;
    int diff = INT((monthStart - a11) / 29.0);
    int lunarMonth = diff + 11;
    bool lunarLeap = false;

    if (b11 - a11 > 365) {
        int leapMonthDiff = GetLeapMonthOffset(a11, tz_hours);
        if (diff >= leapMonthDiff) {
            lunarMonth = diff + 10;
            if (diff == leapMonthDiff) lunarLeap = true;
        }
    }

    if (lunarMonth > 12) lunarMonth -= 12;
    if (lunarMonth >= 11 && diff < 4) lunarYear -= 1;

    out.day = lunarDay;
    out.month = lunarMonth;
    out.year = lunarYear;
    out.is_leap = lunarLeap;
    return out;
}

void LunarToSolar(int lday, int lmonth, int lyear, bool is_leap, int tz_hours,
                  int& dd, int& mm, int& yy) {
    int a11, b11;
    if (lmonth < 11) {
        a11 = GetLunarMonth11(lyear - 1, tz_hours);
        b11 = GetLunarMonth11(lyear, tz_hours);
    } else {
        a11 = GetLunarMonth11(lyear, tz_hours);
        b11 = GetLunarMonth11(lyear + 1, tz_hours);
    }
    int k = INT(0.5 + (a11 - 2415021.076998695) / 29.530588853);
    int off = lmonth - 11;
    if (off < 0) off += 12;
    if (b11 - a11 > 365) {
        int leapOff = GetLeapMonthOffset(a11, tz_hours);
        if (is_leap || off >= leapOff) off += 1;
    }
    int monthStart = GetNewMoonDay(k + off, tz_hours);
    DateFromJulianDay(monthStart + lday - 1, dd, mm, yy);
}

std::string CanChiYear(int lunar_year) {
    return StemBranch((lunar_year + 6) % 10, (lunar_year + 8) % 12);
}

std::string CanChiMonth(int lunar_month, int lunar_year, bool is_leap) {
    int chi = (lunar_month + 1) % 12;
    int can = (lunar_year * 12 + lunar_month + 3) % 10;
    std::string res = StemBranch(can, chi);
    return is_leap ? (res + " (nhuận)") : res;
}

std::string CanChiDayFromJulian(int jd) {
    return StemBranch((jd + 9) % 10, (jd + 1) % 12);
}

std::string CanChiDay(int dd, int mm, int yy, int /*tz_hours*/) {
    int jd = JulianDayFromDate(dd, mm, yy);
    return CanChiDayFromJulian(jd);
}

// Phương pháp Ngũ Thử Độn để tính Can giờ
std::string CanChiHour(int hour, int dd, int mm, int yy) {
    // 1. Lấy Can của ngày (Day Stem)
    int jd = JulianDayFromDate(dd, mm, yy);
    int day_stem_idx = (jd + 9) % 10;

    // 2. Lấy Chi của giờ (Hour Branch)
    // 23h-01h: Tý (0), 01h-03h: Sửu (1)...
    // Công thức: ((hour + 1) / 2) % 12
    int hour_branch_idx = ((hour + 1) / 2) % 12;

    // 3. Tính Can của giờ (Hour Stem) dựa trên Can ngày
    // Quy tắc: Ngày Giáp/Kỷ khởi Giáp Tý, Ất/Canh khởi Bính Tý...
    // Formula: (CanNgày % 5) * 2
    int start_hour_stem = (day_stem_idx % 5) * 2; 
    int hour_stem_idx = (start_hour_stem + hour_branch_idx) % 10;

    return StemBranch(hour_stem_idx, hour_branch_idx);
}

int DayBranchIndex(int dd, int mm, int yy) {
    int jd = JulianDayFromDate(dd, mm, yy);
    return DayBranchIndexFromJulian(jd);
}

std::string BranchName(int branch_idx) {
    branch_idx = (branch_idx % 12 + 12) % 12;
    return BRANCHES[branch_idx];
}

std::string AuspiciousHours(int dd, int mm, int yy) {
    // Bảng giờ hoàng đạo theo nhóm ngày: (Tý,Ngọ), (Sửu,Mùi), (Dần,Thân), (Mão,Dậu), (Thìn,Tuất), (Tỵ,Hợi).
    // Mỗi nhóm có 6 giờ hoàng đạo (Chi) trong 12 giờ.
    // Nguồn bảng phổ biến trong lịch vạn niên.
    static constexpr int GOOD_HOURS[6][6] = {
        /* Tý/Ngọ */  {0, 1, 3, 6, 8, 9},
        /* Sửu/Mùi */ {2, 3, 5, 8, 10, 11},
        /* Dần/Thân */{0, 1, 4, 5, 7, 10},
        /* Mão/Dậu */ {0, 2, 3, 6, 7, 9},
        /* Thìn/Tuất */{2, 4, 5, 8, 9, 11},
        /* Tỵ/Hợi */  {1, 4, 6, 7, 10, 11},
    };

    int jd = JulianDayFromDate(dd, mm, yy);
    int day_branch = DayBranchIndexFromJulian(jd);
    int group = day_branch % 6;
    return JoinBranches(GOOD_HOURS[group], 6);
}

DayQuality DayQualityByLunarMonth(int lunar_month, int day_branch_idx) {
    // Bảng hoàng đạo / hắc đạo theo tháng âm (mang tính quy ước phổ biến).
    // Trả về Neutral nếu không thuộc tập hoàng/hắc.
    // Tháng 1 & 7: HD = Tý, Sửu, Tỵ, Mùi; HĐ = Ngọ, Mão, Hợi, Dậu
    // Tháng 2 & 8: HD = Dần, Mão, Mùi, Dậu; HĐ = Thân, Tỵ, Sửu, Hợi
    // Tháng 3 & 9: HD = Thìn, Tỵ, Dậu, Hợi; HĐ = Tuất, Mùi, Sửu, Tý
    // Tháng 4 & 10: HD = Ngọ, Mùi, Sửu, Dậu; HĐ = Tý, Dậu, Tỵ, Mão
    // Tháng 5 & 11: HD = Sửu, Mão, Thân, Dậu; HĐ = Dần, Hợi, Mùi, Tỵ
    // Tháng 6 & 12: HD = Mão, Tỵ, Tuất, Hợi; HĐ = Thìn, Sửu, Dậu, Mùi

    day_branch_idx = (day_branch_idx % 12 + 12) % 12;
    int m = lunar_month;
    if (m < 1 || m > 12) return DayQuality::kNeutral;

    struct Rule {
        int months_a;
        int months_b;
        int good[4];
        int bad[4];
    };

    static const Rule RULES[6] = {
        {1, 7, {0, 1, 5, 7}, {6, 3, 11, 9}},
        {2, 8, {2, 3, 7, 9}, {8, 5, 1, 11}},
        {3, 9, {4, 5, 9, 11}, {10, 7, 1, 0}},
        {4, 10,{6, 7, 1, 9}, {0, 9, 5, 3}},
        {5, 11,{1, 3, 8, 9}, {2, 11, 7, 5}},
        {6, 12,{3, 5, 10, 11}, {4, 1, 9, 7}},
    };

    for (const auto& r : RULES) {
        if (m == r.months_a || m == r.months_b) {
            if (ContainsBranch(r.good, 4, day_branch_idx)) return DayQuality::kGood;
            if (ContainsBranch(r.bad, 4, day_branch_idx)) return DayQuality::kBad;
            return DayQuality::kNeutral;
        }
    }
    return DayQuality::kNeutral;
}

std::string DayQualityText(DayQuality q) {
    switch (q) {
        case DayQuality::kGood: return "Hoàng đạo";
        case DayQuality::kBad: return "Hắc đạo";
        default: return "Bình thường";
    }
}

std::string SolarTermName(int dd, int mm, int yy, int tz_hours) {
    // Xấp xỉ: lấy kinh độ Mặt Trời tại 12:00 giờ địa phương.
    // jd là JDN tại 0:00 UTC tương đối (theo thuật toán dùng trong lịch âm), nên dùng jd - tz/24 để quy về 12:00 địa phương.
    int jd = JulianDayFromDate(dd, mm, yy);
    double jdn = static_cast<double>(jd) - static_cast<double>(tz_hours) / 24.0;
    double deg = SunLongitudeDeg(jdn);
    if (deg < 0) deg += 360.0;
    int idx = static_cast<int>(std::floor(deg / 15.0)) % 24;
    if (idx < 0) idx += 24;
    return SOLAR_TERMS_VI[idx];
}

std::string LunarFestivalName(int lunar_day, int lunar_month) {
    struct Fest {
        int d;
        int m;
        const char* name;
    };
    static const Fest FESTS[] = {
		/* ===== THÁNG 1 ===== */
		{1,  1,  "Tết Nguyên Đán"},
		{2,  1,  "Mùng 2 Tết"},
		{3,  1,  "Mùng 3 Tết / Hóa vàng"},
		{4,  1,  "Mùng 4 Tết"},
		{5,  1,  "Mùng 5 Tết"},
		{6,  1,  "Hạ nêu"},
		{7,  1,  "Khai hạ"},
		{8,  1,  "Ngày vía Ngọc Hoàng"},
		{9,  1,  "Ngày vía Trời"},
		{10, 1,  "Ngày Thần Tài"},
		{14, 1,  "Thượng Nguyên (tiền rằm)"},
		{15, 1,  "Rằm tháng Giêng / Tết Nguyên Tiêu"},
		{16, 1,  "Hạ Nguyên (sau rằm)"},

		/* ===== THÁNG 2 ===== */
		{1,  2,  "Mở cửa mồ mả đầu năm"},
		{2,  2,  "Ngày vía Thổ Địa"},
		{6,  2,  "Ngày vía Phật Di Lặc"},
		{12, 2,  "Ngày vía Quan Âm"},
		{15, 2,  "Rằm tháng Hai"},
		{19, 2,  "Ngày vía Quan Âm đản sinh"},
		{25, 2,  "Lễ tế Xuân"},

		/* ===== THÁNG 3 ===== */
		{3,  3,  "Tết Hàn Thực"},
		{5,  3,  "Thanh Minh (xấp xỉ)"},
		{6,  3,  "Tảo mộ"},
		{10, 3,  "Giỗ Tổ Hùng Vương"},
		{15, 3,  "Rằm tháng Ba"},
		{16, 3,  "Lễ hội mùa Xuân kết thúc"},
		{23, 3,  "Ngày vía Tổ nghề (nhiều ngành)"},

		/* ===== THÁNG 4 ===== */
		{1,  4,  "Khởi đầu an cư (Nam tông)"},
		{8,  4,  "Phật Đản"},
		{14, 4,  "Tiền Phật Đản"},
		{15, 4,  "Rằm tháng Tư"},
		{23, 4,  "Ngày vía Quan Âm thành đạo"},
		{28, 4,  "Lễ hội mùa Hạ bắt đầu"},

		/* ===== THÁNG 5 ===== */
		{5,  5,  "Tết Đoan Ngọ"},
		{7,  5,  "Diệt sâu bọ"},
		{13, 5,  "Ngày vía Quan Âm"},
		{15, 5,  "Rằm tháng Năm"},
		{20, 5,  "Ngày vía Thần Nông"},
		{25, 5,  "Lễ cầu mùa"},

		/* ===== THÁNG 6 ===== */
		{1,  6,  "Mở cửa kho trời (dân gian)"},
		{3,  6,  "Ngày vía Táo Quân phụ"},
		{15, 6,  "Rằm tháng Sáu"},
		{19, 6,  "Ngày vía Quan Âm thành đạo"},
		{24, 6,  "Hội làng mùa Hạ"},

		/* ===== THÁNG 7 ===== */
		{1,  7,  "Mở cửa Quỷ Môn Quan"},
		{7,  7,  "Thất Tịch"},
		{12, 7,  "Ngày vía Thổ Địa"},
		{14, 7,  "Xá tội vong nhân (tiền rằm)"},
		{15, 7,  "Vu Lan / Rằm tháng Bảy"},
		{16, 7,  "Hậu Vu Lan"},
		{20, 7,  "Cúng cô hồn"},
		{29, 7,  "Đóng cửa Quỷ Môn Quan"},
		{30, 7,  "Xá tội vong nhân (cuối tháng)"},

		/* ===== THÁNG 8 ===== */
		{1,  8,  "Khởi thu"},
		{10, 8,  "Ngày vía Thần Tài phụ"},
		{12, 8,  "Ngày vía Quan Âm"},
		{14, 8,  "Tiền Trung Thu"},
		{15, 8,  "Tết Trung Thu"},
		{16, 8,  "Hậu Trung Thu"},
		{18, 8,  "Trăng tàn – kết lễ"},
		{25, 8,  "Lễ hội trăng vùng miền"},

		/* ===== THÁNG 9 ===== */
		{9,  9,  "Tết Trùng Cửu"},
		{10, 9,  "Ngày dưỡng sinh cổ truyền"},
		{15, 9,  "Rằm tháng Chín"},
		{19, 9,  "Ngày vía Quan Âm xuất gia"},
		{23, 9,  "Lễ tạ ơn mùa gặt"},

		/* ===== THÁNG 10 ===== */
		{1,  10, "Khởi đông"},
		{10, 10, "Tết Trùng Thập"},
		{14, 10, "Tiền rằm tháng Mười"},
		{15, 10, "Rằm tháng Mười"},
		{20, 10, "Lễ cơm mới"},
		{25, 10, "Tạ thần nông"},

		/* ===== THÁNG 11 ===== */
		{1,  11, "Lễ cầu an cuối năm"},
		{12, 11, "Ngày vía Quan Âm"},
		{15, 11, "Rằm tháng Mười Một"},
		{20, 11, "Lễ hội tạ đất"},
		{23, 11, "Tiền chạp"},

		/* ===== THÁNG 12 ===== */
		{1,  12, "Khởi chạp"},
		{8,  12, "Phật thành đạo"},
		{12, 12, "Ngày vía Quan Âm"},
		{15, 12, "Rằm tháng Chạp"},
		{16, 12, "Bắt đầu cúng tất niên"},
		{20, 12, "Tảo mộ cuối năm"},
		{23, 12, "Ông Công Ông Táo"},
		{25, 12, "Chạp mả"},
		{29, 12, "Tất niên (thiếu)"},
		{30, 12, "Tất niên (đủ)"},
	};

    for (const auto& f : FESTS) {
        if (lunar_day == f.d && lunar_month == f.m) return f.name;
    }
    return std::string();
}

std::string LunarDayLabel(int lunar_day) {
    if (lunar_day <= 0 || lunar_day > 30) return std::string();
    if (lunar_day == 1) return "Mùng 1";
    if (lunar_day == 15) return "Rằm";
    if (lunar_day >= 2 && lunar_day <= 10) {
        return "Mùng " + std::to_string(lunar_day);
    }
    return "Ngày " + std::to_string(lunar_day);
}
