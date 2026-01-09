#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <ctime>

#include <esp_timer.h>

/*
Alarm subsystem (v2)

Design goals
- Stable IDs per alarm for CRUD from MCP tools
- Day-of-week scheduling via bitmask (Sun..Sat) + one-shot alarms (days_mask=0)
- Optional per-alarm snooze defaults
- Robust triggering: align checks to minute boundary; protect against double-trigger
- Persistence in NVS via Settings namespace "alarm", key "list" (JSON array)

days_mask encoding
- Bit 0: Sunday (tm_wday == 0)
- Bit 1: Monday
- ...
- Bit 6: Saturday
- 0   -> one-shot alarm (fires once then is removed from schedule)
- 127 -> every day
*/

struct Alarm {
    std::string id;                 // stable identifier
    int hour = 0;                   // 0..23
    int minute = 0;                 // 0..59
    uint8_t days_mask = 0;          // 0..127, see above
    bool enabled = true;            // if false: ignored by scheduler
    std::string label;              // optional
    std::string ringtone = "ga";    // "ga" | "alarm1" | "iphone"
    int snooze_minutes = 10;        // default snooze duration for this alarm (1..120)

    // Ring policy (kept per-alarm to allow future customization)
    int ring_interval_sec = 10;     // 1..60
    int max_rings = 10;             // 1..60

    // Runtime-only guard (NOT persisted)
    int64_t last_fired_epoch_min = -1;
};

class AlarmManager {
public:
    static AlarmManager& GetInstance() {
        static AlarmManager instance;
        return instance;
    }

    // Optional callback fired when an alarm starts ringing
    void SetOnTriggered(std::function<void(const Alarm&)> cb);

    // Backward-compatible API (v1): used by older mcp_server.cc
    void AddAlarm(int hour, int minute, const std::string& ringtone, bool repeat_daily);

    // Create or update an alarm.
    // - If spec.id is empty => create a new alarm, generate an id, and return it.
    // - If spec.id exists => update the alarm with same id (or create if not found).
    // Returns true on success; false if rejected (e.g., max capacity).
    bool UpsertAlarm(const Alarm& spec, std::string& out_id, std::string& out_error);

    // Delete by id. Returns true if deleted.
    bool DeleteAlarm(const std::string& id);

    // Enable/disable by id. Returns true if found.
    bool SetAlarmEnabled(const std::string& id, bool enabled);

    // Clear all alarms
    void RemoveAll();

    // Serialize alarms list to JSON (envelope)
    void ListAlarms(std::string& out_json);

    // Ringing control
    bool IsRinging() const;

    // Dismiss current ringing + cancel pending snooze. Returns true if anything was stopped.
    bool StopRinging();

    // Snooze current ringing.
    // minutes <= 0 => use current alarm's snooze_minutes.
    bool Snooze(int minutes);

private:
    AlarmManager();
    ~AlarmManager();
    AlarmManager(const AlarmManager&) = delete;
    AlarmManager& operator=(const AlarmManager&) = delete;

private:
    // Scheduler
    void OnCheckTimer();
    void ScheduleNextCheck();
    void CheckAlarms();

    // Trigger logic
    void TriggerAlarm(const Alarm& a, bool from_snooze);

    // Persistence
    void LoadFromStorage();
    void SaveToStorage();

    // Ring loop
    void StartRinging(const Alarm& a);
    void OnRingTimer();
    void StopRingTimer_NoLock();

    // Helpers
    static bool IsTimeValid(time_t now);
    static void Sanitize(Alarm& a);
    static uint8_t ClampDaysMask(int v);
    static std::string NormalizeRingtone(const std::string& s);
    static std::string GenerateId();

private:
    static constexpr size_t kMaxAlarms = 32;

    std::vector<Alarm> alarms_;

    // Timer: scheduler check aligned to minute boundary (one-shot timer)
    esp_timer_handle_t check_timer_handle_ = nullptr;

    // Timer: periodic ring timer
    esp_timer_handle_t ring_timer_handle_ = nullptr;

    std::function<void(const Alarm&)> on_triggered_;

    // Ringing runtime state
    bool is_ringing_ = false;
    Alarm current_alarm_{};
    int ring_count_ = 0;

    // Snooze runtime state (kept independent from schedule list so one-shot alarms can snooze)
    bool snooze_pending_ = false;
    Alarm snooze_alarm_{};
    int64_t snooze_until_epoch_sec_ = 0;
};
