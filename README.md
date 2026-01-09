<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## Media Offline/Online • Internet Radio • Lịch âm • Thời tiết

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![Vietnamese UI](https://img.shields.io/badge/UI-Ti%E1%BA%BFng%20Vi%E1%BB%87t-000000?style=for-the-badge)](./)

</div>

---

## Mục tiêu tài liệu
Tài liệu này mô tả **các tính năng media + tiện ích** đã có trong source hiện tại:

- 🎵 **Nghe nhạc Offline** từ SD (playlist.json, tìm kiếm, shuffle/repeat, genre)
- 🌐 **Nghe nhạc Online** (stream + tự cache MP3 vào SD)
- 📻 **Internet Radio** (AAC)
- 🌙 **Lịch âm Việt Nam** (offline, Can–Chi, Hoàng Đạo, Tiết khí)
- 🌦️ **Thời tiết** (OpenWeatherMap + auto-location theo IP)
- 🖥️ **Idle overlay**: tự luân phiên *Thời tiết ↔ Lịch âm* (fallback hợp lý khi offline)

> Gợi ý: Nếu bạn muốn README tổng quan build/flash, hãy xem README gốc của dự án. File này chỉ tập trung vào 5 nhóm tính năng trên.

---

## 1) Bảng tính năng (tóm tắt nhanh)

| Nhóm | Tính năng | Offline | Online | Ghi chú kỹ thuật |
|---|---|:--:|:--:|---|
| 🎵 SD Music | Phát nhạc từ SD | ✅ | — | Ưu tiên đọc `playlist.json`, hỏng/thiếu mới quét SD |
| 🎵 SD Music | Tìm kiếm / duyệt thư mục / phát theo index | ✅ | — | Hỗ trợ thao tác qua MCP tools `self.sdmusic.*` |
| 🎵 SD Music | Shuffle / Repeat | ✅ | — | Repeat: none / one / all |
| 🎵 SD Music | Genre (ID3) | ✅ | — | Có bảng genre ID3v1 + chuẩn hoá TCON (ID3v2) |
| 🌐 Online Music | Phát online | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | Auto-cache MP3 vào SD | ✅* | ✅ | Cache vào `/sdcard/music` sau khi nghe tối thiểu 15 giây |
| 📻 Radio | Phát radio Internet | — | ✅ | AAC format only; có danh sách kênh VN dựng sẵn + phát theo URL |
| 🌙 Lịch âm | Solar ↔ Lunar + Vạn niên | ✅ | — | Can Chi giờ, Hoàng đạo, Tiết khí, Lễ hội |
| 🌦️ Thời tiết | Hiện tại + dự báo 5 ngày | — | ✅ | OWM; auto-location theo IP (ipwho.is → ipwhois.app → ipapi.co) |
| 🖥️ Idle overlay | Luân phiên Weather/Lunar | ✅ | ✅ | 1 phút/lần; không có weather → giữ Lunar | Nền tự động đổi màu theo trạng thái ngày và đêm

\* Auto-cache chỉ ghi vào SD, nhưng nguồn dữ liệu là online.

---

## 2) 🎵 Nghe nhạc OFFLINE (SD Card)

### 2.1 Trải nghiệm người dùng
- Phát nhạc trực tiếp từ SD, phù hợp khi:
  - Không có mạng
  - Muốn phát “tủ nhạc” cố định, độ trễ thấp
- Hỗ trợ:
  - Duyệt thư mục / liệt kê bài
  - Tìm theo từ khoá
  - Phát theo index
  - Shuffle / Repeat
  - Playlist theo **thể loại (genre)**

### 2.2 playlist.json (tăng tốc khởi động & duyệt thư viện)
Cơ chế đọc playlist được tối ưu cho thiết bị embedded:

- Mặc định dùng:  
  `/<SD_MOUNT>/playlist.json`  
  (hoặc theo thư mục khi bạn đặt root khác)
- Luồng xử lý:
  1) Nếu `playlist.json` **có nội dung hợp lệ** → chỉ đọc file (nhanh)
  2) Nếu **thiếu/hỏng/rỗng** → quét SD để rebuild và lưu lại `playlist.json`

> Gợi ý vận hành: khi bạn copy thêm/xoá nhạc trên SD, có thể gọi tool `self.sdmusic.reload` để quét lại thư viện.

