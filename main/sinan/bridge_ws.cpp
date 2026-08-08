/*
 * bridge_ws.cpp
 *
 * 断线不弹窗。桌面仪表出现错误弹窗是设计事故 ——
 * 数据陈旧就把 Rim 转成靛青，人看得懂，不用打断他。
 */
#include "bridge_ws.h"
#include "state.h"
#include <ArduinoJson.h>
#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <hal/hal.h>
#include <mbedtls/base64.h>
#include <nvs_flash.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sinan::ws {

namespace {

constexpr char TAG[] = "sinan.ws";
// 一帧 base64 音频的硬顶，约 1.4MB 原始 PCM ≈ 45 秒 16k 单声道
constexpr size_t kMaxPcmB64 = 1900 * 1024;
constexpr size_t kMaxFrame  = 2 * 1024 * 1024;
esp_websocket_client_handle_t s_client = nullptr;
bool s_wifi_up = false;
uint32_t s_last_start = 0;   // 上次调 start 的时刻，用于给握手留出窗口
std::string s_buf;
std::string s_headers;

WorkerState parse_state(const char* s)
{
    if (!s) return WorkerState::Down;
    if (std::strcmp(s, "run") == 0)   return WorkerState::Run;
    if (std::strcmp(s, "idle") == 0)  return WorkerState::Idle;
    if (std::strcmp(s, "stall") == 0) return WorkerState::Stall;
    return WorkerState::Down;
}

void handle_fleet(JsonDocument& doc)
{
    FleetState f;
    f.last_recv = GetHAL().millis();
    f.stale = false;
    int i = 0;
    for (JsonObject w : doc["workers"].as<JsonArray>()) {
        if (i >= FleetState::kMax) break;
        f.workers[i].id    = w["id"] | "";
        f.workers[i].label = w["label"] | "";
        f.workers[i].task  = w["task"] | "";
        f.workers[i].state = parse_state(w["state"]);
        f.workers[i].quota = w["quota"] | 0.0f;
        i++;
    }
    f.count = i;
    State::get().mutate([&](Snapshot& s) { s.fleet = f; });
}

void handle_almanac(JsonDocument& doc)
{
    AlmanacState a;
    a.last_recv     = GetHAL().millis();
    a.number_code   = doc["number"]["code"] | "";
    a.number_title  = doc["number"]["title"] | "";
    a.huangli_trend = doc["huangli"]["trend"] | "flat";
    a.huangli_yi    = doc["huangli"]["yi"] | "";
    a.huangli_ji    = doc["huangli"]["ji"] | "";
    if (doc["ring"].is<JsonArray>() && doc["ring"].size() > 0) {
        a.ring_doy = doc["ring"][0]["doy"] | 0;
        a.ring_tag = doc["ring"][0]["tag"] | "";
    }
    State::get().mutate([&](Snapshot& s) { s.almanac = a; });
}

/*
 * 播放队列。
 *
 * 早期版本在 WS 事件回调里同步 audioPlay ——
 * 一段几秒的语音会把整条 WebSocket 卡死，心跳丢完就断线重连。
 * 回调里只解码 + 投递，播放交给独立任务。
 */
QueueHandle_t s_play_q = nullptr;

struct PlayJob {
    std::vector<int16_t>* pcm;
};

void play_task(void*)
{
    PlayJob job;
    while (true) {
        if (xQueueReceive(s_play_q, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job.pcm) {
            GetHAL().audioPlay(*job.pcm, false);
            delete job.pcm;
        }
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
    }
}

void handle_say(JsonDocument& doc)
{
    const std::string text = doc["text"] | "";
    State::get().mutate([&](Snapshot& s) {
        s.voice.reply = text;
        s.voice.phase = VoicePhase::Speaking;
    });

    const char* b64 = doc["pcm"] | "";
    const size_t b64len = std::strlen(b64);
    if (b64len == 0) {
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
        return;
    }
    // 硬顶。超了宁可只显示文字，也不要为一段跑飞的音频把堆吃穿
    if (b64len > kMaxPcmB64) {
        ESP_LOGW(TAG, "pcm too large (%u), text only", static_cast<unsigned>(b64len));
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
        return;
    }

    std::vector<uint8_t> raw(b64len * 3 / 4 + 4);
    size_t olen = 0;
    if (mbedtls_base64_decode(raw.data(), raw.size(), &olen,
                              reinterpret_cast<const unsigned char*>(b64), b64len) != 0) {
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
        return;
    }

    auto* pcm = new std::vector<int16_t>(olen / 2);
    std::memcpy(pcm->data(), raw.data(), pcm->size() * 2);
    PlayJob job{pcm};
    if (!s_play_q || xQueueSend(s_play_q, &job, 0) != pdTRUE) {
        delete pcm;
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
    }
}

void handle_line(const std::string& line)
{
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return;
    const char* t = doc["t"] | "";

    if (std::strcmp(t, "fleet") == 0)        handle_fleet(doc);
    else if (std::strcmp(t, "almanac") == 0) handle_almanac(doc);
    else if (std::strcmp(t, "say") == 0)     handle_say(doc);
    else if (std::strcmp(t, "asr_result") == 0) {
        const std::string heard = doc["text"] | "";
        State::get().mutate([&](Snapshot& s) {
            s.voice.heard = heard;
            if (heard.empty()) {
                // 转写出来是空的：多半没说清或者环境太吵，别让 UI 干等到超时
                s.voice.phase = VoicePhase::Error;
                s.voice.note  = "didn't catch that";
            } else {
                s.voice.phase = VoicePhase::Ready;
                s.voice.note.clear();
            }
        });
    }
    else if (std::strcmp(t, "voice_status") == 0) {
        // daemon 汇报它走到哪一步了，好让弦区显示 transcribing / running
        const char* ph = doc["phase"] | "";
        State::get().mutate([&](Snapshot& s) {
            if (std::strcmp(ph, "transcribing") == 0)   s.voice.phase = VoicePhase::Transcribing;
            else if (std::strcmp(ph, "ready") == 0)     s.voice.phase = VoicePhase::Ready;
            else if (std::strcmp(ph, "running") == 0)   s.voice.phase = VoicePhase::Thinking;
            else if (std::strcmp(ph, "error") == 0) {
                s.voice.phase = VoicePhase::Error;
                s.voice.note  = doc["note"] | "failed";
            }
        });
    } else if (std::strcmp(t, "ping") == 0) {
        JsonDocument out;
        out["t"]  = "pong";
        out["ts"] = doc["ts"] | 0;
        std::string s;
        serializeJson(out, s);
        send(s);
    }
}

void ws_event(void*, esp_event_base_t, int32_t id, void* data)
{
    auto* ev = static_cast<esp_websocket_event_data_t*>(data);
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED: {
            State::get().setLink(s_wifi_up, true);
            JsonDocument hello;
            hello["t"]   = "hello";
            hello["dev"] = "sinan";
            hello["ver"] = "4.2.0";
            std::string s;
            serializeJson(hello, s);
            send(s);
            break;
        }
        case WEBSOCKET_EVENT_DISCONNECTED:
            State::get().setLink(s_wifi_up, false);
            s_buf.clear();
            break;
        case WEBSOCKET_EVENT_DATA:
            if (ev->op_code == 0x01 && ev->data_len > 0) {
                s_buf.append(ev->data_ptr, ev->data_len);
                if (ev->payload_offset + ev->data_len >= ev->payload_len) {
                    handle_line(s_buf);
                    s_buf.clear();
                }
                // 对端跑飞时别让缓冲无限涨。语音帧本来就大，顶设在 2MB
                if (s_buf.size() > kMaxFrame) {
                    ESP_LOGW(TAG, "frame over cap, dropped");
                    s_buf.clear();
                }
            }
            break;
        default:
            break;
    }
}

/*
 * 自己管重连退避。esp_websocket_client 的 reconnect_timeout_ms 是固定间隔，
 * daemon 关着的时候会每 3 秒敲一次门，一整天几万次，日志也刷没了。
 * 改成 3s 起步翻倍、上限 30s。
 */
void backoff_task(void*)
{
    constexpr uint32_t kHandshakeGrace = 9000;   // 略大于 network_timeout_ms

    uint32_t wait_ms = 3000;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!s_client || !s_wifi_up) continue;

