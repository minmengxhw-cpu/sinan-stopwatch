/*
 * bridge_hid.cpp — HID over GATT 键盘，与 Buddy NUS 同主机同连接。
 *
 * 两个容易踩的点：
 *  1. macOS 只在广播里看到 0x1812 才把它当键盘 —— 广播包由 bridge_ble 组包，
 *     这里只提供 GATT 服务与报告发送；
 *  2. 报告必须等对端订阅（CCCD）之后才能 notify，否则头几发按键丢进空气。
 */
#include "bridge_hid.h"
#include "state.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <services/gap/ble_svc_gap.h>
#include <cstdio>
#include <cstring>
#include <utility>

namespace sinan::hid {
namespace {

constexpr char TAG[] = "sinan.hid";
constexpr char kNvsNs[] = "sinan_hid";

/* --------------------------- UUID --------------------------- */

const ble_uuid16_t kUuidSvc        = BLE_UUID16_INIT(0x1812);
const ble_uuid16_t kUuidHidInfo    = BLE_UUID16_INIT(0x2A4A);
const ble_uuid16_t kUuidReportMap  = BLE_UUID16_INIT(0x2A4B);
const ble_uuid16_t kUuidCtrlPoint  = BLE_UUID16_INIT(0x2A4C);
const ble_uuid16_t kUuidProtoMode  = BLE_UUID16_INIT(0x2A4E);
const ble_uuid16_t kUuidReport     = BLE_UUID16_INIT(0x2A4D);
const ble_uuid16_t kUuidBootIn     = BLE_UUID16_INIT(0x2A22);
const ble_uuid16_t kUuidBootOut    = BLE_UUID16_INIT(0x2A32);
const ble_uuid16_t kUuidReportRef  = BLE_UUID16_INIT(0x2908);

/* --------------------------- HID 描述子 --------------------------- */

// 标准 6KRO 键盘：8 字节输入报告（修饰键 + 保留 + 6 键码），1 字节 LED 输出
constexpr uint8_t kReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute) — 修饰键字节
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant) — 保留字节
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x05,        //   Usage Maximum (5)
    0x91, 0x02,        //   Output (Data, Variable, Absolute) — LED
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant) — LED 填充
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array) — 6 个键码
    0xC0,              // End Collection
};

// bcdHID 1.11，无国度码，NormallyConnectable
constexpr uint8_t kHidInfo[] = {0x11, 0x01, 0x00, 0x02};

// 常用键码（keyboard page 0x07）
constexpr uint8_t KEY_ENTER = 0x28;
constexpr uint8_t KEY_ESC   = 0x29;
constexpr uint8_t KEY_SPACE = 0x2C;

constexpr uint8_t MOD_CTRL  = 0x01;
constexpr uint8_t MOD_SHIFT = 0x02;
constexpr uint8_t MOD_GUI   = 0x08;

uint16_t s_conn       = BLE_HS_CONN_HANDLE_NONE;
uint16_t s_report_in  = 0;   // 输入报告特征值句柄
bool     s_subscribed = false;
uint8_t  s_proto_mode = 1;   // 1 = report protocol
ble_gap_event_listener s_listener;

/* --------------------------- 键位映射 --------------------------- */

KeySeq s_map[7];

const KeySeq& default_of(Action a)
{
    static const KeySeq kOk     {0, KEY_ENTER, 24};
    static const KeySeq kNg     {0, KEY_ESC, 24};
    static const KeySeq kFast   {MOD_CTRL, KEY_ENTER, 24};
    static const KeySeq kPlan   {0, 0, 0};               // 走文本注入，见 send()
    static const KeySeq kAi     {MOD_GUI, 0x0E, 24};     // GUI+K，命令面板
    static const KeySeq kEnter  {0, KEY_ENTER, 24};
    static const KeySeq kCancel {MOD_CTRL, 0x1D, 24};    // Ctrl+Z
    switch (a) {
        case Action::Ok:     return kOk;
        case Action::Ng:     return kNg;
        case Action::Fast:   return kFast;
        case Action::Plan:   return kPlan;
        case Action::Ai:     return kAi;
        case Action::Enter:  return kEnter;
        default:             return kCancel;
    }
}

void load_map()
{
    for (int i = 0; i < 7; i++) s_map[i] = default_of(static_cast<Action>(i));

    nvs_handle_t h;
    if (nvs_open(kNvsNs, NVS_READONLY, &h) != ESP_OK) return;
    for (int i = 0; i < 7; i++) {
        char key[4];
        std::snprintf(key, sizeof(key), "k%d", i);
        uint32_t packed = 0;
        if (nvs_get_u32(h, key, &packed) == ESP_OK) {
            s_map[i].mods    = packed & 0xFF;
            s_map[i].key     = (packed >> 8) & 0xFF;
            s_map[i].hold_ms = (packed >> 16) & 0xFFFF;
        }
    }
    nvs_close(h);
}

