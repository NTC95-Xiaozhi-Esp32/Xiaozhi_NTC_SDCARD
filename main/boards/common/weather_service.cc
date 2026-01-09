#include "weather_service.h"

#include "application.h"
#include "board.h"
#include "system_info.h"

#include <esp_log.h>
#include <esp_err.h>

#include <nvs.h>
#include <nvs_flash.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <ctime>
#include <limits>
#include <cstdio> // snprintf

#include <esp_sntp.h>

// Theo logic trong weather.txt: dùng esp_http_client, tăng buffer cho forecast JSON lớn.
#include <esp_http_client.h>
#include <esp_crt_bundle.h>

#define TAG "WeatherService"

namespace {

// Trim ASCII whitespace
static inline std::string TrimAscii(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

static inline std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'A' && uc <= 'Z') c = static_cast<char>(uc - 'A' + 'a');
    }
    return s;
}

static inline bool IsAutoCityValue(const std::string& city) {
    const std::string v = ToLowerAscii(TrimAscii(city));
    return v.empty() || v == "auto";
}

static bool ReadNvsString(const char* ns, const char* key, std::string& out) {
    out.clear();

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return false;

    size_t required = 0;
    err = nvs_get_str(h, key, nullptr, &required);
    if (err != ESP_OK || required <= 1) {
        nvs_close(h);
        return false;
    }

    std::string buf;
    buf.resize(required);
    err = nvs_get_str(h, key, buf.data(), &required);
    nvs_close(h);
    if (err != ESP_OK) return false;

    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    out = TrimAscii(buf);
    return !out.empty();
}

static void ClearWeatherData(WeatherData& w) {
    w = WeatherData{};
    for (auto& d : w.forecast) d = DailyForecast{};
}

static void ClearForecastOnly(WeatherData& w) {
    for (auto& d : w.forecast) d = DailyForecast{};
    w.forecast_count = 0;
}

static bool UnixUtcToLocalTmShifted(int64_t unix_utc, int tz_offset_seconds, struct tm* out) {
    if (!out) return false;
    time_t t = static_cast<time_t>(unix_utc + tz_offset_seconds);
    return gmtime_r(&t, out) != nullptr; // dùng gmtime vì đã shift +offset
}

static int ParseUtcOffsetToSeconds(const char* s) {
    // Hỗ trợ:
    // - "+09:00", "-03:30"
    // - "+0900", "-0800"
    // - "+9" (giờ)
    if (!s || !*s) return 0;

    int sign = 1;
    if (*s == '+') { sign = 1; s++; }
    else if (*s == '-') { sign = -1; s++; }

    int hh = 0;
    int mm = 0;

    if (strchr(s, ':')) {
        if (sscanf(s, "%d:%d", &hh, &mm) >= 1) {
            return sign * (hh * 3600 + mm * 60);
        }
        return 0;
    }

    const size_t len = strlen(s);
    if (len >= 4 && std::isdigit(static_cast<unsigned char>(s[0])) && std::isdigit(static_cast<unsigned char>(s[1])) &&
        std::isdigit(static_cast<unsigned char>(s[2])) && std::isdigit(static_cast<unsigned char>(s[3]))) {
        hh = (s[0] - '0') * 10 + (s[1] - '0');
        mm = (s[2] - '0') * 10 + (s[3] - '0');
        return sign * (hh * 3600 + mm * 60);
    }

    if (sscanf(s, "%d", &hh) == 1) {
        return sign * (hh * 3600);
    }

    return 0;
}

static bool HasValidLatLon(float lat, float lon) {
    if (!std::isfinite(lat) || !std::isfinite(lon)) return false;
    if (lat < -90.0f || lat > 90.0f) return false;
    if (lon < -180.0f || lon > 180.0f) return false;
    // Tránh coi 0,0 là hợp lệ nếu thiếu city (đa phần là lỗi/thiếu dữ liệu)
    if (std::fabs(lat) < 0.0001f && std::fabs(lon) < 0.0001f) return false;
    return true;
}

static int ScoreLocation(const GeoLocation& loc) {
    int score = 0;
    if (HasValidLatLon(loc.latitude, loc.longitude)) score += 2;
    if (!loc.city.empty()) score += 1;
    if (loc.has_timezone) score += 1;
    return score;
}

// Helper: Cập nhật biến môi trường TZ của hệ thống (POSIX format)
// ESP32/POSIX TZ format: "STD[-offset]DST"
// Offset là giờ phải cộng vào Local để ra UTC.
// Ví dụ: VN là UTC+7 -> Local + (-7) = UTC -> Offset ghi là -7 -> "UTC-7"
static void ApplySystemTimezone(int offset_seconds) {
    // Tính giờ và phút
    // Đảo dấu offset vì POSIX yêu cầu ngược (West +, East -)
    int hours = -offset_seconds / 3600;
    int minutes = (std::abs(offset_seconds) % 3600) / 60;
    
    char tz_buf[32];
    if (minutes == 0) {
        snprintf(tz_buf, sizeof(tz_buf), "UTC%+d", hours);
    } else {
        snprintf(tz_buf, sizeof(tz_buf), "UTC%+d:%02d", hours, minutes);
    }

    setenv("TZ", tz_buf, 1);
    tzset();
    ESP_LOGI(TAG, "System TZ updated to: %s (offset %d s)", tz_buf, offset_seconds);
}

} // namespace

