#ifndef ESP32_RADIO_H
#define ESP32_RADIO_H

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <map>

#include "radio.h"
#include "mp3dec.h" 

// AAC Decoder (Giữ nguyên cho VOV)
extern "C" {
#include "esp_audio_simple_dec_default.h"
}
// DSP Structure 
struct AudioDSPState {
    float bass_state;
    float dc_prev_in;
    float dc_prev_out;
	float voice_state;  // Thêm voice state
    // Constructor để reset về 0
    AudioDSPState() : bass_state(0), dc_prev_in(0), dc_prev_out(0),voice_state(0) {}
};

// HLS Data Structures
struct HlsSegment {
    std::string url;
    float duration;
    int sequence_number;
};

struct HlsPlaylistInfo {
    std::string base_url;
    int version;
    int target_duration;
    int media_sequence;
    int discontinuity_sequence;
    std::vector<HlsSegment> segments;
    bool is_live;
    bool has_ended;
};

struct RadioAudioChunk {
    uint8_t* data;
    size_t size;
    size_t offset;
    size_t capacity;

    RadioAudioChunk() : data(nullptr), size(0), offset(0), capacity(0) {}
    RadioAudioChunk(uint8_t* d, size_t s, size_t cap = 0)
        : data(d), size(s), offset(0), capacity(cap ? cap : s) {}
};


struct RadioStation {
    std::string name;
    std::string url;
    float volume;
    RadioStation() : volume(1.0f) {}
    RadioStation(const std::string& n, const std::string& u, float v = 1.0f)
        : name(n), url(u), volume(v) {}
};

class Esp32Radio : public Radio {
public:
    enum DisplayMode { DISPLAY_MODE_SPECTRUM = 0, DISPLAY_MODE_INFO = 1 };
	// Hỗ trợ cả 3 định dạng phổ biến
    enum StreamFormat { FORMAT_UNKNOWN, FORMAT_AAC, FORMAT_MP3, FORMAT_TS };

    // Chế độ buffer: ổn định hay ưu tiên độ trễ thấp
    enum BufferMode { BUFFER_MODE_STABLE = 0, BUFFER_MODE_LOW_LATENCY = 1 };

private:
    std::string current_station_name_;
    std::string current_station_url_;
    bool station_name_displayed_;
    float current_station_volume_;
    // Gain nội bộ cho stream (bù âm lượng đài). Nhân vào current_station_volume_.
    std::atomic<float> stream_gain_{1.0f};
    std::map<std::string, RadioStation> radio_stations_;
    
    std::atomic<DisplayMode> display_mode_;
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_downloading_;
    std::thread play_thread_;
    std::thread download_thread_;
    
    std::queue<RadioAudioChunk> audio_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    size_t buffer_size_;
    // Memory profile: define RAM_LOW_MEM for boards without PSRAM / tight heap.
#if defined(RAM_LOW_MEM)
    static constexpr size_t MAX_BUFFER_SIZE = 128 * 1024;
    static constexpr size_t MIN_BUFFER_SIZE = 16 * 1024;
#else
    static constexpr size_t MAX_BUFFER_SIZE = 512 * 1024;
    static constexpr size_t MIN_BUFFER_SIZE = 64 * 1024;
#endif
    // Buffer policy & metadata
    std::atomic<BufferMode> buffer_mode_{BUFFER_MODE_STABLE};
    std::atomic<bool> metadata_enabled_{true};

    // Now playing (ICY metadata / ID3) - thread-safe
    mutable std::mutex now_playing_mutex_;
    std::string now_playing_;

    // Chunk pool để giảm phân mảnh heap (ưu tiên SPIRAM)
    static constexpr size_t POOL_BLOCK_SIZE = 4096;
    // Pool blocks are allocated up-front in ctor; scale down aggressively on low-mem builds.
#if defined(RAM_LOW_MEM)
    static constexpr size_t POOL_BLOCK_COUNT = 16; // ~64KB
#else
    static constexpr size_t POOL_BLOCK_COUNT = 48; // ~192KB
#endif
    std::mutex pool_mutex_;
    std::vector<uint8_t*> pool_blocks_;

