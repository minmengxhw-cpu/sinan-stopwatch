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
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_random.h>
#include <sys/time.h>
#include <hal/hal.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include "photo_store.h"

#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <algorithm>
#include <cstdlib>
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

/*
 * 只保留最后一段路径，且只允许安全字符。
 * 拒绝 ".."、绝对路径、任何分隔符 —— 这条链路会写文件系统，
 * 而路径是从线上来的。
 */
std::string sanitize_name(const std::string& raw)
{
    const size_t slash = raw.find_last_of("/\\");
    std::string n = (slash == std::string::npos) ? raw : raw.substr(slash + 1);
    if (n.empty() || n == "." || n == "..") return "";
    if (n.size() > 48) return "";
    for (char ch : n) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) return "";
    }
    return n;
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
    // 全程在一把锁里原地改。早期版本是"读快照 → 改 → 写回"，
    // 那是跨线程读改写：UI 线程刚清掉 prompt，这里就拿着批准前的
    // 旧副本写回去，prompt 会诈尸，严重时同一个请求被批两次
    bool is_new_prompt = false;
    State::get().mutate([&](Snapshot& snap) {
    BleState& s = snap.ble;
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
        if (id.empty()) {
            s.has_prompt = false;
        } else if (id == s.settled_id) {
            // 已经批过/拒过了，桌面端还没跟上。吃掉这一拍陈旧心跳
            s.has_prompt = false;
        } else {
            if (id != s.prompt_id) {
                s.prompt_since = GetHAL().millis();
                is_new_prompt = true;   // 震动放到锁外，别在临界区里碰硬件
            }
            s.has_prompt  = true;
            s.prompt_id   = id;
            s.prompt_tool = p["tool"] | "";
            s.prompt_hint = p["hint"] | "";
        }
    } else {
        s.has_prompt = false;
        s.prompt_id.clear();
        s.prompt_tool.clear();
        s.prompt_hint.clear();
        s.settled_id.clear();   // 桌面端已经不带 prompt 了，去重记录可以清了
    }
    });

    if (is_new_prompt) GetHAL().vibrate(180, 100);
}