WeatherService::WeatherService() = default;

WeatherService::~WeatherService() {
    Stop();
}

void WeatherService::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Weather service already running");
        return;
    }

    ESP_LOGI(TAG, "Starting weather service");
    running_ = true;

    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
    }

    // SNTP chạy nền, không block
    InitializeSntp();

    // Worker task (tất cả network/JSON chạy ở đây để không treo UI)
    if (!worker_task_handle_) {
        xTaskCreate(&WeatherService::WorkerTask, "weather_worker", 8192, this, 5, &worker_task_handle_);
    }

    // Periodic timer: chỉ notify worker
    esp_timer_create_args_t timer_args{};
    timer_args.callback = &WeatherService::UpdateTimerCallback;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "weather_update";

    esp_err_t err = esp_timer_create(&timer_args, &update_timer_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        running_ = false;
        return;
    }

    ESP_ERROR_CHECK(esp_timer_start_periodic(update_timer_, UPDATE_INTERVAL_US));
    ESP_LOGI(TAG, "Weather service started, update interval: %llu seconds",
             static_cast<unsigned long long>(UPDATE_INTERVAL_US / 1000000ULL));

    // Update lần đầu: NON-BLOCKING
    UpdateWeatherNow();
}

void WeatherService::Stop() {
    if (!running_) return;
    running_ = false;

    if (update_timer_) {
        esp_timer_stop(update_timer_);
        esp_timer_delete(update_timer_);
        update_timer_ = nullptr;
    }

    if (worker_task_handle_) {
        // FIX: Không gọi vTaskDelete trực tiếp vì task tự delete.
        // Chỉ cần notify để đánh thức task, nó sẽ check running_ = false và tự thoát.
        xTaskNotifyGive(worker_task_handle_);
        worker_task_handle_ = nullptr;
    }

    if (sntp_task_handle_) {
        // FIX: Không gọi vTaskDelete trực tiếp vì rủi ro task đã tự kết thúc.
        // SntpPollTask sẽ check running_ trong vòng lặp và tự thoát.
        sntp_task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "Weather service stopped");
}

WeatherData WeatherService::GetCurrentWeather() {
    WeatherData out;
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    out = current_weather_;
    if (mutex_) xSemaphoreGive(mutex_);
    return out;
}

GeoLocation WeatherService::GetCurrentLocation() {
    GeoLocation out;
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    out = current_location_;
    if (mutex_) xSemaphoreGive(mutex_);
    return out;
}

bool WeatherService::UpdateWeatherNow() {
    if (!running_ || !worker_task_handle_) {
        ESP_LOGW(TAG, "Weather service not ready");
        return false;
    }
    // Chỉ notify, không làm network ở đây
    xTaskNotifyGive(worker_task_handle_);
    return true;
}

void WeatherService::OnWeatherUpdated(std::function<void(const WeatherData&)> callback) {
    on_weather_updated_ = std::move(callback);
}

void WeatherService::OnError(std::function<void(const std::string&)> callback) {
    on_error_ = std::move(callback);
}

void WeatherService::UpdateTimerCallback(void* arg) {
    auto* self = static_cast<WeatherService*>(arg);
    if (!self || !self->running_ || !self->worker_task_handle_) return;
    xTaskNotifyGive(self->worker_task_handle_);
}

void WeatherService::WorkerTask(void* arg) {
    auto* self = static_cast<WeatherService*>(arg);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }

    // Đợi notify rồi mới update (tránh chạy ngay lúc boot nếu không cần)
    while (self->running_) {
        // block cho tới khi có notify (timer hoặc UpdateWeatherNow)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!self->running_) break;
        self->DoOneUpdate();
    }

    vTaskDelete(nullptr);
}

void WeatherService::DoOneUpdate() {
    ESP_LOGI(TAG, "Performing weather update (worker)");

    // Nạp cấu hình mỗi lần để bắt config mới
    if (!LoadConfigFromNvs()) {
        PostError("Chưa cấu hình khóa API thời tiết");
        return;
    }

    if (!EnsureLocation()) {
        PostError("Không thể xác định vị trí theo IP");
        return;
    }

    WeatherData fetched;
    ClearWeatherData(fetched);

    // --- PHẦN 1 (theo weather.txt): LẤY THỜI TIẾT HIỆN TẠI (CURRENT) ---
    if (!FetchCurrentData(fetched)) {
        PostError("Không thể lấy thời tiết hiện tại");
        return;
    }

    // --- PHẦN 2 (theo weather.txt): LẤY DỰ BÁO 5 NGÀY (FORECAST) ---
    if (!FetchForecastData(fetched)) {
        PostError("Không thể lấy dữ liệu dự báo thời tiết");
        return;
    }

    fetched.timestamp = esp_timer_get_time() / 1000ULL;
    fetched.valid = true;

    // Update shared cache
    int current_offset = 0;
    bool has_offset = false;

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    current_weather_ = fetched;

    // Nếu đang chạy auto-location, ưu tiên hiển thị city resolve từ OpenWeather.
    if (!UseExplicitCityConfig() && !configured_city_.empty()) {
        current_location_.city = configured_city_;
        current_location_.valid = true;
    }
    // Đồng bộ timezone offset về location (phục vụ UI nếu cần)
    if (fetched.timezone_offset_seconds != 0) {
        current_location_.offset_seconds = fetched.timezone_offset_seconds;
        current_location_.has_timezone = true;
    }

    // Lấy offset để set giờ hệ thống
    if (current_location_.has_timezone) {
        current_offset = current_location_.offset_seconds;
        has_offset = true;
    }
    if (mutex_) xSemaphoreGive(mutex_);

    // --- FIX: Cập nhật giờ hệ thống theo offset lấy được từ API ---
    if (has_offset) {
        ApplySystemTimezone(current_offset);
    }

    PostWeatherUpdated(fetched);
    ESP_LOGI(TAG, "Weather update completed successfully");
}

