#include "alarm_manager.h"

#include "application.h"
#include "assets/lang_config.h"  // defines namespace Lang (auto-generated)
#include "display/display.h"            // needed for Display::ClearNotification (Board.h only forward-declares)
#include "board.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_random.h>
#include <cJSON.h>

#include <algorithm>
#include <ctime>
#include <cstdio>
#include <string_view>

#define TAG "AlarmManager"

using namespace Lang;

namespace {

// For devices that start with an invalid epoch (e.g., 1970), treat time as invalid.
// 2023-01-01T00:00:00Z = 1672531200
constexpr time_t kMinValidEpoch = 1672531200;

constexpr int64_t US(int64_t sec) { return sec * 1000000LL; }

static inline uint8_t wday_bit(const tm& ti) {
    // tm_wday: 0=Sunday..6=Saturday
    int w = ti.tm_wday;
    if (w < 0 || w > 6) return 0;
    return static_cast<uint8_t>(1u << w);
}

static const std::string_view* ringtone_to_ogg(const std::string& id) {
    if (id == "ga") return &Lang::Sounds::OGG_GA;
    if (id == "alarm1") return &Lang::Sounds::OGG_ALARM1;
    if (id == "iphone") return &Lang::Sounds::OGG_IPHONE;
    return &Lang::Sounds::OGG_GA;
}

} // namespace

AlarmManager::AlarmManager() {
    // Scheduler check timer (one-shot; rescheduled after each run)
    esp_timer_create_args_t check_args = {
        .callback = [](void* arg) {
            static_cast<AlarmManager*>(arg)->OnCheckTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_check_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&check_args, &check_timer_handle_));

    // Ring timer (periodic while ringing)
    esp_timer_create_args_t ring_args = {
        .callback = [](void* arg) {
            static_cast<AlarmManager*>(arg)->OnRingTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_ring_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&ring_args, &ring_timer_handle_));

    LoadFromStorage();
    ScheduleNextCheck();
}

AlarmManager::~AlarmManager() {
    if (check_timer_handle_) {
        esp_timer_stop(check_timer_handle_);
        esp_timer_delete(check_timer_handle_);
        check_timer_handle_ = nullptr;
    }
    if (ring_timer_handle_) {
        esp_timer_stop(ring_timer_handle_);
        esp_timer_delete(ring_timer_handle_);
        ring_timer_handle_ = nullptr;
    }
}

void AlarmManager::SetOnTriggered(std::function<void(const Alarm&)> cb) {
    on_triggered_ = std::move(cb);
}

void AlarmManager::AddAlarm(int hour, int minute,
                            const std::string& ringtone,
                            bool repeat_daily) {
    Alarm spec;
    spec.hour = hour;
    spec.minute = minute;

    // Accept legacy ringtone strings (e.g. "/spiffs/iphone.ogg") by substring matching.
    if (ringtone.find("iphone") != std::string::npos) spec.ringtone = "iphone";
    else if (ringtone.find("alarm1") != std::string::npos) spec.ringtone = "alarm1";
    else if (ringtone.find("ga") != std::string::npos) spec.ringtone = "ga";
    else spec.ringtone = ringtone;

    spec.days_mask = repeat_daily ? 127 : 0;
    spec.enabled = true;

    std::string out_id, err;
    if (!UpsertAlarm(spec, out_id, err)) {
        ESP_LOGW(TAG, "AddAlarm failed: %s", err.c_str());
    }
}

bool AlarmManager::IsRinging() const {
    return is_ringing_;
}

bool AlarmManager::UpsertAlarm(const Alarm& spec, std::string& out_id, std::string& out_error) {
    out_error.clear();

    Alarm a = spec;
    if (a.id.empty()) {
        a.id = GenerateId();
    }

    Sanitize(a);

    auto it = std::find_if(alarms_.begin(), alarms_.end(),
                           [&a](const Alarm& x) { return x.id == a.id; });
    bool creating = (it == alarms_.end());

    if (creating && alarms_.size() >= kMaxAlarms) {
        out_error = "Reached max alarms capacity";
        return false;
    }

    if (creating) {
        alarms_.push_back(a);
    } else {
        // preserve runtime guard
        int64_t last = it->last_fired_epoch_min;
        *it = a;
        it->last_fired_epoch_min = last;
    }

    std::sort(alarms_.begin(), alarms_.end(), [](const Alarm& x, const Alarm& y) {
        if (x.hour != y.hour) return x.hour < y.hour;
        if (x.minute != y.minute) return x.minute < y.minute;
        return x.id < y.id;
    });

    SaveToStorage();
    out_id = a.id;

    ESP_LOGI(TAG, "UpsertAlarm id=%s time=%02d:%02d mask=%u enabled=%d",
             a.id.c_str(), a.hour, a.minute, (unsigned)a.days_mask, a.enabled ? 1 : 0);

    return true;
}