### 2.3 Metadata & Genre (ID3)
- Có bảng tra **genre ID3v1**
- Có cơ chế đọc ID3v2 ở chế độ “an toàn” (tối giản), tập trung các frame thông dụng như:
  - Title/Artist/Album/Year/Genre  
- Genre được chuẩn hoá để hiển thị gọn và thống nhất.

---

## 3) 🌐 Nghe nhạc ONLINE (Streaming + tự cache vào SD)

### 3.1 Điều phối ưu tiên OFFLINE trước
Tool **`self.music.play_song`** có logic:

1) Nếu SD có track trùng tên (thư viện SD đã load) → phát **OFFLINE**
2) Nếu không có → phát **ONLINE** (download/stream)

### 3.2 Auto-cache MP3 vào SD (nghe online nhưng lưu để nghe lại)
Khi phát online, firmware có cơ chế tự cache:

- Thư mục cache: ` /sdcard/music `
- Chỉ bắt đầu cache khi thời gian phát đạt tối thiểu: **15 giây**
- Giới hạn số bài trong thư mục cache: **100 bài**
  - Nếu vượt giới hạn → tự xoá **file cũ nhất** (dựa theo `st_mtime`) để giải phóng chỗ

> Lưu ý: Đây là cache “thực dụng” để tối ưu trải nghiệm. Nếu bạn cần chính sách cache khác (theo dung lượng, theo LRU thực sự, theo tag/album…), hãy chỉnh hằng số và hàm eviction trong `esp32_music.cc`.

### 3.3 Cấu hình server nhạc
URL server nhạc mặc định được khai báo tại:

- `main/boards/common/esp32_music.cc`  
  `#define DEFAULT_MUSIC_URL "http://...:5005"`

Bạn có thể đổi server bằng cách sửa macro và build lại firmware.

---

## 4) 📻 Internet Radio (AAC)

### 4.1 Điểm nổi bật
- Phát radio Internet với decoder **AAC**
- Có danh sách kênh dựng sẵn (ưu tiên nội dung VN), ví dụ:
  - JoyFM 98.9
  - VOV1 / VOV2 / VOV3 / VOV5
  - VOV Giao thông (HN/HCM)
  - VOV vùng miền (Tây Bắc / Đông Bắc / Tây Nguyên / Mekong / …)

### 4.2 Hỗ trợ điều khiển theo “tên kênh” và theo “URL”
- Phát theo tên station: `self.radio.play_station`
- Phát theo URL: `self.radio.play_url`
- Dừng: `self.radio.stop`
- Lấy danh sách kênh: `self.radio.get_stations`
- Đổi chế độ hiển thị: `self.radio.set_display_mode`

> Ghi chú kỹ thuật: log khởi tạo nêu rõ radio list hiện thiết kế theo hướng **AAC format only**.

---

## 5) 🌙 Lịch âm Việt Nam (Offline)

### 5.1 Chức năng cơ bản
- Chuyển đổi **Dương lịch ↔ Âm lịch** (hỗ trợ tính tháng nhuận/leap month)
- Hiển thị hệ thống Can–Chi đầy đủ:
  - **Năm** (`CanChiYear`): Ví dụ: Ất Tỵ
  - **Tháng** (`CanChiMonth`): Ví dụ: Mậu Dần
  - **Ngày** (`CanChiDay`): Ví dụ: Tân Tỵ
  - **Giờ** (`CanChiHour`): Tính theo phương pháp "Ngũ Thử Độn" (Ví dụ: Giờ Đinh Dậu)

### 5.2 Lịch Vạn Niên & Phong Thủy
- **Ngày Hoàng Đạo / Hắc Đạo**: Xác định ngày tốt/xấu dựa trên tháng âm và Chi của ngày.
- **Giờ Hoàng Đạo**: Tính danh sách các khung giờ tốt trong ngày (Tý, Sửu, Thìn...).
- **Tiết Khí**: Tính 24 tiết khí dựa trên kinh độ Mặt Trời (Lập Xuân, Thanh Minh, Đông Chí...).
- **Sự kiện & Ngày lễ**: Nhận diện ngày lễ cổ truyền (Tết, Rằm, Ông Táo...).