bool WeatherService::LoadConfigFromNvs() {
    // Yêu cầu: đọc từ namespace "wifi": weather_api_key, weather_city
    // Thực tế web config hiện tại đang lưu vào namespace "weather": api_key, city
    // => ưu tiên wifi, fallback weather để không vỡ tương thích.

    std::string api;
    std::string city;

    bool ok_api = ReadNvsString("wifi", "weather_api_key", api);
    bool ok_city = ReadNvsString("wifi", "weather_city", city);

    if (!ok_api) ok_api = ReadNvsString("weather", "api_key", api);
    if (!ok_city) ok_city = ReadNvsString("weather", "city", city);

    api_key_ = TrimAscii(api);
    configured_city_ = TrimAscii(city);

    // Theo weather.txt: city rỗng hoặc "auto" -> tự động xác định vị trí
    if (IsAutoCityValue(configured_city_)) {
        configured_city_.clear();
        has_explicit_city_config_ = false;
    } else {
        has_explicit_city_config_ = !configured_city_.empty();
    }

    // Ưu tiên lấy từ NVS. Nếu NVS không có/đang trống thì fallback sang key hardcode.
	if (api_key_.empty()) {
		api_key_ = TrimAscii(std::string(OPEN_WEATHERMAP_API_KEY_DEFAULT));
		if (!api_key_.empty()) {
			ESP_LOGW(TAG, "NVS: không có khóa API thời tiết, dùng OPEN_WEATHERMAP_API_KEY_DEFAULT (len=%u)",
					 static_cast<unsigned>(api_key_.size()));
		} else {
			ESP_LOGW(TAG, "NVS: khóa API thời tiết đang trống (wifi.weather_api_key / weather.api_key) và OPEN_WEATHERMAP_API_KEY_DEFAULT rỗng");
			return false;
		}
	}

    if (has_explicit_city_config_) {
        ESP_LOGI(TAG, "NVS: đã nạp cấu hình thời tiết (api_key_len=%u, city=\"%s\")",
                 static_cast<unsigned>(api_key_.size()), configured_city_.c_str());
    } else {
        ESP_LOGI(TAG, "NVS: đã nạp cấu hình thời tiết (api_key_len=%u, city=auto)",
                 static_cast<unsigned>(api_key_.size()));
    }

    return true;
}

bool WeatherService::UseExplicitCityConfig() const {
    return has_explicit_city_config_ && !configured_city_.empty();
}

bool WeatherService::EnsureLocation() {
    // 1) Nếu người dùng cấu hình city thủ công, coi như đã có vị trí.
    if (UseExplicitCityConfig()) {
        if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
        current_location_.city = configured_city_;
        current_location_.valid = true;
        if (mutex_) xSemaphoreGive(mutex_);
        return true;
    }

    const uint64_t now_ms = esp_timer_get_time() / 1000ULL;

    // 2) Dùng cache nếu còn hạn
    GeoLocation cached = GetCurrentLocation();
    if (cached.valid && (now_ms - last_geolocation_ms_) < GEOLOCATION_CACHE_TTL_MS) {
        return true;
    }

    // 3) Auto geolocation: thử nhiều provider và chọn kết quả tốt nhất.
    GeoLocation best{};
    int best_score = -1;

    GeoLocation tmp{};
    if (FetchGeolocationPrimary(tmp)) {
        const int s = ScoreLocation(tmp);
        if (s > best_score) { best = tmp; best_score = s; }
    }

    tmp = GeoLocation{};
    if (FetchGeolocationFallback(tmp)) {
        const int s = ScoreLocation(tmp);
        if (s > best_score) { best = tmp; best_score = s; }
    }

    tmp = GeoLocation{};
    if (FetchGeolocationTertiary(tmp)) {
        const int s = ScoreLocation(tmp);
        if (s > best_score) { best = tmp; best_score = s; }
    }

    // Nếu thất bại nhưng vẫn có cache cũ -> dùng cache cũ
    if (best_score < 0) {
        if (cached.valid) {
            ESP_LOGW(TAG, "Geolocation failed; using cached location");
            return true;
        }
        return false;
    }

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    current_location_ = best;
    if (mutex_) xSemaphoreGive(mutex_);

    last_geolocation_ms_ = now_ms;

    // Lưu city để UI hiển thị (không coi là city cấu hình thủ công)
    if (configured_city_.empty() && !best.city.empty()) {
        configured_city_ = best.city;
    }

    ESP_LOGI(TAG, "Geolocation: city=%s, country=%s (%.4f, %.4f) tz=%s offset=%d",
             best.city.c_str(), best.country.c_str(),
             best.latitude, best.longitude,
             best.timezone.c_str(), best.offset_seconds);

    // Thành công nếu có tọa độ hoặc ít nhất có city
    return HasValidLatLon(best.latitude, best.longitude) || !best.city.empty();
}

