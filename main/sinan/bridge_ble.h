/*
 * bridge_ble.h — Anthropic Hardware Buddy BLE 协议。
 *
 * 职责边界：只负责收发官方协议的 JSON 行，并把结果写进 sinan::State。
 * 不做任何 UI 判断，也不定义协议之外的字段 —— 权威规范是
 * anthropics/claude-desktop-buddy 的 REFERENCE.md。
 *
 * 角色：设备是 GATT 外围端，Claude 桌面端扫描并连接。
 */
#pragma once
#include <cstdint>
#include <string>

namespace sinan::ble {

enum class Decision { Once, Deny };

// 起 NimBLE 主机任务并开始广播。只能调一次
void start();

bool connected();

/*
 * 回传权限决策。
 *
 * id 必须与当前 State 里的 prompt_id 完全一致 —— 函数内部会再校验一次，
 * 不一致直接返回 false 并丢弃。这是防止误批准另一个请求的唯一保险。
 */
bool send_permission(const std::string& id, Decision d);

// 设备显示名，会被桌面端的 {"cmd":"name"} 覆盖
const char* device_name();

}  // namespace sinan::ble
