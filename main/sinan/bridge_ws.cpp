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
#include <esp_netif.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <hal/hal.h>
#include <mbedtls/base64.h>
#include <nvs_flash.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sinan::ws {

namespace {

constexpr char TAG[] = "sinan.ws";
esp_websocket_client_handle_t s_client = nullptr;
bool s_wifi_up = false;
std::string s_buf;
// esp_websocket_client_config_t.headers 只存指针，缓冲区必须活得比 client 久，故用静态存储
std::string s_ws_headers;

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
    State::get().setFleet(f);
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
    State::get().setAlmanac(a);
}

void handle_say(JsonDocument& doc)
{
    VoiceState v = State::get().snapshot().voice;
    v.reply = doc["text"] | "";
    v.phase = VoicePhase::Speaking;
    State::get().setVoice(v);

    const char* b64 = doc["pcm"] | "";
    const size_t b64len = std::strlen(b64);
    if (b64len == 0) {
        v.phase = VoicePhase::Idle;
        State::get().setVoice(v);
        return;
    }

    std::vector<uint8_t> raw(b64len * 3 / 4 + 4);
    size_t olen = 0;
    if (mbedtls_base64_decode(raw.data(), raw.size(), &olen,
                              reinterpret_cast<const unsigned char*>(b64), b64len) == 0) {
        std::vector<int16_t> pcm(olen / 2);
        std::memcpy(pcm.data(), raw.data(), pcm.size() * 2);
        GetHAL().audioPlay(pcm, false);
    }
    v.phase = VoicePhase::Idle;
    State::get().setVoice(v);
}

void handle_line(const std::string& line)
{
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return;
    const char* t = doc["t"] | "";

    if (std::strcmp(t, "fleet") == 0)        handle_fleet(doc);
    else if (std::strcmp(t, "almanac") == 0) handle_almanac(doc);
    else if (std::strcmp(t, "say") == 0)     handle_say(doc);
    else if (std::strcmp(t, "event") == 0) {
        // DONE / ERROR 全屏通知。设备端去重（45s 同 key 不重复抢占）
        IrqEvent e;
        e.active   = true;
        const char* kind = doc["kind"] | "done";
        e.kind     = std::strcmp(kind, "error") == 0 ? IrqKind::Error : IrqKind::Done;
        e.id       = doc["id"] | "";
        e.title    = doc["title"] | (e.kind == IrqKind::Error ? "ERROR" : "DONE");
        e.body     = doc["body"] | "";
        e.since_ms = GetHAL().millis();
        State::get().setIrq(e);
    }
    else if (std::strcmp(t, "asr_result") == 0) {
        VoiceState v = State::get().snapshot().voice;
        v.heard = doc["text"] | "";
        v.phase = VoicePhase::Thinking;
        State::get().setVoice(v);
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
            hello["ver"] = "0.1.0";
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
                if (s_buf.size() > 32768) s_buf.clear();
            }
            break;
        default:
            break;
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
        if (s_client) esp_websocket_client_start(s_client);
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

    esp_websocket_client_config_t wsc{};
    wsc.uri                  = uri;
    wsc.reconnect_timeout_ms = 3000;
    wsc.network_timeout_ms   = 8000;
    // 15 秒心跳。调更短会跟 BLE 抢时隙，Ward 那边会开始丢心跳
    wsc.ping_interval_sec    = 15;
    // 带上配对令牌，daemon 侧同网段但没有这个 token 的设备会被拒绝握手
    if (token && token[0] != '\0') {
        s_ws_headers = std::string("X-Sinan-Token: ") + token + "\r\n";
        wsc.headers  = s_ws_headers.c_str();
    }
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

bool send_audio_begin()
{
    JsonDocument d;
    d["t"]    = "asr_begin";
    d["rate"] = GetHAL().getAudioSampleRate();
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
