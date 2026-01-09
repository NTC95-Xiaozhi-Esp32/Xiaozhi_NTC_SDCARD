<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## 离线/在线媒体 • 网络电台 • 越南阴历 • 天气

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![Vietnamese UI](https://img.shields.io/badge/UI-Ti%E1%BA%BFng%20Vi%E1%BB%87t-000000?style=for-the-badge)](./)

</div>

---

## 文档目标
本文档描述当前源码中已具备的 **媒体功能 + 实用工具**：

- 🎵 **SD离线听歌**（playlist.json、搜索、随机/循环、流派）
- 🌐 **在线听歌**（流媒体 + 自动把MP3缓存到SD）
- 📻 **网络电台**（AAC）
- 🌙 **越南阴历**（离线、Can–Chi、黄道/黑道、节气）
- 🌦️ **天气**（OpenWeatherMap + 基于IP自动定位）
- 🖥️ **Idle overlay**：自动轮换 *天气 ↔ 阴历*（离线时也有合理回退）

> 提示：如果你需要项目整体的 build/flash README，请查看项目原始README。本文件只聚焦上述5大功能组。

---

## 1) 功能表（快速摘要）

| 组别 | 功能 | 离线 | 在线 | 技术说明 |
|---|---|:--:|:--:|---|
| 🎵 SD Music | 从SD播放音乐 | ✅ | — | 优先读取 `playlist.json`；损坏/缺失时才扫描SD |
| 🎵 SD Music | 搜索 / 浏览目录 / 按index播放 | ✅ | — | 支持通过 MCP tools `self.sdmusic.*` 操作 |
| 🎵 SD Music | 随机 / 循环 | ✅ | — | 循环：none / one / all |
| 🎵 SD Music | 流派（ID3） | ✅ | — | 内置 ID3v1 流派表 + TCON 规范化（ID3v2） |
| 🌐 Online Music | 在线播放 | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | 自动缓存MP3到SD | ✅* | ✅ | 在线播放后，至少听满15秒才缓存到 `/sdcard/music` |
| 📻 Radio | 播放网络电台 | — | ✅ | 仅AAC格式；内置越南电台列表 + 支持URL播放 |
| 🌙 阴历 | 阳历 ↔ 阴历 + 万年历 | ✅ | — | 时辰干支、黄道/黑道、节气、节日 |
| 🌦️ 天气 | 当前 + 5天预报 | — | ✅ | OWM；IP自动定位（ipwho.is → ipwhois.app → ipapi.co） |
| 🖥️ Idle overlay | 轮换显示天气/阴历 | ✅ | ✅ | 每1分钟一次；无天气则保持阴历；背景按昼夜状态自动变色 |

\* 自动缓存只写入SD，但数据来源仍为在线。

---

## 2) 🎵 离线听歌（SD Card）

### 2.1 用户体验
- 直接从SD播放，适用于：
  - 没有网络
  - 想要播放固定的“本地歌库”，低延迟
- 支持：
  - 浏览目录 / 列出歌曲
  - 关键词搜索
  - 按index播放
  - 随机 / 循环
  - 按 **流派（genre）** 播放列表

### 2.2 playlist.json（加速启动 & 浏览库）
针对嵌入式设备优化了playlist读取机制：

- 默认路径：  
  `/<SD_MOUNT>/playlist.json`  
  （如果你设置了不同root，也可能按目录变化）
- 处理流程：
  1) `playlist.json` **内容有效** → 仅读取该文件（更快）
  2) **缺失/损坏/为空** → 扫描SD重建并保存 `playlist.json`

> 运维提示：当你在SD上新增/删除音乐后，可以调用 `self.sdmusic.reload` 重新扫描音乐库。

### 2.3 元数据 & 流派（ID3）
- 内置 **ID3v1** 流派对照表
- 以“安全模式”（最小化）读取 **ID3v2**，聚焦常用frame：
  - Title/Artist/Album/Year/Genre
- 对流派做规范化处理，显示更统一

---

## 3) 🌐 在线听歌（Streaming + 自动缓存到SD）

### 3.1 优先离线的调度逻辑
Tool **`self.music.play_song`** 的逻辑：

1) SD里存在同名track（SD库已加载）→ **离线播放**
2) 否则 → **在线播放**（download/stream）

### 3.2 自动缓存MP3到SD（在线听，也能离线回放）
在线播放时固件会自动缓存：

- 缓存目录：`/sdcard/music`
- 播放满 **15秒** 才开始缓存
- 缓存上限：**100首**
  - 超过上限会按 `st_mtime` 自动删除 **最旧文件**

> 注意：这是偏“实用”的缓存策略，用于提升体验。如需按容量、严格LRU、按标签/专辑等策略，请在 `esp32_music.cc` 中调整常量和淘汰逻辑。

### 3.3 音乐服务器配置
默认音乐服务器URL定义在：

- `main/boards/common/esp32_music.cc`  
  `#define DEFAULT_MUSIC_URL "http://...:5005"`

修改该宏并重新编译固件即可切换服务器。

---

## 4) 📻 网络电台（AAC）

### 4.1 亮点
- 使用 **AAC** 解码器播放网络电台
- 内置电台列表（优先越南内容），例如：
  - JoyFM 98.9
  - VOV1 / VOV2 / VOV3 / VOV5
  - VOV 交通（HN/HCM）
  - VOV 各地区（西北 / 东北 / 西原 / 湄公河 / …）