        if (esp_websocket_client_is_connected(s_client)) {
            wait_ms = 3000;                      // 连上了，退避归零
            continue;
        }

        /*
         * 关键：握手期间不要动它。
         * 早期版本只看 is_connected()，而握手要几百毫秒到几秒 ——
         * 500ms 醒一次的任务会把每一次正在建立的连接都 stop 掉，
         * 结果是 WS 永远连不上，而日志里看着像"一直在重连"。
         */
        if (GetHAL().millis() - s_last_start < kHandshakeGrace) continue;

        esp_websocket_client_stop(s_client);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
        if (s_wifi_up) {
            s_last_start = GetHAL().millis();
            esp_websocket_client_start(s_client);
        }
        wait_ms = wait_ms * 2 > 30000 ? 30000 : wait_ms * 2;
    }
}

void wifi_event(void*, esp_event_base_t base, int32_t id, void*)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_up = false;
        State::get().setLink(false, false);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_up = true;
        State::get().setLink(true, false);
        if (s_client) {
            s_last_start = GetHAL().millis();
            esp_websocket_client_start(s_client);
        }
    }
}

}  // namespace

void start(const char* ssid, const char* pass, const char* uri, const char* token)
{
    if (nvs_flash_init() == ESP_ERR_NVS_NO_FREE_PAGES) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr, nullptr);

    wifi_config_t wc{};
    std::snprintf(reinterpret_cast<char*>(wc.sta.ssid), sizeof(wc.sta.ssid), "%s", ssid);
    std::snprintf(reinterpret_cast<char*>(wc.sta.password), sizeof(wc.sta.password), "%s", pass);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    // S3 单天线，BLE 和 WiFi 抢时隙。省电模式关掉反而让两者都稳
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_start();

    // 播放任务先起来，第一条 say 到达时队列必须已经在
    s_play_q = xQueueCreate(3, sizeof(PlayJob));
    xTaskCreatePinnedToCore(play_task, "sinan_play", 4096, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(backoff_task, "sinan_ws_bk", 3072, nullptr, 3, nullptr, 1);

    esp_websocket_client_config_t wsc{};
    wsc.uri = uri;
    s_headers = std::string("X-Sinan-Token: ") + token + "\r\n";
    wsc.headers = s_headers.c_str();
    wsc.network_timeout_ms     = 8000;
    wsc.disable_auto_reconnect = true;   // 退避自己管，见 backoff_task
    // 15 秒心跳。调更短会跟 BLE 抢时隙，守那边会开始丢心跳
    wsc.ping_interval_sec = 15;
    s_client = esp_websocket_client_init(&wsc);
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event, nullptr);
    ESP_LOGI(TAG, "ws target %s", uri);
}

