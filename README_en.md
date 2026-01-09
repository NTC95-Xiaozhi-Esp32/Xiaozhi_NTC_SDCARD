<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## Offline/Online Media • Internet Radio • Vietnamese Lunar Calendar • Weather

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![Vietnamese UI](https://img.shields.io/badge/UI-Ti%E1%BA%BFng%20Vi%E1%BB%87t-000000?style=for-the-badge)](./)

</div>

---

## Document goals
This document describes the **media features + utilities** already present in the current source:

- 🎵 **Offline music from SD** (playlist.json, search, shuffle/repeat, genre)
- 🌐 **Online music** (streaming + auto-cache MP3 to SD)
- 📻 **Internet Radio** (AAC)
- 🌙 **Vietnamese lunar calendar** (offline, Can–Chi, auspicious hours/days, solar terms)
- 🌦️ **Weather** (OpenWeatherMap + IP-based auto-location)
- 🖥️ **Idle overlay**: automatically rotates *Weather ↔ Lunar* (with sensible offline fallback)

> Tip: If you want the build/flash overview README, see the project’s original README. This file focuses only on the 5 feature groups above.

---

## 1) Feature matrix (quick summary)

| Group | Feature | Offline | Online | Technical notes |
|---|---|:--:|:--:|---|
| 🎵 SD Music | Play music from SD | ✅ | — | Prefer `playlist.json`; scan SD only if missing/corrupted |
| 🎵 SD Music | Search / browse folders / play by index | ✅ | — | Controllable via MCP tools `self.sdmusic.*` |
| 🎵 SD Music | Shuffle / Repeat | ✅ | — | Repeat: none / one / all |
| 🎵 SD Music | Genre (ID3) | ✅ | — | ID3v1 genre table + TCON normalization (ID3v2) |
| 🌐 Online Music | Play online | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | Auto-cache MP3 to SD | ✅* | ✅ | Cache to `/sdcard/music` after at least 15 seconds of playback |
| 📻 Radio | Play Internet radio | — | ✅ | AAC format only; built-in VN station list + play by URL |
| 🌙 Lunar | Solar ↔ Lunar + almanac | ✅ | — | Hour Can–Chi, auspicious days/hours, solar terms, festivals |
| 🌦️ Weather | Current + 5-day forecast | — | ✅ | OWM; IP auto-location (ipwho.is → ipwhois.app → ipapi.co) |
| 🖥️ Idle overlay | Rotate Weather/Lunar | ✅ | ✅ | Every 1 minutes; if no weather, keep Lunar; background auto changes based on day/night |

\* Auto-cache writes to SD, but the data source is online.

---

## 2) 🎵 OFFLINE music (SD Card)

### 2.1 User experience
- Play directly from SD — useful when:
  - There is no network
  - You want a fixed “music library” with low latency
- Supports:
  - Folder browsing / track listing
  - Keyword search
  - Play by index
  - Shuffle / Repeat
  - Genre-based playlists

### 2.2 playlist.json (faster boot & library browsing)
The playlist loading flow is optimized for embedded devices:

- Default path:  
  `/<SD_MOUNT>/playlist.json`  
  (or under a folder if you use a different root)
- Processing:
  1) If `playlist.json` has **valid content** → read the file only (fast)
  2) If **missing/corrupted/empty** → scan SD, rebuild, and save `playlist.json`

> Ops tip: after copying/removing music on the SD card, you can call `self.sdmusic.reload` to rescan the library.

### 2.3 Metadata & Genre (ID3)
- Includes an **ID3v1** genre lookup table
- Reads ID3v2 in a “safe” (minimal) mode, focusing on common frames:
  - Title / Artist / Album / Year / Genre
- Genre values are normalized for clean, consistent display

---

## 3) 🌐 ONLINE music (Streaming + auto-cache to SD)

### 3.1 Offline-first routing
Tool **`self.music.play_song`** follows this logic:

1) If SD contains a matching track name (SD library already loaded) → play **OFFLINE**
2) Otherwise → play **ONLINE** (download/stream)

### 3.2 Auto-cache MP3 to SD (listen online, keep for offline replay)
When streaming online, the firmware can auto-cache:

- Cache folder: `/sdcard/music`
- Start caching only after **15 seconds** of playback
- Cache limit: **100 tracks**
  - If exceeded, automatically delete the **oldest file** (by `st_mtime`) to free space

> Note: This is a pragmatic cache policy optimized for UX. If you need a different policy (by size, true LRU, tag/album-based, etc.), adjust the constants and eviction logic in `esp32_music.cc`.

### 3.3 Music server configuration
The default music server URL is defined in:

- `main/boards/common/esp32_music.cc`  
  `#define DEFAULT_MUSIC_URL "http://...:5005"`

Change the macro and rebuild the firmware to switch servers.

---

## 4) 📻 Internet Radio (AAC)

### 4.1 Highlights
- Internet radio playback with an **AAC** decoder
- Built-in station list (prioritizing VN content), e.g.:
  - JoyFM 98.9
  - VOV1 / VOV2 / VOV3 / VOV5
  - VOV Traffic (HN/HCM)
  - Regional VOV (Tây Bắc / Đông Bắc / Tây Nguyên / Mekong / …)

### 4.2 Control by “station name” or “URL”
- Play by station name: `self.radio.play_station`
- Play by URL: `self.radio.play_url`
- Stop: `self.radio.stop`
- Get station list: `self.radio.get_stations`
- Set display mode: `self.radio.set_display_mode`

> Technical note: Initialization logs clearly indicate the station list is currently designed as **AAC format only**.

---

## 5) 🌙 Vietnamese Lunar Calendar (Offline)

