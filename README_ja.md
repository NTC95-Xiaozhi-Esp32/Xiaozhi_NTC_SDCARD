<div align="center">

# XiaoZhi NTC + SD Card  : NGUYỄN THỌ CHUNG
## オフライン/オンラインメディア • インターネットラジオ • ベトナム旧暦 • 天気

[![ESP32](https://img.shields.io/badge/Target-ESP32-000000?style=for-the-badge)](./)
[![SD Card](https://img.shields.io/badge/Storage-SD%20Card-000000?style=for-the-badge)](./)
[![OpenWeatherMap](https://img.shields.io/badge/Weather-OpenWeatherMap-000000?style=for-the-badge)](./)
[![Vietnamese UI](https://img.shields.io/badge/UI-Ti%E1%BA%BFng%20Vi%E1%BB%87t-000000?style=for-the-badge)](./)

</div>

---

## ドキュメントの目的
このドキュメントは、現在のソースに実装済みの **メディア機能＋ユーティリティ** を説明します。

- 🎵 **SDからのオフライン音楽再生**（playlist.json、検索、シャッフル/リピート、ジャンル）
- 🌐 **オンライン音楽再生**（ストリーミング＋MP3をSDへ自動キャッシュ）
- 📻 **インターネットラジオ**（AAC）
- 🌙 **ベトナム旧暦**（オフライン、Can–Chi、黄道/黒道、節気）
- 🌦️ **天気**（OpenWeatherMap＋IPによる自動ロケーション）
- 🖥️ **Idle overlay**：*天気 ↔ 旧暦* の自動ローテーション（オフライン時も適切にフォールバック）

> ヒント：ビルド/フラッシュ手順などの全体READMEが必要な場合は、プロジェクトの元READMEを参照してください。本ファイルは上記5グループの機能にフォーカスしています。

---

## 1) 機能一覧（クイックサマリ）

| カテゴリ | 機能 | オフライン | オンライン | 技術メモ |
|---|---|:--:|:--:|---|
| 🎵 SD Music | SDからの音楽再生 | ✅ | — | `playlist.json` を優先読み込み。壊れている/不足時のみSDをスキャン |
| 🎵 SD Music | 検索 / ディレクトリ閲覧 / index再生 | ✅ | — | MCP tools `self.sdmusic.*` で操作可能 |
| 🎵 SD Music | シャッフル / リピート | ✅ | — | Repeat：none / one / all |
| 🎵 SD Music | ジャンル（ID3） | ✅ | — | ID3v1ジャンル表＋TCON正規化（ID3v2） |
| 🌐 Online Music | オンライン再生 | — | ✅ | Tool `self.music.play_song` |
| 🌐 Online Music | MP3をSDへ自動キャッシュ | ✅* | ✅ | オンライン再生後、15秒以上再生したら `/sdcard/music` にキャッシュ |
| 📻 Radio | インターネットラジオ再生 | — | ✅ | AAC format only；ベトナム向けの既定局リスト＋URL再生 |
| 🌙 旧暦 | 太陽暦 ↔ 旧暦 + 万年暦 | ✅ | — | 時刻干支、黄道/黒道、節気、祭事 |
| 🌦️ 天気 | 現在 + 5日予報 | — | ✅ | OWM；IP自動ロケーション（ipwho.is → ipwhois.app → ipapi.co） |
| 🖥️ Idle overlay | 天気/旧暦のローテーション表示 | ✅ | ✅ | 1分ごと。天気がない場合は旧暦を維持。背景は昼夜状態で自動的に色変更 |

\* 自動キャッシュはSDへ書き込みますが、データソースはオンラインです。

---

## 2) 🎵 オフライン音楽（SD Card）

### 2.1 ユーザー体験
- SDから直接再生（次のような場面に適合）：
  - ネットワークがない
  - 固定の“音楽ライブラリ”を低遅延で再生したい
- 対応：
  - ディレクトリ閲覧 / 曲一覧
  - キーワード検索
  - index指定再生
  - シャッフル / リピート
  - **ジャンル（genre）** ベースのプレイリスト

### 2.2 playlist.json（起動 & ライブラリ閲覧の高速化）
embedded向けに、playlist読み込みを最適化しています。

- 既定の利用パス：  
  `/<SD_MOUNT>/playlist.json`  
  （rootを変更している場合はフォルダに応じて変化）
- 処理フロー：
  1) `playlist.json` が **有効な内容** → ファイルのみ読み込み（高速）
  2) **欠落/破損/空** → SDをスキャンして再構築し、`playlist.json` を保存

> 運用ヒント：SD上の音楽を追加/削除した後は、tool `self.sdmusic.reload` を呼び出して再スキャンできます。

### 2.3 メタデータ & ジャンル（ID3）
- **ID3v1** のジャンル表を搭載
- **ID3v2** を「安全モード」（最小限）で読み取り、代表的なフレームに集中：
  - Title / Artist / Album / Year / Genre
- ジャンル表示は正規化して、見やすく統一

---

## 3) 🌐 オンライン音楽（ストリーミング + SDへ自動キャッシュ）

### 3.1 OFFLINE優先のルーティング
Tool **`self.music.play_song`** のロジック：

1) SDに同名トラックが存在（SDライブラリをロード済み）→ **OFFLINE再生**
2) なければ → **ONLINE再生**（download/stream）

### 3.2 MP3をSDへ自動キャッシュ（オンラインで聴いて、あとでオフライン再生）
オンライン再生時、ファームウェアが自動キャッシュします。

- キャッシュフォルダ：`/sdcard/music`
- **15秒以上** 再生してからキャッシュ開始
- キャッシュ上限：**100曲**
  - 超過時は `st_mtime` を基準に **最も古いファイル** を自動削除

> 注意：これは体験最適化のための“実用的キャッシュ”です。容量制限・厳密なLRU・タグ/アルバム単位などが必要なら、`esp32_music.cc` の定数と eviction 関数を調整してください。

### 3.3 音楽サーバ設定
既定の音楽サーバURLは以下で定義されています。

- `main/boards/common/esp32_music.cc`  
  `#define DEFAULT_MUSIC_URL "http://...:5005"`

マクロを変更してファームウェアを再ビルドすれば、サーバを切り替えられます。

---

## 4) 📻 インターネットラジオ（AAC）

### 4.1 特長
- **AAC** デコーダでインターネットラジオを再生
- 既定の局リスト（ベトナム向け優先）例：
  - JoyFM 98.9
  - VOV1 / VOV2 / VOV3 / VOV5
  - VOV 交通（HN/HCM）
  - VOV 地域（西北 / 東北 / 西原 / メコン / …）

### 4.2 「局名」と「URL」の両方で制御可能
- 局名で再生：`self.radio.play_station`
- URLで再生：`self.radio.play_url`
- 停止：`self.radio.stop`
- 局一覧：`self.radio.get_stations`
- 表示モード：`self.radio.set_display_mode`

> 技術メモ：初期化ログで、現状のラジオリストは **AAC format only** 設計であることが明示されています。

---

## 5) 🌙 ベトナム旧暦（オフライン）

### 5.1 基本機能
- **太陽暦 ↔ 旧暦** 変換（うるう月/leap monthに対応）
- Can–Chi（干支）をフル表示：
  - **年**（`CanChiYear`）：例：Ất Tỵ
  - **月**（`CanChiMonth`）：例：Mậu Dần
  - **日**（`CanChiDay`）：例：Tân Tỵ
  - **時**（`CanChiHour`）："Ngũ Thử Độn" に基づく算出（例：Giờ Đinh Dậu）

### 5.2 万年暦 & 風水
- **黄道日 / 黒道日**：旧暦月と日の支から吉凶を判定
- **黄道時刻**：当日の吉時間帯（Tý, Sửu, Thìn...）を算出
- **節気**：太陽黄経から24節気（Lập Xuân, Thanh Minh, Đông Chí...）を算出
- **イベント & 祝祭日**：伝統的な祭日（Tết, Rằm, Ông Táo...）を認識

### 5.3 スマートUI（Lunar Widget）
- **Day/Night Theme**：18時〜翌6時に背景/文字色を自動切替
- **Carousel Info**：Can Chi情報（日/月/年/時）を3秒ごとに自動ローテーション
- **フォント処理**：`NormalizeVietnameseForFont` により、埋め込みフォントでもベトナム語を滑らかに表示

### 5.4 タイムゾーン設定
- 既定：`UTC+7`（ベトナム）
- compile-time override：`LUNAR_TZ_HOURS`

---

## 6) 🌦️ 天気（OpenWeatherMap + IP自動ロケーション）

### 6.1 表示データ
- 現在の天気（current）
- 5日予報（forecast）
- 説明言語：**ベトナム語**（`OPENWEATHER_LANG = "vi"`）

### 6.2 IPによる自動ロケーション（city未設定時）
geolocationの優先順：

1) `ipwho.is`  
2) `ipwhois.app`  
3) `ipapi.co`

IP位置キャッシュ：
- TTL：**6時間**

天気の更新周期：
- **10分ごと**

### 6.3 APIキー & City（NVS）設定
WeatherServiceはNVSから次の順で設定を読み込みます。

- Namespace `wifi`：
  - `weather_api_key`
  - `weather_city`
- Fallback namespace `weather`：
  - `api_key`
  - `city`

ルール：
- `city` が空、または `"auto"` → IP自動ロケーションを有効化

> 注：ファームウェアにテスト用の既定APIキーが含まれる場合があります。production環境では、クォータ管理と追跡のために独自キーを設定してください。

---

## 7) 🖥️ Idle overlay：天気 ↔ 旧暦（アイドル時の体験最適化）
デバイスがIdle状態に入ると、UIは次のように動作します。

- **天気が利用可能** → ローテーション表示：
  - Weather widget ↔ Lunar widget
- **ネットなし / 天気が利用不可** → Lunar widgetを維持（標準フォールバック）

ローテーション頻度：
- **180秒（3分）** ごと

---

## 8) MCP Tools：「意図」↔「tool」（制御向け）
MCP serverに登録されているツールのマッピング：

### 8.1 音楽（SDを自動優先）
- `self.music.play_song`
  - Args:
    - `song_name`（必須）
    - `artist_name`（任意）
  - 動作：
    - SDに曲がある → OFFLINE再生
    - ない → ONLINE再生

### 8.2 SD Music（SDライブラリを直接操作したい場合）
- 基本再生：`self.sdmusic.playback`（`action = play|pause|stop|next|prev`）
- 再生モード：`self.sdmusic.mode`（shuffle / repeat）
- Track：`self.sdmusic.track`（index再生、track情報取得など）
- ディレクトリ：`self.sdmusic.directory`
- 検索：`self.sdmusic.search`
- ライブラリ：`self.sdmusic.library`
- 再スキャン：`self.sdmusic.reload`
- 推薦：`self.sdmusic.suggest`
- 進捗：`self.sdmusic.progress`
- ジャンル：`self.sdmusic.genre` と `self.sdmusic.genre_list`

### 8.3 Radio
- `self.radio.play_station`
- `self.radio.play_url`
- `self.radio.stop`
- `self.radio.get_stations`
- `self.radio.set_display_mode`

---

## 9) 関連ファイル/モジュール（コード上の場所）

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

### 🌦️ 天気
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

## 10) 運用チェックリスト（簡易）

### SD card
- [ ] SDマウントOK
- [ ] SDに音楽がある
- [ ] 書き込み許可（作成対象）：
  - `playlist.json`
  - キャッシュフォルダ `/sdcard/music`

### Online/Radio/Weather
- [ ] Wi‑Fiが安定
- [ ] Weather：`weather_api_key` を設定（推奨）
- [ ] City：IP自動定位を使うなら `"auto"`

---

### ドキュメント範囲
本ドキュメントは現行ソースに準拠し、**offline music / online music / radio / lunar / weather** の機能群にフォーカスします。