bool WeatherService::FetchGeolocationPrimary(GeoLocation& out) {
    return FetchGeolocation(GEOLOCATION_URL_PRIMARY, out);
}

bool WeatherService::FetchGeolocationFallback(GeoLocation& out) {
    return FetchGeolocation(GEOLOCATION_URL_FALLBACK, out);
}

bool WeatherService::FetchGeolocationTertiary(GeoLocation& out) {
    return FetchGeolocation(GEOLOCATION_URL_TERTIARY, out);
}

bool WeatherService::FetchGeolocation(const char* url, GeoLocation& out) {
    out = GeoLocation{};

    std::string response;
    if (!HttpGetToString(url, response, 15000, 4096)) {
        ESP_LOGE(TAG, "Failed to fetch geolocation data");
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse geolocation JSON");
        return false;
    }

    // Các API có thể có trường báo lỗi.
    if (strstr(url, "ipapi.co")) {
        // ipapi.co/json/
        cJSON* err = cJSON_GetObjectItem(root, "error");
        if (err && cJSON_IsBool(err) && cJSON_IsTrue(err)) {
            cJSON_Delete(root);
            return false;
        }

        cJSON* city = cJSON_GetObjectItem(root, "city");
        cJSON* country_name = cJSON_GetObjectItem(root, "country_name");
        cJSON* lat = cJSON_GetObjectItem(root, "latitude");
        cJSON* lon = cJSON_GetObjectItem(root, "longitude");
        cJSON* tz = cJSON_GetObjectItem(root, "timezone");
        cJSON* utc_offset = cJSON_GetObjectItem(root, "utc_offset");

        out.city = (city && cJSON_IsString(city) && city->valuestring) ? city->valuestring : "";
        out.country = (country_name && cJSON_IsString(country_name) && country_name->valuestring) ? country_name->valuestring : "";
        out.latitude = (lat && cJSON_IsNumber(lat)) ? static_cast<float>(lat->valuedouble) : 0.0f;
        out.longitude = (lon && cJSON_IsNumber(lon)) ? static_cast<float>(lon->valuedouble) : 0.0f;
        out.timezone = (tz && cJSON_IsString(tz) && tz->valuestring) ? tz->valuestring : "";

        if (utc_offset && cJSON_IsString(utc_offset) && utc_offset->valuestring) {
            out.offset_seconds = ParseUtcOffsetToSeconds(utc_offset->valuestring);
            out.has_timezone = (out.offset_seconds != 0);
        }
    } else {
        // ipwho.is / ipwhois.app
        cJSON* success = cJSON_GetObjectItem(root, "success");
        if (success && cJSON_IsBool(success) && !cJSON_IsTrue(success)) {
            cJSON* message = cJSON_GetObjectItem(root, "message");
            ESP_LOGE(TAG, "Geolocation API error: %s",
                     (message && cJSON_IsString(message)) ? message->valuestring : "unknown");
            cJSON_Delete(root);
            return false;
        }

        cJSON* city = cJSON_GetObjectItem(root, "city");
        cJSON* country = cJSON_GetObjectItem(root, "country");
        cJSON* lat = cJSON_GetObjectItem(root, "latitude");
        cJSON* lon = cJSON_GetObjectItem(root, "longitude");

        out.city = (city && cJSON_IsString(city) && city->valuestring) ? city->valuestring : "";
        out.country = (country && cJSON_IsString(country) && country->valuestring) ? country->valuestring : "";
        out.latitude = (lat && cJSON_IsNumber(lat)) ? static_cast<float>(lat->valuedouble) : 0.0f;
        out.longitude = (lon && cJSON_IsNumber(lon)) ? static_cast<float>(lon->valuedouble) : 0.0f;

        // timezone parsing (ipwho.is: timezone object; ipwhois.app: timezone string + offset int)
        cJSON* tz = cJSON_GetObjectItem(root, "timezone");
        cJSON* offset = cJSON_GetObjectItem(root, "offset");

        if (tz && cJSON_IsObject(tz)) {
            cJSON* id = cJSON_GetObjectItem(tz, "id");
            cJSON* off = cJSON_GetObjectItem(tz, "offset");   // thường là giây
            cJSON* utc = cJSON_GetObjectItem(tz, "utc");      // "+09:00"

            out.timezone = (id && cJSON_IsString(id) && id->valuestring) ? id->valuestring : "";
            if (off && cJSON_IsNumber(off)) {
                out.offset_seconds = off->valueint;
                out.has_timezone = true;
            } else if (utc && cJSON_IsString(utc) && utc->valuestring) {
                out.offset_seconds = ParseUtcOffsetToSeconds(utc->valuestring);
                out.has_timezone = (out.offset_seconds != 0);
            }
        } else if (tz && cJSON_IsString(tz) && tz->valuestring) {
            out.timezone = tz->valuestring;
            if (offset && cJSON_IsNumber(offset)) {
                out.offset_seconds = offset->valueint;
                out.has_timezone = true;
            }
        }
    }

    out.valid = HasValidLatLon(out.latitude, out.longitude) || !out.city.empty();
    cJSON_Delete(root);
    return out.valid;
}

