<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## オフライン/オンラインメディア • インターネットラジオ • アラーム • ベトナム旧暦 • 天気

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![日本語](https://img.shields.io/badge/UI-%E6%97%A5%E6%9C%AC%E8%AA%9E-000000?style=for-the-badge)](./)

**開発者/メンテナー:** **Nguyễn Thọ Chung (NTC)**  
統合の主眼：SD カード（オフライン/自動キャッシュ）、インターネットラジオ、アラーム、旧暦、天気、MCP tools。

**言語:** [English](README.en.md) | [简体中文](README.zh-CN.md) | **日本語** | [Tiếng Việt](README.md)

</div>

---

## ドキュメントの目的
本ドキュメントは、プロジェクトに実装済みの **メディア + ユーティリティ機能** を説明します。狙いは「読めば使い方が分かり、かつコード内の該当ファイルをすぐ特定できる」ことです。

対象範囲：
- 🎵 SD カードのオフライン音楽（インデックス、metadata/genre、MCP tools による閲覧/検索）
- 🌐 オンライン音楽（SD 優先、ストリーミング + 自動キャッシュ）
- 📻 インターネットラジオ（AAC）
- ⏰ アラーム（オフライン、NVS 永続化、snooze/dismiss）
- 🌙 ベトナム旧暦（オフライン、万年暦）
- 🌦️ 天気（OpenWeatherMap + IP 自動ロケーション）
- 🖥️ Idle オーバーレイ（Weather ↔ Lunar の切替）
- 🧩 MCP tools レジストリ（tool 名/引数/挙動）

---

## 1) 機能一覧（クイックサマリ）

| グループ | 機能 | オフライン | オンライン | 技術メモ |
|---|---|:--:|:--:|---|
| 🎵 SD Music | SD から再生 | ✅ | — | `playlist.json` を優先。欠損/破損時のみ SD をスキャン |
| 🎵 SD Music | 検索 / フォルダ閲覧 / index 再生 | ✅ | — | MCP tools `self.sdmusic.*` で操作 |
| 🎵 SD Music | Shuffle / Repeat | ✅ | — | Repeat: none / one / all |
| 🎵 SD Music | Genre（ID3） | ✅ | — | ID3v1 genre 表 + TCON 正規化（ID3v2） |
| 🌐 Online Music | オンライン再生 | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | MP3 を SD に自動キャッシュ | ✅* | ✅ | 最低 ~15 秒再生後に `/sdcard/music` へキャッシュ |
| 📻 Radio | ネットラジオ再生 | — | ✅ | AAC。既定チャンネル + URL 再生 |
| ⏰ アラーム | スケジュール + Snooze/Dismiss + 着信音 | ✅ | — | NVS 永続化（`alarm/list`）、tool `self.alarm.*` |
| 🌙 旧暦 | 西暦 ↔ 旧暦 + 万年暦 | ✅ | — | 時刻干支、吉時、節気、祭日 |
| 🌦️ 天気 | 現在 + 予報 | — | ✅ | OpenWeatherMap、IP 自動ロケーション、位置キャッシュ |

\* 自動キャッシュには SD のマウントと書き込み権限が必要です。

---

## 2) 🎵 オフライン音楽（SD Card）

### 2.1 ユーザー体験
- ユーザーが**曲の再生**を要求すると、システムは：
  - **まず SD を検索**（高速・通信不要）
  - 見つからない場合 → オンラインへフォールバック（オンライン機能が有効な場合）

### 2.2 `playlist.json`（起動とライブラリ閲覧の高速化）
- 起動時のライブラリスキャン時間を減らすため、SD にインデックスを生成します：
  - デフォルト：`/sdcard/playlist.json`
  - （別の root を設定した場合はその配下）
- 処理フロー：
  1) `playlist.json` が**有効**なら → それだけを読み込む（高速）
  2) **欠損/破損/空**なら → SD をスキャンして再生成し、`playlist.json` に保存

> 運用ヒント：SD の曲を追加/削除した後は、`self.sdmusic.reload` を呼んで再スキャンできます。

### 2.3 Metadata & Genre（ID3）
- **ID3v1 genre** の参照表を内蔵
- ID3v2 の “安全な” 最小実装（一般的な frame に集中）：
  - Title/Artist/Album/Year/Genre  
