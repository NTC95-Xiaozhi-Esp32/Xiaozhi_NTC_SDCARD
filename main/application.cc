#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "sd_mount.h"

// Headers for specific modules
#include "esp32_music.h"
#include "esp32_radio.h"
#include "esp32_sd_music.h"
#include "alarm_manager.h" 
#include "weather_service.h"

#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <thread>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "Application"

// =============================================================================
// GLOBAL MANAGERS & STATE TRACKING
// =============================================================================
// Sử dụng biến tĩnh để quản lý AlarmManager vì Board không cung cấp quyền truy cập
static AlarmManager* g_alarm_manager = nullptr;

// Biến theo dõi thời điểm cuối cùng có dữ liệu âm thanh (Music/Radio/SD)
// Dùng để xác định trạng thái "Đang phát nhạc" mà không cần gọi IsPlaying() của từng module
static int64_t g_last_audio_data_time = 0;

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (idle_overlay_timer_ != nullptr) {
        esp_timer_stop(idle_overlay_timer_);
        esp_timer_delete(idle_overlay_timer_);
        idle_overlay_timer_ = nullptr;
    }
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        OnStateChanged(old_state, new_state);
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Initialize Alarm Manager globally
    if (!g_alarm_manager) {
        g_alarm_manager = &AlarmManager::GetInstance();
    }

    // Khi báo thức bắt đầu reo: ẩn Âm lịch/Thời tiết và xoá MusicInfo ngay lập tức
    // (callback có thể chạy từ timer/task khác nên chuyển về main thread qua Schedule).
    if (g_alarm_manager) {
        g_alarm_manager->SetOnTriggered([this](const Alarm&) {
            Schedule([this]() {
                if (GetDeviceState() != kDeviceStateIdle) return;
                auto display = Board::GetInstance().GetDisplay();
                if (!display) return;
                display->HideWeatherWidget();
                display->HideLunarWidget();
                display->SetMusicInfo("");
            });
        });
    }

    // Add MCP common tools
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
            case NetworkEvent::WifiConfigModeExit:
                break;
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                display->SetChatMessage("system", Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network
    board.StartNetwork();

    // Init SD Card
    ESP_LOGI(TAG, "Initializing SD card...");
    SdMount::GetInstance().Init();
    if (auto sd = board.GetSdMusic()) {
        sd->loadTrackList();
    }

    display->UpdateStatusBar(true);
}

void Application::Run() {
    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }

            // =================================================================================
            // [UI STATE SUPERVISOR]
            // Quản lý hiển thị Widget/Overlay dựa trên trạng thái Báo thức và Nhạc
            // =================================================================================
            if (GetDeviceState() == kDeviceStateIdle) {
                
                // 1. Kiểm tra Báo thức (Ưu tiên cao nhất)
                bool is_alarm_active = (g_alarm_manager && g_alarm_manager->IsRinging());

                // 2. Kiểm tra Nhạc/Radio/SD (Ưu tiên nhì)
                // Sử dụng g_last_audio_data_time để phát hiện chung cho cả 3 nguồn
                // Nếu có gói tin âm thanh trong 1.5 giây qua -> coi như đang phát
                int64_t now_us = esp_timer_get_time();
                bool is_media_active = (now_us - g_last_audio_data_time) < 1500000; // 1.5s tolerance

                // 3. Quyết định hiển thị
                if (is_alarm_active) {
                    // Báo thức đang reo: Ẩn Widget, Xóa thông tin nhạc (ưu tiên hiển thị thông báo báo thức)
                    display->HideWeatherWidget();
                    display->HideLunarWidget();
                    display->SetMusicInfo(""); 
                } 
                else if (is_media_active) {
                    // Nhạc đang phát: Ẩn Widget (nhường chỗ cho Music Info/Spectrum)
                    display->HideWeatherWidget();
                    display->HideLunarWidget();
                } 
                else {
                    // Không báo thức, không nhạc: Hiển thị Widget thời tiết/âm lịch
                    bool weather_ready = network_connected_ && (weather_service_ != nullptr);
                    
                    if (!weather_ready) {
                        // Offline: Chỉ hiện Âm lịch
                        display->ShowLunarWidget();
                        display->HideWeatherWidget();
                    } else {
                        // Online: Xoay vòng dựa trên timer
                        if (idle_overlay_show_weather_) {
                            display->ShowWeatherWidget();
                            display->HideLunarWidget();
                        } else {
                            display->ShowLunarWidget();
                            display->HideWeatherWidget();
                        }
                    }
                }
            }
            // =================================================================================
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    network_connected_ = true;
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);

    StartWeatherSubsystemIfReady();
}