/* --------------------------- 发送 --------------------------- */

struct Job {
    uint8_t mods;
    uint8_t key;
    uint16_t hold_ms;
    char text[96];   // 非空则逐键注入文本
};

QueueHandle_t s_q = nullptr;

bool notify_report(uint8_t mods, uint8_t key)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_subscribed || s_report_in == 0) return false;
    uint8_t report[8] = {mods, 0, key, 0, 0, 0, 0, 0};
    os_mbuf* om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (!om) return false;
    return ble_gatts_notify_custom(s_conn, s_report_in, om) == 0;
}

void type_char(char ch)
{
    uint8_t mods = 0, key = 0;
    if (ch >= 'a' && ch <= 'z')           key = 0x04 + (ch - 'a');
    else if (ch >= 'A' && ch <= 'Z')      { key = 0x04 + (ch - 'A'); mods = MOD_SHIFT; }
    else if (ch >= '1' && ch <= '9')      key = 0x1E + (ch - '1');
    else if (ch == '0')  key = 0x27;
    else if (ch == '\n' || ch == '\r') key = KEY_ENTER;
    else if (ch == ' ')  key = KEY_SPACE;
    else if (ch == '/')  key = 0x38;
    else if (ch == '-')  key = 0x2D;
    else if (ch == '.')  key = 0x37;
    else if (ch == ',')  key = 0x36;
    else if (ch == ':')  { key = 0x33; mods = MOD_SHIFT; }
    else if (ch == '!')  { key = 0x1E; mods = MOD_SHIFT; }
    else if (ch == '?')  { key = 0x38; mods = MOD_SHIFT; }
    else return;  // 映射不了的字符跳过，比往焦点窗口里乱按强

    notify_report(mods, key);
    vTaskDelay(pdMS_TO_TICKS(16));
    notify_report(0, 0);
    vTaskDelay(pdMS_TO_TICKS(16));
}

void sender_task(void*)
{
    Job j;
    while (xQueueReceive(s_q, &j, portMAX_DELAY) == pdTRUE) {
        if (j.text[0]) {
            for (const char* p = j.text; *p; p++) type_char(*p);
        } else if (j.key || j.mods) {
            notify_report(j.mods, j.key);
            vTaskDelay(pdMS_TO_TICKS(j.hold_ms ? j.hold_ms : 24));
            notify_report(0, 0);
        }
    }
    vTaskDelete(nullptr);
}

/* --------------------------- GATT 回调 --------------------------- */

int read_static(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void* arg)
{
    // arg 指向 {数据指针, 长度} 的静态块。HID Info / Report Map 共用
    auto* blk = static_cast<const std::pair<const uint8_t*, size_t>*>(arg);
    if (os_mbuf_append(ctxt->om, blk->first, blk->second) != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
    return 0;
}

const std::pair<const uint8_t*, size_t> kBlkInfo{ kHidInfo, sizeof(kHidInfo) };
const std::pair<const uint8_t*, size_t> kBlkMap { kReportMap, sizeof(kReportMap) };

int proto_mode_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (os_mbuf_append(ctxt->om, &s_proto_mode, 1) != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t v = 0;
        uint16_t copied = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, &v, 1, &copied) == 0 && copied == 1) {
            s_proto_mode = v & 0x01;
        }
        return 0;
    }
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