- Genre 表示は正規化して統一します。

---

## 3) 🌐 オンライン音楽（ストリーミング + SD 自動キャッシュ）

### 3.1 オフライン優先ルーティング
- Tool `self.music.play_song` は：
  - SD で（曲名/アーティスト）検索 → あればオフライン再生
  - なければ → オンラインプレーヤーへ

### 3.2 MP3 の自動キャッシュ（オンラインで聴いて、あとでオフライン）
- MP3 のオンラインストリーム再生中、以下へ保存できます：
  - `/sdcard/music/`
- キャッシュ方針（現行実装）：
  - 最低再生時間（通常 ~15 秒）に達してからコミット
  - 早期エラー時は不要ファイルを残さない

### 3.3 音楽サーバ設定
- 音楽サーバ URL / API は使用している firmware/server に依存します。
- キャッシュと offline/online 分岐は music モジュールにあります（**File/Module** 参照）。

---

## 4) 📻 インターネットラジオ（AAC）

### 4.1 ポイント
- チャンネル一覧から Internet Radio（AAC）を再生
- URL 直接再生も可能

### 4.2 「チャンネル名」と「URL」での制御
- チャンネル一覧：
  - `self.radio.play_station`
- URL：
  - `self.radio.play_url`
- 停止：
  - `self.radio.stop`

---

## 5) 🌙 ベトナム旧暦（オフライン）

### 5.1 基本機能
- **西暦 ↔ 旧暦** 変換（閏月 / leap month 対応）
- 干支表示：
  - **年**（`CanChiYear`）：例 Ất Tỵ
  - **月**（`CanChiMonth`）
  - **日**（`CanChiDay`）
  - **時**（`CanChiHour`）：“Ngũ Thử Độn” に基づく

### 5.2 万年暦・風水情報
- **黄道日/黒道日**：旧暦月と日支に基づく
- **吉時**：良い時間帯の一覧（Tý、Sửu、Thìn…）
- **節気**：太陽黄経による 24 節気
- **行事/祭日**：伝統行事を識別（Tết、Rằm、Ông Táo...）

### 5.3 スマート UI（Lunar Widget）
- **Day/Night Theme**：背景/文字色を自動切替（18:00–06:00 はダーク）
- **情報カルーセル**：干支（日/月/年/時/日支）を約 3 秒ごとに切替
- **フォント正規化**：`NormalizeVietnameseForFont` により埋め込みフォントでベトナム語を安定表示

### 5.4 タイムゾーン設定
- デフォルト：`UTC+7`（ベトナム）
- コンパイル時上書き：`LUNAR_TZ_HOURS`

### 5.5 ソース構成（ファイルから素早く追う）
- 変換アルゴリズムと万年暦：
  - `main/boards/common/lunar_calendar.cc|.h`
  - 主 API：`SolarToLunar()`, `LunarToSolar()`, `SolarTermName()`, `AuspiciousHours()`, `DayQualityByLunarMonth()`
- UI widget：
  - `main/boards/common/lunar_widget.cc|.h`
  - `LunarWidget::UpdateAll()` がテキスト/色のリアルタイム更新の中心
- Idle 表示の制御：
  - `main/application.cc`（Weather ↔ Lunar のローテーション）

### 5.6 西暦 ↔ 旧暦変換アルゴリズム（実装準拠）
`lunar_calendar.cc` は一般的な天文計算（Julian day、新月、太陽黄経）を用います：
- `JulianDayFromDate()` / `DateFromJulianDay()`：西暦日付 ↔ JD
- `GetNewMoonDay(k, tz)`：新月日
- `GetSunLongitude(jd, tz)`：太陽黄経を算出し 12 区分に量子化
- `GetLunarMonth11(yy, tz)`：年の基準となる旧暦 11 月を探索
- `GetLeapMonthOffset(a11, tz)`：新月間での太陽黄経“区分”の比較で閏月を決定

出力 `LunarDate`：`day`, `month`, `year`, `is_leap`。

### 5.7 Lunar Widget の高度な表示ロジック
- 干支カルーセル：mode 0..5、約 3 秒ごとに切替：
  - 旧暦日ラベル（Mùng/Rằm）、日干支、月干支、年干支、時干支、日支
