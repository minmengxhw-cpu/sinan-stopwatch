/*
 * bridge_ble.cpp
 *
 * Nordic UART Service 上的换行分隔 JSON。两个容易踩的点：
 *  1. notify 会在 MTU 边界分片，所以接收端必须自己累积到 '\n' 才解析；
 *  2. 链路上会流过 transcript 片段和工具调用提示，所以特征必须要求加密，
 *     否则附近任何一根 nRF dongle 都能把你的会话内容抓下来。
 */
#include "bridge_ble.h"
#include "state.h"
#include "config.h"
#include "haptics.h"
#if SN_HID_ENABLE
#include "bridge_hid.h"
#endif
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_random.h>
#include <hal/hal.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "photo_store.h"

#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <cstring>
#include <string>

namespace sinan::ble {

namespace {

constexpr char TAG[] = "sinan.ble";

/* --------------------------- NUS UUID --------------------------- */
/* 6e400001/2/3-b5a3-f393-e0a9-e50e24dcca9e，低字节在前 */

constexpr ble_uuid128_t kSvcUuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
constexpr ble_uuid128_t kRxUuid =  // 桌面 → 设备
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
constexpr ble_uuid128_t kTxUuid =  // 设备 → 桌面
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

uint16_t s_conn    = BLE_HS_CONN_HANDLE_NONE;
uint16_t s_tx_attr = 0;
uint8_t s_addr_type = 0;
char s_name[24] = "Claude-SINAN";
std::string s_rx_buf;          // 行缓冲：累积到 '\n' 再解析
uint32_t s_boot_ms = 0;

/* 文件夹推送的接收上下文。协议是严格串行的，所以不需要缓冲整个文件 */
struct XferCtx {
    bool active = false;
    std::string dir;
    FILE* fp = nullptr;
    size_t written = 0;
};

// 校验单级路径片段：拒绝空串、'/'、'\\'、以及 "." "..",
// 防止桌面端传来的 name/path 借 ".." 跳出预期目录（任意文件读写/清空）
bool is_safe_component(const std::string& s)
{
    if (s.empty() || s == "." || s == "..") return false;
    return s.find('/') == std::string::npos && s.find('\\') == std::string::npos;
}

// 收到新一批照片前先清空旧的，否则 4MB 分区几批就满了
void wipe_dir(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::remove((dir + "/" + e->d_name).c_str());
    }
    closedir(d);
}
XferCtx s_xfer;

void advertise();

/* --------------------------- 发送 --------------------------- */

bool send_line(const std::string& line)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || s_tx_attr == 0) return false;
    std::string out = line;
    if (out.empty() || out.back() != '\n') out.push_back('\n');

    // 按 MTU 切片。协议允许分片，桌面端会自己重组
    const uint16_t mtu = ble_att_mtu(s_conn);
    const size_t chunk = (mtu > 3) ? static_cast<size_t>(mtu - 3) : 20;
    for (size_t off = 0; off < out.size(); off += chunk) {
        const size_t n = std::min(chunk, out.size() - off);
        os_mbuf* om = ble_hs_mbuf_from_flat(out.data() + off, n);
        if (!om) return false;
        if (ble_gatts_notify_custom(s_conn, s_tx_attr, om) != 0) return false;
    }
    return true;
}

void send_ack(const char* cmd, bool ok, uint32_t n = 0, const char* err = nullptr)
{
    JsonDocument doc;
    doc["ack"] = cmd;
    doc["ok"]  = ok;
    doc["n"]   = n;
    if (err) doc["error"] = err;
    std::string out;
    serializeJson(doc, out);
    send_line(out);
}

void send_status_ack()
{
    auto snap = State::get().snapshot();

    JsonDocument doc;
    doc["ack"] = "status";
    doc["ok"]  = true;
    JsonObject d = doc["data"].to<JsonObject>();
    d["name"] = s_name;
    d["sec"]  = true;

    JsonObject bat = d["bat"].to<JsonObject>();
    bat["pct"] = GetHAL().getBatteryLevel();
    bat["usb"] = GetHAL().isBatteryCharging();
    // mA 为负表示在充电，这是协议约定
    bat["mA"] = GetHAL().isBatteryCharging() ? -120 : 0;

    JsonObject sys = d["sys"].to<JsonObject>();
    sys["up"]   = (GetHAL().millis() - s_boot_ms) / 1000;
    sys["heap"] = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    JsonObject st = d["stats"].to<JsonObject>();
    st["appr"] = snap.tally.approved;
    st["deny"] = snap.tally.denied;

    std::string out;
    serializeJson(doc, out);
    send_line(out);
}

