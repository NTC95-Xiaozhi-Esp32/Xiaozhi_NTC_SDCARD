<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## 离线/在线媒体 • 网络电台 • 闹钟 • 越南农历 • 天气

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![简体中文](https://img.shields.io/badge/UI-%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-000000?style=for-the-badge)](./)

**开发者/维护者：** **Nguyễn Thọ Chung (NTC)**  
集成重点：SD 卡（离线/自动缓存）、网络电台、闹钟、农历、天气、MCP tools。

**语言：** [English](README.en.md) | **简体中文** | [日本語](README.ja.md) | [Tiếng Việt](README.md)

</div>

---

## 文档目标
本文档用于说明项目中已有的**媒体 + 实用功能**，强调“读完就会用，并且知道去代码里找对应文件”。

重点范围：
- 🎵 SD 卡离线音乐（索引、metadata/genre、通过 MCP tools 浏览/搜索）
- 🌐 在线音乐（优先 SD，其次在线播放 + 自动缓存）
- 📻 网络电台（AAC）
- ⏰ 闹钟（离线、NVS 持久化、snooze/dismiss）
- 🌙 越南农历（离线、万年历）
- 🌦️ 天气（OpenWeatherMap + IP 自动定位）
- 🖥️ 空闲叠层（Weather ↔ Lunar 轮换）
- 🧩 MCP tools 注册表（tool 名称/参数/行为）

---

## 1) 功能概览（快速摘要）

| 组别 | 功能 | 离线 | 在线 | 技术说明 |
|---|---|:--:|:--:|---|
| 🎵 SD Music | 从 SD 播放音乐 | ✅ | — | 优先读取 `playlist.json`；缺失/损坏才扫描 SD |
| 🎵 SD Music | 搜索 / 浏览目录 / 按 index 播放 | ✅ | — | 通过 MCP tools `self.sdmusic.*` 操作 |
| 🎵 SD Music | Shuffle / Repeat | ✅ | — | Repeat: none / one / all |
| 🎵 SD Music | Genre（ID3） | ✅ | — | ID3v1 genre 表 + 规范化 TCON（ID3v2） |
| 🌐 Online Music | 在线播放 | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | 自动缓存 MP3 到 SD | ✅* | ✅ | 听满约 ~15 秒后缓存到 `/sdcard/music` |
| 📻 Radio | 网络电台播放 | — | ✅ | AAC；内置台站列表 + 支持 URL |
| ⏰ 闹钟 | 定时 + Snooze/Dismiss + 铃声 | ✅ | — | NVS 持久化（`alarm/list`），tool `self.alarm.*` |
| 🌙 农历 | 阳历 ↔ 农历 + 万年历 | ✅ | — | 时辰干支、吉时、节气、节日 |
| 🌦️ 天气 | 当前 + 预报 | — | ✅ | OpenWeatherMap；IP 自动定位；位置缓存 |

\* 自动缓存需要 SD 卡已挂载且具备写权限。

---

## 2) 🎵 离线音乐（SD Card）

### 2.1 用户体验
- 当用户请求**播放歌曲**时，系统会：
  - **先在 SD 上查找**（速度快，不耗网络）
  - 找不到 → 再回退到在线播放（如果启用了在线子系统）

### 2.2 `playlist.json`（加速启动与库浏览）
- 在 SD 上生成索引文件，用于减少每次启动扫描库的时间：
  - 默认：`/sdcard/playlist.json`
  - （或在你设置的 root 目录下）
- 流程：
  1) 如果 `playlist.json` **有效** → 直接读取（快）
  2) 如果**缺失/损坏/为空** → 扫描 SD 重建，并写回 `playlist.json`

> 运维建议：当你在 SD 上新增/删除音乐后，可调用 `self.sdmusic.reload` 重新扫描库。

### 2.3 Metadata & Genre（ID3）
- 内置 **ID3v1 genre** 对照表
- 提供“安全”的最小化 ID3v2 读取（聚焦常用 frame）：
  - Title/Artist/Album/Year/Genre  
- 对 genre 做规范化，保证显示一致。

---

## 3) 🌐 在线音乐（Streaming + 自动缓存到 SD）

