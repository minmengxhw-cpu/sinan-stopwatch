/*
 * bridge_hid.h — 第三通道：BLE HID 键盘。
 *
 * 职责边界：把「OK / NG / FAST / 对讲文本」变成焦点窗口里的真实按键。
 * 与 Buddy NUS、WiFi WS 并列，任一条挂掉不影响其他。
 *
 * 冷启动路径：系统蓝牙设置里配对（设备显示 6 位码）→ 设备就是键盘，
 * 不需要 Mac 上跑任何程序。
 *
 * GATT 注册由 bridge_ble 在同一主机里顺带完成（单连接多服务），
 * 这里只管报告发送与键位映射。
 */
#pragma once
#include <cstdint>

struct ble_gatt_svc_def;

namespace sinan::hid {

enum class Action : uint8_t {
    Ok,      // 默认 Enter
    Ng,      // 默认 Esc
    Fast,    // 默认 Ctrl+Enter
    Plan,    // 默认注入文本 "/plan" + Enter
    Ai,      // 默认 GUI+K（命令面板），可用 set_map 改
    Enter,   // 显式 Enter
    Cancel,  // 默认 Ctrl+Z
};

struct KeySeq {
    uint8_t mods     = 0;    // bit0 Ctrl, bit1 Shift, bit2 Alt, bit3 GUI（左修饰键）
    uint8_t key      = 0;    // HID usage（keyboard page 0x07）
    uint16_t hold_ms = 24;   // 按下保持时长
};

void start();          // 建发送任务、从 NVS 读键位映射。ble::start 之后调
bool connected();      // 已连接且报告通道已被对端订阅

bool send(Action a);             // 未连接返回 false，调用方在弦区提示，不弹窗
bool send_text(const char* utf8);  // ASCII 逐键注入；无法映射的字符跳过

void set_map(Action a, KeySeq seq);   // 立即生效并写 NVS
KeySeq get_map(Action a);

// 给 bridge_ble 的注册钩子。应用代码不要碰
const ble_gatt_svc_def* gatt_svcs();

}  // namespace sinan::hid
