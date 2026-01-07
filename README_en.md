<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## Offline/Online Media • Internet Radio • Alarm • Vietnamese Lunar Calendar • Weather

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![English](https://img.shields.io/badge/UI-English-000000?style=for-the-badge)](./)

**Developer/Maintainer:** **Nguyễn Thọ Chung (NTC)**  
Focus integrations: SD Card (offline/auto-cache), Internet Radio, Alarm, Lunar calendar, Weather, MCP tools.

**Language:** **English** | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [Tiếng Việt](README.md)

</div>

---

## Document goal
This document describes the existing **media + utility features** in the project, in the spirit of: “read it once and you can use it, and also quickly locate the right file in the code”.

Focus scope:
- 🎵 SD card offline music (index, metadata/genre, browse/search via MCP tools)
- 🌐 Online music (prioritize SD first, streaming + auto-cache)
- 📻 Internet radio (AAC)
- ⏰ Alarm (offline, NVS persistence, snooze/dismiss)
- 🌙 Vietnamese lunar calendar (offline, perpetual calendar)
- 🌦️ Weather (OpenWeatherMap + IP auto-location)
- 🖥️ Idle overlay (rotates Weather ↔ Lunar)
- 🧩 MCP tools registry (tool name/args/behavior)

---

## 1) Feature matrix (quick summary)

| Group | Feature | Offline | Online | Technical notes |
|---|---|:--:|:--:|---|
| 🎵 SD Music | Play music from SD | ✅ | — | Prefer reading `playlist.json`; scan SD only if missing/corrupt |
| 🎵 SD Music | Search / browse folders / play by index | ✅ | — | Controlled via MCP tools `self.sdmusic.*` |
| 🎵 SD Music | Shuffle / Repeat | ✅ | — | Repeat: none / one / all |
| 🎵 SD Music | Genre (ID3) | ✅ | — | ID3v1 genre table + normalized TCON (ID3v2) |
| 🌐 Online Music | Online playback | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | Auto-cache MP3 to SD | ✅* | ✅ | Cached to `/sdcard/music` after playing for ~15 seconds minimum |
| 📻 Radio | Internet radio playback | — | ✅ | AAC; built-in station list + play by URL |
| ⏰ Alarm | Scheduling + Snooze/Dismiss + ringtone | ✅ | — | Persisted in NVS (`alarm/list`), tool `self.alarm.*` |
| 🌙 Lunar | Solar ↔ Lunar + perpetual calendar | ✅ | — | Hour Can-Chi, auspicious hours, solar terms, festivals |
| 🌦️ Weather | Current + forecast | — | ✅ | OpenWeatherMap; IP auto-location; cached location |

\* Auto-cache requires SD card mounted and write permission.

---

## 2) 🎵 OFFLINE music (SD Card)

### 2.1 User experience
- When the user requests **play a song**, the system will:
  - **Search on SD first** (fast, no network)
  - If not found → fall back to online (if the online subsystem is enabled)

### 2.2 `playlist.json` (faster boot & library browsing)
- An index file is generated on the SD card to reduce library scanning time at each boot:
  - Default: `/sdcard/playlist.json`
  - (or under a folder if you set a different root)
- Flow:
  1) If `playlist.json` **is valid** → read it only (fast)
  2) If **missing/corrupt/empty** → scan SD to rebuild and persist `playlist.json`

> Ops tip: when you copy/add/remove music on the SD card, you can call tool `self.sdmusic.reload` to rescan the library.

### 2.3 Metadata & Genre (ID3)
- Includes an **ID3v1 genre** lookup table
- Includes a “safe” minimal ID3v2 reader focused on common frames:
  - Title/Artist/Album/Year/Genre  
- Genre is normalized for consistent display.

---

## 3) 🌐 ONLINE music (Streaming + auto-cache to SD)

### 3.1 Offline-first routing
- Tool `self.music.play_song` will:
  - Look up the track on SD (by song/artist name) → if found: play offline
  - If not found → invoke the online player