bool AlarmManager::DeleteAlarm(const std::string& id) {
    if (id.empty()) return false;

    // If deleting a currently ringing/snoozed alarm, dismiss/cancel.
    if (is_ringing_ && current_alarm_.id == id) {
        StopRinging();
    }
    if (snooze_pending_ && snooze_alarm_.id == id) {
        snooze_pending_ = false;
        snooze_until_epoch_sec_ = 0;
        snooze_alarm_ = Alarm{};
    }

    auto before = alarms_.size();
    alarms_.erase(std::remove_if(alarms_.begin(), alarms_.end(),
                                [&id](const Alarm& a) { return a.id == id; }),
                  alarms_.end());

    bool deleted = (alarms_.size() != before);
    if (deleted) {
        SaveToStorage();
        ESP_LOGI(TAG, "DeleteAlarm id=%s", id.c_str());
    }
    return deleted;
}

bool AlarmManager::SetAlarmEnabled(const std::string& id, bool enabled) {
    if (id.empty()) return false;
    for (auto& a : alarms_) {
        if (a.id == id) {
            a.enabled = enabled;
            SaveToStorage();
            ESP_LOGI(TAG, "SetAlarmEnabled id=%s enabled=%d", id.c_str(), enabled ? 1 : 0);
            return true;
        }
    }
    return false;
}

void AlarmManager::RemoveAll() {
    StopRinging();
    alarms_.clear();
    snooze_pending_ = false;
    snooze_until_epoch_sec_ = 0;
    snooze_alarm_ = Alarm{};
    SaveToStorage();
    ESP_LOGI(TAG, "All alarms cleared");
}

void AlarmManager::ListAlarms(std::string& out_json) {
    cJSON* alarms = cJSON_CreateArray();
    for (const auto& a : alarms_) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", a.id.c_str());
        cJSON_AddNumberToObject(item, "hour", a.hour);
        cJSON_AddNumberToObject(item, "minute", a.minute);
        cJSON_AddNumberToObject(item, "days_mask", a.days_mask);
        cJSON_AddBoolToObject(item, "enabled", a.enabled);
        cJSON_AddStringToObject(item, "label", a.label.c_str());
        cJSON_AddStringToObject(item, "ringtone", a.ringtone.c_str());
        cJSON_AddNumberToObject(item, "snooze_minutes", a.snooze_minutes);
        cJSON_AddNumberToObject(item, "ring_interval_sec", a.ring_interval_sec);
        cJSON_AddNumberToObject(item, "max_rings", a.max_rings);
        cJSON_AddItemToArray(alarms, item);
    }

    cJSON* envelope = cJSON_CreateObject();
    cJSON_AddItemToObject(envelope, "alarms", alarms);

    cJSON_AddBoolToObject(envelope, "is_ringing", is_ringing_);
    if (is_ringing_) {
        cJSON* cur = cJSON_CreateObject();
        cJSON_AddStringToObject(cur, "id", current_alarm_.id.c_str());
        cJSON_AddStringToObject(cur, "label", current_alarm_.label.c_str());
        cJSON_AddStringToObject(cur, "ringtone", current_alarm_.ringtone.c_str());
        cJSON_AddNumberToObject(cur, "ring_count", ring_count_);
        cJSON_AddItemToObject(envelope, "ringing_alarm", cur);
    }

    cJSON_AddBoolToObject(envelope, "snooze_pending", snooze_pending_);
    if (snooze_pending_) {
        cJSON_AddNumberToObject(envelope, "snooze_until_epoch_sec", (double)snooze_until_epoch_sec_);
        cJSON* s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "id", snooze_alarm_.id.c_str());
        cJSON_AddStringToObject(s, "label", snooze_alarm_.label.c_str());
        cJSON_AddStringToObject(s, "ringtone", snooze_alarm_.ringtone.c_str());
        cJSON_AddItemToObject(envelope, "snooze_alarm", s);
    }

    char* s = cJSON_PrintUnformatted(envelope);
    out_json.assign(s ? s : "{\"alarms\":[]}");
    if (s) cJSON_free(s);
    cJSON_Delete(envelope);
}

