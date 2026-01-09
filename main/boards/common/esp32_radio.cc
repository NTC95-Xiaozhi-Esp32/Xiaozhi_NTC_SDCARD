#include "esp32_radio.h"
#include "board.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"

// --- THƯ VIỆN BỔ SUNG ---
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <cJSON.h>
#include "mp3dec.h" // MP3 decoder
#include "esp_wifi.h" 
#include <math.h>
#include <stdint.h>

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iomanip>

#define TAG "Esp32Radio"

// --- HÀM HỖ TRỢ ---

static std::string UrlEncode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char)c);
        escaped << std::nouppercase;
    }
    return escaped.str();
}

// [NEW] Hàm hỗ trợ: Làm sạch chuỗi để so sánh thông minh
// Ví dụ: "VOH FM 99.9" -> "vohfm999"
//        "VOHFM"       -> "vohfm"
static std::string CleanString(std::string s) {
    // 1. Chuyển về chữ thường
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    
    // 2. Xóa khoảng trắng, dấu chấm, dấu gạch ngang
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) {
        return std::isspace((unsigned char)c) || c == '.' || c == '-' || c == '_';
    }), s.end());
    
    return s;
}

// =====================
// Cấu hình âm thanh CHO LOA MONO NHỎ
// =====================
#define BASS_CUTOFF_HZ   170.0f   // 90–200 Hz (loa nhỏ)
#define BASS_MIX         0.15f    // 0.15–0.35
#define HEADROOM         0.90f    // tránh limiter làm việc quá nhiều
#define LIMITER_DRIVE    1.5f     // 1.5–2.5 (voice/music balance)
#define DC_BLOCK_R       0.998f   // DC removal

// Thêm Voice Boost
#define VOICE_CENTER_HZ  2000.0f  // 2kHz - voice clarity
#define VOICE_BOOST_DB   2.0f     // +2dB cho voice

// =====================
// Hàm xử lý âm thanh
// =====================
static void ApplyVolume(int16_t* data, size_t samples, float volume, uint32_t sample_rate, AudioDSPState* state) {
    if (!data || samples == 0 || !state) return;
    if (sample_rate == 0) sample_rate = 44100;
    
    // Improved config for mono speaker
    float bass_alpha = 2.0f * M_PI * 200.0f / sample_rate;  // Tăng cutoff
    if (bass_alpha > 1.0f) bass_alpha = 1.0f;
    
    // Voice boost parameters
    float voice_alpha = 2.0f * M_PI * 2000.0f / sample_rate;
    
    for (size_t i = 0; i < samples; ++i) {
        // 1. Normalize
        float x = (float)data[i] / 32768.0f;
        
        // 2. DC Block (gentler)
        float y = x - state->dc_prev_in + 0.998f * state->dc_prev_out;
        state->dc_prev_in = x;
        state->dc_prev_out = y;
        x = y;
        
        // 3. Bass Enhancement (reduced)
        state->bass_state += (x - state->bass_state) * bass_alpha;
        x = x * 0.85f + state->bass_state * 0.15f;  // Giảm bass mix
        
        // 4. Voice Enhancement (NEW)
        state->voice_state += (x - state->voice_state) * voice_alpha;
        x = x + state->voice_state * 0.1f;  // +10% mid boost
        
        // 5. Apply Volume + Headroom
        x *= volume * 0.90f;  // Tăng headroom
        
        // 6. Soft Limiter (gentler)
        x = tanhf(x * 1.5f);  // Giảm drive
        
        // 7. Float -> int16
        int32_t out = (int32_t)(x * 32767.0f);
        if (out >  32767) out =  32767;
        if (out < -32768) out = -32768;
        data[i] = (int16_t)out;
    }
}

// Hàm mixing intelligent STEREO to Mono
static void IntelligentStereoToMono(int16_t* stereo_data, size_t stereo_samples) {
    for (size_t i = 0; i < stereo_samples; i += 2) {
        int32_t left = stereo_data[i];
        int32_t right = stereo_data[i + 1];
        
        // 1. Mid/Side processing
        int32_t mid = (left + right) / 2;      // Mono (center information)
        int32_t side = (left - right) / 4;     // Stereo width (reduced)
        
        // 2. Voice enhancement - boost mid frequencies
        // Simple high-pass để giảm bass rumble
        if (abs(mid) < 1000) {  // Filter low amplitude bass
            mid = mid * 0.7f;
        }
        
        // 3. Combine with some side information for spaciousness
        int32_t mono = mid + side;
        
        // 4. Soft saturation để giữ dynamics
        if (mono > 20000) mono = 20000 + (mono - 20000) * 0.3f;
        if (mono < -20000) mono = -20000 + (mono + 20000) * 0.3f;
        
        stereo_data[i / 2] = (int16_t)mono;
    }
}

// --- IMPLEMENTATION ---

Esp32Radio::Esp32Radio() : current_station_name_(), current_station_url_(),
                         station_name_displayed_(false), current_station_volume_(4.0f), radio_stations_(),
                         display_mode_(DISPLAY_MODE_SPECTRUM), is_playing_(false), is_downloading_(false), 
                         play_thread_(), download_thread_(), audio_buffer_(), buffer_mutex_(), 
                         buffer_cv_(), buffer_size_(0),
						 // DSP Init
						 dsp_state_(),
                         // AAC Init
                         aac_decoder_(nullptr), aac_info_(),
                         aac_decoder_initialized_(false), aac_info_ready_(false), aac_out_buffer_(),
                         // MP3 Init
                         mp3_decoder_(nullptr), mp3_frame_info_(), mp3_decoder_initialized_(false),
						 // TS Buffer
                         ts_buffer_()
						 {
	ts_buffer_.reserve(8192); // Cấp phát sẵn bộ nhớ cho TS
}