std::string WeatherService::UrlEncode(const std::string& s) {
    // Percent-encode UTF-8 bytes
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        const bool unreserved =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) out.push_back(static_cast<char>(c));
        else if (c == ' ') out += "%20";
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned>(c));
            out += buf;
        }
    }
    return out;
}

std::string WeatherService::BuildOpenWeatherCurrentUrl() {
    if (api_key_.empty()) return {};

    char url[512];

    // Nếu city được cấu hình thủ công -> ưu tiên q=
    if (UseExplicitCityConfig()) {
        const std::string q = UrlEncode(configured_city_);
        snprintf(url, sizeof(url),
                 "%s?q=%s&appid=%s&units=metric&lang=%s",
                 OPENWEATHER_CURRENT_URL,
                 q.c_str(), api_key_.c_str(), OPENWEATHER_LANG);
        return std::string(url);
    }

    // Auto: ưu tiên lat/lon (chính xác hơn city-name)
    GeoLocation loc = GetCurrentLocation();
    if (loc.valid && HasValidLatLon(loc.latitude, loc.longitude)) {
        snprintf(url, sizeof(url),
                 "%s?lat=%.4f&lon=%.4f&appid=%s&units=metric&lang=%s",
                 OPENWEATHER_CURRENT_URL,
                 loc.latitude, loc.longitude,
                 api_key_.c_str(), OPENWEATHER_LANG);
        return std::string(url);
    }

    // Fallback: nếu chỉ có city
    if (!configured_city_.empty()) {
        const std::string q = UrlEncode(configured_city_);
        snprintf(url, sizeof(url),
                 "%s?q=%s&appid=%s&units=metric&lang=%s",
                 OPENWEATHER_CURRENT_URL,
                 q.c_str(), api_key_.c_str(), OPENWEATHER_LANG);
        return std::string(url);
    }

    return {};
}

std::string WeatherService::BuildOpenWeatherForecastUrl() {
    if (api_key_.empty()) return {};

    char url[512];

    // Theo weather.txt: dùng /forecast và cnt=40 để đủ 5 ngày (8 mốc/ngày)
    if (UseExplicitCityConfig()) {
        const std::string q = UrlEncode(configured_city_);
        snprintf(url, sizeof(url),
                 "%s?q=%s&appid=%s&units=metric&lang=%s&cnt=40",
                 OPENWEATHER_FORECAST_URL,
                 q.c_str(), api_key_.c_str(), OPENWEATHER_LANG);
        return std::string(url);
    }

    GeoLocation loc = GetCurrentLocation();
    if (loc.valid && HasValidLatLon(loc.latitude, loc.longitude)) {
        snprintf(url, sizeof(url),
                 "%s?lat=%.4f&lon=%.4f&appid=%s&units=metric&lang=%s&cnt=40",
                 OPENWEATHER_FORECAST_URL,
                 loc.latitude, loc.longitude,
                 api_key_.c_str(), OPENWEATHER_LANG);
        return std::string(url);
    }

    if (!configured_city_.empty()) {
        const std::string q = UrlEncode(configured_city_);
        snprintf(url, sizeof(url),
                 "%s?q=%s&appid=%s&units=metric&lang=%s&cnt=40",
                 OPENWEATHER_FORECAST_URL,
                 q.c_str(), api_key_.c_str(), OPENWEATHER_LANG);
        return std::string(url);
    }

    return {};
}