### 3.2 Auto-cache MP3 to SD (listen online, keep for later)
- While streaming MP3 online, the system can save the file to:
  - `/sdcard/music/`
- Cache policy (current implementation):
  - Only commit after a minimum listen duration (typically ~15 seconds)
  - If the stream fails early, it avoids leaving junk files

### 3.3 Music server configuration
- Music server URL / API depends on the firmware/server you are using.
- Cache behavior and offline/online routing live in the music module (see **File/Module** section).

---

## 4) 📻 Internet Radio (AAC)

### 4.1 Highlights
- Plays Internet radio (AAC) from a station list
- Can also play directly by URL

### 4.2 Control by “station name” and by “URL”
- By station list:
  - `self.radio.play_station`
- By URL:
  - `self.radio.play_url`
- Stop:
  - `self.radio.stop`

---

## 5) 🌙 Vietnamese Lunar Calendar (Offline)

### 5.1 Core functionality
- Convert **Solar ↔ Lunar** (supports leap months)
- Full Can–Chi display:
  - **Year** (`CanChiYear`): e.g., Ất Tỵ
  - **Month** (`CanChiMonth`)
  - **Day** (`CanChiDay`)
  - **Hour** (`CanChiHour`): based on “Ngũ Thử Độn”

### 5.2 Perpetual calendar & feng shui
- **Auspicious/inauspicious day**: based on lunar month + day’s Chi
- **Auspicious hours**: list of favorable hour blocks (Tý, Sửu, Thìn…)
- **Solar terms**: 24 solar terms based on the Sun’s ecliptic longitude
- **Events & festivals**: identifies traditional dates (Tết, Rằm, Ông Táo...)

### 5.3 Smart UI (Lunar Widget)
- **Day/Night Theme**: auto switches background/text (dark from 18:00 to 06:00)
- **Carousel Info**: rotates Can-Chi (Day/Month/Year/Hour/Day’s Chi) every ~3 seconds
- **Font normalization**: `NormalizeVietnameseForFont` for stable Vietnamese rendering on embedded fonts

### 5.4 Timezone configuration
- Default: `UTC+7` (Vietnam)
- Compile-time override: `LUNAR_TZ_HOURS`

### 5.5 Source architecture (quick file guide)
- Conversion + perpetual calendar:
  - `main/boards/common/lunar_calendar.cc|.h`
  - Main APIs: `SolarToLunar()`, `LunarToSolar()`, `SolarTermName()`, `AuspiciousHours()`, `DayQualityByLunarMonth()`
- UI widget:
  - `main/boards/common/lunar_widget.cc|.h`
  - `LunarWidget::UpdateAll()` is the central routine updating text/colors in real time
- Idle display coordination:
  - `main/application.cc` (idle overlay rotation Weather ↔ Lunar)

### 5.6 Solar ↔ Lunar conversion algorithm (as implemented)
Implementation in `lunar_calendar.cc` uses common astronomical formulas (Julian day, new moon, Sun longitude):
- `JulianDayFromDate()` / `DateFromJulianDay()` converts Solar date ↔ JD
- `GetNewMoonDay(k, tz)` computes new moon day
- `GetSunLongitude(jd, tz)` computes Sun longitude and quantizes to 12 segments
- `GetLunarMonth11(yy, tz)` finds lunar month 11 as a year anchor
- `GetLeapMonthOffset(a11, tz)` determines leap month by comparing Sun-longitude “segments” across new moons

Output `LunarDate`: `day`, `month`, `year`, `is_leap`.

### 5.7 Advanced display logic in Lunar Widget
- Can-Chi carousel: mode 0..5, switches every ~3 seconds:
  - Lunar day label (Mùng/Rằm), Day Can-Chi, Month Can-Chi, Year Can-Chi, Hour Can-Chi, Day’s Chi
- Footer:
  - Prefer **auspicious hours** in rotation; if none → show **solar term**
  - If the day matches a lunar festival (`LunarFestivalName`) → highlight the festival

