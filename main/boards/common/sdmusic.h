#ifndef SDMUSIC_H
#define SDMUSIC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * SdMusic
 * -------
 * Interface for SD-card music playback (playlist, directory browsing, transport controls).
 *
 * Naming and method signatures are aligned to the existing Esp32SdMusic implementation.
 */
class SdMusic {
public:
    virtual ~SdMusic() = default;

    // ============================================================
    // State / Repeat
    // ============================================================
    enum class PlayerState {
        Stopped = 0,
        Preparing,
        Playing,
        Paused,
        Error
    };

    enum class RepeatMode {
        None = 0,
        RepeatOne,
        RepeatAll
    };

    // ============================================================
    // Track metadata (lightweight, ID3v1-focused)
    // ============================================================
    struct TrackInfo {
        // Display
        std::string name;  // Display name (prefer ID3v1 title, fallback: filename)
        std::string path;  // Absolute path

        // Basic metadata (ID3v1)
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;
        std::string comment;
        std::string year;
        int         track_number = 0;

        // Audio info (updated during decode)
        int         duration_ms  = 0;
        int         bitrate_kbps = 0;
        size_t      file_size    = 0;

        // Cover art (kept for compatibility; not parsed)
        uint32_t    cover_size = 0;
        std::string cover_mime;
    };

    struct TrackProgress {
        int64_t position_ms = 0;
        int64_t duration_ms = 0;
    };

    // ============================================================
    // Playlist / browsing
    // ============================================================
    virtual bool loadTrackList() = 0;
    virtual size_t getTotalTracks() const = 0;
    virtual std::vector<TrackInfo> listTracks() const = 0;

    virtual bool setDirectory(const std::string& relative_dir) = 0;
    virtual bool playDirectory(const std::string& relative_dir) = 0;

    virtual bool playByName(const std::string& keyword) = 0;
    virtual TrackInfo getTrackInfo(int index) const = 0;
    virtual bool setTrack(int index) = 0;

    virtual std::string getCurrentTrack() const = 0;
    virtual std::string getCurrentTrackPath() const = 0;

    virtual std::vector<std::string> listDirectories() const = 0;
    virtual std::vector<TrackInfo> searchTracks(const std::string& keyword) const = 0;

    virtual std::string resolveLongName(const std::string& path) = 0;
    virtual std::string resolveCaseInsensitiveDir(const std::string& path) = 0;

    virtual size_t countTracksInDirectory(const std::string& relative_dir) = 0;
    virtual size_t countTracksInCurrentDirectory() const = 0;

    virtual std::vector<TrackInfo> listTracksPage(size_t page_index,
                                                  size_t page_size = 10) const = 0;

    virtual bool rebuildPlaylistFromSd() = 0;

    // ============================================================
    // Playback control
    // ============================================================
    virtual bool play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;

    virtual bool next() = 0;
    virtual bool prev() = 0;

    // ============================================================
    // Playback settings
    // ============================================================
    virtual void shuffle(bool enabled) = 0;
    virtual void repeat(RepeatMode mode) = 0;

    // ============================================================
    // State / FFT query
    // ============================================================
    virtual PlayerState getState() const = 0;
    virtual TrackProgress updateProgress() const = 0;
    virtual int16_t* getFFTData() const = 0;

    virtual int64_t getDurationMs() const = 0;
    virtual int64_t getCurrentPositionMs() const = 0;
    virtual int getBitrate() const = 0;
    virtual std::string getDurationString() const = 0;
    virtual std::string getCurrentTimeString() const = 0;

    // ============================================================
    // Suggestions
    // ============================================================
    virtual std::vector<TrackInfo> suggestNextTracks(size_t max_results = 5) = 0;
    virtual std::vector<TrackInfo> suggestSimilarTo(const std::string& name_or_path,
                                                    size_t max_results = 5) = 0;

    // ============================================================
    // Genre-based playlist (ID3v1 genre index)
    // ============================================================
    virtual bool buildGenrePlaylist(const std::string& genre) = 0;
    virtual bool playGenreIndex(int pos) = 0;
    virtual bool playNextGenre() = 0;
    virtual std::vector<std::string> listGenres() const = 0;
};

#endif // SDMUSIC_H