### 5.1 Core features
- Convert **Solar ↔ Lunar** (supports leap month)
- Full Can–Chi system:
  - **Year** (`CanChiYear`): e.g., Ất Tỵ
  - **Month** (`CanChiMonth`): e.g., Mậu Dần
  - **Day** (`CanChiDay`): e.g., Tân Tỵ
  - **Hour** (`CanChiHour`): computed via "Ngũ Thử Độn" (e.g., Giờ Đinh Dậu)

### 5.2 Almanac & Feng Shui
- **Auspicious / inauspicious days**: based on lunar month and day’s “Chi”
- **Auspicious hours**: computes good time windows (Tý, Sửu, Thìn...)
- **Solar terms**: computes 24 solar terms by Sun’s ecliptic longitude (Lập Xuân, Thanh Minh, Đông Chí...)
- **Events & festivals**: recognizes traditional holidays (Tết, Rằm, Ông Táo...)

### 5.3 Smart UI (Lunar Widget)
- **Day/Night Theme**: auto switches background/text colors from 18:00 to 06:00
- **Carousel Info**: rotates Can–Chi info (Day/Month/Year/Hour) every 3 seconds
- **Font handling**: `NormalizeVietnameseForFont` for smooth Vietnamese rendering on embedded fonts

### 5.4 Timezone configuration
- Default: `UTC+7` (Vietnam)
- Compile-time override: `LUNAR_TZ_HOURS`

---

## 6) 🌦️ Weather (OpenWeatherMap + IP auto-location)

### 6.1 Displayed data
- Current weather
- 5-day forecast
- Description language: **Vietnamese** (`OPENWEATHER_LANG = "vi"`)

### 6.2 IP-based auto-location (when city is not configured)
Geolocation priority chain:

1) `ipwho.is`  
2) `ipwhois.app`  
3) `ipapi.co`

IP location cache:
- TTL: **6 hours**

Weather refresh interval:
- **Every 10 minutes**

### 6.3 API key & City configuration (NVS)
WeatherService reads configuration from NVS in this order:

- Namespace `wifi`:
  - `weather_api_key`
  - `weather_city`
- Fallback namespace `weather`:
  - `api_key`
  - `city`

Convention:
- `city` empty or `"auto"` → enable IP auto-location

> Note: The firmware may include a default API key for quick testing. In production, you should set your own key to manage quota and improve traceability.

---

## 7) 🖥️ Idle overlay: Weather ↔ Lunar (idle-time UX optimization)
When the device is idle, the UI behaves as follows:

- If **weather is ready** → rotate:
  - Weather widget ↔ Lunar widget
- If **no network / weather not ready** → keep Lunar widget (standard fallback)

Rotation frequency:
- **180 seconds (3 minutes)**

---

## 8) MCP Tools: “intent” ↔ “tool” (for control)
Below is the mapping of tools registered in the MCP server:

### 8.1 Music (auto-prioritize SD first)
- `self.music.play_song`
  - Args:
    - `song_name` (required)
    - `artist_name` (optional)
  - Behavior:
    - Track exists on SD → play OFFLINE
    - Otherwise → play ONLINE

### 8.2 SD Music (use when the user wants direct SD library control)
- Basic playback: `self.sdmusic.playback` (`action = play|pause|stop|next|prev`)
- Playback mode: `self.sdmusic.mode` (shuffle / repeat)
- Track: `self.sdmusic.track` (play by index, get track info, etc.)
- Directory: `self.sdmusic.directory`
- Search: `self.sdmusic.search`
- Library: `self.sdmusic.library`
- Rescan: `self.sdmusic.reload`
- Suggestions: `self.sdmusic.suggest`
- Progress: `self.sdmusic.progress`
- Genre: `self.sdmusic.genre` and `self.sdmusic.genre_list`

### 8.3 Radio
- `self.radio.play_station`
- `self.radio.play_url`
- `self.radio.stop`
- `self.radio.get_stations`
- `self.radio.set_display_mode`

---

## 9) Related files/modules (to find the right place in code)

<details>
<summary><b>📁 Core file list</b></summary>

### 🎵 SD music
- `main/boards/common/esp32_sd_music.cc`
- `main/boards/common/esp32_sd_music.h`
- `main/boards/common/sd_music.h`

### 🌐 Online music + auto-cache
- `main/boards/common/esp32_music.cc`
- `main/boards/common/esp32_music.h`
- `main/boards/common/music.h`

### 📻 Radio
- `main/boards/common/esp32_radio.cc`
- `main/boards/common/esp32_radio.h`
- `main/boards/common/radio.h`

### 🌙 Lunar
- `main/boards/common/lunar_calendar.cc`
- `main/boards/common/lunar_calendar.h`
- `main/boards/common/lunar_widget.cc`
- `main/boards/common/lunar_widget.h`

### 🌦️ Weather
- `main/boards/common/weather_service.cc`
- `main/boards/common/weather_service.h`
- `main/boards/common/weather_widget.cc`
- `main/boards/common/weather_widget.h`

### 🖥️ Idle overlay rotation
- `main/application.cc`

### 🔧 MCP Tools registry
- `main/mcp_server.cc`

</details>

---

## 10) Ops checklist (quick & practical)

### SD card
- [ ] SD mount OK
- [ ] Music present on SD
- [ ] Write access to create:
  - `playlist.json`
  - cache folder `/sdcard/music`

### Online/Radio/Weather
- [ ] Stable Wi‑Fi
- [ ] Weather: set `weather_api_key` (recommended)
- [ ] City: set to `"auto"` if you want IP auto-location

---

### Document scope
This document tracks the current source and focuses on: **offline music / online music / radio / lunar / weather**.