bool WeatherService::HttpGetToString(const char* url, std::string& out_body, int timeout_ms, int buffer_size) {
    out_body.clear();
    if (!url || !*url) return false;

    esp_http_client_config_t config{};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = timeout_ms;
    config.buffer_size = buffer_size;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP status: %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Đọc theo chunk (giống weather.txt); tránh buffer stack lớn.
    constexpr int kChunk = 2048;
    char* buf = static_cast<char*>(malloc(kChunk));
    if (!buf) {
        ESP_LOGE(TAG, "malloc failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int read_len = 0;
    while ((read_len = esp_http_client_read(client, buf, kChunk)) > 0) {
        out_body.append(buf, read_len);
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return !out_body.empty();
}

bool WeatherService::FetchCurrentData(WeatherData& out) {
    const std::string url = BuildOpenWeatherCurrentUrl();
    if (url.empty()) {
        ESP_LOGE(TAG, "Không thể tạo URL thời tiết hiện tại (thiếu API key hoặc vị trí)");
        return false;
    }

    ESP_LOGI(TAG, "Fetching current weather from OpenWeatherMap");

    std::string response;
    if (!HttpGetToString(url.c_str(), response, 15000, 4096)) {
        return false;
    }

    return ParseCurrentResponse(response, out);
}

bool WeatherService::ParseCurrentResponse(const std::string& response, WeatherData& out) {
    // Không reset forecast ở đây; chỉ cập nhật phần current.

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse current weather JSON");
        return false;
    }

    // timezone
    int tz_offset = 0;
    if (cJSON* tz = cJSON_GetObjectItem(root, "timezone"); tz && cJSON_IsNumber(tz)) {
        tz_offset = tz->valueint;
    }
    out.timezone_offset_seconds = tz_offset;

    // city name
    std::string city_name;
    if (cJSON* name = cJSON_GetObjectItem(root, "name"); name && cJSON_IsString(name) && name->valuestring) {
        city_name = name->valuestring;
        if (!UseExplicitCityConfig() && !city_name.empty()) {
            configured_city_ = city_name; // phục vụ UI
        }
    }

    // main
    cJSON* main = cJSON_GetObjectItem(root, "main");
    cJSON* temp = main ? cJSON_GetObjectItem(main, "temp") : nullptr;
    cJSON* humidity = main ? cJSON_GetObjectItem(main, "humidity") : nullptr;
    cJSON* pressure = main ? cJSON_GetObjectItem(main, "pressure") : nullptr;

    if (!temp || !cJSON_IsNumber(temp)) {
        ESP_LOGE(TAG, "Current weather missing main.temp");
        cJSON_Delete(root);
        return false;
    }

    out.temperature = static_cast<float>(temp->valuedouble);
    out.humidity = (humidity && cJSON_IsNumber(humidity)) ? static_cast<float>(humidity->valuedouble) : 0.0f;
    out.pressure = (pressure && cJSON_IsNumber(pressure)) ? static_cast<float>(pressure->valuedouble) : 0.0f;
	
	// UV index 
    cJSON* uv = cJSON_GetObjectItem(root, "uvi");
    if (!uv) uv = cJSON_GetObjectItem(root, "uv");
    if (!uv) uv = cJSON_GetObjectItem(root, "uv_index");
    if (!uv && main) uv = cJSON_GetObjectItem(main, "uvi");
    if (!uv && main) uv = cJSON_GetObjectItem(main, "uv");
    if (!uv && main) uv = cJSON_GetObjectItem(main, "uv_index");
    out.uv_index = (uv && cJSON_IsNumber(uv)) ? static_cast<float>(uv->valuedouble) : 0.0f;

    // wind
    cJSON* wind = cJSON_GetObjectItem(root, "wind");
    cJSON* w_speed = wind ? cJSON_GetObjectItem(wind, "speed") : nullptr;
    cJSON* w_deg = wind ? cJSON_GetObjectItem(wind, "deg") : nullptr;
    out.wind_speed = (w_speed && cJSON_IsNumber(w_speed)) ? static_cast<float>(w_speed->valuedouble) : 0.0f;
    out.wind_direction = (w_deg && cJSON_IsNumber(w_deg)) ? w_deg->valueint : 0;

    // sunrise/sunset
    int64_t sunrise_utc = 0;
    int64_t sunset_utc = 0;
    if (cJSON* sys = cJSON_GetObjectItem(root, "sys"); sys && cJSON_IsObject(sys)) {
        if (cJSON* sr = cJSON_GetObjectItem(sys, "sunrise"); sr && cJSON_IsNumber(sr)) {
            sunrise_utc = static_cast<int64_t>(sr->valuedouble);
        }
        if (cJSON* ss = cJSON_GetObjectItem(sys, "sunset"); ss && cJSON_IsNumber(ss)) {
            sunset_utc = static_cast<int64_t>(ss->valuedouble);
        }

        if (cJSON* cc = cJSON_GetObjectItem(sys, "country"); cc && cJSON_IsString(cc) && cc->valuestring) {
            if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
            if (current_location_.country.empty()) current_location_.country = cc->valuestring;
            if (mutex_) xSemaphoreGive(mutex_);
        }
    }
    if (sunrise_utc > 0) out.sunrise_local = sunrise_utc + tz_offset;
    if (sunset_utc > 0) out.sunset_local = sunset_utc + tz_offset;

    // weather[0]
    int weather_id = 0;
    std::string ow_icon;
    std::string ow_desc;

    if (cJSON* weather = cJSON_GetObjectItem(root, "weather"); weather && cJSON_IsArray(weather) &&
        cJSON_GetArraySize(weather) > 0) {
        if (cJSON* w0 = cJSON_GetArrayItem(weather, 0); w0 && cJSON_IsObject(w0)) {
            if (cJSON* id = cJSON_GetObjectItem(w0, "id"); id && cJSON_IsNumber(id)) {
                weather_id = id->valueint;
            }
            if (cJSON* icon = cJSON_GetObjectItem(w0, "icon"); icon && cJSON_IsString(icon) && icon->valuestring) {
                ow_icon = icon->valuestring;
            }
            if (cJSON* desc = cJSON_GetObjectItem(w0, "description"); desc && cJSON_IsString(desc) && desc->valuestring) {
                ow_desc = desc->valuestring;
            }
        }
    }

    bool is_night = false;

	// Ưu tiên dùng icon 01d/01n từ OpenWeather (chuẩn nhất)
	if (ow_icon.size() == 3 && (ow_icon[2] == 'd' || ow_icon[2] == 'n')) {
		is_night = (ow_icon[2] == 'n');
	} else if (out.sunrise_local > 0 && out.sunset_local > 0) {
		// Fallback: so sánh theo sunrise/sunset nếu icon thiếu
		const int64_t now_local = static_cast<int64_t>(time(nullptr)) + tz_offset;
		is_night = (now_local < out.sunrise_local || now_local > out.sunset_local);
	}

	out.description = MapOpenWeatherDescriptionShortVi(weather_id, ow_desc, is_night);
	out.icon = MapOpenWeatherIcon(ow_icon, weather_id);

    // Đồng bộ city + timezone về location cache (tốt cho UI và cho lần forecast tiếp theo).
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!city_name.empty()) current_location_.city = city_name;
    if (tz_offset != 0) {
        current_location_.offset_seconds = tz_offset;
        current_location_.has_timezone = true;
    }
    current_location_.valid = current_location_.valid || !current_location_.city.empty() ||
                              HasValidLatLon(current_location_.latitude, current_location_.longitude);
    if (mutex_) xSemaphoreGive(mutex_);

    ESP_LOGI(TAG, "Current: %.1f°C, %.0f%% RH, %.1f m/s, %.0f hPa - %s",
             out.temperature, out.humidity, out.wind_speed, out.pressure, out.description.c_str());

    cJSON_Delete(root);
    return true;
}

bool WeatherService::FetchForecastData(WeatherData& out) {
    const std::string url = BuildOpenWeatherForecastUrl();
    if (url.empty()) {
        ESP_LOGE(TAG, "Không thể tạo URL dự báo (thiếu API key hoặc vị trí)");
        return false;
    }

    ESP_LOGI(TAG, "Fetching forecast from OpenWeatherMap");

    // Theo weather.txt: buffer_size phải lớn (forecast JSON thường ~15KB)
    std::string response;
    if (!HttpGetToString(url.c_str(), response, 15000, 16384)) {
        return false;
    }

    return ParseForecastResponse(response, out);
}

bool WeatherService::ParseForecastResponse(const std::string& response, WeatherData& out) {
    ClearForecastOnly(out);

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse forecast JSON");
        return false;
    }

    // timezone offset ưu tiên dùng offset đã có từ current.
    int tz_offset = out.timezone_offset_seconds;

    // Nếu current chưa set timezone, thử lấy từ forecast.city.timezone
    if (tz_offset == 0) {
        if (cJSON* city = cJSON_GetObjectItem(root, "city"); city && cJSON_IsObject(city)) {
            if (cJSON* tz = cJSON_GetObjectItem(city, "timezone"); tz && cJSON_IsNumber(tz)) {
                tz_offset = tz->valueint;
                out.timezone_offset_seconds = tz_offset;
            }
        }
    }

    cJSON* list = cJSON_GetObjectItem(root, "list");
    if (!list || !cJSON_IsArray(list) || cJSON_GetArraySize(list) <= 0) {
        ESP_LOGE(TAG, "Forecast response missing list[]");
        cJSON_Delete(root);
        return false;
    }

    const int array_size = cJSON_GetArraySize(list);
    int count = 0;

    // Theo weather.txt: lặp và chọn các mốc có dt_txt chứa "12:00:00".
    for (int i = 0; i < array_size && count < 5; ++i) {
        cJSON* item = cJSON_GetArrayItem(list, i);
        if (!item || !cJSON_IsObject(item)) continue;

        cJSON* dt_txt = cJSON_GetObjectItem(item, "dt_txt");
        if (!dt_txt || !cJSON_IsString(dt_txt) || !dt_txt->valuestring) continue;

        if (!strstr(dt_txt->valuestring, "12:00:00")) continue;

        DailyForecast& df = out.forecast[count];

        // 1) Thứ trong tuần
        if (cJSON* dt = cJSON_GetObjectItem(item, "dt"); dt && cJSON_IsNumber(dt)) {
            const int64_t utc_ts = static_cast<int64_t>(dt->valuedouble);
            struct tm tm_local{};
            if (UnixUtcToLocalTmShifted(utc_ts, tz_offset, &tm_local)) {
                df.wday = tm_local.tm_wday;
            }
        }

        // 2) Nhiệt độ (theo weather.txt: dùng main.temp)
        cJSON* main = cJSON_GetObjectItem(item, "main");
        if (main && cJSON_IsObject(main)) {
            if (cJSON* t = cJSON_GetObjectItem(main, "temp"); t && cJSON_IsNumber(t)) {
                const float temp_c = static_cast<float>(t->valuedouble);
                df.temp_min = temp_c;
                df.temp_max = temp_c;
            }
        }

        // 3) Icon (theo weather.txt: dùng weather[0].icon)
        int weather_id = 0;
        std::string icon_code;
        if (cJSON* w_arr = cJSON_GetObjectItem(item, "weather"); w_arr && cJSON_IsArray(w_arr) && cJSON_GetArraySize(w_arr) > 0) {
            if (cJSON* w0 = cJSON_GetArrayItem(w_arr, 0); w0 && cJSON_IsObject(w0)) {
                if (cJSON* id = cJSON_GetObjectItem(w0, "id"); id && cJSON_IsNumber(id)) {
                    weather_id = id->valueint;
                }
                if (cJSON* ic = cJSON_GetObjectItem(w0, "icon"); ic && cJSON_IsString(ic) && ic->valuestring) {
                    icon_code = ic->valuestring;
                }
            }
        }
        df.icon = MapOpenWeatherIcon(icon_code, weather_id);

        df.valid = true;
        count++;
    }

    out.forecast_count = count;

    ESP_LOGI(TAG, "Forecast days: %d", out.forecast_count);

    cJSON_Delete(root);
    return out.forecast_count > 0;
}