/* --------------------------- 接收：解析一行 --------------------------- */

void handle_heartbeat(JsonDocument& doc)
{
    BleState s = State::get().snapshot().ble;
    s.connected    = true;
    s.last_beat    = GetHAL().millis();
    s.total        = doc["total"] | 0;
    s.running      = doc["running"] | 0;
    s.waiting      = doc["waiting"] | 0;
    s.tokens_today = doc["tokens_today"] | 0u;
    s.msg          = doc["msg"] | "";

    for (auto& e : s.entries) e.clear();
    if (doc["entries"].is<JsonArray>()) {
        int i = 0;
        for (JsonVariant v : doc["entries"].as<JsonArray>()) {
            if (i >= static_cast<int>(s.entries.size())) break;
            s.entries[i++] = v.as<const char*>() ? v.as<const char*>() : "";
        }
    }

    // prompt 只在需要决策时存在。它消失就意味着决策已在别处完成
    if (doc["prompt"].is<JsonObject>()) {
        JsonObject p = doc["prompt"];
        const std::string id = p["id"] | "";
        if (!id.empty() && id != s.prompt_id) {
            s.prompt_since = GetHAL().millis();
            // 新请求到达才提醒，同一请求的重复心跳不再打扰
            haptics::play(haptics::Cue::Alert);
        }
        s.has_prompt  = !id.empty();
        s.prompt_id   = id;
        s.prompt_tool = p["tool"] | "";
        s.prompt_hint = p["hint"] | "";
    } else {
        s.has_prompt = false;
        s.prompt_id.clear();
        s.prompt_tool.clear();
        s.prompt_hint.clear();
    }

    State::get().setBle(s);
}

void handle_command(JsonDocument& doc)
{
    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if (std::strcmp(cmd, "status") == 0) {
        send_status_ack();
    } else if (std::strcmp(cmd, "name") == 0) {
        const char* n = doc["name"] | "";
        std::snprintf(s_name, sizeof(s_name), "%s", n);
        send_ack("name", true);
    } else if (std::strcmp(cmd, "owner") == 0) {
        BleState s = State::get().snapshot().ble;
        s.owner = doc["name"] | "";
        State::get().setBle(s);
        send_ack("owner", true);
    } else if (std::strcmp(cmd, "unpair") == 0) {
        ble_store_clear();
        send_ack("unpair", true);
    } else if (std::strcmp(cmd, "char_begin") == 0) {
        // 文件夹名（或 manifest.json 里的 name）决定落盘目录。
        // 团团的照片走 "tuan" -> /spiflash/tuan
        if (s_xfer.fp) fclose(s_xfer.fp);
        s_xfer = XferCtx{};
        const std::string name = doc["name"] | "misc";
        if (!is_safe_component(name)) {
            ESP_LOGW(TAG, "char_begin: unsafe name rejected");
            send_ack("char_begin", false);
            return;
        }
        s_xfer.dir = "/spiflash/" + name;
        mkdir(s_xfer.dir.c_str(), 0775);
        wipe_dir(s_xfer.dir);
        s_xfer.active = true;
        send_ack("char_begin", true);
    } else if (std::strcmp(cmd, "file") == 0) {
        if (s_xfer.fp) fclose(s_xfer.fp);
        const std::string path = doc["path"] | "";
        s_xfer.fp = (s_xfer.active && is_safe_component(path))
                        ? fopen((s_xfer.dir + "/" + path).c_str(), "wb")
                        : nullptr;
        if (s_xfer.active && !is_safe_component(path)) {
            ESP_LOGW(TAG, "file: unsafe path rejected");
        }
        s_xfer.written = 0;
        send_ack("file", s_xfer.fp != nullptr);
    } else if (std::strcmp(cmd, "chunk") == 0) {
        // 协议严格串行（发一块等一个 ack），所以收到就写，不用缓冲整个文件
        const char* b64 = doc["d"] | "";
        const size_t b64len = std::strlen(b64);
        bool ok = false;
        if (s_xfer.fp && b64len) {
            std::vector<uint8_t> raw(b64len * 3 / 4 + 4);
            size_t olen = 0;
            if (mbedtls_base64_decode(raw.data(), raw.size(), &olen,
                                      reinterpret_cast<const unsigned char*>(b64), b64len) == 0) {
                ok = fwrite(raw.data(), 1, olen, s_xfer.fp) == olen;
                s_xfer.written += olen;
            }
        }
        send_ack("chunk", ok, s_xfer.written);
    } else if (std::strcmp(cmd, "file_end") == 0) {
        if (s_xfer.fp) { fclose(s_xfer.fp); s_xfer.fp = nullptr; }
        send_ack("file_end", true, s_xfer.written);
    } else if (std::strcmp(cmd, "char_end") == 0) {
        if (s_xfer.fp) { fclose(s_xfer.fp); s_xfer.fp = nullptr; }
        s_xfer.active = false;
        photo::rescan();   // 新照片立刻生效，不用重启
        send_ack("char_end", true);
        haptics::play(haptics::Cue::Ok);
    }
}