void handle_command(JsonDocument& doc)
{
    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if (std::strcmp(cmd, "status") == 0) {
        send_status_ack();
    } else if (std::strcmp(cmd, "name") == 0) {
        // 广播名必须以 Claude 开头，否则桌面端的设备选择器过滤不到。
        // 直接照抄用户给的名字会把设备改到再也扫不出来
        const char* n = doc["name"] | "";
        // s_name 是 24 字节。超了会被 snprintf 截断成一个丑名字挂在广播上，
        // 不如直接拒绝，让桌面端知道
        if (!n || !*n) {
            send_ack("name", false, 0, "empty");
        } else if (std::strlen(n) > sizeof(s_name) - 8) {
            send_ack("name", false, 0, "too long");
        } else if (std::strncmp(n, "Claude", 6) == 0) {
            std::snprintf(s_name, sizeof(s_name), "%s", n);
            ble_svc_gap_device_name_set(s_name);
            send_ack("name", true);
        } else {
            std::snprintf(s_name, sizeof(s_name), "Claude-%s", n);
            ble_svc_gap_device_name_set(s_name);
            send_ack("name", true);
        }
    } else if (std::strcmp(cmd, "owner") == 0) {
        const std::string owner = doc["name"] | "";
        State::get().mutate([&](Snapshot& s) { s.ble.owner = owner; });
        send_ack("owner", true);
    } else if (std::strcmp(cmd, "unpair") == 0) {
        ble_store_clear();
        send_ack("unpair", true);
    } else if (std::strcmp(cmd, "char_begin") == 0) {
        // 文件夹名（或 manifest.json 里的 name）决定落盘目录。
        // 团团的照片走 "tuan" -> /spiflash/tuan
        if (s_xfer.fp) fclose(s_xfer.fp);
        s_xfer = XferCtx{};
        std::string name = sanitize_name(doc["name"] | "misc");
        if (name.empty()) name = "misc";
        s_xfer.dir = "/spiflash/" + name;
        mkdir(s_xfer.dir.c_str(), 0775);
        wipe_dir(s_xfer.dir);
        s_xfer.active = true;
        send_ack("char_begin", true);
    } else if (std::strcmp(cmd, "file") == 0) {
        if (s_xfer.fp) fclose(s_xfer.fp);
        // 只收 basename。对端是配过对的，但路径直接拼进 fopen 是白送的
        // 目录穿越，`../../` 就能写到分区里任何地方
        const std::string safe = sanitize_name(doc["path"] | "");
        s_xfer.fp = (s_xfer.active && !safe.empty())
                        ? fopen((s_xfer.dir + "/" + safe).c_str(), "wb")
                        : nullptr;
        s_xfer.written = 0;
        send_ack("file", s_xfer.fp != nullptr, 0, s_xfer.fp ? nullptr : "bad path");
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
        GetHAL().vibrate(120, 90);
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
        /*
         * 桌面端授时：[epoch 秒, 时区偏移秒]。比 NTP 靠谱，它不需要联外网。
         *
         * **第二项不能丢。** 早期版本只取了 epoch，系统时间被设成 UTC 而时区
         * 从没设过 —— 在上海，望页会显示比实际早 8 小时的时间，而且看起来
         * 一切正常（数字在动、秒针在跑），只是不对。
         */
        const int64_t epoch = doc["time"][0] | 0;
        if (epoch > 0) {
            timeval tv{static_cast<time_t>(epoch), 0};
            settimeofday(&tv, nullptr);
        }
        if (doc["time"].size() > 1) {
            const int off = doc["time"][1] | 0;   // 东八区是 +28800
            // POSIX TZ 的符号是反的：UTC+8 要写成 "UTC-8"
            const int inv = -off;
            char tz[24];
            if (inv % 3600 == 0) {
                std::snprintf(tz, sizeof(tz), "UTC%+d", inv / 3600);
            } else {
                std::snprintf(tz, sizeof(tz), "UTC%+d:%02d", inv / 3600,
                              std::abs(inv % 3600) / 60);
            }
            GetHAL().setTimezone(tz);
            ESP_LOGI(TAG, "tz offset %ds -> %s", off, tz);
        }
        if (epoch > 0) GetHAL().syncSystemTimeToRtc();
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
        .flags       = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
        .val_handle  = &s_tx_attr,
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
                State::get().mutate([](Snapshot& s) {
                    s.ble.connected = true;
                    s.ble.last_beat = GetHAL().millis();
                });
                GetHAL().vibrate(60, 60);
            } else {
                advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            s_rx_buf.clear();
            State::get().mutate([](Snapshot& s) {
                s.ble.connected  = false;
                s.ble.has_prompt = false;
                s.ble.passkey    = 0;
                s.ble.prompt_id.clear();
            });
            advertise();
            break;
        }

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            /*
             * DisplayOnly：设备显示 6 位码，用户在 macOS 弹窗里输入。
             * 只打到串口是不行的 —— 桌面终端平时没接串口，
             * 码看不见就配不上对，守整条线是死的。写进 State 交给守去画。
             */
            if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
                ble_sm_io io{};
                io.action  = BLE_SM_IOACT_DISP;
                // State 用 0 表示“不在配对”。同时固定为真正的六位数，避免
                // 随机出 000000 时配对页被误判成无需显示。
                io.passkey = 100000 + (esp_random() % 900000);
                State::get().mutate([&](Snapshot& s) { s.ble.passkey = io.passkey; });
                ESP_LOGI(TAG, "passkey %06u", static_cast<unsigned>(io.passkey));
                GetHAL().vibrate(80, 90);
                ble_sm_inject_io(ev->passkey.conn_handle, &io);
            }
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            // 配对有结果了，码可以下屏
            State::get().mutate([](Snapshot& s) { s.ble.passkey = 0; });
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
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name  = reinterpret_cast<const uint8_t*>(s_name);
    fields.name_len = std::strlen(s_name);
    fields.name_is_complete = 1;
    fields.uuids128 = const_cast<ble_uuid128_t*>(&kSvcUuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

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

    // 记账 + 清 prompt + 记下去重 id，全在一把锁里完成
    State::get().decide(id, d == Decision::Once);
    return true;
}

}  // namespace sinan::ble