void Application::HandleNetworkDisconnectedEvent() {
    network_connected_ = false;
    StopWeatherSubsystem();

    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->InitLunarWidget();

            const bool is_alarm_active = (g_alarm_manager && g_alarm_manager->IsRinging());
            const int64_t now_us = esp_timer_get_time();
            const bool is_media_active = (now_us - g_last_audio_data_time) < 1500000; // 1.5s tolerance

            if (is_alarm_active) {
                // Báo thức đang reo: ẩn tất cả phần overlay trên
                display->HideWeatherWidget();
                display->HideLunarWidget();
                display->SetMusicInfo("");
            } else if (is_media_active) {
                // Đang phát nhạc/radio/sd: ẩn Âm lịch và Thời tiết
                display->HideWeatherWidget();
                display->HideLunarWidget();
            } else {
                // Offline + không phát nhạc: chỉ hiện Âm lịch
                display->ShowLunarWidget();
                display->HideWeatherWidget();
                idle_overlay_show_weather_ = false;
            }
        }
    }

    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);

    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    StartWeatherSubsystemIfReady();
}

void Application::ActivationTask() {
    ota_ = std::make_unique<Ota>();
    CheckAssetsVersion();
    CheckNewVersion();
    InitializeProtocol();
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    if (assets_version_checked_) return;
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10;

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) break;
            }
            retry_delay *= 2;
            continue;
        }
        retry_count = 0;
        retry_delay = 10;

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return;
            }
        }

        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) break;
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                // cJSON_PrintUnformatted allocates; 반드시 free 해야 메모리 leak을 방지할 수 있음.
                char* tmp = cJSON_PrintUnformatted(payload);
                std::string payload_str = tmp ? tmp : "";
                if (tmp) {
                    cJSON_free(tmp);
                }
                Schedule([this, display, payload_str = std::move(payload_str)]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) return;

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        play_popup_on_listening_ = true;
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            if (!audio_service_.IsAudioProcessorRunning()) {
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }

            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);
            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            break;
    }
}


