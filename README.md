<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## Media Offline/Online • Internet Radio • Báo thức • Lịch âm • Thời tiết

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![Vietnamese UI](https://img.shields.io/badge/UI-Ti%E1%BA%BFng%20Vi%E1%BB%87t-000000?style=for-the-badge)](./)

**Người phát triển/maintainer:** **Nguyễn Thọ Chung (NTC)**  
Tập trung tích hợp: SD Card (offline/auto-cache), Internet Radio, Báo thức, Lịch âm, Thời tiết, MCP tools.

</div>

---

## Mục tiêu tài liệu
Tài liệu này mô tả **các tính năng media + tiện ích** đã có trong source hiện tại, theo hướng “đọc xong biết dùng + biết tìm đúng file trong code”.

Phạm vi trọng tâm:
- 🎵 SD card offline music (index, metadata/genre, duyệt/tìm kiếm qua MCP tools)
- 🌐 Online music (ưu tiên SD trước, online + auto-cache)
- 📻 Internet radio (AAC)
- ⏰ Báo thức (offline, persist NVS, snooze/dismiss)
- 🌙 Lịch âm Việt Nam (offline, vạn niên)
- 🌦️ Thời tiết (OpenWeatherMap + auto-location IP)
- 🖥️ Idle overlay (luân phiên Weather ↔ Lunar)
- 🧩 MCP tools registry (tool name/args/hành vi)

---

## 1) Bảng tính năng (tóm tắt nhanh)

| Nhóm | Tính năng | Offline | Online | Ghi chú kỹ thuật |
|---|---|:--:|:--:|---|
| 🎵 SD Music | Phát nhạc từ SD | ✅ | — | Ưu tiên đọc `playlist.json`, hỏng/thiếu mới quét SD |
| 🎵 SD Music | Tìm kiếm / duyệt thư mục / phát theo index | ✅ | — | Hỗ trợ thao tác qua MCP tools `self.sdmusic.*` |
| 🎵 SD Music | Shuffle / Repeat | ✅ | — | Repeat: none / one / all |
| 🎵 SD Music | Genre (ID3) | ✅ | — | Có bảng genre ID3v1 + chuẩn hoá TCON (ID3v2) |
| 🌐 Online Music | Phát online | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | Auto-cache MP3 vào SD | ✅* | ✅ | Cache vào `/sdcard/music` sau khi nghe tối thiểu ~15 giây |
| 📻 Radio | Phát radio Internet | — | ✅ | AAC; có danh sách kênh dựng sẵn + phát theo URL |
| ⏰ Báo thức | Lập lịch + Snooze/Dismiss + ringtone | ✅ | — | Persist NVS (`alarm/list`), tool `self.alarm.*` |
| 🌙 Lịch âm | Solar ↔ Lunar + Vạn niên | ✅ | — | Can Chi giờ, Hoàng đạo, Tiết khí, Lễ hội |
| 🌦️ Thời tiết | Current + forecast | — | ✅ | OpenWeatherMap; auto-location IP; cache vị trí |

\* Auto-cache yêu cầu SD card mount OK và có quyền ghi.

---

## 2) 🎵 Nghe nhạc OFFLINE (SD Card)

### 2.1 Trải nghiệm người dùng
- Khi người dùng yêu cầu **phát bài hát**, hệ thống sẽ:
  - **Tìm trên SD trước** (nhanh, không tốn mạng)
  - Nếu không có → fallback sang online (nếu bật online subsystem)

### 2.2 playlist.json (tăng tốc khởi động & duyệt thư viện)
- File index được sinh ở SD để giảm thời gian quét thư viện mỗi lần khởi động:
  - Mặc định: `/sdcard/playlist.json`
  (hoặc theo thư mục khi bạn đặt root khác)
- Luồng xử lý:
  1) Nếu `playlist.json` **có nội dung hợp lệ** → chỉ đọc file (nhanh)
  2) Nếu **thiếu/hỏng/rỗng** → quét SD để rebuild và lưu lại `playlist.json`

> Gợi ý vận hành: khi bạn copy thêm/xoá nhạc trên SD, có thể gọi tool `self.sdmusic.reload` để quét lại thư viện.

### 2.3 Metadata & Genre (ID3)
- Có bảng tra **genre ID3v1**
- Có cơ chế đọc ID3v2 “an toàn” (tối giản), tập trung các frame thông dụng:
  - Title/Artist/Album/Year/Genre  
- Genre được chuẩn hoá để hiển thị gọn và thống nhất.

---