    // Track sample rate để tránh SetOutputSampleRate liên tục
    uint32_t last_output_sample_rate_ = 0;

	
	//--- Audio DSP ---
	AudioDSPState dsp_state_;
    
    // --- AAC DECODER ---
    esp_audio_simple_dec_handle_t aac_decoder_;
    esp_audio_simple_dec_info_t aac_info_;
    bool aac_decoder_initialized_;
    bool aac_info_ready_;
    std::vector<uint8_t> aac_out_buffer_;

    // --- MP3 DECODER ---
    HMP3Decoder mp3_decoder_;
    MP3FrameInfo mp3_frame_info_;
    bool mp3_decoder_initialized_;
	
	// --- TS (MPEG Transport Stream) SUPPORT ---
    std::vector<uint8_t> ts_buffer_;

    // --- HLS STATE MANAGEMENT ---
    std::atomic<bool> is_hls_mode_{false};
    HlsPlaylistInfo current_hls_playlist_;

    // Yêu cầu reset decoder (khi reconnect/format thay đổi)
    std::atomic<bool> decoder_reset_requested_{false};
    
    // Private methods
    void InitializeRadioStations();
    void DownloadRadioStream(const std::string& radio_url);
    void PlayRadioStream(); 

    // Helper decoders
    void ProcessAAC(uint8_t*& read_ptr, int& bytes_left, size_t& total_played);
    void ProcessMP3(uint8_t*& read_ptr, int& bytes_left, int16_t* pcm_output_buffer, size_t& total_played);

    // Hàm xử lý gói tin TS (MPEG Transport Stream)
    void ProcessTS(uint8_t*& read_ptr, int& bytes_left, int16_t* pcm_buffer, size_t& total_played);

    // Buffer helpers
    uint8_t* AcquirePoolBlock(size_t& capacity);
    void ReleasePoolBlock(uint8_t* ptr);
    bool EnqueueAudio(const uint8_t* data, size_t len);
    void ReleaseChunkData(RadioAudioChunk& chunk);
	void ClearAudioBuffer();

    // Metadata helpers
    void UpdateNowPlaying(const std::string& now_playing);

    bool InitializeAacDecoder();

    void CleanupAacDecoder();

    bool InitializeMp3Decoder(); 
    void CleanupMp3Decoder();    

    void ResetSampleRate();
    size_t SkipId3Tag(uint8_t* data, size_t size);

    // HLS Playlist Helper
    std::string ParseHlsPlaylist(const char* playlist_data, size_t size, const std::string& base_url);
    HlsPlaylistInfo ParseHlsPlaylistAdvanced(const char* playlist_data, size_t size, const std::string& base_url);
    void DownloadHlsStream(const HlsPlaylistInfo& playlist_info);
    bool DownloadHlsSegment(const std::string& segment_url, size_t buffer_size, int max_retries);
    HlsPlaylistInfo RefreshHlsPlaylist(const std::string& playlist_url);
    
    int16_t* final_pcm_data_fft = nullptr;

public:
    Esp32Radio();
    ~Esp32Radio();

    void Initialize();
    virtual bool PlayStation(const std::string& station_name) override;
    virtual bool PlayUrl(const std::string& radio_url, const std::string& station_name = "") override;
    virtual bool Stop() override;
    virtual std::vector<std::string> GetStationList() const override;
    virtual bool IsPlaying() const override { return is_playing_; }
    virtual std::string GetCurrentStation() const override { return current_station_name_; }
    virtual size_t GetBufferSize() const override { return buffer_size_; }
    virtual bool IsDownloading() const override { return is_downloading_; }
    virtual int16_t* GetAudioData() override { return final_pcm_data_fft; }
    void SetDisplayMode(DisplayMode mode);

    // Gain nội bộ cho stream (khác với volume của loa). Dùng để bù âm lượng đài nhỏ.
    void SetStreamGain(float gain);
    float GetStreamGain() const;

    void SetBufferMode(BufferMode mode);
    BufferMode GetBufferMode() const;

    void SetMetadataEnabled(bool enabled);
    bool IsMetadataEnabled() const;
    std::string GetNowPlaying() const;

	// Hàm tìm kiếm thông minh (Hybrid: Offline + Online)
    virtual std::vector<RadioStation> SearchStations(const std::string& query);

};

#endif // ESP32_RADIO_H