bool connected()
{
    return s_client && esp_websocket_client_is_connected(s_client);
}

bool send(const std::string& json)
{
    if (!connected()) return false;
    return esp_websocket_client_send_text(s_client, json.c_str(),
                                          static_cast<int>(json.size()), portMAX_DELAY) >= 0;
}

bool send_audio_begin(const char* target)
{
    JsonDocument d;
    d["t"]    = "asr_begin";
    d["rate"] = GetHAL().getAudioSampleRate();
    d["target"] = target ? target : "";
    std::string s;
    serializeJson(d, s);
    return send(s);
}

bool send_audio_chunk(const int16_t* pcm, size_t samples, uint32_t seq)
{
    const size_t bytes = samples * 2;
    size_t b64len = 0;
    std::vector<unsigned char> b64(bytes * 4 / 3 + 8);
    if (mbedtls_base64_encode(b64.data(), b64.size(), &b64len,
                              reinterpret_cast<const unsigned char*>(pcm), bytes) != 0) {
        return false;
    }
    JsonDocument d;
    d["t"]   = "asr_chunk";
    d["seq"] = seq;
    d["pcm"] = std::string(reinterpret_cast<char*>(b64.data()), b64len);
    std::string s;
    serializeJson(d, s);
    return send(s);
}

bool send_audio_end() { return send("{\"t\":\"asr_end\"}"); }

bool send_audio_cancel() { return send("{\"t\":\"asr_cancel\"}"); }

bool send_voice(const char* target)
{
    JsonDocument d;
    d["t"] = "voice_send";
    d["target"] = target ? target : "";
    std::string s;
    serializeJson(d, s);
    return send(s);
}

bool trigger(const char* action_id)
{
    JsonDocument d;
    d["t"]  = "act";
    d["id"] = action_id;
    std::string s;
    serializeJson(d, s);
    return send(s);
}

}  // namespace sinan::ws