## 3) 🌐 Nghe nhạc ONLINE (Streaming + tự cache vào SD)

### 3.1 Điều phối ưu tiên OFFLINE trước
- Tool `self.music.play_song` sẽ:
  - Tìm bài trên SD (theo tên bài/ca sĩ) → nếu có: phát offline
  - Không có → gọi online player

### 3.2 Auto-cache MP3 vào SD (nghe online nhưng lưu để nghe lại)
- Khi stream MP3 online, hệ thống có thể tự lưu file về:
  - `/sdcard/music/`
- Chính sách cache (theo implementation hiện tại):
  - Nghe đủ một khoảng thời gian tối thiểu (thường ~15 giây) thì mới commit file
  - Nếu stream lỗi sớm thì không tạo file rác

### 3.3 Cấu hình server nhạc
- URL server nhạc / API tuỳ theo firmware/server bạn đang dùng.
- Phần cache và phân luồng offline/online nằm ở module music (xem mục **File/Module**).

---

## 4) 📻 Internet Radio (AAC)

### 4.1 Điểm nổi bật
- Chạy radio Internet (định dạng AAC) theo danh sách kênh
- Có thể play trực tiếp theo URL

### 4.2 Hỗ trợ điều khiển theo “tên kênh” và theo “URL”
- Theo danh sách kênh:
  - `self.radio.play_station`
- Theo URL:
  - `self.radio.play_url`
- Dừng:
  - `self.radio.stop`

---

## 5) 🌙 Lịch âm Việt Nam (Offline)

### 5.1 Chức năng cơ bản
- Chuyển đổi **Dương lịch ↔ Âm lịch** (hỗ trợ tháng nhuận/leap month)
- Hiển thị Can–Chi đầy đủ:
  - **Năm** (`CanChiYear`): ví dụ Ất Tỵ
  - **Tháng** (`CanChiMonth`)
  - **Ngày** (`CanChiDay`)
  - **Giờ** (`CanChiHour`): theo “Ngũ Thử Độn”

### 5.2 Lịch Vạn Niên & Phong Thủy
- **Ngày Hoàng Đạo / Hắc Đạo**: theo tháng âm và Chi ngày
- **Giờ Hoàng Đạo**: danh sách các khung giờ tốt (Tý, Sửu, Thìn…)
- **Tiết Khí**: 24 tiết khí theo kinh độ Mặt Trời
- **Sự kiện & Ngày lễ**: nhận diện ngày lễ cổ truyền (Tết, Rằm, Ông Táo...)

### 5.3 Giao diện thông minh (Lunar Widget)
- **Day/Night Theme**: tự động chuyển màu nền/chữ (tối từ 18:00 đến 06:00)
- **Carousel Info**: Can Chi (Ngày/Tháng/Năm/Giờ/Chi ngày) xoay vòng mỗi ~3 giây
- **Font xử lý**: `NormalizeVietnameseForFont` để hiển thị tiếng Việt ổn định trên font nhúng

### 5.4 Cấu hình Timezone
- Mặc định: `UTC+7` (Việt Nam)
- Override compile-time: `LUNAR_TZ_HOURS`

### 5.5 Kiến trúc mã nguồn (đọc nhanh theo file)
- Thuật toán chuyển đổi & lịch vạn niên:
  - `main/boards/common/lunar_calendar.cc|.h`
  - API chính: `SolarToLunar()`, `LunarToSolar()`, `SolarTermName()`, `AuspiciousHours()`, `DayQualityByLunarMonth()`
- UI widget:
  - `main/boards/common/lunar_widget.cc|.h`
  - `LunarWidget::UpdateAll()` là trung tâm cập nhật text/màu theo thời gian thực
- Điều phối hiển thị khi Idle:
  - `main/application.cc` (idle overlay rotation Weather ↔ Lunar)

### 5.6 Thuật toán chuyển đổi Dương ↔ Âm (đúng theo implementation)
Implementation trong `lunar_calendar.cc` sử dụng bộ công thức thiên văn phổ biến (Julian day + sóc vọng + kinh độ Mặt Trời):
- `JulianDayFromDate()` / `DateFromJulianDay()` đổi ngày dương ↔ JD
- `GetNewMoonDay(k, tz)` tính ngày sóc (trăng non)
- `GetSunLongitude(jd, tz)` tính kinh độ Mặt Trời và lượng tử hoá theo 12 cung
- `GetLunarMonth11(yy, tz)` tìm tháng 11 âm làm mốc năm âm
- `GetLeapMonthOffset(a11, tz)` xác định tháng nhuận bằng so sánh “cung” Mặt Trời giữa các kỳ sóc