Esp32Radio::~Esp32Radio() {
    ESP_LOGI(TAG, "Destroying radio player - stopping all operations");
    Stop();
	
    is_downloading_ = false;
    is_playing_ = false;

    { std::lock_guard<std::mutex> lock(buffer_mutex_); buffer_cv_.notify_all(); }

    if (download_thread_.joinable()) {
        download_thread_.join();
    }

    if (play_thread_.joinable()) {
        play_thread_.join();
    }
       
    ClearAudioBuffer();
    CleanupAacDecoder();
    CleanupMp3Decoder();
    ESP_LOGI(TAG, "Radio player destroyed successfully");
}

void Esp32Radio::Initialize() {
    ESP_LOGI(TAG, "Hybrid Radio player initialized (AAC/MP3/TS/HLS support)");
    InitializeRadioStations();
}

void Esp32Radio::InitializeRadioStations() {
    // Config volume cho các đài (Mặc định khoảng 4.5x - 6.0x cho các đài nhỏ)
    radio_stations_["VOV1"]         = RadioStation("VOV 1 - Thời sự",                   "https://stream.vovmedia.vn/vov-1",       4.0f);
    radio_stations_["VOV2"]         = RadioStation("VOV 2 - Văn hóa & Giáo dục",        "https://stream.vovmedia.vn/vov-2",       4.0f);
    radio_stations_["VOV3"]         = RadioStation("VOV 3 - Âm nhạc & Giải trí",        "https://stream.vovmedia.vn/vov-3",       4.0f);
    radio_stations_["VOV5"]         = RadioStation("VOV 5 - Đối ngoại",                 "https://stream.vovmedia.vn/vov5",        5.0f);
    radio_stations_["VOV_GT_HN"]    = RadioStation("VOV Giao thông Hà Nội",             "https://stream.vovmedia.vn/vovgt-hn",    5.0f);
    radio_stations_["VOV_GT_HCM"]   = RadioStation("VOV Giao thông TP.HCM",             "https://stream.vovmedia.vn/vovgt-hcm",   4.0f);
    radio_stations_["VOV_MEKONG"]   = RadioStation("VOV Mekong FM",                     "https://stream.vovmedia.vn/vovmekong",   5.0f);
    radio_stations_["VOV5_ENGLISH"] = RadioStation("VOV 5 – English 24/7",              "https://stream.vovmedia.vn/vov247",      5.0f);
	
	// [MỚI] Thêm danh sách đài VOH (Dữ liệu từ API radio-browser)
    // Tăng volume lên 4.0f vì đài VOH thường bé hơn VOV một chút
    radio_stations_["VOH_99.9"]     = RadioStation("VOH FM 99.9", "https://strm.voh.com.vn/radio/channel3/chunklist_w1005696319.m3u8", 3.5f);
    radio_stations_["VOH_95.6"]     = RadioStation("VOH FM 95.6", "https://strm.voh.com.vn/radio/channel1/chunklist_w829828563.m3u8", 3.5f);
    radio_stations_["VOH_AM_610"]   = RadioStation("VOH AM 610",  "https://strm.voh.com.vn/radio/channel2/chunklist_w884773542.m3u8", 3.5f);
    radio_stations_["VOH_87.7"]     = RadioStation("VOH FM 87.7", "https://strm.voh.com.vn/radio/channel5/chunklist_w2071193605.m3u8", 3.5f);

    ESP_LOGI(TAG, "Initialized %d VN radio stations", radio_stations_.size());
}

// Danh sách các Server API dự phòng (Mirror Servers)
static const char* RADIO_BROWSER_SERVERS[] = {
    "fi1.api.radio-browser.info",
    "de2.api.radio-browser.info"
};