- フッター：
  - **吉時** を優先して巡回表示。なければ **節気** を表示
  - 旧暦祭日（`LunarFestivalName`）に一致した場合は祭日を強調

---

## 6) ⏰ アラーム（Alarm Manager v2）

### 6.1 概要と体験
- 端末が**有効なシステム時刻**を取得すると、アラームは**オフライン**で動作します
- 時刻になると：
  - UI 通知：**「Báo thức – Đã đến giờ!」**
  - 選択したアラーム音（OGG）を周期的に再生

### 6.2 データモデルと繰り返し（`days_mask`）
各アラームは `Alarm` struct（主要フィールド）：
- `id`（string）：MCP で CRUD するための安定 ID
- `hour`, `minute`：24 時間表記
- `days_mask`（0..127）：曜日繰り返し bitmask
  - Bit0=日、Bit1=月、… Bit6=土
  - `0`：ワンショット（鳴動後に自動削除）
  - `127`：毎日
  - 月–金 = `62`（0b0111110）
- `enabled`：有効/無効
- `label`：ラベル
- `ringtone`：`"ga" | "alarm1" | "iphone"`（フォールバックあり）
- `snooze_minutes`：1..120（既定 10）
- `ring_interval_sec`：1..60
- `max_rings`：1..60

### 6.3 スケジューラ：分境界に同期 + 重複鳴動防止
- 2 つのタイマー：
  - `check_timer`：**次の分境界 + 1 秒**に合わせてチェック
  - `ring_timer`：鳴動中に周期再生
- 重複鳴動防止：
  - runtime guard `last_fired_epoch_min`（非永続）→ 同一分内の再トリガを抑止

> コード上の「有効時刻」：`time(nullptr) >= 2023-01-01 00:00:00 UTC`（`kMinValidEpoch`）。  
> SNTP/RTC が未同期の場合、scheduler は待機し、アラームをトリガしません。

### 6.4 Ringing / Dismiss / Snooze
- 鳴動：
  - `AudioService::PlaySound()` を `ring_interval_sec` 周期で実行
  - リングトーンのフォールバック：selected → `alarm1` → `ga`
- Dismiss：
  - 鳴動停止、snooze 取消、`display->ClearNotification()`
- Snooze：
  - runtime の独立 “snooze alarm” を生成（ワンショットも snooze 可）
  - `snooze_until_epoch_sec_` 到達で再トリガ

### 6.5 永続化（NVS/Settings）
- `Settings` 経由で NVS に保存：
  - Namespace：`"alarm"`
  - Key：`"list"`
  - 形式：JSON array
- 後方互換：
  - 旧ストレージ `repeat_daily=true` → `days_mask=127` にマップ

### 6.6 アラーム用 MCP tools（`self.alarm.*`）
- `self.alarm.set` — 作成/更新  
  Args：`id?`, `hour`, `minute`, `days_mask`, `repeat_daily?`, `label?`, `ringtone?`, `snooze_minutes?`, `ring_interval_sec?`, `max_rings?`  
  Return：`{success,id,message}`
- `self.alarm.list` — 一覧 + runtime 状態（ringing/snooze）
- `self.alarm.delete` — `id` で削除
- `self.alarm.enable` — `id` で有効/無効
- `self.alarm.clear` — 全削除
- `self.alarm.stop` — dismiss + snooze 取消
- `self.alarm.snooze` — 鳴動中アラームを snooze（`minutes=0` → 既定値）

### 6.7 関連ファイル/モジュール
- Core：`main/boards/common/alarm_manager.cc|.h`
- Tool registry：`main/mcp_server.cc`
- Sound assets：`main/assets/common/alarm1.ogg`, `main/assets/common/ga.ogg`, `main/assets/common/iphone.ogg`

---

## 7) 🌦️ 天気（OpenWeatherMap + IP 自動ロケーション）

### 7.1 表示データ
- 現在の天気
- 5 日予報
- 説明言語：ベトナム語（`OPENWEATHER_LANG = "vi"`）

### 7.2 IP ベースの自動ロケーション（city 未設定時）
ジオロケーションの優先順：
1) `ipwho.is`
2) `ipwhois.app`
3) `ipapi.co`