### 3.1 离线优先路由
- Tool `self.music.play_song` 会：
  - 先在 SD 按（歌名/歌手）查找 → 若存在：离线播放
  - 不存在 → 走在线播放器

### 3.2 自动缓存 MP3 到 SD（在线听，但可离线复听）
- 在线流式播放 MP3 时，系统可将文件保存到：
  - `/sdcard/music/`
- 缓存策略（按当前实现）：
  - 需要播放达到最短时长（通常 ~15 秒）才提交文件
  - 流早期失败时避免产生垃圾文件

### 3.3 音乐服务器配置
- 音乐服务器 URL / API 取决于你使用的 firmware/server。
- 缓存与离线/在线分流逻辑在 music 模块中（见 **File/Module** 小节）。

---

## 4) 📻 网络电台（AAC）

### 4.1 亮点
- 播放网络电台（AAC），支持台站列表
- 也支持直接按 URL 播放

### 4.2 支持按“台站名称”和按“URL”控制
- 按台站列表：
  - `self.radio.play_station`
- 按 URL：
  - `self.radio.play_url`
- 停止：
  - `self.radio.stop`

---

## 5) 🌙 越南农历（离线）

### 5.1 基本功能
- **阳历 ↔ 农历**转换（支持闰月 / leap month）
- 完整干支显示：
  - **年**（`CanChiYear`）：例如 Ất Tỵ
  - **月**（`CanChiMonth`）
  - **日**（`CanChiDay`）
  - **时**（`CanChiHour`）：按“Ngũ Thử Độn”

### 5.2 万年历与风水信息
- **黄道/黑道日**：按农历月与日支判断
- **吉时**：优选时段列表（Tý、Sửu、Thìn…）
- **节气**：按太阳黄经计算的 24 节气
- **事件与节日**：识别传统节日（Tết、Rằm、Ông Táo...）

### 5.3 智能界面（Lunar Widget）
- **昼/夜主题**：自动切换背景/文字（18:00–06:00 为暗色）
- **信息轮播**：干支（日/月/年/时/日支）约每 3 秒轮换
- **字体处理**：`NormalizeVietnameseForFont`，确保越南语在嵌入字体上稳定显示

### 5.4 时区配置
- 默认：`UTC+7`（越南）
- 编译期覆盖：`LUNAR_TZ_HOURS`

### 5.5 代码架构（按文件快速定位）
- 转换算法与万年历：
  - `main/boards/common/lunar_calendar.cc|.h`
  - 主要 API：`SolarToLunar()`, `LunarToSolar()`, `SolarTermName()`, `AuspiciousHours()`, `DayQualityByLunarMonth()`
- UI widget：
  - `main/boards/common/lunar_widget.cc|.h`
  - `LunarWidget::UpdateAll()` 是实时更新文本/颜色的核心
- 空闲显示协调：
  - `main/application.cc`（Weather ↔ Lunar 轮换）

### 5.6 阳历 ↔ 农历转换算法（按实现）
`lunar_calendar.cc` 的实现使用常见天文公式（Julian day、新月、太阳黄经）：
- `JulianDayFromDate()` / `DateFromJulianDay()`：阳历日期 ↔ JD
- `GetNewMoonDay(k, tz)`：新月日
- `GetSunLongitude(jd, tz)`：太阳黄经并量化为 12 宫
- `GetLunarMonth11(yy, tz)`：以农历 11 月作为年锚点
- `GetLeapMonthOffset(a11, tz)`：通过相邻朔望间太阳黄经“宫位”变化判断闰月

输出 `LunarDate`：`day`, `month`, `year`, `is_leap`。

### 5.7 Lunar Widget 的高级显示逻辑
- 干支轮播：mode 0..5，约每 3 秒切换：
  - 农历日标签（Mùng/Rằm）、日干支、月干支、年干支、时干支、日支
- 底部信息：
  - 优先轮播**吉时**；若无 → 显示**节气**
  - 若匹配农历节日（`LunarFestivalName`）则高亮节日

---

## 6) ⏰ 闹钟（Alarm Manager v2）

### 6.1 总览与体验
- 设备获得**有效系统时间**后，闹钟可**离线**运行
- 到点后：
  - UI 弹出通知：**“Báo thức – Đã đến giờ!”**
  - 按铃声（OGG）循环播放