void handle_line(const std::string& line)
{
    if (line.empty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        ESP_LOGW(TAG, "bad json, %d bytes", static_cast<int>(line.size()));
        return;
    }

    if (doc["cmd"].is<const char*>()) {
        handle_command(doc);
        return;
    }
    if (doc["time"].is<JsonArray>()) {
        // 桌面端授时：epoch 秒 + 时区偏移秒。比 NTP 靠谱，因为它不需要联外网
        const int64_t epoch = doc["time"][0] | 0;
        if (epoch > 0) {
            timeval tv{static_cast<time_t>(epoch), 0};
            settimeofday(&tv, nullptr);
            GetHAL().syncSystemTimeToRtc();
        }
        return;
    }
    if (doc["evt"].is<const char*>()) {
        // turn 事件暂时不入 UI，留给后续做转写滚动条
        return;
    }
    handle_heartbeat(doc);
}

/* --------------------------- GATT 回调 --------------------------- */

int chr_access(uint16_t conn, uint16_t attr, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) return 0;
    std::string in(len, '\0');
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, in.data(), len, &copied) != 0) return BLE_ATT_ERR_UNLIKELY;
    in.resize(copied);

    s_rx_buf += in;
    // 累积到换行才成句。桌面端一次 write 可能只带半行
    size_t nl;
    while ((nl = s_rx_buf.find('\n')) != std::string::npos) {
        handle_line(s_rx_buf.substr(0, nl));
        s_rx_buf.erase(0, nl + 1);
    }
    // 防御：对端异常时别让缓冲无限涨
    if (s_rx_buf.size() > 8192) s_rx_buf.clear();

    (void)conn;
    (void)attr;
    return 0;
}

const ble_gatt_chr_def kChrs[] = {
    {
        .uuid       = &kRxUuid.u,
        .access_cb  = chr_access,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid        = &kTxUuid.u,
        .access_cb   = chr_access,
        .val_handle  = &s_tx_attr,
        .flags       = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
    },
    {0},
};

const ble_gatt_svc_def kSvcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &kSvcUuid.u,
        .characteristics = kChrs,
    },
    {0},
};

/* --------------------------- GAP --------------------------- */

int gap_event(ble_gap_event* ev, void*)
{
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                s_conn = ev->connect.conn_handle;
                // 先要求加密再放行数据。特征已标 ENC，这一步只是主动发起
                ble_gap_security_initiate(s_conn);
                BleState s = State::get().snapshot().ble;
                s.connected = true;
                s.last_beat = GetHAL().millis();
                State::get().setBle(s);
                State::get().clearPasskey();
                haptics::play(haptics::Cue::Pair);
            } else {
                State::get().clearPasskey();
                advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            s_rx_buf.clear();
            BleState s = State::get().snapshot().ble;
            s.connected  = false;
            s.has_prompt = false;
            s.prompt_id.clear();
            State::get().setBle(s);
            State::get().clearPasskey();
            advertise();
            break;
        }

        case BLE_GAP_EVENT_ENC_CHANGE:
            // 加密链路建立（含配对完成），配对码页该收了
            if (ev->enc_change.status == 0) State::get().clearPasskey();
            break;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            // DisplayOnly：设备显示 6 位码，用户在 macOS 弹窗里输入。
            // 码必须上屏 —— 只打 log 的话这条链路永远配不上
            if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
                ble_sm_io io{};
                io.action = BLE_SM_IOACT_DISP;
                io.passkey = esp_random() % 1000000;
                ESP_LOGI(TAG, "passkey %06u", static_cast<unsigned>(io.passkey));
                State::get().setPasskey(io.passkey);
                ble_sm_inject_io(ev->passkey.conn_handle, &io);
            }
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;

        default:
            break;
    }
    return 0;
}

