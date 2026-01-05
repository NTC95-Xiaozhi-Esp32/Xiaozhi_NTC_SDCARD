// Weather service: lấy dữ liệu từ OpenWeatherMap
// - Current weather (2.5/weather)
// - 5-day / 3-hour forecast (2.5/forecast)
// và cung cấp snapshot cho UI.

#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

// 0: dùng IP geolocation (mặc định)
// 1: dùng vị trí tĩnh (phục vụ debug/offline)
#ifndef WEATHER_USE_STATIC_LOCATION
#define WEATHER_USE_STATIC_LOCATION 0
#endif

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include <esp_timer.h>
#include <cJSON.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Dự báo theo ngày (tối đa 5 ngày)
struct DailyForecast {
    int wday = 0;              // 0=CN..6=T7 (theo tm_wday)
    float temp_min = 0.0f;     // °C
    float temp_max = 0.0f;     // °C
    std::string icon;          // khóa icon (UI tự map)
    bool valid = false;
};

// Snapshot thời tiết
struct WeatherData {
    float temperature = 0.0f;       // °C
    float humidity = 0.0f;          // %
    float wind_speed = 0.0f;        // m/s
    int wind_direction = 0;         // degrees
    float pressure = 0.0f;          // hPa
    std::string description;        // mô tả ngắn tiếng Việt ("Trời nắng", "Có mây", ...)
    std::string icon;               // icon key (mapped by UI)

    // Mặt trời mọc/lặn (giờ địa phương), dạng epoch (giây) đã cộng timezone offset
    int64_t sunrise_local = 0;
    int64_t sunset_local = 0;

    // Timezone offset (giây) lấy từ OpenWeather timezone
    int timezone_offset_seconds = 0;

    // Dự báo 5 ngày
    std::array<DailyForecast, 5> forecast{};
    int forecast_count = 0;

    bool valid = false;
    uint64_t timestamp = 0;         // ms (esp_timer)
};

// Vị trí địa lý (theo IP hoặc cấu hình)
struct GeoLocation {
    float latitude = 0.0f;
    float longitude = 0.0f;
    std::string city;
    std::string country;
    std::string timezone;
    int offset_seconds = 0;         // local - UTC (seconds)
    bool valid = false;
    bool has_timezone = false;
};

class WeatherService {
public:
    WeatherService();
    ~WeatherService();

    // Start periodic updates (safe to call multiple times)
    void Start();
    void Stop();

    // Get cached values (copy snapshot)
    WeatherData GetCurrentWeather();
    GeoLocation GetCurrentLocation();

    // Force an immediate update (non-blocking)
    bool UpdateWeatherNow();

    // Callbacks
    void OnWeatherUpdated(std::function<void(const WeatherData&)> callback);
    void OnError(std::function<void(const std::string&)> callback);

private:
    // OpenWeatherMap
    static constexpr const char* OPENWEATHER_CURRENT_URL = "https://api.openweathermap.org/data/2.5/weather";
    static constexpr const char* OPENWEATHER_FORECAST_URL = "https://api.openweathermap.org/data/2.5/forecast";
    static constexpr const char* OPENWEATHER_LANG = "vi";

    // IP geolocation
    // - ưu tiên ipwho.is
    // - fallback ipwhois.app
    // - fallback ipapi.co (thường cho kết quả ổn định + có utc_offset)
    static constexpr const char* GEOLOCATION_URL_PRIMARY =
        "https://ipwho.is/?fields=success,message,city,country,latitude,longitude,timezone";
    static constexpr const char* GEOLOCATION_URL_FALLBACK =
        "https://ipwhois.app/json/?fields=country,city,latitude,longitude,timezone,offset,success,message";
    static constexpr const char* GEOLOCATION_URL_TERTIARY =
        "https://ipapi.co/json/";

    // Cache vị trí IP để tránh gọi geolocation mỗi chu kỳ update
    static constexpr uint64_t GEOLOCATION_CACHE_TTL_MS = 6ULL * 60ULL * 60ULL * 1000ULL; // 6 giờ

    // Update interval (10 minutes)
    static constexpr uint64_t UPDATE_INTERVAL_US = 600ULL * 1000000ULL;

    // Timer cập nhật định kỳ
    esp_timer_handle_t update_timer_ = nullptr;

    // Worker task để tránh block UI
    TaskHandle_t worker_task_handle_ = nullptr;

    // SNTP task
    bool sntp_initialized_ = false;
    bool sntp_synced_ = false;
    TaskHandle_t sntp_task_handle_ = nullptr;

    // Shared data
    SemaphoreHandle_t mutex_ = nullptr;
    WeatherData current_weather_;
    GeoLocation current_location_;

    uint64_t last_geolocation_ms_ = 0;

    // Config từ NVS
    std::string api_key_;
    std::string configured_city_;
    bool has_explicit_city_config_ = false; // true nếu city được cấu hình thủ công trong NVS (không phải auto)

    // Callback
    std::function<void(const WeatherData&)> on_weather_updated_;
    std::function<void(const std::string&)> on_error_;

    bool running_ = false;

private:
    // Timer callback -> notify worker
    static void UpdateTimerCallback(void* arg);

    // Worker loop
    static void WorkerTask(void* arg);
    void DoOneUpdate(); // chạy trong worker task

    // Workflow
    bool LoadConfigFromNvs();
    bool EnsureLocation();              // nếu city trống -> geolocate
    bool FetchGeolocation(const char* url, GeoLocation& out);
    bool FetchGeolocationPrimary(GeoLocation& out);
    bool FetchGeolocationFallback(GeoLocation& out);
    bool FetchGeolocationTertiary(GeoLocation& out);

    // OpenWeather
    std::string UrlEncode(const std::string& s);
    bool UseExplicitCityConfig() const;
    std::string BuildOpenWeatherCurrentUrl();
    std::string BuildOpenWeatherForecastUrl();

    bool FetchCurrentData(WeatherData& out);
    bool FetchForecastData(WeatherData& out);

    bool ParseCurrentResponse(const std::string& response, WeatherData& out);
    bool ParseForecastResponse(const std::string& response, WeatherData& out);

    bool HttpGetToString(const char* url, std::string& out_body, int timeout_ms, int buffer_size);

    std::string MapOpenWeatherIcon(const std::string& ow_icon, int weather_id);
    std::string MapOpenWeatherDescriptionShortVi(int weather_id, const std::string& ow_desc);

    // Time sync
    void InitializeSntp();
    static void SntpPollTask(void* arg);

    // Safe invoke callbacks on UI thread (Application::Schedule)
    void PostError(const std::string& msg);
    void PostWeatherUpdated(const WeatherData& data);
};

#endif // WEATHER_SERVICE_H
