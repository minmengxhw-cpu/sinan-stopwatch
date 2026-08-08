/*
 * bridge_ws.h — 自建 WebSocket 通道，接 Mac 上的 sinand.py。
 *
 * 职责边界：Fleet / Almanac 的数据入口，以及语音的上下行。
 * 与 BLE 通道完全解耦 —— 这边挂了 Ward 必须照常工作。
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace sinan::ws {

void start(const char* ssid, const char* pass, const char* uri);
bool connected();

// 发一行 JSON。未连接时返回 false，调用方自己决定要不要重试
bool send(const std::string& json);

// 语音上行。pcm 是 16k 单声道 s16le
bool send_audio_begin();
bool send_audio_chunk(const int16_t* pcm, size_t samples, uint32_t seq);
bool send_audio_end();

// 触发 daemon 白名单动作
bool trigger(const char* action_id);

}  // namespace sinan::ws