void advertise()
{
    // 广播包 31 字节的预算分配：flags + 名字 + HID UUID16（让 macOS 认出键盘），
    // 放不下的 NUS 128 位 UUID 与键盘外观挪进扫描响应
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name  = reinterpret_cast<const uint8_t*>(s_name);
    fields.name_len = std::strlen(s_name);
    fields.name_is_complete = 1;
#if SN_HID_ENABLE
    static const ble_uuid16_t kHidUuid = BLE_UUID16_INIT(0x1812);
    fields.uuids16 = const_cast<ble_uuid16_t*>(&kHidUuid);
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
#else
    fields.uuids128 = const_cast<ble_uuid128_t*>(&kSvcUuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
#endif
    ble_gap_adv_set_fields(&fields);

#if SN_HID_ENABLE
    ble_hs_adv_fields rsp{};
    rsp.uuids128 = const_cast<ble_uuid128_t*>(&kSvcUuid);
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rsp.appearance = 0x03C1;  // Generic HID > Keyboard
    rsp.appearance_is_present = 1;
    ble_gap_adv_rsp_set_fields(&rsp);
#endif

    ble_gap_adv_params adv{};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(s_addr_type, nullptr, BLE_HS_FOREVER, &adv, gap_event, nullptr);
    ESP_LOGI(TAG, "advertising as %s", s_name);
}

void on_sync()
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_addr_type);
    advertise();
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

void start()
{
    s_boot_ms = GetHAL().millis();

    // 广播名必须以 Claude 开头，否则桌面端的设备选择器过滤不到。
    // 后缀取 MAC 末两字节，多台设备并存时才分得清
    auto mac = GetHAL().getFactoryMac();
    std::snprintf(s_name, sizeof(s_name), "Claude-SINAN-%02X%02X", mac[4], mac[5]);

    nimble_port_init();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // LE Secure Connections + 绑定。链路上有会话内容，明文不可接受
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm    = 1;
    ble_hs_cfg.sm_sc      = 1;
    ble_hs_cfg.sm_io_cap  = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(kSvcs);
    ble_gatts_add_svcs(kSvcs);
#if SN_HID_ENABLE
    // 单连接多服务：同一条加密链路上，系统看见键盘，Buddy 仍走 NUS
    ble_gatts_count_cfg(sinan::hid::gatt_svcs());
    ble_gatts_add_svcs(sinan::hid::gatt_svcs());
#endif
    ble_svc_gap_device_name_set(s_name);

    nimble_port_freertos_init(host_task);
}

bool connected() { return s_conn != BLE_HS_CONN_HANDLE_NONE; }

const char* device_name() { return s_name; }

bool send_permission(const std::string& id, Decision d)
{
    if (id.empty()) return false;

    // 再校验一次。UI 显示的和即将回传的必须是同一个请求，
    // 否则就是在替另一个还没看过的请求做决定
    auto snap = State::get().snapshot();
    if (!snap.ble.has_prompt || snap.ble.prompt_id != id) {
        ESP_LOGW(TAG, "permission id mismatch, dropped");
        return false;
    }

    JsonDocument doc;
    doc["cmd"]      = "permission";
    doc["id"]       = id;
    doc["decision"] = (d == Decision::Once) ? "once" : "deny";
    std::string out;
    serializeJson(doc, out);

    if (!send_line(out)) return false;

    if (d == Decision::Once) State::get().bumpApproved();
    else State::get().bumpDenied();
    State::get().clearPrompt();
    return true;
}

}  // namespace sinan::ble