std::string WeatherService::MapOpenWeatherIcon(const std::string& ow_icon, int weather_id) {
    if (ow_icon.size() >= 2) {
        const std::string code = ow_icon.substr(0, 2);
        if (code == "01") return "sun";
        if (code == "02") return "cloud_sun";
        if (code == "03" || code == "04") return "cloud";
        if (code == "09" || code == "10") return "cloud";
        if (code == "11") return "wind";
        if (code == "13") return "snowflake";
        if (code == "50") return "wind";
    }

    if (weather_id == 800) return "sun";
    if (weather_id >= 801 && weather_id <= 802) return "cloud_sun";
    if (weather_id >= 803 && weather_id <= 804) return "cloud";
    if (weather_id >= 600 && weather_id <= 622) return "snowflake";
    if (weather_id >= 200 && weather_id <= 232) return "wind";
    if (weather_id >= 700 && weather_id <= 781) return "wind";
    return "cloud";
}

std::string WeatherService::MapOpenWeatherDescriptionShortVi(int weather_id, const std::string& ow_desc, bool is_night) {
    if (weather_id == 800) return is_night ? "Trời quang" : "Trời nắng";
    if (weather_id >= 801 && weather_id <= 802) return "Có mây";
    if (weather_id >= 803 && weather_id <= 804) return "Nhiều mây";

    if (weather_id >= 200 && weather_id <= 232) return "Có giông";
    if (weather_id >= 300 && weather_id <= 321) return "Mưa phùn";
    if (weather_id >= 500 && weather_id <= 531) return "Trời mưa";
    if (weather_id >= 600 && weather_id <= 622) return "Có tuyết";
    if (weather_id >= 700 && weather_id <= 781) return "Sương mù";

    if (!ow_desc.empty()) {
        std::string s = ow_desc;
        unsigned char c = static_cast<unsigned char>(s[0]);
        if (c >= 'a' && c <= 'z') s[0] = static_cast<char>(c - 32);
        return s;
    }
    return "Thời tiết";
}