Đầu ra `LunarDate`: `day`, `month`, `year`, `is_leap`.

### 5.7 Logic hiển thị nâng cao trong Lunar Widget
- Carousel Can-Chi: mode 0..5, đổi mỗi ~3 giây:
  - Nhãn ngày âm (Mùng/Rằm), Can Chi ngày, Can Chi tháng, Can Chi năm, Can Chi giờ, Chi ngày
- Footer:
  - Ưu tiên **Giờ hoàng đạo** theo chu kỳ; nếu không có → hiển thị **Tiết khí**
  - Nếu trùng ngày lễ âm (`LunarFestivalName`) thì highlight ngày lễ

---

## 6) ⏰ Báo thức (Alarm Manager v2)

### 6.1 Tổng quan & trải nghiệm
- Báo thức chạy **offline** sau khi thiết bị có **giờ hệ thống hợp lệ**
- Khi đến giờ:
  - Hiện thông báo UI: **“Báo thức – Đã đến giờ!”**
  - Phát âm báo thức (OGG) theo ringtone, lặp theo chu kỳ

### 6.2 Mô hình dữ liệu & lịch lặp (days_mask)
Mỗi báo thức là struct `Alarm` (tóm tắt trường chính):
- `id` (string): định danh ổn định để CRUD từ MCP
- `hour`, `minute`: giờ/phút 24h
- `days_mask` (0..127): bitmask lặp theo thứ
  - Bit0=CN, Bit1=T2, … Bit6=T7
  - `0`: one-shot (reo xong tự xoá)
  - `127`: mỗi ngày
  - T2–T6 = `62` (0b0111110)
- `enabled`: bật/tắt
- `label`: nhãn
- `ringtone`: `"ga" | "alarm1" | "iphone"` (có fallback)
- `snooze_minutes`: 1..120 (mặc định 10)
- `ring_interval_sec`: 1..60
- `max_rings`: 1..60

### 6.3 Scheduler: canh đúng phút & chống reo trùng
- 2 timer:
  - `check_timer`: canh tới **mốc phút tiếp theo + 1 giây** rồi check alarm
  - `ring_timer`: phát tiếng định kỳ khi đang reo
- Chống reo trùng:
  - guard runtime `last_fired_epoch_min` (không lưu) → tránh trigger lặp trong cùng phút

> Giờ “hợp lệ” theo code: `time(nullptr) >= 2023-01-01 00:00:00 UTC` (`kMinValidEpoch`).  
> Nếu chưa sync SNTP/RTC → scheduler chờ và không trigger.

### 6.4 Ringing / Dismiss / Snooze
- Reo:
  - `AudioService::PlaySound()` theo chu kỳ `ring_interval_sec`
  - Fallback ringtone: selected → `alarm1` → `ga`
- Dismiss:
  - dừng reo, huỷ snooze pending, `display->ClearNotification()`
- Snooze:
  - tạo “snooze alarm” runtime độc lập (one-shot vẫn snooze được)
  - tới `snooze_until_epoch_sec_` → trigger lại

### 6.5 Lưu trữ bền vững (NVS/Settings)
- Persist NVS bằng `Settings`:
  - Namespace: `"alarm"`
  - Key: `"list"`
  - Format: JSON array
- Tương thích ngược:
  - storage cũ `repeat_daily=true` → map sang `days_mask=127`

### 6.6 MCP tools cho báo thức (`self.alarm.*`)
- `self.alarm.set` — tạo/cập nhật alarm  
  Args: `id?`, `hour`, `minute`, `days_mask`, `repeat_daily?`, `enabled?`, `label?`, `ringtone?`, `snooze_minutes?`, `ring_interval_sec?`, `max_rings?`  
  Return: `{success,id,message}`
- `self.alarm.list` — danh sách alarm + trạng thái runtime (reo/snooze)
- `self.alarm.delete` — xoá theo `id`
- `self.alarm.enable` — bật/tắt theo `id`
- `self.alarm.clear` — xoá toàn bộ
- `self.alarm.stop` — dismiss + huỷ snooze
- `self.alarm.snooze` — snooze alarm đang reo (`minutes=0` → dùng default của alarm)

### 6.7 File/Module liên quan
- Core: `main/boards/common/alarm_manager.cc|.h`
- Tool registry: `main/mcp_server.cc`
- Sound assets: `main/assets/common/alarm1.ogg`, `main/assets/common/ga.ogg`, `main/assets/common/iphone.ogg`

---