### 6.2 数据模型与重复规则（`days_mask`）
每个闹钟是一个 `Alarm` struct（关键字段）：
- `id`（string）：稳定 ID，用于 MCP CRUD
- `hour`, `minute`：24 小时制
- `days_mask`（0..127）：按星期重复的 bitmask
  - Bit0=周日，Bit1=周一，… Bit6=周六
  - `0`：一次性（响完自动删除）
  - `127`：每天
  - 周一–周五 = `62`（0b0111110）
- `enabled`：开关
- `label`：标签
- `ringtone`：`"ga" | "alarm1" | "iphone"`（有 fallback）
- `snooze_minutes`：1..120（默认 10）
- `ring_interval_sec`：1..60
- `max_rings`：1..60

### 6.3 调度器：分钟对齐 + 防重复触发
- 两个 timer：
  - `check_timer`：对齐到**下一分钟 + 1 秒**再检查
  - `ring_timer`：响铃期间按周期播放
- 防重复触发：
  - 运行时 guard：`last_fired_epoch_min`（不持久化）→ 避免同一分钟重复触发

> 代码中“有效时间”判断：`time(nullptr) >= 2023-01-01 00:00:00 UTC`（`kMinValidEpoch`）。  
> 若 SNTP/RTC 未同步，scheduler 会等待且不触发。

### 6.4 Ringing / Dismiss / Snooze
- 响铃：
  - `AudioService::PlaySound()` 按 `ring_interval_sec` 周期播放
  - 铃声 fallback：selected → `alarm1` → `ga`
- Dismiss：
  - 停止响铃、取消 snooze、`display->ClearNotification()`
- Snooze：
  - 创建独立的运行时 “snooze alarm”（一次性闹钟也可 snooze）
  - 到 `snooze_until_epoch_sec_` 后再次触发

### 6.5 持久化存储（NVS/Settings）
- 通过 `Settings` 写入 NVS：
  - Namespace：`"alarm"`
  - Key：`"list"`
  - 格式：JSON array
- 向后兼容：
  - 旧存储 `repeat_daily=true` → 映射到 `days_mask=127`

### 6.6 闹钟相关 MCP tools（`self.alarm.*`）
- `self.alarm.set` — 新建/更新闹钟  
  Args：`id?`, `hour`, `minute`, `days_mask`, `repeat_daily?`, `label?`, `ringtone?`, `snooze_minutes?`, `ring_interval_sec?`, `max_rings?`  
  Return：`{success,id,message}`
- `self.alarm.list` — 闹钟列表 + 运行时状态（响铃/snooze）
- `self.alarm.delete` — 按 `id` 删除
- `self.alarm.enable` — 按 `id` 开关
- `self.alarm.clear` — 清空全部
- `self.alarm.stop` — dismiss + 取消 snooze
- `self.alarm.snooze` — snooze 当前响铃闹钟（`minutes=0` → 使用默认）

### 6.7 相关文件/模块
- Core：`main/boards/common/alarm_manager.cc|.h`
- Tool registry：`main/mcp_server.cc`
- Sound assets：`main/assets/common/alarm1.ogg`, `main/assets/common/ga.ogg`, `main/assets/common/iphone.ogg`

---

## 7) 🌦️ 天气（OpenWeatherMap + IP 自动定位）

### 7.1 显示数据
- 当前天气
- 5 天预报
- 描述语言：越南语（`OPENWEATHER_LANG = "vi"`）

### 7.2 基于 IP 的自动定位（未配置 city 时）
定位接口优先级：
1) `ipwho.is`
2) `ipwhois.app`
3) `ipapi.co`

IP 位置缓存：
- TTL：**6 小时**

天气更新周期：
- 后台 scheduler（不阻塞 UI）
- 后台 SNTP 同步，保证系统时间准确（服务闹钟 + 日历）

### 7.3 API key 与 City 配置（NVS）
- Namespace `wifi`：
  - `weather_api_key`
  - `weather_city`
- Fallback namespace `weather`：
  - `api_key`
  - `city`

约定：
- `city` 为空或 `"auto"` → 启用 IP 自动定位

> 生产建议：配置独立 API key 以便管理 quota 与追踪调用。