---

## 6) ⏰ Alarm (Alarm Manager v2)

### 6.1 Overview & user experience
- Alarm runs **offline** once the device has a **valid system time**
- When triggered:
  - Shows UI notification: **“Alarm – Time is up!”**
  - Plays the selected alarm sound (OGG) in a looped cycle

### 6.2 Data model & repeat schedule (`days_mask`)
Each alarm is a struct `Alarm` (key fields):
- `id` (string): stable identifier for CRUD via MCP
- `hour`, `minute`: 24h time
- `days_mask` (0..127): weekday repeat bitmask
  - Bit0=Sun, Bit1=Mon, … Bit6=Sat
  - `0`: one-shot (auto-deletes after ringing)
  - `127`: every day
  - Mon–Fri = `62` (0b0111110)
- `enabled`: on/off
- `label`: label
- `ringtone`: `"ga" | "alarm1" | "iphone"` (with fallback)
- `snooze_minutes`: 1..120 (default 10)
- `ring_interval_sec`: 1..60
- `max_rings`: 1..60

### 6.3 Scheduler: minute-accurate & duplicate-trigger guard
- Two timers:
  - `check_timer`: schedules to **next minute boundary + 1 second**, then checks alarms
  - `ring_timer`: plays sound periodically while ringing
- Duplicate trigger prevention:
  - runtime guard `last_fired_epoch_min` (not persisted) → prevents multiple triggers within the same minute

> “Valid time” per code: `time(nullptr) >= 2023-01-01 00:00:00 UTC` (`kMinValidEpoch`).  
> If SNTP/RTC is not synced yet, the scheduler waits and does not trigger alarms.

### 6.4 Ringing / Dismiss / Snooze
- Ring:
  - `AudioService::PlaySound()` every `ring_interval_sec`
  - Ringtone fallback: selected → `alarm1` → `ga`
- Dismiss:
  - stop ringing, cancel pending snooze, `display->ClearNotification()`
- Snooze:
  - creates an independent runtime “snooze alarm” (one-shot alarms can snooze too)
  - when reaching `snooze_until_epoch_sec_` → triggers again

### 6.5 Persistent storage (NVS/Settings)
- Persisted via `Settings` into NVS:
  - Namespace: `"alarm"`
  - Key: `"list"`
  - Format: JSON array
- Backward compatibility:
  - legacy storage `repeat_daily=true` → mapped to `days_mask=127`

### 6.6 MCP tools for alarms (`self.alarm.*`)
- `self.alarm.set` — create/update an alarm  
  Args: `id?`, `hour`, `minute`, `days_mask`, `repeat_daily?`, `label?`, `ringtone?`, `snooze_minutes?`, `ring_interval_sec?`, `max_rings?`  
  Return: `{success,id,message}`
- `self.alarm.list` — list alarms + runtime status (ringing/snooze)
- `self.alarm.delete` — delete by `id`
- `self.alarm.enable` — enable/disable by `id`
- `self.alarm.clear` — delete all
- `self.alarm.stop` — dismiss + cancel snooze
- `self.alarm.snooze` — snooze the currently ringing alarm (`minutes=0` → use the alarm default)

### 6.7 Related files/modules
- Core: `main/boards/common/alarm_manager.cc|.h`
- Tool registry: `main/mcp_server.cc`
- Sound assets: `main/assets/common/alarm1.ogg`, `main/assets/common/ga.ogg`, `main/assets/common/iphone.ogg`

---

## 7) 🌦️ Weather (OpenWeatherMap + IP auto-location)

### 7.1 Displayed data
- Current weather
- 5-day forecast
- Description language: Vietnamese (`OPENWEATHER_LANG = "vi"`)

### 7.2 IP-based auto-location (when city is not configured)
Geolocation fallback chain:
1) `ipwho.is`
2) `ipwhois.app`
3) `ipapi.co`