## 7) 🌦️ Thời tiết (OpenWeatherMap + auto-location IP)

### 7.1 Dữ liệu hiển thị
- Current weather
- Forecast 5 ngày
- Ngôn ngữ mô tả: tiếng Việt (`OPENWEATHER_LANG = "vi"`)

### 7.2 Auto-location theo IP (khi không cấu hình city)
Chuỗi ưu tiên geolocation:
1) `ipwho.is`
2) `ipwhois.app`
3) `ipapi.co`

Cache vị trí IP:
- TTL: **6 giờ**

Chu kỳ cập nhật thời tiết:
- Có scheduler nền (không block UI)
- Kèm SNTP sync nền để đảm bảo hệ thống có thời gian chuẩn (phục vụ báo thức + lịch)

### 7.3 Cấu hình API key & City (NVS)
- Namespace `wifi`:
  - `weather_api_key`
  - `weather_city`
- Fallback namespace `weather`:
  - `api_key`
  - `city`

Quy ước:
- `city` rỗng hoặc `"auto"` → bật auto-location theo IP

> Khuyến nghị production: set API key riêng để quản trị quota và truy vết.

---

## 8) 🖥️ Idle overlay: Weather ↔ Lunar (tối ưu trải nghiệm khi rảnh)
Khi thiết bị vào trạng thái Idle, UI sẽ:
- Nếu **weather sẵn sàng** → luân phiên hiển thị:
  - Weather widget ↔ Lunar widget
- Nếu **không có mạng / weather không sẵn sàng** → giữ Lunar widget (fallback)

Tần suất luân phiên:
- **180 giây (3 phút)** một lần

---

## 9) MCP Tools: “ý định” ↔ “tool” (dành cho điều khiển)
Dưới đây là mapping công cụ đã đăng ký trong MCP server (thiết bị xử lý trực tiếp).

### 9.1 System / UI / Audio / Camera
- `self.get_device_status` — trạng thái thiết bị (network/audio/state/…)
- `self.audio_speaker.set_volume` — set volume
- `self.screen.set_brightness` — set brightness
- `self.screen.set_theme` — theme UI
- `self.camera.take_photo` — chụp ảnh (tuỳ board)

### 9.2 Nghe nhạc (tự ưu tiên SD trước)
- `self.music.play_song`
  - Args:
    - `song_name` (bắt buộc)
    - `artist_name` (tuỳ chọn)
  - Hành vi:
    - Có bài trên SD → phát OFFLINE
    - Không có → phát ONLINE (và có thể auto-cache)

### 9.3 SD Music (chỉ dùng khi người dùng muốn thao tác trực tiếp thư viện SD)
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

### 9.5 Báo thức
- `self.alarm.set`
- `self.alarm.list`
- `self.alarm.delete`
- `self.alarm.enable`
- `self.alarm.clear`
- `self.alarm.stop`
- `self.alarm.snooze`

---

## 10) File/Module liên quan (để bạn tìm đúng chỗ trong code)

<details>
<summary><b>📁 Danh sách file cốt lõi</b></summary>

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

### 🌙 Lịch âm
- `main/boards/common/lunar_calendar.cc`
- `main/boards/common/lunar_calendar.h`
- `main/boards/common/lunar_widget.cc`
- `main/boards/common/lunar_widget.h`

### ⏰ Báo thức
- `main/boards/common/alarm_manager.cc`
- `main/boards/common/alarm_manager.h`

### 🌦️ Thời tiết
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

## 11) Phân tích mã nguồn toàn dự án (theo kiến trúc)

Phần này đi theo đúng cấu trúc source để bạn “đọc code có định hướng” (ESP-IDF / C++).

### 11.1 Cấu trúc thư mục cấp cao
- `main/` — toàn bộ logic firmware (core app, protocol, audio, display, boards…)
- `docs/` — tài liệu giao thức (MCP), websocket/mqtt-udp, hướng dẫn custom board…
- `scripts/` — tool build assets, convert audio/image, release helper
- `partitions/` — bảng phân vùng (v1/v2), tuỳ flash size
- `sdkconfig.defaults*` — cấu hình mặc định theo chip (esp32/esp32s3/esp32c3…)
- `.github/` — CI/CD workflow

### 11.2 Luồng khởi động (Entry → Main loop)
1) `main/main.cc`
   - Khởi tạo NVS (`nvs_flash_init`) cho Wi-Fi/settings
   - Gọi `Application::GetInstance().Initialize()` rồi `Run()` (không return)