---

## 8) 🖥️ 空闲叠层：Weather ↔ Lunar（优化空闲体验）
设备进入 Idle 状态时：
- 若**weather 已就绪** → 轮换显示：
  - Weather widget ↔ Lunar widget
- 若**无网络 / weather 未就绪** → 保持 Lunar widget（fallback）

轮换频率：
- 每 **180 秒（3 分钟）**一次

---

## 9) MCP Tools：“意图” ↔ “tool”（控制映射）
以下为设备端 MCP server 注册的工具映射。

### 9.1 System / UI / Audio / Camera
- `self.get_device_status` — 设备状态（network/audio/state/…）
- `self.audio_speaker.set_volume` — 设置音量
- `self.screen.set_brightness` — 设置亮度
- `self.screen.set_theme` — UI 主题
- `self.camera.take_photo` — 拍照（取决于 board）

### 9.2 播放音乐（自动优先 SD）
- `self.music.play_song`
  - Args：
    - `song_name`（必填）
    - `artist_name`（可选）
  - 行为：
    - SD 有 → 离线播放
    - SD 无 → 在线播放（并可能自动缓存）

### 9.3 SD Music（仅当用户明确要操作 SD 库时使用）
- Playback：`self.sdmusic.playback`（`action=play|pause|stop|next|prev`）
- Mode：`self.sdmusic.mode`（shuffle / repeat）
- Track ops：`self.sdmusic.track`（set/info/list/current）
- Directory ops：`self.sdmusic.directory`
- Search：`self.sdmusic.search`
- Library summary：`self.sdmusic.library`
- Reload index：`self.sdmusic.reload`
- Suggest：`self.sdmusic.suggest`
- Progress：`self.sdmusic.progress`
- Genre：`self.sdmusic.genre` + `self.sdmusic.genre_list`

### 9.4 Radio
- `self.radio.play_station`
- `self.radio.play_url`
- `self.radio.stop`
- `self.radio.get_stations`
- `self.radio.set_display_mode`

### 9.5 闹钟
- `self.alarm.set`
- `self.alarm.list`
- `self.alarm.delete`
- `self.alarm.enable`
- `self.alarm.clear`
- `self.alarm.stop`
- `self.alarm.snooze`

---

## 10) 相关文件/模块（方便快速定位代码）

<details>
<summary><b>📁 核心文件列表</b></summary>

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

### 🌙 农历
- `main/boards/common/lunar_calendar.cc`
- `main/boards/common/lunar_calendar.h`
- `main/boards/common/lunar_widget.cc`
- `main/boards/common/lunar_widget.cc`
- `main/boards/common/lunar_widget.h`

### ⏰ 闹钟
- `main/boards/common/alarm_manager.cc`
- `main/boards/common/alarm_manager.h`

### 🌦️ 天气
- `main/boards/common/weather_service.cc`
- `main/boards/common/weather_service.h`
- `main/boards/common/weather_widget.cc`
- `main/boards/common/weather_widget.h`

### 🖥️ 空闲叠层轮换
- `main/application.cc`

### 🔧 MCP Tools registry
- `main/mcp_server.cc`

</details>

---

## 11) 全项目源码分析（按架构）

本节按源码结构梳理，帮助你“有方向地读代码”（ESP-IDF / C++）。

### 11.1 顶层目录结构
- `main/` — 固件逻辑（core app、protocol、audio、display、boards…）
- `docs/` — 协议文档（MCP）、websocket/mqtt-udp、custom board 指南…
- `scripts/` — 资产构建工具、音频/图像转换、发布脚本
- `partitions/` — 分区表（v1/v2），取决于 flash size
- `sdkconfig.defaults*` — 各芯片默认配置（esp32/esp32s3/esp32c3…）
- `.github/` — CI/CD workflow

### 11.2 启动流程（Entry → Main loop）
1) `main/main.cc`
   - 初始化 NVS（`nvs_flash_init`）用于 Wi‑Fi/settings
   - 调用 `Application::GetInstance().Initialize()` 然后 `Run()`（不返回）
2) `main/application.cc|.h`
   - Setup display/audio/board callbacks
   - 初始化 protocol（按 build 选择 Websocket/MQTT）
   - 跑 event loop：network、state machine、UI/audio、MCP messages