IP location cache:
- TTL: **6 hours**

Weather update cadence:
- Runs a background scheduler (non-blocking UI)
- Includes background SNTP sync to keep the system clock accurate (for alarm + calendar)

### 7.3 API key & City configuration (NVS)
- Namespace `wifi`:
  - `weather_api_key`
  - `weather_city`
- Fallback namespace `weather`:
  - `api_key`
  - `city`

Conventions:
- Empty `city` or `"auto"` → enable IP auto-location

> Production recommendation: set a dedicated API key to manage quota and trace usage.

---

## 8) 🖥️ Idle overlay: Weather ↔ Lunar (better idle experience)
When the device is idle, the UI will:
- If **weather is ready** → rotate:
  - Weather widget ↔ Lunar widget
- If **no network / weather not ready** → keep Lunar widget (fallback)

Rotation interval:
- Once every **180 seconds (3 minutes)**

---

## 9) MCP Tools: “intent” ↔ “tool” (control mapping)
Below is the tool mapping registered in the on-device MCP server.

### 9.1 System / UI / Audio / Camera
- `self.get_device_status` — device status (network/audio/state/…)
- `self.audio_speaker.set_volume` — set volume
- `self.screen.set_brightness` — set brightness
- `self.screen.set_theme` — UI theme
- `self.camera.take_photo` — take a photo (board dependent)

### 9.2 Music playback (auto-prioritize SD first)
- `self.music.play_song`
  - Args:
    - `song_name` (required)
    - `artist_name` (optional)
  - Behavior:
    - Found on SD → play OFFLINE
    - Not found → play ONLINE (and may auto-cache)

### 9.3 SD Music (use only when the user explicitly wants to operate the SD library)
- Playback: `self.sdmusic.playback` (`action=play|pause|stop|next|prev`)
- Mode: `self.sdmusic.mode` (shuffle / repeat)
- Track ops: `self.sdmusic.track` (set/info/list/current)
- Directory ops: `self.sdmusic.directory`
- Search: `self.sdmusic.search`
- Library summary: `self.sdmusic.library`
- Reload index: `self.sdmusic.reload`
- Suggest: `self.sdmusic.suggest`
- Progress: `self.sdmusic.progress`
- Genre: `self.sdmusic.genre` + `self.sdmusic.genre_list`

### 9.4 Radio
- `self.radio.play_station`
- `self.radio.play_url`
- `self.radio.stop`
- `self.radio.get_stations`
- `self.radio.set_display_mode`

### 9.5 Alarm
- `self.alarm.set`
- `self.alarm.list`
- `self.alarm.delete`
- `self.alarm.enable`
- `self.alarm.clear`
- `self.alarm.stop`
- `self.alarm.snooze`

---

## 10) Related files/modules (to quickly find the right code)

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

### ⏰ Alarm
- `main/boards/common/alarm_manager.cc`
- `main/boards/common/alarm_manager.h`

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

## 11) Whole-project source analysis (by architecture)

This section follows the source structure so you can “read the code with intent” (ESP-IDF / C++).

### 11.1 High-level folder structure
- `main/` — all firmware logic (core app, protocol, audio, display, boards…)
- `docs/` — protocol docs (MCP), websocket/mqtt-udp, custom board guides…
- `scripts/` — asset build tools, audio/image conversion, release helpers
- `partitions/` — partition tables (v1/v2), depending on flash size
- `sdkconfig.defaults*` — default configs per chip (esp32/esp32s3/esp32c3…)
- `.github/` — CI/CD workflows

### 11.2 Boot flow (Entry → Main loop)
1) `main/main.cc`
   - Initialize NVS (`nvs_flash_init`) for Wi-Fi/settings
   - Call `Application::GetInstance().Initialize()` then `Run()` (no return)
2) `main/application.cc|.h`
   - Setup display/audio/board callbacks
   - Init protocol (Websocket/MQTT depending on build)
   - Run the event loop: network, state machine, UI/audio, MCP messages