void Application::OnStateChanged(DeviceState old_state, DeviceState new_state) {
    if (old_state == kDeviceStateIdle && new_state != kDeviceStateIdle) {
        auto& board = Board::GetInstance();

        if (auto music = board.GetMusic()) {
            ESP_LOGI(TAG, "Stopping music streaming due to state change");
            music->StopStreaming();
        }

        if (auto radio = board.GetRadio()) {
            ESP_LOGI(TAG, "Stopping radio streaming due to state change");
            radio->Stop();
        }

        if (auto sd = board.GetSdMusic()) {
            ESP_LOGI(TAG, "Stopping SD music due to state change");
            sd->stop();
        }

        if (auto display = board.GetDisplay()) {
            display->ClearQRCode();
        }

        Schedule([this]() {
            StopIdleOverlayRotation();
            if (auto display = Board::GetInstance().GetDisplay()) {
                display->HideWeatherWidget();
                display->HideLunarWidget();
            }
        });
    }

    if (new_state == kDeviceStateIdle && old_state != kDeviceStateIdle) {
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            if (!display) return;

            display->InitLunarWidget();
            StartWeatherSubsystemIfReady();

            const bool is_alarm_active = (g_alarm_manager && g_alarm_manager->IsRinging());
            const int64_t now_us = esp_timer_get_time();
            const bool is_media_active = (now_us - g_last_audio_data_time) < 1500000; // 1.5s tolerance

            if (is_alarm_active) {
                display->HideWeatherWidget();
                display->HideLunarWidget();
                display->SetMusicInfo("");
                return;
            }

            if (is_media_active) {
                display->HideWeatherWidget();
                display->HideLunarWidget();
                return;
            }

            const bool weather_ready = network_connected_ && (weather_service_ != nullptr);
            if (weather_ready) {
                // Online: mặc định hiện thời tiết trước, sau đó timer xoay vòng
                idle_overlay_show_weather_ = true;
                display->ShowWeatherWidget();
                display->HideLunarWidget();
                StartIdleOverlayRotation();
            } else {
                // Offline: chỉ hiện âm lịch, không cần xoay vòng
                idle_overlay_show_weather_ = false;
                display->ShowLunarWidget();
                display->HideWeatherWidget();
                StopIdleOverlayRotation();
            }
        });
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::StartWeatherSubsystemIfReady() {
    if (!network_connected_) return;

    auto state = GetDeviceState();
    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring || state == kDeviceStateActivating ||
        state == kDeviceStateUpgrading) {
        return;
    }

    if (!weather_service_) {
        weather_service_ = std::make_unique<WeatherService>();
        weather_service_->OnWeatherUpdated([this](const WeatherData&) {
            Schedule([this]() {
                if (auto display = Board::GetInstance().GetDisplay()) {
                    display->UpdateWeatherWidget();
                }
            });
        });
        weather_service_->OnError([this](const std::string& err) {
            Schedule([this, err]() {
                ESP_LOGW(TAG, "Weather error: %s", err.c_str());
                if (auto display = Board::GetInstance().GetDisplay()) {
                    if (GetDeviceState() == kDeviceStateIdle) {
                        display->ShowNotification(err.c_str(), 3000);
                    }
                }
            });
        });

        if (auto display = Board::GetInstance().GetDisplay()) {
            display->InitWeatherWidget(weather_service_.get());
        }
    }

    weather_service_->Start();

    if (auto display = Board::GetInstance().GetDisplay()) {
        if (GetDeviceState() == kDeviceStateIdle) {
            const bool is_alarm_active = (g_alarm_manager && g_alarm_manager->IsRinging());
            const int64_t now_us = esp_timer_get_time();
            const bool is_media_active = (now_us - g_last_audio_data_time) < 1500000; // 1.5s tolerance

            if (is_alarm_active) {
                // Báo thức đang reo: ẩn tất cả phần overlay trên
                display->HideWeatherWidget();
                display->HideLunarWidget();
                display->SetMusicInfo("");
            } else if (is_media_active) {
                // Đang phát nhạc/radio/sd: ẩn Âm lịch và Thời tiết
                display->HideWeatherWidget();
                display->HideLunarWidget();
            } else {
                // Không báo thức, không phát nhạc: hiển thị theo cơ chế xoay vòng
                if (idle_overlay_show_weather_) {
                    display->ShowWeatherWidget();
                    display->HideLunarWidget();
                } else {
                    display->ShowLunarWidget();
                    display->HideWeatherWidget();
                }
            }
        } else {
            display->HideWeatherWidget();
        }
    }
}

void Application::StopWeatherSubsystem() {
    if (auto display = Board::GetInstance().GetDisplay()) {
        display->HideWeatherWidget();
    }
    if (weather_service_) {
        weather_service_->Stop();
    }
}

void Application::StartIdleOverlayRotation() {
    if (idle_overlay_timer_ == nullptr) {
        const esp_timer_create_args_t args = {
            .callback = &Application::IdleOverlayTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "idle_overlay",
            .skip_unhandled_events = true
        };
        esp_err_t err = esp_timer_create(&args, &idle_overlay_timer_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create idle overlay timer: %d", (int)err);
            idle_overlay_timer_ = nullptr;
            return;
        }
    }

	esp_timer_stop(idle_overlay_timer_);
	esp_timer_start_periodic(idle_overlay_timer_, 3ULL * 60ULL * 1000000ULL);
}

void Application::StopIdleOverlayRotation() {
    if (idle_overlay_timer_ != nullptr) {
        esp_timer_stop(idle_overlay_timer_);
    }
}