// HYBRID SEARCH: Tìm trong Local trước, sau đó tìm theo API
std::vector<RadioStation> Esp32Radio::SearchStations(const std::string& query) {
    std::string encoded_query = UrlEncode(query);
    std::string clean_query = CleanString(query); 
    std::vector<RadioStation> stations;
    
    // --- 1. TÌM TRONG DANH SÁCH CỨNG (OFFLINE) (Tìm theo Key & Name)---
    ESP_LOGI(TAG, "Searching offline list for: %s", clean_query.c_str());
     
    for (const auto& pair : radio_stations_) {
        std::string clean_key = CleanString(pair.first);       
        std::string clean_name = CleanString(pair.second.name); 
        
        // Logic so sánh: Khớp Key hoặc khớp Name
        if (clean_name.find(clean_query) != std::string::npos || 
            clean_query.find(clean_name) != std::string::npos ||
            clean_key.find(clean_query) != std::string::npos ||  
            clean_query.find(clean_key) != std::string::npos) {
            
            stations.push_back(pair.second);
            ESP_LOGI(TAG, "Found local match: %s", pair.second.name.c_str());
        }
    }
	
	 
    // Nếu đã tìm thấy đài trong danh sách Offline -> TRẢ VỀ NGAY!
    if (!stations.empty()) {
        ESP_LOGI(TAG, "Local match found. Skipping online search.");
        return stations;
    }

    // --- 2. TÌM TRÊN API (ONLINE) ---
    // Chỉ chạy xuống đây nếu không tìm thấy gì trong máy
    for (const char* server : RADIO_BROWSER_SERVERS) {
        std::string api_url = std::string("https://") + server + 
                              "/json/stations/search?name=" + encoded_query + 
                              "&limit=10&order=clickcount&reverse=true&hidebroken=true";

        ESP_LOGI(TAG, "Searching online: %s", server);

        esp_http_client_config_t config = {};
        config.url = api_url.c_str();
        config.timeout_ms = 4000; // [FIX] Giảm Timeout xuống 4s để fail nhanh hơn nếu mạng lag
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.disable_auto_redirect = false;
        config.use_global_ca_store = true;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) continue;

        if (esp_http_client_open(client, 0) == ESP_OK) {
            int content_length = esp_http_client_fetch_headers(client);
            if (content_length > 0) {
                std::vector<char> buffer(content_length + 1);
                int read_len = esp_http_client_read(client, buffer.data(), content_length);
                if (read_len > 0) {
                    buffer[read_len] = 0;
                    cJSON *json = cJSON_Parse(buffer.data());
                    if (json != NULL) {
                        if (cJSON_IsArray(json)) {
                            int count = cJSON_GetArraySize(json);
                            for (int i = 0; i < count; i++) {
                                cJSON *item = cJSON_GetArrayItem(json, i);
                                cJSON *name = cJSON_GetObjectItem(item, "name");
                                cJSON *url = cJSON_GetObjectItem(item, "url_resolved");
                                
                                if (cJSON_IsString(name) && cJSON_IsString(url) && url->valuestring) {
                                    std::string st_name = name->valuestring;
                                    bool is_duplicate = false;
                                    for(const auto& existing : stations) {
                                        if(existing.name == st_name) { is_duplicate = true; break; }
                                    }
                                    if (!is_duplicate) stations.emplace_back(st_name, std::string(url->valuestring));
                                }
                            }
                        }
                        cJSON_Delete(json);
                        
                        if (!stations.empty()) {
                            esp_http_client_cleanup(client);
                            goto finish_search; 
                        }
                    }
                }
            }
        }
        else {
             ESP_LOGW(TAG, "Failed to connect to %s", server);
        }
        esp_http_client_cleanup(client);
    }

finish_search:
    return stations;
}


bool Esp32Radio::PlayStation(const std::string& station_name) {
    ESP_LOGI(TAG, "Request to play radio station: %s", station_name.c_str());
    
	// 1. Gọi hàm tìm kiếm Hybrid mới
    auto stations = SearchStations(station_name);

    // 2. Nếu có kết quả -> Phát kết quả đầu tiên (Top 1)
    if (!stations.empty()) {
        ESP_LOGI(TAG, "Auto-playing top result: %s", stations[0].name.c_str());
        
        // Nếu là đài trong danh sách cứng, lấy volume đã config
        // Nếu là đài online, mặc định volume 4.0
        current_station_volume_ = (stations[0].volume <= 0.1f) ? 4.0f : stations[0].volume;
		
        return PlayUrl(stations[0].url, stations[0].name);
    }
	
	// 3. Không tìm thấy
    ESP_LOGW(TAG, "No station found for query: %s", station_name.c_str());
    auto display = Board::GetInstance().GetDisplay();
    if (display) display->SetMusicInfo("❌ Không tìm thấy đài.");
    return false;
}

bool Esp32Radio::PlayUrl(const std::string& radio_url, const std::string& station_name) {
    if (radio_url.empty()) return false;

    ESP_LOGI(TAG, "PlayUrl: %s (%s) [Vol: %.1f]", station_name.c_str(), radio_url.c_str(), current_station_volume_);
    Stop();

    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->StopFFT();
        display->ReleaseAudioBuffFFT();
        display->SetMusicInfo(nullptr);
    }

    current_station_url_ = radio_url;
    current_station_name_ = station_name.empty() ? "Custom Radio" : station_name;
    station_name_displayed_ = false;
    is_hls_mode_ = false;

    if (current_station_volume_ <= 0.0f) current_station_volume_ = 4.0f;

    ClearAudioBuffer();
	ts_buffer_.clear();

    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 1024 * 12;  
    cfg.prio = 5;
    cfg.thread_name = "radio_stream";
    esp_pthread_set_cfg(&cfg);

    is_downloading_ = true;
	is_playing_ = true;
	
    download_thread_ = std::thread(&Esp32Radio::DownloadRadioStream, this, radio_url);
    play_thread_ = std::thread(&Esp32Radio::PlayRadioStream, this);

    return true;
}

bool Esp32Radio::Stop() {
    if (!is_playing_ && !is_downloading_) return true;

    ESP_LOGI(TAG, "Stopping radio streaming");
    ResetSampleRate();

    is_downloading_ = false;
    is_playing_ = false;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");
    }

    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    if (download_thread_.joinable()) download_thread_.join();
    if (play_thread_.joinable()) play_thread_.join();

    if (display) display->StopFFT();

    return true;
}

std::vector<std::string> Esp32Radio::GetStationList() const {
    std::vector<std::string> station_list;
    for (const auto& station : radio_stations_) {
        station_list.push_back(station.first + " - " + station.second.name);
    }
    return station_list;
}