// -----------------------------------------------------------------------------
// Scheduler
// -----------------------------------------------------------------------------

void AlarmManager::OnCheckTimer() {
    CheckAlarms();
    ScheduleNextCheck();
}

void AlarmManager::ScheduleNextCheck() {
    if (!check_timer_handle_) return;

    esp_timer_stop(check_timer_handle_);

    time_t now = time(nullptr);
    int64_t delay_sec = 60;

    if (IsTimeValid(now)) {
        // Align to next minute boundary + 1s offset.
        int64_t next_minute = (static_cast<int64_t>(now) / 60 + 1) * 60;
        delay_sec = std::max<int64_t>(1, (next_minute - static_cast<int64_t>(now)) + 1);
    }

    ESP_ERROR_CHECK(esp_timer_start_once(check_timer_handle_, US(delay_sec)));
}

void AlarmManager::CheckAlarms() {
    time_t now = time(nullptr);

    // Snooze first.
    if (snooze_pending_ && IsTimeValid(now)) {
        if (static_cast<int64_t>(now) >= snooze_until_epoch_sec_) {
            snooze_pending_ = false;
            snooze_until_epoch_sec_ = 0;
            TriggerAlarm(snooze_alarm_, true);
            return;
        }
    }

    if (!IsTimeValid(now)) {
        // Waiting for SNTP/RTC.
        return;
    }

    tm ti;
    localtime_r(&now, &ti);

    bool modified = false;
    int64_t epoch_min = static_cast<int64_t>(now) / 60;

    for (auto it = alarms_.begin(); it != alarms_.end();) {
        Alarm& a = *it;

        if (!a.enabled) {
            ++it;
            continue;
        }

        if (ti.tm_hour != a.hour || ti.tm_min != a.minute) {
            ++it;
            continue;
        }

        if (a.last_fired_epoch_min == epoch_min) {
            ++it;
            continue;
        }

        if (a.days_mask != 0) {
            if ((a.days_mask & wday_bit(ti)) == 0) {
                ++it;
                continue;
            }
        }

        a.last_fired_epoch_min = epoch_min;
        Alarm snapshot = a;

        // One-shot: remove after firing.
        if (a.days_mask == 0) {
            it = alarms_.erase(it);
            modified = true;
        } else {
            ++it;
        }

        TriggerAlarm(snapshot, false);
        break; // avoid overlaps
    }

    if (modified) {
        SaveToStorage();
    }
}

// -----------------------------------------------------------------------------
// Persistence
// -----------------------------------------------------------------------------

