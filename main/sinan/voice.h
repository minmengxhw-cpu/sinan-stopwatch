/*
 * voice.h — 录音与上行。
 *
 * 不接小智。语音只做本地链路：录音 → WS 上传 → Mac 端转写 →
 * 交给用户自己的 CLI → 结果回传播放。数据不出内网，也不引入第二套协议栈。
 */
#pragma once

namespace sinan::voice {

void begin();  // 起录音任务
void end();    // 收尾并发 asr_end
void abort();  // 丢弃本次录音

}  // namespace sinan::voice