void Esp32Radio::DownloadRadioStream(const std::string& radio_url) {
    ESP_LOGD(TAG, "Starting radio stream download from: %s", radio_url.c_str());

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);

    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Connection", "keep-alive");
    http->SetHeader("Icy-MetaData", "1");
    http->SetTimeout(15000); 

    if (!http->Open("GET", radio_url)) {
        ESP_LOGE(TAG, "Failed to connect to radio stream URL");
        is_downloading_ = false;
        return;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206) {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }

    ESP_LOGI(TAG, "Started downloading radio stream, status: %d", status_code);

    const size_t chunk_size = 4096;
    char* buffer = new char[chunk_size];
    size_t total_downloaded = 0;
    int reconnect_attempts = 0;

    while (is_downloading_ && is_playing_) {
        int bytes_read = http->Read(buffer, chunk_size);
        if (bytes_read <= 0) {
            if (++reconnect_attempts > 3) break;
            vTaskDelay(pdMS_TO_TICKS(1500));
            http->Close(); if (!http->Open("GET", radio_url)) continue;
            continue;
        }
        reconnect_attempts = 0;

        // Detect Format at start
        if (total_downloaded == 0 && bytes_read >= 4) {
            if (memcmp(buffer, "#EXT", 4) == 0) {
                ESP_LOGI(TAG, "Detected HLS playlist");
                HlsPlaylistInfo playlist_info = ParseHlsPlaylistAdvanced(buffer, bytes_read, radio_url);
                if (!playlist_info.segments.empty()) {
                    http->Close(); delete[] buffer;
                    is_hls_mode_ = true;
                    current_hls_playlist_ = playlist_info;
                    { std::lock_guard<std::mutex> lock(buffer_mutex_); buffer_cv_.notify_all(); }
                    DownloadHlsStream(playlist_info);
                    return; 
                }
            } 
        }

        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data) break;
        memcpy(chunk_data, buffer, bytes_read);
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });
            if (is_downloading_) {
                audio_buffer_.push(RadioAudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                buffer_cv_.notify_one();
            } else { heap_caps_free(chunk_data); break; }
        }
    }
    delete[] buffer; http->Close(); is_downloading_ = false;
    { std::lock_guard<std::mutex> lock(buffer_mutex_); buffer_cv_.notify_all(); }
    ESP_LOGI(TAG, "Radio stream download thread finished");
}

