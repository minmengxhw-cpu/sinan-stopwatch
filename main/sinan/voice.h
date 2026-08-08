/*
 * voice.h — 录音与上行。
 *
 * 不接小智。语音只做本地链路：录音 → WS 上传 → Mac 端转写 →
 * 交给用户自己的 CLI → 结果回传播放。数据不出内网，也不引入第二套协议栈。
 */
#pragma once

namespace sinan::voice {

/* Work 内的编程语音：A 开始、A 停止并转写、B 确认发送。 */
bool begin(const char* target);
void end();
bool submit(const char* target);
void clear();
void abort();

// 录音任务还在跑。UI 用它判断能不能开始下一次
bool busy();

}  // namespace sinan::voice