### 11.3 Core orchestration: Application + eventing model
- `Application` holds system “pieces”:
  - `DeviceStateMachine state_machine_`
  - `AudioService audio_service_`
  - `Protocol` (server transport; receives MCP/tool calls)
  - `WeatherService` (conditional), idle overlay timer (Weather↔Lunar)
- Orchestration pattern:
  - callbacks from ISR/aux tasks push events/closures into `main_tasks_`
  - `Run()` pulls and processes sequentially to avoid races

### 11.4 State machine (what is the device doing?)
- `main/device_state.h` — state enum (idle/listening/speaking/…)
- `main/device_state_machine.cc|.h`
  - enforces valid transitions
  - emits state-changed events to `Application::OnStateChanged()` to update UI/audio and start/stop timers

### 11.5 Board abstraction & driver layer
- `main/boards/common/board.cc|.h` — shared interface: Display, audio codec, buttons, mic/speaker, power/battery, SD mount…
- `main/boards/<board-name>/...` — board-specific pinmap and drivers: display type, codec, LED strip…
- Shared modules:
  - `sd_mount.cc|.h` — SD mount (`/sdcard`)
  - `wifi_board.cc|.h` + `blufi.h` — Wi‑Fi + provisioning (board dependent)
  - `adc_battery_monitor.*`, PMIC (`axp2101.*`, `sy6970.*`) — power/battery

### 11.6 Audio stack
- `main/audio/`:
  - `audio_service.*` — playback API / audio pipeline
  - `audio_codec.*` + `audio/codecs/*` — codec drivers (ES8311/ES8388/…)
  - `audio_processor.*` — pre-processing (AEC/NS/AGC depending on build)
- Audio assets:
  - `main/assets/common/*.ogg` + `main/assets/locales/<lang>/*.ogg`
  - `main/assets.cc|.h` — packs/looks up localized assets

### 11.7 Display / LVGL UI
- `main/display/`:
  - `display.*` — LVGL wrapper, root screen, notification, theme
  - `lvgl_display/*` — drivers & helpers (gif/jpg, font, canvas…)
- Utility widgets (`main/boards/common/`):
  - `weather_widget.*`, `lunar_widget.*`
- Notification:
  - `Application::Alert(...)` is called by Alarm/Protocol to show notifications

### 11.8 Protocol layer + MCP (tool calling)
- `main/protocols/`:
  - `protocol.h` — abstraction
  - `websocket_protocol.*`, `mqtt_protocol.*` — transports
- MCP server (on-device):
  - `main/mcp_server.cc|.h`
  - registers tools by capability:
    - system: volume/brightness/theme/camera/status
    - media: music/sdmusic/radio
    - utility: alarm

### 11.9 Storage: NVS + SD card
- `main/settings.cc|.h` — NVS wrapper (string/int/bool, erase…)
- SD card:
  - offline music + cache: `/sdcard/music`
  - index: `playlist.json`
- Alarm:
  - NVS: namespace `alarm`, key `list` (JSON array)

### 11.10 Tooling (scripts)
- `scripts/build_default_assets.py` — build default assets
- `scripts/gen_lang.py` — generate `language.json` / localized language assets
- `scripts/mp3_to_ogg.sh`, `scripts/ogg_converter/*` — normalize audio assets
- `scripts/Image_Converter/*` — convert images to LVGL formats
- `scripts/release.py`, `scripts/versions.py` — release helpers

---

## 12) Operational checklist (practical)

### SD card
- [ ] SD mounted OK
- [ ] Music present on SD
- [ ] Write enabled to create:
  - `playlist.json`
  - cache folder `/sdcard/music`

### Online/Radio/Weather
- [ ] Stable Wi‑Fi
- [ ] Weather: `weather_api_key` set (recommended)
- [ ] City: use `"auto"` for IP auto-location

---

### Document scope
This doc follows the current source and focuses on:  
**offline music / online music / radio / alarm / lunar / weather**.