void Esp32Radio::DownloadHlsStream(const HlsPlaylistInfo& playlist_info) {
    ESP_LOGI(TAG, "Starting HLS stream with %zu segments", playlist_info.segments.size());

    const int kMaxSegmentRetries = 10;      // Tăng số lần thử tải 1 file nhạc
    const int kSegmentBufferSize = 8192; 

    size_t current_segment_index = 0;
    
    // Copy playlist ban đầu để có thể cập nhật
    HlsPlaylistInfo current_playlist = playlist_info;

    // Biến theo dõi lỗi liên tiếp (để log cảnh báo)
    int consecutive_refresh_failures = 0;

    while (is_downloading_ && is_playing_) {
        
        // ----------------------------------------------------------------
        // 1. KIỂM TRA HẾT DANH SÁCH -> TẢI DANH SÁCH MỚI (REFRESH)
        // ----------------------------------------------------------------
        if (current_segment_index >= current_playlist.segments.size()) {
            if (current_playlist.is_live) {
                // ESP_LOGI(TAG, "Reached end of playlist, refreshing..."); // Log ít lại cho đỡ rối
                
                // Thử tải lại playlist mới
                HlsPlaylistInfo refreshed_playlist = RefreshHlsPlaylist(current_playlist.base_url);

                if (refreshed_playlist.segments.empty()) {
                    // [FIX QUAN TRỌNG] Nếu lỗi mạng/SSL khi refresh -> KHÔNG ĐƯỢC THOÁT
                    consecutive_refresh_failures++;
                    
                    // Chỉ log cảnh báo mỗi 5 lần lỗi để đỡ spam log
                    if (consecutive_refresh_failures % 5 == 1) {
                         ESP_LOGW(TAG, "Failed to refresh playlist (SSL/Net Error). Retrying... (%d attempts)", consecutive_refresh_failures);
                    }
                    
                    // Chờ 2 giây rồi QUAY LẠI ĐẦU VÒNG LẶP để thử lại
                    vTaskDelay(pdMS_TO_TICKS(2000)); 
                    continue; 
                }

                // --- Nếu tải thành công ---
                consecutive_refresh_failures = 0; // Reset đếm lỗi

                // Logic tìm điểm nối tiếp (sequence number) để không phát lại bài cũ
                int last_seq = -1;
                if (!current_playlist.segments.empty()) {
                    last_seq = current_playlist.segments.back().sequence_number;
                }

                current_playlist = refreshed_playlist;
                
                // Tìm index của segment mới (phải lớn hơn last_seq)
                current_segment_index = 0;
                bool found_new_segment = false;
                
                if (last_seq != -1) {
                    for (size_t i = 0; i < current_playlist.segments.size(); i++) {
                        if (current_playlist.segments[i].sequence_number > last_seq) {
                            current_segment_index = i;
                            found_new_segment = true;
                            break;
                        }
                    }
                    
                    // Nếu Server chưa cập nhật list mới (toàn segment cũ)
                    if (!found_new_segment) {
                        // ESP_LOGD(TAG, "Playlist not updated yet on server. Waiting...");
                        current_segment_index = current_playlist.segments.size(); // Ép vòng lặp quay lại refresh
                        vTaskDelay(pdMS_TO_TICKS(2000)); // Chờ server cập nhật
                        continue;
                    }
                }
                
                ESP_LOGI(TAG, "Playlist refreshed. New segments start at seq %d", 
                         current_playlist.segments[current_segment_index].sequence_number);

            } else {
                ESP_LOGI(TAG, "Reached end of VOD playlist");
                break; // Nếu không phải Live stream thì dừng
            }
        }

        // ----------------------------------------------------------------
        // 2. TẢI FILE NHẠC (SEGMENT)
        // ----------------------------------------------------------------
        if (current_segment_index < current_playlist.segments.size()) {
            const HlsSegment& segment = current_playlist.segments[current_segment_index];
            
            // Tải file .aac con
            bool segment_success = DownloadHlsSegment(segment.url, kSegmentBufferSize, kMaxSegmentRetries);

            if (segment_success) {
                current_segment_index++;
            } else {
                ESP_LOGW(TAG, "Failed to download segment seq %d. Skipping.", segment.sequence_number);
                // Nếu tải file nhạc lỗi (404/Net), bỏ qua file này để sang file kế tiếp
                // Tránh bị kẹt mãi ở 1 file lỗi
                current_segment_index++; 
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }

    is_downloading_ = false;
    // Báo cho luồng phát biết là đã dừng tải
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
}

bool Esp32Radio::DownloadHlsSegment(const std::string& segment_url, size_t buffer_size, int max_retries) {
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Connection", "keep-alive");
    http->SetTimeout(10000);

    for (int attempt = 0; attempt < max_retries; attempt++) {
        if (!http->Open("GET", segment_url)) continue;

        if (http->GetStatusCode() != 200) {
            http->Close();
            continue;
        }

        char* temp_buffer = new char[buffer_size];
        size_t total_read = 0;

        while (is_downloading_ && is_playing_) {
            int bytes_read = http->Read(temp_buffer, buffer_size);
            if (bytes_read <= 0) break;

            uint8_t* data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
            if (data) {
                memcpy(data, temp_buffer, bytes_read);
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });
                
                if (is_downloading_) {
                    audio_buffer_.push(RadioAudioChunk(data, bytes_read));
                    buffer_size_ += bytes_read;
                    buffer_cv_.notify_one();
                    total_read += bytes_read;
                } else {
                    heap_caps_free(data);
                }
            }
        }
        delete[] temp_buffer;
        http->Close();
        
        if (total_read > 0) return true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

HlsPlaylistInfo Esp32Radio::RefreshHlsPlaylist(const std::string& playlist_url) {
    HlsPlaylistInfo empty_playlist;
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetTimeout(10000);

    if (!http->Open("GET", playlist_url)) return empty_playlist;
    if (http->GetStatusCode() != 200) {
        http->Close();
        return empty_playlist;
    }

    const size_t max_playlist_size = 8192;
    char* playlist_buffer = new char[max_playlist_size];
    size_t total_read = 0;

    while (is_downloading_ && is_playing_) {
        int bytes_read = http->Read(playlist_buffer + total_read, max_playlist_size - total_read);
        if (bytes_read <= 0) break;
        total_read += bytes_read;
        if (total_read >= max_playlist_size) break;
    }
    http->Close();

    if (total_read > 0) {
        HlsPlaylistInfo info = ParseHlsPlaylistAdvanced(playlist_buffer, total_read, playlist_url);
        delete[] playlist_buffer;
        return info;
    }
    
    delete[] playlist_buffer;
    return empty_playlist;
}

HlsPlaylistInfo Esp32Radio::ParseHlsPlaylistAdvanced(const char* playlist_data, size_t size, const std::string& base_url) {
    HlsPlaylistInfo playlist_info;
    playlist_info.base_url = base_url;
    playlist_info.version = 3;
    playlist_info.target_duration = 10;
    playlist_info.media_sequence = 0;
    playlist_info.discontinuity_sequence = 0;
    playlist_info.is_live = true;
    playlist_info.has_ended = false;

    std::string playlist_str(playlist_data, size);
    size_t start = 0;
    size_t end = playlist_str.find('\n');
    HlsSegment current_segment;
    bool in_segment = false;

    while (end != std::string::npos) {
        std::string line = playlist_str.substr(start, end - start);
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) {
            start = end + 1;
            end = playlist_str.find('\n', start);
            continue;
        }

        if (line[0] == '#') {
            if (line.find("#EXTM3U") == 0) {
                // Header
            }
            else if (line.find("#EXT-X-VERSION:") == 0) {
                if (line.length() > 15) playlist_info.version = atoi(line.substr(15).c_str());
            }
            else if (line.find("#EXT-X-TARGETDURATION:") == 0) {
                if (line.length() > 22) playlist_info.target_duration = atoi(line.substr(22).c_str());
            }
            else if (line.find("#EXT-X-MEDIA-SEQUENCE:") == 0) {
                if (line.length() > 22) playlist_info.media_sequence = atoi(line.substr(22).c_str());
            }
            else if (line.find("#EXT-X-DISCONTINUITY-SEQUENCE:") == 0) {
                if (line.length() > 30) playlist_info.discontinuity_sequence = atoi(line.substr(30).c_str());
            }
            else if (line.find("#EXT-X-ENDLIST") == 0) {
                playlist_info.is_live = false;
                playlist_info.has_ended = true;
            }
            else if (line.find("#EXTINF:") == 0) {
                size_t comma_pos = line.find(',', 8);
                if (comma_pos != std::string::npos) {
                    current_segment.duration = atof(line.substr(8, comma_pos - 8).c_str());
                    in_segment = true;
                }
            }
            else if (line.find("#EXT-X-DISCONTINUITY") == 0) {
                current_segment.sequence_number = -1; 
            }
        }
        else {
            if (in_segment) {
                current_segment.sequence_number = playlist_info.media_sequence + playlist_info.segments.size();
                current_segment.url = line;

                if (line.find("http") != 0) {
                    size_t last_slash = base_url.find_last_of('/');
                    if (last_slash != std::string::npos) {
                        current_segment.url = base_url.substr(0, last_slash + 1) + line;
                    } else {
                        current_segment.url = base_url + "/" + line;
                    }
                }
                playlist_info.segments.push_back(current_segment);
                in_segment = false;
            }
        }
        start = end + 1;
        end = playlist_str.find('\n', start);
    }
    return playlist_info;
}

void Esp32Radio::PlayRadioStream() {
    ESP_LOGI(TAG, "Starting Hybrid Playback");
    auto codec = Board::GetInstance().GetAudioCodec();
    
    // [FIX] Tách kiểm tra codec và EnableOutput ra
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        is_playing_ = false;
        return;
    }
    
    // Gọi hàm EnableOutput riêng biệt (vì nó trả về void)
    codec->EnableOutput(true);

    uint8_t* input_buffer = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!input_buffer) {
        is_playing_ = false;
        return;
    }

    int16_t* pcm_buffer = new int16_t[2304 * 2];
    int bytes_left = 0;
    uint8_t* read_ptr = nullptr;
    size_t total_played = 0;
    StreamFormat format = FORMAT_UNKNOWN;

    if (is_hls_mode_) {
        ESP_LOGI(TAG, "HLS Mode: Waiting for segments to auto-detect format...");
    } else {
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ >= MIN_BUFFER_SIZE || !is_downloading_; });
        }
    }

    auto display = Board::GetInstance().GetDisplay();
    static int64_t last_ps_update = 0;

    while (is_playing_) {
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();

        if (current_state == kDeviceStateSpeaking) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        } 
        else if (current_state == kDeviceStateListening || current_state == kDeviceStateConnecting) {
             ESP_LOGI(TAG, "Forcing state to IDLE for Radio Playback");
             app.StopListening(); 
             if (current_state != kDeviceStateIdle) {
                 app.SetDeviceState(kDeviceStateIdle);
                 current_state = kDeviceStateIdle;
             }
        }
        else if (current_state != kDeviceStateIdle) {
             vTaskDelay(pdMS_TO_TICKS(50));
             continue;
        }

        if (esp_timer_get_time() - last_ps_update > 5000000) { 
            esp_wifi_set_ps(WIFI_PS_NONE);
            last_ps_update = esp_timer_get_time();
        }

        if (!station_name_displayed_ && !current_station_name_.empty()) {
            if (display) {
                if (display_mode_ == DISPLAY_MODE_SPECTRUM) display->StartFFT();
                std::string formatted_station_name = "Radio 《" + current_station_name_ + "》Đang phát...";
                display->SetMusicInfo(formatted_station_name.c_str());
                station_name_displayed_ = true;
            }
        }

        if (bytes_left < 4096) {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            if (audio_buffer_.empty()) {
                if (!is_downloading_) break;
                buffer_cv_.wait(lock, [this] { return !audio_buffer_.empty() || !is_downloading_; });
                if (audio_buffer_.empty()) continue;
            }

            RadioAudioChunk chunk = audio_buffer_.front();
            audio_buffer_.pop();
            buffer_size_ -= chunk.size;
            lock.unlock();
            buffer_cv_.notify_one();

            if (bytes_left > 0 && read_ptr != input_buffer) memmove(input_buffer, read_ptr, bytes_left);

            size_t copy_len = std::min(chunk.size, 8192 - (size_t)bytes_left);
            memcpy(input_buffer + bytes_left, chunk.data, copy_len);
            bytes_left += copy_len;
            read_ptr = input_buffer;
            heap_caps_free(chunk.data);
        }

        if (bytes_left < 1500 && is_downloading_) {
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        if (format == FORMAT_UNKNOWN && bytes_left > 188) {
            if (memcmp(read_ptr, "ID3", 3) == 0) {
                size_t skip = SkipId3Tag(read_ptr, bytes_left);
                if (skip > bytes_left) {
                    if (bytes_left < 4096) continue;
                }
                if (bytes_left > skip + 2) {
                    uint8_t* payload = read_ptr + skip;
                    if (payload[0] == 0xFF && (payload[1] & 0xF6) == 0xF0) {
                        format = FORMAT_AAC;
                        InitializeAacDecoder();
                        if (display) display->SetMusicInfo(("Radio AAC: " + current_station_name_).c_str());
                    } 
                    else if (payload[0] == 0xFF && (payload[1] & 0xE0) == 0xE0) {
                        format = FORMAT_MP3;
                        InitializeMp3Decoder();
                        if (display) display->SetMusicInfo(("Radio MP3: " + current_station_name_).c_str());
                    }
                    read_ptr += skip; 
                    bytes_left -= skip;
                } else {
                    continue; 
                }
            } 
            else if (read_ptr[0] == 0x47 && read_ptr[188] == 0x47) {
                format = FORMAT_TS;
                ESP_LOGI(TAG, "Detected TS Stream");
                if (display) display->SetMusicInfo(("Radio TS: " + current_station_name_).c_str());
            }
            else if ((read_ptr[0] == 0xFF) && ((read_ptr[1] & 0xF6) == 0xF0)) {
                format = FORMAT_AAC;
                InitializeAacDecoder();
                if (display) display->SetMusicInfo(("Radio AAC: " + current_station_name_).c_str());
            } 
            else if ((read_ptr[0] == 0xFF) && ((read_ptr[1] & 0xE0) == 0xE0)) {
                format = FORMAT_MP3;
                InitializeMp3Decoder();
                if (display) display->SetMusicInfo(("Radio MP3: " + current_station_name_).c_str());
            } 
            else {
                read_ptr++; bytes_left--; continue;
            }
            if (display && format != FORMAT_UNKNOWN) display->StartFFT();
        }

        if (format == FORMAT_AAC) ProcessAAC(read_ptr, bytes_left, total_played);
        else if (format == FORMAT_MP3) ProcessMP3(read_ptr, bytes_left, pcm_buffer, total_played);
        else if (format == FORMAT_TS) ProcessTS(read_ptr, bytes_left, pcm_buffer, total_played);
        else vTaskDelay(pdMS_TO_TICKS(10));
    }

    heap_caps_free(input_buffer);
    delete[] pcm_buffer;
    is_playing_ = false;
    CleanupAacDecoder();
    CleanupMp3Decoder();
    ResetSampleRate();
}