void AlarmManager::LoadFromStorage() {
    Settings settings("alarm", true);
    std::string json = settings.GetString("list", "[]");

    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        alarms_.clear();
        ESP_LOGW(TAG, "Invalid alarm storage format; reset to empty");
        return;
    }

    alarms_.clear();

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (!cJSON_IsObject(item)) continue;

        Alarm a;

        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* hour = cJSON_GetObjectItem(item, "hour");
        cJSON* minute = cJSON_GetObjectItem(item, "minute");

        if (cJSON_IsString(id) && id->valuestring) a.id = id->valuestring;
        if (cJSON_IsNumber(hour)) a.hour = hour->valueint;
        if (cJSON_IsNumber(minute)) a.minute = minute->valueint;

        cJSON* days_mask = cJSON_GetObjectItem(item, "days_mask");
        cJSON* enabled = cJSON_GetObjectItem(item, "enabled");
        cJSON* label = cJSON_GetObjectItem(item, "label");
        cJSON* ringtone = cJSON_GetObjectItem(item, "ringtone");
        cJSON* snooze_minutes = cJSON_GetObjectItem(item, "snooze_minutes");
        cJSON* ring_interval_sec = cJSON_GetObjectItem(item, "ring_interval_sec");
        cJSON* max_rings = cJSON_GetObjectItem(item, "max_rings");
        // Backward compatibility with v1 storage
        cJSON* repeat_daily = cJSON_GetObjectItem(item, "repeat_daily");

        if (cJSON_IsNumber(days_mask)) {
            a.days_mask = ClampDaysMask(days_mask->valueint);
        } else if (cJSON_IsBool(repeat_daily) && (repeat_daily->valueint == 1)) {
            a.days_mask = 127;
        }
        if (cJSON_IsBool(enabled)) a.enabled = (enabled->valueint == 1);
        if (cJSON_IsString(label) && label->valuestring) a.label = label->valuestring;
        if (cJSON_IsString(ringtone) && ringtone->valuestring) a.ringtone = ringtone->valuestring;
        if (cJSON_IsNumber(snooze_minutes)) a.snooze_minutes = snooze_minutes->valueint;
        if (cJSON_IsNumber(ring_interval_sec)) a.ring_interval_sec = ring_interval_sec->valueint;
        if (cJSON_IsNumber(max_rings)) a.max_rings = max_rings->valueint;

        if (a.id.empty()) a.id = GenerateId();

        Sanitize(a);
        alarms_.push_back(std::move(a));

        if (alarms_.size() >= kMaxAlarms) break;
    }

    cJSON_Delete(root);

    std::sort(alarms_.begin(), alarms_.end(), [](const Alarm& x, const Alarm& y) {
        if (x.hour != y.hour) return x.hour < y.hour;
        if (x.minute != y.minute) return x.minute < y.minute;
        return x.id < y.id;
    });

    ESP_LOGI(TAG, "Loaded %d alarm(s) from storage", (int)alarms_.size());
}

void AlarmManager::SaveToStorage() {
    Settings settings("alarm", true);

    cJSON* root = cJSON_CreateArray();
    for (const auto& a : alarms_) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", a.id.c_str());
        cJSON_AddNumberToObject(item, "hour", a.hour);
        cJSON_AddNumberToObject(item, "minute", a.minute);
        cJSON_AddNumberToObject(item, "days_mask", a.days_mask);
        cJSON_AddBoolToObject(item, "enabled", a.enabled);
        cJSON_AddStringToObject(item, "label", a.label.c_str());
        cJSON_AddStringToObject(item, "ringtone", a.ringtone.c_str());
        cJSON_AddNumberToObject(item, "snooze_minutes", a.snooze_minutes);
        cJSON_AddNumberToObject(item, "ring_interval_sec", a.ring_interval_sec);
        cJSON_AddNumberToObject(item, "max_rings", a.max_rings);
        cJSON_AddItemToArray(root, item);
    }

    char* s = cJSON_PrintUnformatted(root);
    settings.SetString("list", s ? s : "[]");
    if (s) cJSON_free(s);
    cJSON_Delete(root);
}

// -----------------------------------------------------------------------------
// Trigger + Ringing
// -----------------------------------------------------------------------------

void AlarmManager::TriggerAlarm(const Alarm& a, bool from_snooze) {
    if (is_ringing_) {
        ESP_LOGW(TAG, "Trigger ignored: already ringing (id=%s)", a.id.c_str());
        return;
    }

    ESP_LOGI(TAG, "Alarm triggered id=%s time=%02d:%02d snooze=%d",
             a.id.c_str(), a.hour, a.minute, from_snooze ? 1 : 0);

    Application::GetInstance().Alert("Báo thức", "Đã đến giờ!", "bell", "");

    StartRinging(a);

    if (on_triggered_) {
        on_triggered_(a);
    }
}

void AlarmManager::StartRinging(const Alarm& a) {
    current_alarm_ = a;
    ring_count_ = 0;
    is_ringing_ = true;

    int interval = std::max(1, std::min(60, a.ring_interval_sec));

    if (ring_timer_handle_) {
        esp_timer_stop(ring_timer_handle_);
        ESP_ERROR_CHECK(esp_timer_start_periodic(ring_timer_handle_, US(interval)));
    }

    ESP_LOGI(TAG, "StartRinging id=%s ringtone=%s interval=%ds max=%d",
             a.id.c_str(), a.ringtone.c_str(), interval, a.max_rings);
}