### 11.3 核心编排：Application + 事件模型
- `Application` 维护系统组件：
  - `DeviceStateMachine state_machine_`
  - `AudioService audio_service_`
  - `Protocol`（连接 server、接收 MCP/toolcall）
  - `WeatherService`（按条件启用）、空闲叠层 timer（Weather↔Lunar）
- 编排模式：
  - ISR/辅助 task 的回调把 event/closure 推入 `main_tasks_`
  - `Run()` 顺序拉取处理，避免 race

### 11.4 状态机（设备正在做什么）
- `main/device_state.h` — 状态 enum（idle/listening/speaking/…）
- `main/device_state_machine.cc|.h`
  - 确保合法 transition
  - 触发 state-changed event 给 `Application::OnStateChanged()` 更新 UI/audio、启停 timer

### 11.5 Board 抽象与驱动层
- `main/boards/common/board.cc|.h` — 通用接口：Display、audio codec、buttons、mic/speaker、power/battery、SD mount…
- `main/boards/<board-name>/...` — board-specific：pinmap、驱动、屏幕类型、codec、led strip…
- 通用模块：
  - `sd_mount.cc|.h` — mount SD（`/sdcard`）
  - `wifi_board.cc|.h` + `blufi.h` — Wi‑Fi + provisioning（取决于 board）
  - `adc_battery_monitor.*`、PMIC（`axp2101.*`, `sy6970.*`）— 电源/电池

### 11.6 音频栈
- `main/audio/`：
  - `audio_service.*` — 播放 API / pipeline
  - `audio_codec.*` + `audio/codecs/*` — codec driver（ES8311/ES8388/…）
  - `audio_processor.*` — 预处理（AEC/NS/AGC 按配置）
- 音频资产：
  - `main/assets/common/*.ogg` + `main/assets/locales/<lang>/*.ogg`
  - `main/assets.cc|.h` — 按语言打包/查找资产

### 11.7 显示 / LVGL UI
- `main/display/`：
  - `display.*` — LVGL 封装：root screen、notification、theme
  - `lvgl_display/*` — driver & helpers（gif/jpg、font、canvas…）
- 实用 widget（`main/boards/common/`）：
  - `weather_widget.*`, `lunar_widget.*`
- Notification：
  - `Application::Alert(...)` 由 Alarm/Protocol 调用以弹出通知

### 11.8 Protocol 层 + MCP（tool calling）
- `main/protocols/`：
  - `protocol.h` — abstraction
  - `websocket_protocol.*`, `mqtt_protocol.*` — transport
- 设备端 MCP server：
  - `main/mcp_server.cc|.h`
  - 按 capability 注册 tool：
    - system：volume/brightness/theme/camera/status
    - media：music/sdmusic/radio
    - utility：alarm

### 11.9 存储：NVS + SD card
- `main/settings.cc|.h` — NVS wrapper（string/int/bool、erase…）
- SD card：
  - 离线音乐 + cache：`/sdcard/music`
  - 索引：`playlist.json`
- Alarm：
  - NVS：namespace `alarm`，key `list`（JSON array）

### 11.10 工具脚本（scripts）
- `scripts/build_default_assets.py` — 构建默认资产
- `scripts/gen_lang.py` — 生成 `language.json` / 语言资产
- `scripts/mp3_to_ogg.sh`, `scripts/ogg_converter/*` — 规范化 audio asset
- `scripts/Image_Converter/*` — 将图片转换为 LVGL 格式
- `scripts/release.py`, `scripts/versions.py` — 发布辅助

---

## 12) 运行检查清单（实用）

### SD card
- [ ] SD 挂载正常
- [ ] SD 内有音乐
- [ ] 开启写权限以生成：
  - `playlist.json`
  - 缓存目录 `/sdcard/music`

### Online/Radio/Weather
- [ ] Wi‑Fi 稳定
- [ ] Weather：已设置 `weather_api_key`（推荐）
- [ ] City：若需 IP 自动定位，则设为 `"auto"`

---

### 文档范围
本文档紧贴当前源码，聚焦功能：  
**offline music / online music / radio / alarm / lunar / weather**。