2) `main/application.cc|.h`
   - Setup display/audio/board callbacks
   - Init protocol (Websocket/MQTT tuỳ build)
   - Chạy event loop: network, state machine, UI/audio, MCP messages

### 11.3 Core orchestration: Application + Eventing model
- `Application` giữ các “mảnh” hệ thống:
  - `DeviceStateMachine state_machine_`
  - `AudioService audio_service_`
  - `Protocol` (kênh kết nối server, nhận MCP/toolcall)
  - `WeatherService` (tuỳ điều kiện), idle overlay timer (Weather↔Lunar)
- Mẫu điều phối:
  - callback từ ISR/task phụ đẩy event/closure vào `main_tasks_`
  - `Run()` pull và xử lý tuần tự, tránh race

### 11.4 State machine (thiết bị đang làm gì?)
- `main/device_state.h` — enum trạng thái (idle/listening/speaking/…)
- `main/device_state_machine.cc|.h`
  - đảm bảo transition hợp lệ
  - phát sự kiện state-changed cho `Application::OnStateChanged()` để cập nhật UI/audio, start/stop timer

### 11.5 Board abstraction & driver layer
- `main/boards/common/board.cc|.h` — interface chung: Display, audio codec, buttons, mic/speaker, power/battery, SD mount…
- `main/boards/<board-name>/...` — cấu hình theo board: pinmap, driver cụ thể, loại màn hình, codec, led strip…
- Module dùng chung:
  - `sd_mount.cc|.h` — mount SD (`/sdcard`)
  - `wifi_board.cc|.h` + `blufi.h` — Wi-Fi + provisioning (tuỳ board)
  - `adc_battery_monitor.*`, PMIC (`axp2101.*`, `sy6970.*`) — nguồn/pin

### 11.6 Audio stack
- `main/audio/`:
  - `audio_service.*` — API phát sound/pipeline audio
  - `audio_codec.*` + `audio/codecs/*` — driver codec (ES8311/ES8388/…)
  - `audio_processor.*` — tiền xử lý (AEC/NS/AGC tuỳ cấu hình)
- Asset âm thanh:
  - `main/assets/common/*.ogg` + `main/assets/locales/<lang>/*.ogg`
  - `main/assets.cc|.h` — đóng gói/tra cứu asset theo ngôn ngữ

### 11.7 Display / LVGL UI
- `main/display/`:
  - `display.*` — wrapper LVGL, quản lý root screen, notification, theme
  - `lvgl_display/*` — driver & helpers (gif/jpg, font, canvas…)
- Widget tiện ích (`main/boards/common/`):
  - `weather_widget.*`, `lunar_widget.*`
- Notification:
  - `Application::Alert(...)` được Alarm/Protocol gọi để hiện thông báo

### 11.8 Protocol layer + MCP (tool calling)
- `main/protocols/`:
  - `protocol.h` — abstraction
  - `websocket_protocol.*`, `mqtt_protocol.*` — transport
- MCP server (on-device):
  - `main/mcp_server.cc|.h`
  - đăng ký tool theo capability:
    - system: volume/brightness/theme/camera/status
    - media: music/sdmusic/radio
    - utility: alarm

### 11.9 Storage: NVS + SD card
- `main/settings.cc|.h` — wrapper NVS (string/int/bool, erase…)
- SD card:
  - offline music + cache: `/sdcard/music`
  - index: `playlist.json`
- Alarm:
  - NVS: namespace `alarm`, key `list` (JSON array)

### 11.10 Tooling (scripts)
- `scripts/build_default_assets.py` — build assets mặc định
- `scripts/gen_lang.py` — sinh `language.json`/asset ngôn ngữ
- `scripts/mp3_to_ogg.sh`, `scripts/ogg_converter/*` — chuẩn hoá audio asset
- `scripts/Image_Converter/*` — convert image sang format LVGL
- `scripts/release.py`, `scripts/versions.py` — hỗ trợ phát hành

---

## 12) Checklist vận hành (nhanh, thực dụng)

### SD card
- [ ] SD mount OK
- [ ] Có nhạc trong SD
- [ ] Cho phép ghi để tạo:
  - `playlist.json`
  - thư mục cache `/sdcard/music`

### Online/Radio/Weather
- [ ] Wi-Fi ổn định
- [ ] Weather: đã set `weather_api_key` (khuyến nghị)
- [ ] City: để `"auto"` nếu muốn tự định vị theo IP

---

### Phạm vi tài liệu
Tài liệu này bám sát source hiện có và tập trung vào nhóm tính năng:  
**offline music / online music / radio / alarm / lunar / weather**.