void AlarmManager::OnRingTimer() {
    if (!is_ringing_) return;

    int max_rings = std::max(1, std::min(60, current_alarm_.max_rings));
    if (ring_count_ >= max_rings) {
        ESP_LOGI(TAG, "Reached max ring count, auto-stopping");
        StopRinging();
        return;
    }

    const std::string ringtone = NormalizeRingtone(current_alarm_.ringtone);
    const std::string_view* ogg = ringtone_to_ogg(ringtone);

    // Fallback chain
    if (ogg->empty()) {
        ESP_LOGW(TAG, "Selected ringtone empty, fallback to ALARM1");
        ogg = &Lang::Sounds::OGG_ALARM1;
        if (ogg->empty()) {
            ESP_LOGW(TAG, "ALARM1 empty, fallback to GA");
            ogg = &Lang::Sounds::OGG_GA;
        }
    }

    ESP_LOGI(TAG, "Playing alarm sound #%d (id=%s)", ring_count_ + 1, ringtone.c_str());
    Application::GetInstance().GetAudioService().PlaySound(*ogg);
    ring_count_++;
}

void AlarmManager::StopRingTimer_NoLock() {
    if (ring_timer_handle_) {
        esp_timer_stop(ring_timer_handle_);
    }
}

bool AlarmManager::StopRinging() {
    if (!is_ringing_ && !snooze_pending_) {
        return false;
    }

    // Dismiss cancels snooze
    snooze_pending_ = false;
    snooze_until_epoch_sec_ = 0;
    snooze_alarm_ = Alarm{};

    if (is_ringing_) {
        is_ringing_ = false;
        ring_count_ = 0;
        StopRingTimer_NoLock();
        if (auto display = Board::GetInstance().GetDisplay()) {
            display->ClearNotification();
        }
        ESP_LOGI(TAG, "Alarm dismissed");
    } else {
        ESP_LOGI(TAG, "Pending snooze cancelled");
    }

    return true;
}

bool AlarmManager::Snooze(int minutes) {
    if (!is_ringing_) return false;

    time_t now = time(nullptr);
    if (!IsTimeValid(now)) return false;

    int m = minutes;
    if (m <= 0) {
        m = current_alarm_.snooze_minutes;
    }
    m = std::max(1, std::min(120, m));

    StopRingTimer_NoLock();
    is_ringing_ = false;
    ring_count_ = 0;

    snooze_alarm_ = current_alarm_;
    snooze_until_epoch_sec_ = static_cast<int64_t>(now) + static_cast<int64_t>(m) * 60;
    snooze_pending_ = true;

    ESP_LOGI(TAG, "Snoozed alarm id=%s for %d minute(s)", snooze_alarm_.id.c_str(), m);
    return true;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

bool AlarmManager::IsTimeValid(time_t now) {
    return now >= kMinValidEpoch;
}

uint8_t AlarmManager::ClampDaysMask(int v) {
    if (v < 0) return 0;
    if (v > 127) return 127;
    return static_cast<uint8_t>(v);
}

std::string AlarmManager::NormalizeRingtone(const std::string& s) {
    if (s == "ga" || s == "alarm1" || s == "iphone") return s;
    return "ga";
}

void AlarmManager::Sanitize(Alarm& a) {
    if (a.hour < 0) a.hour = 0;
    if (a.hour > 23) a.hour = 23;
    if (a.minute < 0) a.minute = 0;
    if (a.minute > 59) a.minute = 59;

    a.days_mask = ClampDaysMask(a.days_mask);
    a.ringtone = NormalizeRingtone(a.ringtone);

    if (a.snooze_minutes < 1) a.snooze_minutes = 1;
    if (a.snooze_minutes > 120) a.snooze_minutes = 120;

    if (a.ring_interval_sec < 1) a.ring_interval_sec = 1;
    if (a.ring_interval_sec > 60) a.ring_interval_sec = 60;

    if (a.max_rings < 1) a.max_rings = 1;
    if (a.max_rings > 60) a.max_rings = 60;

    if (a.label.size() > 48) {
        a.label.resize(48);
    }
}

std::string AlarmManager::GenerateId() {
    // Compact id: 8 hex chars derived from esp_random()
    uint32_t r = esp_random();
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", (unsigned)r);
    return std::string(buf);
}