IP 位置キャッシュ：
- TTL：**6 時間**

更新サイクル：
- バックグラウンド scheduler（UI をブロックしない）
- バックグラウンド SNTP 同期で時刻精度を確保（アラーム + カレンダーのため）

### 7.3 API key と City 設定（NVS）
- Namespace `wifi`：
  - `weather_api_key`
  - `weather_city`
- Fallback namespace `weather`：
  - `api_key`
  - `city`

規約：
- `city` が空、または `"auto"` → IP 自動ロケーションを有効化

> 本番推奨：専用 API key を設定し、quota 管理と追跡を容易にする。

---

## 8) 🖥️ Idle オーバーレイ：Weather ↔ Lunar（アイドル時の体験改善）
端末が Idle のとき UI は：
- **weather が利用可能**ならローテーション表示：
  - Weather widget ↔ Lunar widget
- **ネットワークなし / weather 未準備**なら Lunar widget を維持（fallback）

ローテーション間隔：
- **180 秒（3 分）**ごと

---

## 9) MCP Tools：「意図」 ↔ 「tool」（制御マッピング）
以下はデバイス側 MCP server に登録されたツールのマッピングです。

### 9.1 System / UI / Audio / Camera
- `self.get_device_status` — デバイス状態（network/audio/state/…）
- `self.audio_speaker.set_volume` — 音量設定
- `self.screen.set_brightness` — 輝度設定
- `self.screen.set_theme` — UI テーマ
- `self.camera.take_photo` — 写真撮影（board 依存）

### 9.2 音楽再生（SD 優先）
- `self.music.play_song`
  - Args：
    - `song_name`（必須）
    - `artist_name`（任意）
  - 挙動：
    - SD に存在 → OFFLINE 再生
    - なければ → ONLINE 再生（自動キャッシュする場合あり）

### 9.3 SD Music（ユーザーが SD ライブラリを直接操作したい場合のみ）
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

### 9.5 アラーム
- `self.alarm.set`
- `self.alarm.list`
- `self.alarm.delete`
- `self.alarm.enable`
- `self.alarm.clear`
- `self.alarm.stop`
- `self.alarm.snooze`

---

## 10) 関連ファイル/モジュール（コードを素早く探す）

<details>
<summary><b>📁 コアファイル一覧</b></summary>

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

### 🌙 旧暦
- `main/boards/common/lunar_calendar.cc`
- `main/boards/common/lunar_calendar.h`
- `main/boards/common/lunar_widget.cc`
- `main/boards/common/lunar_widget.h`

### ⏰ アラーム
- `main/boards/common/alarm_manager.cc`
- `main/boards/common/alarm_manager.h`

### 🌦️ 天気
- `main/boards/common/weather_service.cc`
- `main/boards/common/weather_service.h`
- `main/boards/common/weather_widget.cc`
- `main/boards/common/weather_widget.h`

### 🖥️ Idle オーバーレイ
- `main/application.cc`

### 🔧 MCP Tools registry
- `main/mcp_server.cc`

</details>

---

## 11) 全体アーキテクチャ（ソース解析）

本節はソース構造に沿って、読み進めやすい形で整理しています（ESP-IDF / C++）。

### 11.1 上位ディレクトリ構成
- `main/` — ファームウェア本体（core app、protocol、audio、display、boards…）
- `docs/` — プロトコル文書（MCP）、websocket/mqtt-udp、custom board ガイド…
- `scripts/` — アセット生成、音声/画像変換、リリース補助
- `partitions/` — パーティション表（v1/v2）、flash size に依存
- `sdkconfig.defaults*` — チップ別既定設定（esp32/esp32s3/esp32c3…）
- `.github/` — CI/CD

### 11.2 起動フロー（Entry → Main loop）
1) `main/main.cc`
   - Wi‑Fi/settings 用に NVS（`nvs_flash_init`）を初期化
   - `Application::GetInstance().Initialize()` を呼び、`Run()` へ（戻らない）
2) `main/application.cc|.h`
   - display/audio/board callbacks を設定
   - protocol を初期化（ビルドにより Websocket/MQTT）
   - event loop：network、state machine、UI/audio、MCP messages