### 5.3 Giao diện thông minh (Lunar Widget)
- **Day/Night Theme**: Tự động chuyển màu nền/chữ tối từ 18h đến 6h sáng.
- **Carousel Info**: Thông tin Can Chi (Ngày/Tháng/Năm/Giờ) tự động xoay vòng mỗi 3 giây.
- **Font xử lý**: Có cơ chế `NormalizeVietnameseForFont` để hiển thị tiếng Việt mượt mà trên font nhúng.

### 5.4 Cấu hình Timezone
- Mặc định: `UTC+7` (Việt Nam)
- Override compile-time: `LUNAR_TZ_HOURS`

---

## 6) 🌦️ Thời tiết (OpenWeatherMap + auto-location IP)

### 6.1 Dữ liệu hiển thị
- Thời tiết hiện tại (current)
- Dự báo 5 ngày (forecast)
- Ngôn ngữ mô tả: **tiếng Việt** (`OPENWEATHER_LANG = "vi"`)

### 6.2 Auto-location theo IP (khi không cấu hình city)
Chuỗi ưu tiên geolocation:

1) `ipwho.is`  
2) `ipwhois.app`  
3) `ipapi.co`

Cache vị trí IP:
- TTL: **6 giờ**

Chu kỳ cập nhật thời tiết:
- **10 phút/lần**

### 6.3 Cấu hình API key & City (NVS)
WeatherService ưu tiên đọc cấu hình từ NVS theo thứ tự:

- Namespace `wifi`:
  - `weather_api_key`
  - `weather_city`
- Fallback namespace `weather`:
  - `api_key`
  - `city`

Quy ước:
- `city` rỗng hoặc `"auto"` → bật auto-location theo IP

> Ghi chú: Firmware có thể chứa API key mặc định để chạy thử. Trong môi trường production, nên set key riêng để quản trị quota và truy vết.

---

## 7) 🖥️ Idle overlay: Weather ↔ Lunar (tối ưu trải nghiệm khi rảnh)
Khi thiết bị vào trạng thái Idle, UI sẽ:

- Nếu **weather sẵn sàng** → luân phiên hiển thị:
  - Weather widget ↔ Lunar widget
- Nếu **không có mạng / weather không sẵn sàng** → giữ Lunar widget (fallback chuẩn)

Tần suất luân phiên:
- **180 giây (3 phút)** một lần

---

## 8) MCP Tools: “ý định” ↔ “tool” (dành cho điều khiển)
Dưới đây là mapping công cụ đã đăng ký trong MCP server:

### 8.1 Nghe nhạc (tự ưu tiên SD trước)
- `self.music.play_song`
  - Args:
    - `song_name` (bắt buộc)
    - `artist_name` (tuỳ chọn)
  - Hành vi:
    - Có bài trên SD → phát OFFLINE
    - Không có → phát ONLINE

### 8.2 SD Music (chỉ dùng khi người dùng muốn thao tác trực tiếp thư viện SD)
- Playback cơ bản: `self.sdmusic.playback` (`action = play|pause|stop|next|prev`)
- Chế độ phát: `self.sdmusic.mode` (shuffle / repeat)
- Track: `self.sdmusic.track` (phát theo index, lấy thông tin track, v.v.)
- Thư mục: `self.sdmusic.directory`
- Tìm kiếm: `self.sdmusic.search`
- Thư viện: `self.sdmusic.library`
- Quét lại: `self.sdmusic.reload`
- Gợi ý: `self.sdmusic.suggest`
- Tiến độ: `self.sdmusic.progress`
- Genre: `self.sdmusic.genre` và `self.sdmusic.genre_list`

### 8.3 Radio
- `self.radio.play_station`
- `self.radio.play_url`
- `self.radio.stop`
- `self.radio.get_stations`
- `self.radio.set_display_mode`

---

## 9) File/Module liên quan (để bạn tìm đúng chỗ trong code)

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

## 10) Checklist vận hành (nhanh, thực dụng)

### SD card
- [ ] SD mount OK
- [ ] Có nhạc trong SD
- [ ] Cho phép ghi để tạo:
  - `playlist.json`
  - thư mục cache `/sdcard/music`

### Online/Radio/Weather
- [ ] Wi‑Fi ổn định
- [ ] Weather: đã set `weather_api_key` (khuyến nghị)
- [ ] City: để `"auto"` nếu muốn tự định vị theo IP

---

### Phạm vi tài liệu
Tài liệu này bám sát source hiện có và tập trung vào nhóm tính năng: **offline music / online music / radio / lunar / weather**.