int report_in_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint8_t zeros[8] = {};
        if (os_mbuf_append(ctxt->om, zeros, sizeof(zeros)) != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

int report_out_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    // LED 输出（CapsLock 等）。设备没有 LED 可点，读回 0，写进空气
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint8_t zero = 0;
        if (os_mbuf_append(ctxt->om, &zero, 1) != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

int ctrl_point_access(uint16_t, uint16_t, ble_gatt_access_ctxt*, void*)
{
    // Suspend / ExitSuspend。桌面设备不休眠，照单全收即可
    return 0;
}

int report_ref_in(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    const uint8_t ref[2] = {0x00, 0x01};  // report id 0, input
    if (os_mbuf_append(ctxt->om, ref, sizeof(ref)) != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
    return 0;
}

int report_ref_out(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    const uint8_t ref[2] = {0x00, 0x02};  // report id 0, output
    if (os_mbuf_append(ctxt->om, ref, sizeof(ref)) != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
    return 0;
}

const ble_gatt_dsc_def kDscsIn[] = {
    {
        .uuid      = &kUuidReportRef.u,
        .att_flags = BLE_GATT_DSC_F_READ,
        .access_cb = report_ref_in,
    },
    {},
};

const ble_gatt_dsc_def kDscsOut[] = {
    {
        .uuid      = &kUuidReportRef.u,
        .att_flags = BLE_GATT_DSC_F_READ,
        .access_cb = report_ref_out,
    },
    {},
};

const ble_gatt_chr_def kChrs[] = {
    {
        .uuid      = &kUuidHidInfo.u,
        .access_cb = read_static,
        .arg       = const_cast<std::pair<const uint8_t*, size_t>*>(&kBlkInfo),
        .flags     = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid      = &kUuidReportMap.u,
        .access_cb = read_static,
        .arg       = const_cast<std::pair<const uint8_t*, size_t>*>(&kBlkMap),
        .flags     = BLE_GATT_CHR_F_READ,
    },
    {
        // 未配对/未加密链路不允许写 Control Point：否则未鉴权的中心设备
        // 也能触发 Suspend/Exit-Suspend 之类的控制指令
        .uuid      = &kUuidCtrlPoint.u,
        .access_cb = ctrl_point_access,
        .flags     = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        // 同上：Protocol Mode 决定 Boot/Report 协议切换，也必须要求加密后才可写
        .uuid      = &kUuidProtoMode.u,
        .access_cb = proto_mode_access,
        .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP |
                     BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid        = &kUuidReport.u,
        .access_cb   = report_in_access,
        .descriptors = const_cast<ble_gatt_dsc_def*>(kDscsIn),
        .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
        .val_handle  = &s_report_in,
    },
    {
        .uuid        = &kUuidReport.u,
        .access_cb   = report_out_access,
        .descriptors = const_cast<ble_gatt_dsc_def*>(kDscsOut),
        .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                       BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        // Boot Protocol 兜底：老式主机（BIOS 等）只认这两个特征
        .uuid      = &kUuidBootIn.u,
        .access_cb = report_in_access,
        .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
    },
    {
        // Boot Output 同样能驱动 report_out_access，跟上面 Report(out) 保持一致要求加密
        .uuid      = &kUuidBootOut.u,
        .access_cb = report_out_access,
        .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                     BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {},
};

const ble_gatt_svc_def kSvcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &kUuidSvc.u,
        .characteristics = kChrs,
    },
    {0},
};

/* --------------------------- GAP 监听 --------------------------- */

int gap_listener(ble_gap_event* ev, void*)
{
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                s_conn = ev->connect.conn_handle;
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            s_conn       = BLE_HS_CONN_HANDLE_NONE;
            s_subscribed = false;
            State::get().setHid(false);
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (ev->subscribe.attr_handle == s_report_in) {
                s_subscribed = ev->subscribe.cur_notify != 0;
                State::get().setHid(s_subscribed);
                ESP_LOGI(TAG, "report channel %s", s_subscribed ? "armed" : "closed");
            }
            break;
        default:
            break;
    }
    return 0;
}

}  // namespace

const ble_gatt_svc_def* gatt_svcs() { return kSvcs; }

void start()
{
    // WS 关闭时没人替我们做 nvs_flash_init，自己来。重复调用无害
    if (nvs_flash_init() == ESP_ERR_NVS_NO_FREE_PAGES) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    load_map();
    if (!s_q) {
        s_q = xQueueCreate(6, sizeof(Job));
        xTaskCreatePinnedToCore(sender_task, "sinan_hid", 3072, nullptr, 4, nullptr, 1);
    }
    ble_gap_event_listener_register(&s_listener, gap_listener, nullptr);
    ESP_LOGI(TAG, "hid sender ready");
}

bool connected()
{
    return s_conn != BLE_HS_CONN_HANDLE_NONE && s_subscribed;
}

bool send(Action a)
{
    if (!connected()) return false;
    if (!s_q) return false;

    // Plan 默认是文本注入：往焦点窗口打 "/plan" 再回车
    if (a == Action::Plan && s_map[static_cast<int>(a)].key == 0) {
        return send_text("/plan\n");
    }

    const KeySeq& ks = s_map[static_cast<int>(a)];
    Job j{};
    j.mods    = ks.mods;
    j.key     = ks.key;
    j.hold_ms = ks.hold_ms;
    return xQueueSend(s_q, &j, 0) == pdTRUE;
}

bool send_text(const char* utf8)
{
    if (!connected() || !utf8 || !utf8[0]) return false;
    if (!s_q) return false;
    Job j{};
    std::snprintf(j.text, sizeof(j.text), "%s", utf8);
    return xQueueSend(s_q, &j, 0) == pdTRUE;
}

void set_map(Action a, KeySeq seq)
{
    s_map[static_cast<int>(a)] = seq;

    nvs_handle_t h;
    if (nvs_open(kNvsNs, NVS_READWRITE, &h) != ESP_OK) return;
    char key[4];
    std::snprintf(key, sizeof(key), "k%d", static_cast<int>(a));
    const uint32_t packed = static_cast<uint32_t>(seq.mods) |
                            (static_cast<uint32_t>(seq.key) << 8) |
                            (static_cast<uint32_t>(seq.hold_ms) << 16);
    nvs_set_u32(h, key, packed);
    nvs_commit(h);
    nvs_close(h);
}

KeySeq get_map(Action a) { return s_map[static_cast<int>(a)]; }

}  // namespace sinan::hid