void Application::IdleOverlayTimerCallback(void* arg) {
    auto* app = static_cast<Application*>(arg);
    if (!app) return;
    app->idle_overlay_show_weather_ = !app->idle_overlay_show_weather_;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        audio_service_.Start();
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) return;

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        play_popup_on_listening_ = true;
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() { if (protocol_) protocol_->CloseAudioChannel(); });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) return false;
    if (protocol_ && protocol_->IsAudioChannelOpened()) return false;
    if (!audio_service_.IsIdle()) return false;
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload = std::move(payload)]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::AddAudioData(AudioStreamPacket&& packet) {
    // ---------------------------------------------------------
    // TRACK AUDIO ACTIVITY FOR UI SUPERVISOR
    // ---------------------------------------------------------
    // Cập nhật thời gian nhận gói tin âm thanh cuối cùng
    // Tất cả các nguồn phát (Music, Radio, SD) đều gọi hàm này
    const int64_t now_us_for_media = esp_timer_get_time();
    const bool media_was_inactive = (g_last_audio_data_time <= 0) || ((now_us_for_media - g_last_audio_data_time) >= 1500000); // 1.5s tolerance
    g_last_audio_data_time = now_us_for_media;

    // Nếu vừa bắt đầu phát (từ trạng thái im lặng), ẩn Âm lịch/Thời tiết ngay lập tức để tránh nháy
    if (media_was_inactive) {
        const bool is_alarm_active = (g_alarm_manager && g_alarm_manager->IsRinging());
        if (!is_alarm_active && GetDeviceState() == kDeviceStateIdle) {
            Schedule([this]() {
                if (GetDeviceState() != kDeviceStateIdle) return;
                if (g_alarm_manager && g_alarm_manager->IsRinging()) return;
                auto display = Board::GetInstance().GetDisplay();
                if (!display) return;
                display->HideWeatherWidget();
                display->HideLunarWidget();
            });
        }
    }
    // ---------------------------------------------------------

    auto codec = Board::GetInstance().GetAudioCodec();

    if (GetDeviceState() == kDeviceStateIdle && codec->output_enabled()) {

        static int64_t last_ps_fix_us = 0;
        const int64_t now_us = esp_timer_get_time();

        if ((now_us - last_ps_fix_us) > 200000) { 
            wifi_ps_type_t ps;
            if (esp_wifi_get_ps(&ps) == ESP_OK) {
                if (ps != WIFI_PS_NONE) {
                    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
                    last_ps_fix_us = now_us;
                }
            }
        }

        if (packet.payload.size() >= sizeof(int16_t)) {
            size_t num_samples = packet.payload.size() / sizeof(int16_t);
            std::vector<int16_t> pcm_data(num_samples);
            memcpy(pcm_data.data(), packet.payload.data(), num_samples * sizeof(int16_t));

            if (packet.sample_rate != codec->output_sample_rate()) {
                if (packet.sample_rate <= 0 || codec->output_sample_rate() <= 0) return;

                std::vector<int16_t> resampled;
                if (packet.sample_rate > codec->output_sample_rate()) {
                    if (codec->SetOutputSampleRate(packet.sample_rate)) {
                    }
                } else {
                    float upsample_ratio = codec->output_sample_rate() / static_cast<float>(packet.sample_rate);
                    size_t expected_size = static_cast<size_t>(pcm_data.size() * upsample_ratio + 0.5f);
                    resampled.reserve(expected_size);

                    for (size_t i = 0; i < pcm_data.size(); ++i) {
                        resampled.push_back(pcm_data[i]);
                        int interpolation_count = static_cast<int>(upsample_ratio) - 1;
                        if (interpolation_count > 0 && i + 1 < pcm_data.size()) {
                            int16_t current = pcm_data[i];
                            int16_t next = pcm_data[i + 1];
                            for (int j = 1; j <= interpolation_count; ++j) {
                                float t = static_cast<float>(j) / (interpolation_count + 1);
                                int16_t interpolated = static_cast<int16_t>(current + (next - current) * t);
                                resampled.push_back(interpolated);
                            }
                        } else if (interpolation_count > 0) {
                            for (int j = 1; j <= interpolation_count; ++j) {
                                resampled.push_back(pcm_data[i]);
                            }
                        }
                    }
                    if (!resampled.empty()) pcm_data = std::move(resampled);
                }
            }

            if (!codec->output_enabled()) codec->EnableOutput(true);
            codec->OutputData(pcm_data);
            audio_service_.UpdateOutputTimestamp();
        }
    }
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::ResetProtocol() {
    Schedule([this]() {
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        protocol_.reset();
    });
}