### 4.2 支持按“电台名”和按“URL”控制
- 按电台名播放：`self.radio.play_station`
- 按URL播放：`self.radio.play_url`
- 停止：`self.radio.stop`
- 获取电台列表：`self.radio.get_stations`
- 切换显示模式：`self.radio.set_display_mode`

> 技术说明：初始化日志明确提示当前电台列表按 **AAC format only** 设计。

---

## 5) 🌙 越南阴历（离线）

### 5.1 基本功能
- **阳历 ↔ 阴历** 转换（支持闰月/leap month）
- 完整显示 Can–Chi 系统：
  - **年**（`CanChiYear`）：例：Ất Tỵ
  - **月**（`CanChiMonth`）：例：Mậu Dần
  - **日**（`CanChiDay`）：例：Tân Tỵ
  - **时**（`CanChiHour`）：按 “Ngũ Thử Độn” 方法计算（例：Giờ Đinh Dậu）

### 5.2 万年历 & 风水
- **黄道日 / 黑道日**：根据阴历月与日支判断吉凶
- **黄道时辰**：计算当天吉时列表（Tý, Sửu, Thìn...）
- **节气**：按太阳黄经计算24节气（Lập Xuân, Thanh Minh, Đông Chí...）
- **事件 & 节日**：识别传统节日（Tết, Rằm, Ông Táo...）

### 5.3 智能界面（Lunar Widget）
- **昼/夜主题**：18点到次日6点自动切换背景/文字颜色
- **信息轮播**：Can Chi 信息（日/月/年/时）每3秒自动轮换
- **字体处理**：通过 `NormalizeVietnameseForFont` 让内嵌字体也能更顺滑显示越南语

### 5.4 时区配置
- 默认：`UTC+7`（越南）
- 编译期覆盖：`LUNAR_TZ_HOURS`

---

## 6) 🌦️ 天气（OpenWeatherMap + IP自动定位）

### 6.1 显示数据
- 当前天气（current）
- 5天预报（forecast）
- 描述语言：**越南语**（`OPENWEATHER_LANG = "vi"`）

### 6.2 基于IP自动定位（未配置city时）
地理定位服务优先级：

1) `ipwho.is`  
2) `ipwhois.app`  
3) `ipapi.co`

IP位置缓存：
- TTL：**6小时**

天气更新周期：
- **每10分钟**

### 6.3 配置API key & City（NVS）
WeatherService 按顺序从NVS读取配置：

- Namespace `wifi`：
  - `weather_api_key`
  - `weather_city`
- 回退 namespace `weather`：
  - `api_key`
  - `city`

约定：
- `city` 为空或为 `"auto"` → 启用基于IP自动定位

> 说明：固件可能内置默认API key用于测试。生产环境建议设置自己的key，以便管理quota并便于追踪。

---

## 7) 🖥️ Idle overlay：天气 ↔ 阴历（空闲时优化体验）
当设备进入Idle状态时，UI将：

- 若 **天气就绪** → 轮换显示：
  - Weather widget ↔ Lunar widget
- 若 **无网络/天气不可用** → 保持 Lunar widget（标准回退）

轮换频率：
- **180秒（3分钟）** 一次

---

## 8) MCP Tools：“意图” ↔ “tool”（用于控制）
以下为在MCP server中注册的工具映射：

### 8.1 听歌（自动优先SD）
- `self.music.play_song`
  - Args:
    - `song_name`（必填）
    - `artist_name`（可选）
  - 行为：
    - SD里有歌 → 离线播放
    - 没有 → 在线播放

### 8.2 SD Music（当用户希望直接操作SD库时使用）
- 基础播放：`self.sdmusic.playback`（`action = play|pause|stop|next|prev`）
- 播放模式：`self.sdmusic.mode`（shuffle / repeat）
- Track：`self.sdmusic.track`（按index播放、获取track信息等）
- 目录：`self.sdmusic.directory`
- 搜索：`self.sdmusic.search`
- 库：`self.sdmusic.library`
- 重新扫描：`self.sdmusic.reload`
- 推荐：`self.sdmusic.suggest`
- 进度：`self.sdmusic.progress`
- 流派：`self.sdmusic.genre` 与 `self.sdmusic.genre_list`

### 8.3 Radio
- `self.radio.play_station`
- `self.radio.play_url`
- `self.radio.stop`
- `self.radio.get_stations`
- `self.radio.set_display_mode`

---

## 9) 相关文件/模块（便于在代码中定位）

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

### 🌙 阴历
- `main/boards/common/lunar_calendar.cc`
- `main/boards/common/lunar_calendar.h`
- `main/boards/common/lunar_widget.cc`
- `main/boards/common/lunar_widget.h`

### 🌦️ 天气
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

## 10) 运行检查清单（快速、实用）

### SD card
- [ ] SD挂载正常
- [ ] SD内有音乐
- [ ] 允许写入以生成：
  - `playlist.json`
  - 缓存目录 `/sdcard/music`

### Online/Radio/Weather
- [ ] Wi‑Fi稳定
- [ ] Weather：已设置 `weather_api_key`（推荐）
- [ ] City：若需IP自动定位，请设为 `"auto"`

---

### 文档范围
本文档基于当前源码，聚焦功能：**offline music / online music / radio / lunar / weather**。