// [UPDATED] Hàm xử lý TS thông minh: Tự nhận diện lõi AAC hay MP3
void Esp32Radio::ProcessTS(uint8_t*& read_ptr, int& bytes_left, int16_t* pcm_buffer, size_t& total_played) {
    while (bytes_left >= 188) {
        if (read_ptr[0] != 0x47) { read_ptr++; bytes_left--; continue; }
        uint8_t adaptation = (read_ptr[3] >> 4) & 0x3;
        int payload_offset = 4;
        if (adaptation == 2 || adaptation == 3) payload_offset += (1 + read_ptr[4]);
        
        if ((adaptation & 1) && payload_offset < 188) {
            size_t curr = ts_buffer_.size();
            ts_buffer_.resize(curr + (188 - payload_offset));
            memcpy(ts_buffer_.data() + curr, read_ptr + payload_offset, 188 - payload_offset);
        }
        read_ptr += 188; bytes_left -= 188;
    }

    if (ts_buffer_.size() > 1024) {
        uint8_t* ts_ptr = ts_buffer_.data();
        int ts_bytes = (int)ts_buffer_.size();
        size_t init_bytes = ts_bytes;

        // Auto-detect format bên trong TS
        if (!aac_decoder_initialized_ && !mp3_decoder_initialized_) {
             if (ts_ptr[0] == 0xFF && (ts_ptr[1] & 0xF6) == 0xF0) InitializeAacDecoder();
             else if (ts_ptr[0] == 0xFF && (ts_ptr[1] & 0xE0) == 0xE0) InitializeMp3Decoder();
        }

        if (aac_decoder_initialized_) ProcessAAC(ts_ptr, ts_bytes, total_played);
        else if (mp3_decoder_initialized_) ProcessMP3(ts_ptr, ts_bytes, pcm_buffer, total_played); 

        size_t consumed = init_bytes - ts_bytes;
        if (consumed > 0) ts_buffer_.erase(ts_buffer_.begin(), ts_buffer_.begin() + consumed);
    }
}