### 11.3 コア制御：Application + イベントモデル
- `Application` が保持する主要要素：
  - `DeviceStateMachine state_machine_`
  - `AudioService audio_service_`
  - `Protocol`（server との通信、MCP/toolcall 受信）
  - `WeatherService`（条件付き）、Idle オーバーレイ timer（Weather↔Lunar）
- 制御パターン：
  - ISR/補助 task の callback が event/closure を `main_tasks_` に投入
  - `Run()` が順次処理して race を回避

### 11.4 ステートマシン（端末の状態）
- `main/device_state.h` — 状態 enum（idle/listening/speaking/…）
- `main/device_state_machine.cc|.h`
  - 遷移の整合性を保証
  - state-changed を `Application::OnStateChanged()` に通知し、UI/audio 更新と timer の start/stop を実施

### 11.5 Board 抽象化とドライバ層
- `main/boards/common/board.cc|.h` — 共通インターフェース：Display、audio codec、buttons、mic/speaker、power/battery、SD mount…
- `main/boards/<board-name>/...` — board 固有：pinmap、具体ドライバ、画面種別、codec、led strip…
- 共通モジュール：
  - `sd_mount.cc|.h` — SD マウント（`/sdcard`）
  - `wifi_board.cc|.h` + `blufi.h` — Wi‑Fi + provisioning（board 依存）
  - `adc_battery_monitor.*`、PMIC（`axp2101.*`, `sy6970.*`）— 電源/バッテリー

### 11.6 Audio stack
- `main/audio/`：
  - `audio_service.*` — 再生 API / pipeline
  - `audio_codec.*` + `audio/codecs/*` — codec ドライバ（ES8311/ES8388/…）
  - `audio_processor.*` — 前処理（AEC/NS/AGC は設定依存）
- 音声アセット：
  - `main/assets/common/*.ogg` + `main/assets/locales/<lang>/*.ogg`
  - `main/assets.cc|.h` — 言語別アセットのパッケージ/参照

### 11.7 Display / LVGL UI
- `main/display/`：
  - `display.*` — LVGL ラッパ（root screen、notification、theme）
  - `lvgl_display/*` — ドライバ & ヘルパ（gif/jpg、font、canvas…）
- ユーティリティ widget（`main/boards/common/`）：
  - `weather_widget.*`, `lunar_widget.*`
- Notification：
  - `Application::Alert(...)` が Alarm/Protocol から呼ばれ通知を表示

### 11.8 Protocol layer + MCP（tool calling）
- `main/protocols/`：
  - `protocol.h` — 抽象化
  - `websocket_protocol.*`, `mqtt_protocol.*` — transport
- デバイス側 MCP server：
  - `main/mcp_server.cc|.h`
  - capability に応じて tool を登録：
    - system：volume/brightness/theme/camera/status
    - media：music/sdmusic/radio
    - utility：alarm

### 11.9 Storage: NVS + SD card
- `main/settings.cc|.h` — NVS wrapper（string/int/bool、erase…）
- SD card：
  - オフライン音楽 + キャッシュ：`/sdcard/music`
  - インデックス：`playlist.json`
- Alarm：
  - NVS：namespace `alarm`、key `list`（JSON array）

### 11.10 Tooling（scripts）
- `scripts/build_default_assets.py` — 既定アセットをビルド
- `scripts/gen_lang.py` — `language.json` / 言語アセット生成
- `scripts/mp3_to_ogg.sh`, `scripts/ogg_converter/*` — 音声アセット正規化
- `scripts/Image_Converter/*` — 画像を LVGL 形式へ変換
- `scripts/release.py`, `scripts/versions.py` — リリース補助

---

## 12) 運用チェックリスト（実務向け）

### SD card
- [ ] SD のマウント OK
- [ ] SD に音楽がある
- [ ] 書き込み権限があり、以下を作成できる：
  - `playlist.json`
  - キャッシュフォルダ `/sdcard/music`

### Online/Radio/Weather
- [ ] Wi‑Fi が安定
- [ ] Weather：`weather_api_key` を設定（推奨）
- [ ] City：IP 自動ロケーションを使うなら `"auto"` にする

---

### ドキュメント範囲
本ドキュメントは現行ソースに準拠し、以下に集中します：  
**offline music / online music / radio / alarm / lunar / weather**。
