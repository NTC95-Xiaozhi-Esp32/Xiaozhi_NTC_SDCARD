#ifndef ESP32_MUSIC_H
#define ESP32_MUSIC_H

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>
#include <cstddef>   // size_t
#include <utility>   // std::pair

#include "music.h"

// MP3 decoder support
extern "C" {
#include "mp3dec.h"
}

// Audio data chunk structure
struct AudioChunk {
    uint8_t* data;
    size_t size;

    AudioChunk() : data(nullptr), size(0) {}
    AudioChunk(uint8_t* d, size_t s) : data(d), size(s) {}
};

// LRC/Karaoke lyric line (ms + text)
struct LyricLine {
    int start_ms;
    std::string text;
};

class Esp32Music : public Music {
public:
    // Display mode control
    enum DisplayMode {
        DISPLAY_MODE_SPECTRUM = 0,  // Default: display spectrum
        DISPLAY_MODE_LYRICS   = 1   // Display lyrics
    };

private:
    // Download/meta state
    std::string last_downloaded_data_;
    std::string current_music_url_;
    std::string artist_name_;
    std::string title_name_;
    std::string current_song_name_;

    bool song_name_displayed_ = false;
    bool full_info_displayed_ = false;
    bool fft_started_         = false;

    // Lyrics-related (cũ – cho SetChatMessage)
    std::string current_lyric_url_;
    std::vector<std::pair<int, std::string>> lyrics_;  // Timestamp and lyric text
    std::mutex  lyrics_mutex_;                         // Protect lyrics_
    std::atomic<int>  current_lyric_index_{-1};
    std::thread       lyric_thread_;
    std::atomic<bool> is_lyric_running_{false};

    // Karaoke/LRC (mới – cho UpdateKaraokeLine/Progress)
    std::vector<LyricLine> parsed_lyrics_;
    std::thread            karaoke_thread_;
    std::atomic<bool>      karaoke_running_{false};

    // Streaming + cache controls
    std::atomic<DisplayMode> display_mode_{DISPLAY_MODE_SPECTRUM};
    std::atomic<bool>        is_playing_{false};
    std::atomic<bool>        is_downloading_{false};

    std::atomic<bool>        auto_cache_to_sd_{true};   // bật/tắt auto cache (mặc định bật)
    std::atomic<bool>        is_caching_{false};        // trạng thái thread cache MP3
    std::string              auto_cache_file_name_;     // tên file .mp3 trên SD (từ title)

    std::thread              play_thread_;
    std::thread              download_thread_;
    std::thread              cache_thread_;             // thread tải/lưu MP3

    int64_t                  current_play_time_ms_{0};  // Current playback time (ms)
    int64_t                  last_frame_time_ms_{0};    // Timestamp of the last frame
    int                      total_frames_decoded_{0};  // Total number of decoded frames

    // Audio buffer
    std::queue<AudioChunk>  audio_buffer_;
    std::mutex              buffer_mutex_;
    std::condition_variable buffer_cv_;
    size_t                  buffer_size_{0};

    static constexpr size_t MAX_BUFFER_SIZE = 256 * 1024;  // 256KB buffer
    static constexpr size_t MIN_BUFFER_SIZE = 32 * 1024;   // 32KB minimum playback buffer

    // MP3 decoder-related
    HMP3Decoder  mp3_decoder_{nullptr};
    MP3FrameInfo mp3_frame_info_{};         // zero-init
    bool         mp3_decoder_initialized_{false};

    // FFT buffer for display
    int16_t* final_pcm_data_fft = nullptr;
    size_t   final_pcm_data_fft_size = 0;

private:
    // Private methods
    void DownloadAudioStream(const std::string& music_url);
    void PlayAudioStream();
    void ClearAudioBuffer();
    bool InitializeMp3Decoder();
    void CleanupMp3Decoder();
    void ResetSampleRate();  // Reset sample rate to the original value

    // Lyrics-related private methods (cũ)
    bool DownloadLyrics(const std::string& lyric_url);
    bool ParseLyrics(const std::string& lyric_content);
    void LyricDisplayThread();
    void UpdateLyricDisplay(int64_t current_time_ms);

    // Karaoke/LRC private methods (mới)
    bool  ParseLRC(const std::string& raw_lrc);
    int   FindLyricIndex(int current_ms);
    float LineProgress(int current_ms, int idx);
    void  StartKaraoke();
    void  StopKaraoke();
    void  KaraokeThread();

    // Overload cho RAW LRC (dùng cho ParseLRC)
    bool DownloadLyrics(const std::string& lyric_url, std::string& out_raw_lrc);

    // Check if MP3 file already exists on SD card (avoid duplicate download)
    bool IsSongAlreadyCached();

    // ID3 tag handling
    size_t SkipId3Tag(uint8_t* data, size_t size);

    // Save MP3 to SD (auto cache)
    void DownloadAndSaveMp3(const std::string& url,
                            const std::string& file_path);

public:
    Esp32Music();
    ~Esp32Music();

    virtual bool Download(const std::string& song_name,
                          const std::string& artist_name) override;

    virtual std::string GetDownloadResult() override;

    // Streaming control
    virtual bool   StartStreaming(const std::string& music_url) override;
    virtual bool   StopStreaming() override;
    virtual size_t GetBufferSize() const override { return buffer_size_; }
    virtual bool   IsDownloading() const override { return is_downloading_.load(); }
    virtual int16_t* GetAudioData() override { return final_pcm_data_fft; }

    // Display mode control
    void        SetDisplayMode(DisplayMode mode);
    DisplayMode GetDisplayMode() const { return display_mode_.load(); }

    // Server URL
    std::string GetCheckMusicServerUrl();
};

#endif // ESP32_MUSIC_H