// --- AAC HANDLER (Đã Fix lỗi Stereo bị ồm tiếng + Tăng âm lượng) ---
void Esp32Radio::ProcessAAC(uint8_t*& read_ptr, int& bytes_left, size_t& total_played) {
    if (!aac_decoder_initialized_) return;
    esp_audio_simple_dec_raw_t raw = {read_ptr, (size_t)bytes_left, false};
    esp_audio_simple_dec_out_t out = {aac_out_buffer_.data(), (size_t)aac_out_buffer_.size(), 0, 0};
    
    if (esp_audio_simple_dec_process(aac_decoder_, &raw, &out) == ESP_AUDIO_ERR_OK && out.decoded_size > 0) {
        if (!aac_info_ready_) {
            esp_audio_simple_dec_get_info(aac_decoder_, &aac_info_);
            aac_info_ready_ = true;
            ESP_LOGI(TAG, "AAC Stream Info: Rate=%d Hz, Channels=%d", aac_info_.sample_rate, aac_info_.channel);
        }

        int16_t* pcm_data = (int16_t*)out.buffer;
        size_t samples = out.decoded_size / 2;       
        
        if (aac_info_.channel == 2) {
            // Dùng hàm intelligent stereo→mono mới
            IntelligentStereoToMono(pcm_data, samples);
            samples = samples / 2;  // Chia 2 vì stereo→mono
        }

        // Áp dụng Volume
        ApplyVolume(pcm_data, samples, current_station_volume_, aac_info_.sample_rate, &dsp_state_);

        AudioStreamPacket packet;
        packet.sample_rate = aac_info_.sample_rate;
        packet.frame_duration = 60;
        
        size_t payload_size = samples * sizeof(int16_t);
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), pcm_data, payload_size);
        
        auto display = Board::GetInstance().GetDisplay();
        if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
             final_pcm_data_fft = display->MakeAudioBuffFFT(payload_size);
             if (final_pcm_data_fft) {
                 memcpy(final_pcm_data_fft, pcm_data, payload_size);
                 display->FeedAudioDataFFT(final_pcm_data_fft, payload_size);
             }
        }
        
        Application::GetInstance().AddAudioData(std::move(packet));
        total_played += payload_size;
    }
    
    if (raw.consumed > 0) { read_ptr += raw.consumed; bytes_left -= raw.consumed; }
    else { read_ptr++; bytes_left--; }
}