void WeatherService::InitializeSntp() {
    if (sntp_initialized_) return;

    ESP_LOGI(TAG, "Starting SNTP poll task");
    sntp_initialized_ = true;
    xTaskCreate(&WeatherService::SntpPollTask, "sntp_poll", 4096, this, 5, &sntp_task_handle_);
}

void WeatherService::SntpPollTask(void* arg) {
    auto* self = static_cast<WeatherService*>(arg);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // FIX: Kiểm tra flag running_ trong vòng lặp để thoát nhanh khi Stop() được gọi
    for (int i = 1; i <= 30 && self->running_; ++i) {
        const sntp_sync_status_t st = sntp_get_sync_status();
        if (st == SNTP_SYNC_STATUS_COMPLETED) {
            self->sntp_synced_ = true;

            time_t now = 0;
            time(&now);
            struct tm tm_utc{};
            gmtime_r(&now, &tm_utc);

            char utc_buf[32];
            strftime(utc_buf, sizeof(utc_buf), "%Y-%m-%d %H:%M:%S", &tm_utc);

            ESP_LOGI(TAG, "SNTP time synchronized successfully");
            ESP_LOGI(TAG, "  UTC:   %s", asctime(gmtime(&now)));

            // FIX: Xóa hardcode JST.
            // Mặc định UTC, sẽ được update khi có dữ liệu thời tiết.
            setenv("TZ", "UTC0", 1);
            tzset();

            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            ESP_LOGI(TAG, "  Local time (Default UTC): %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            break;
        }

        ESP_LOGI(TAG, "Waiting for SNTP sync... (%d/30)", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(nullptr);
}

void WeatherService::PostError(const std::string& msg) {
    if (!on_error_) return;

    // đảm bảo callback chạy trên UI thread
    Application::GetInstance().Schedule([cb = on_error_, msg]() {
        cb(msg);
    });
}

void WeatherService::PostWeatherUpdated(const WeatherData& data) {
    if (!on_weather_updated_) return;

    Application::GetInstance().Schedule([cb = on_weather_updated_, data]() {
        cb(data);
    });
}