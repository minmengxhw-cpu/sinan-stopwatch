/*
 * danger.h — 危险命令判定。
 *
 * 职责边界：只回答"这条命令批错了会不会很惨"，不做任何 UI 决策。
 * 规则宁可误报不可漏报 —— 误报的代价是多按 0.8 秒，漏报的代价是没有代价可言。
 */
#pragma once
#include <string_view>

namespace sinan {

enum class Risk {
    Normal,   // 常规操作
    Elevated, // 会写入或联网，值得看一眼
    Grave,    // 不可逆或提权，必须长按确认
};

// tool 是工具名（如 "Bash"），hint 是待执行内容
Risk assess(std::string_view tool, std::string_view hint);

// 命中的规则名，用于在屏幕上告诉用户"为什么它是红的"
const char* risk_reason(std::string_view tool, std::string_view hint);

// 跑一遍内置样本，返回失败条数。改规则表后调一次，比上真机试快
int selftest();

}  // namespace sinan