// --- MP3 HANDLER (Đã Fix Stereo -> Mono) ---
void Esp32Radio::ProcessMP3(uint8_t*& read_ptr, int& bytes_left, int16_t* pcm_output, size_t& total_played) {
    if (!mp3_decoder_initialized_) return;
    int sync = MP3FindSyncWord(read_ptr, bytes_left);
    if (sync < 0) { bytes_left = 0; return; }
    read_ptr += sync; bytes_left -= sync;
    if (bytes_left < 100) return;

    if (MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_output, 0) == 0) {
        MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
        if (mp3_frame_info_.outputSamps > 0) {
            
            int samples = mp3_frame_info_.outputSamps;

            // Xử lý Stereo -> Mono
            if (mp3_frame_info_.nChans == 2) {
               // Dùng hàm intelligent stereo→mono mới
               IntelligentStereoToMono(pcm_output, samples);
               samples = samples / 2;  // Chia 2 vì stereo→mono
            }

            // Áp dụng Volume
            ApplyVolume(pcm_output, samples, current_station_volume_, mp3_frame_info_.samprate, &dsp_state_);

            AudioStreamPacket packet;
            packet.sample_rate = mp3_frame_info_.samprate;
            packet.frame_duration = 60;
            
            size_t pcm_size = samples * sizeof(int16_t);
            packet.payload.resize(pcm_size);
            memcpy(packet.payload.data(), pcm_output, pcm_size);

            auto display = Board::GetInstance().GetDisplay();
            if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
                 final_pcm_data_fft = display->MakeAudioBuffFFT(pcm_size);
                 if (final_pcm_data_fft) {
                     memcpy(final_pcm_data_fft, pcm_output, pcm_size);
                     display->FeedAudioDataFFT(final_pcm_data_fft, pcm_size);
                 }
            }
            Application::GetInstance().AddAudioData(std::move(packet));
            total_played += pcm_size;
        }
    } else {
        if (bytes_left > 0) { read_ptr++; bytes_left--; }
    }
}

void Esp32Radio::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    while (!audio_buffer_.empty()) {
        RadioAudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }
    buffer_size_ = 0;
    ESP_LOGI(TAG, "Radio audio buffer cleared");
}

// --- DECODER MANAGEMENT ---

bool Esp32Radio::InitializeAacDecoder() {
    if (aac_decoder_initialized_) return true;

    ESP_LOGI(TAG, "Initializing AAC Simple Decoder");
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    esp_audio_simple_dec_cfg_t aac_cfg = {};
    aac_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    aac_cfg.dec_cfg = nullptr;
    aac_cfg.cfg_size = 0;

    esp_audio_err_t dec_ret = esp_audio_simple_dec_open(&aac_cfg, &aac_decoder_);
    if (dec_ret != ESP_AUDIO_ERR_OK || !aac_decoder_) {
        ESP_LOGE(TAG, "Failed to open AAC simple decoder, ret=%d", dec_ret);
        esp_audio_simple_dec_unregister_default();
        esp_audio_dec_unregister_default();
        return false;
    }

    aac_out_buffer_.resize(4096);
    aac_info_ready_ = false;
    aac_decoder_initialized_ = true;
    return true;
}

void Esp32Radio::CleanupAacDecoder() {
    if (aac_decoder_) {
        esp_audio_simple_dec_close(aac_decoder_);
        aac_decoder_ = nullptr;
    }
    esp_audio_simple_dec_unregister_default();
    esp_audio_dec_unregister_default();
    aac_decoder_initialized_ = false;
}

bool Esp32Radio::InitializeMp3Decoder() {
    if (mp3_decoder_initialized_) return true;
    ESP_LOGI(TAG, "Init MP3 Decoder");

    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_) {
        mp3_decoder_initialized_ = true;
        return true;
    }
    return false;
}

void Esp32Radio::CleanupMp3Decoder() {
    if (mp3_decoder_) MP3FreeDecoder(mp3_decoder_);
    mp3_decoder_ = nullptr;
    mp3_decoder_initialized_ = false;
}

void Esp32Radio::ResetSampleRate() {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) codec->SetOutputSampleRate(-1);
}

size_t Esp32Radio::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) return 0;
    if (memcmp(data, "ID3", 3) != 0) return 0;

    uint32_t size_enc = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7)  |
                        ((uint32_t)(data[9] & 0x7F));
    size_t skip = 10 + size_enc;
    if (skip > size) skip = size;
    ESP_LOGI(TAG, "Skipping ID3: %d bytes", skip);
    return skip;
}

// --- HLS PLAYLIST PARSING ---

std::string Esp32Radio::ParseHlsPlaylist(const char* playlist_data, size_t size, const std::string& base_url) {
    std::string result_url = "";
    std::string playlist_str(playlist_data, size);
    size_t start = 0;
    size_t end = playlist_str.find('\n');

    while (end != std::string::npos) {
        std::string line = playlist_str.substr(start, end - start);
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') {
            start = end + 1;
            end = playlist_str.find('\n', start);
            continue;
        }

        if (line.find("http") != 0) {
            size_t last_slash = base_url.find_last_of('/');
            if (last_slash != std::string::npos) {
                result_url = base_url.substr(0, last_slash + 1) + line;
            } else {
                result_url = base_url + "/" + line;
            }
            break;
        } else {
            result_url = line;
            break;
        }
    }
    return result_url;
}

void Esp32Radio::SetDisplayMode(DisplayMode mode) {
    DisplayMode old_mode = display_mode_.load();
    display_mode_ = mode;
    ESP_LOGI(TAG, "Display mode changed from %s to %s",
            (old_mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "INFO",
            (mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "INFO");
}