#include "danger.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace sinan {

namespace {

struct Rule {
    const char* needle;
    const char* reason;
    Risk risk;
};

// 顺序即优先级：先命中先返回，所以 Grave 必须排在前面
// 注：chmod 777 家族 / 重定向到 /dev/ / 普通重定向 已改用下方的动态匹配
// （见 contains_chmod_world_writable / has_redirect_to / has_any_redirect），
// 不再依赖必须逐字包含 "chmod 777" 或 "> " 这种容易被绕过的定长子串。
constexpr Rule kRules[] = {
    {"rm -rf",          "recursive delete",   Risk::Grave},
    {"rm -fr",          "recursive delete",   Risk::Grave},
    {"mkfs",            "format filesystem",  Risk::Grave},
    {"dd if=",          "raw disk write",     Risk::Grave},
    {"push --force",    "force push",         Risk::Grave},
    {"push -f",         "force push",         Risk::Grave},
    {"reset --hard",    "discards work",      Risk::Grave},
    {"clean -fdx",      "discards work",      Risk::Grave},
    {"sudo",            "privilege escalation", Risk::Grave},
    {"| sh",            "pipe to shell",      Risk::Grave},
    {"| bash",          "pipe to shell",      Risk::Grave},
    {"drop table",      "drops data",         Risk::Grave},
    {"drop database",   "drops data",         Risk::Grave},
    {"truncate ",       "drops data",         Risk::Grave},
    {"--no-verify",     "skips checks",       Risk::Grave},
    {"eval ",           "dynamic execution",  Risk::Grave},
    {"curl ",           "network fetch",      Risk::Elevated},
    {"wget ",           "network fetch",      Risk::Elevated},
    {"npm publish",     "publishes package",  Risk::Elevated},
    {"pip install",     "installs package",   Risk::Elevated},
    {"git commit",      "writes history",     Risk::Elevated},
    {"git push",        "writes remote",      Risk::Elevated},
    {"mv ",             "moves files",        Risk::Elevated},
};

// chmod 后面只要出现过 777（含 -R 777 / 0777 / a+rwx 之外的常见写法），一律当世界可写处理，
// 不要求 "chmod" 和 "777" 之间没有任何其它字符（原实现要求逐字 "chmod 777" 导致 chmod -R 777 绕过）
bool contains_chmod_world_writable(const std::string& h)
{
    size_t pos = h.find("chmod");
    return pos != std::string::npos && h.find("777", pos) != std::string::npos;
}

// 在 hint 里找 '>' / '>>' 后面（允许紧跟 0 个或多个空格）是否是给定前缀，
// 用于识别 "> /dev/sda1" 与 ">/dev/sda1"（无空格）等等价写法
bool has_redirect_to(const std::string& h, std::string_view prefix)
{
    size_t pos = 0;
    while ((pos = h.find('>', pos)) != std::string::npos) {
        size_t i = pos + 1;
        while (i < h.size() && (h[i] == '>' || h[i] == ' ')) ++i;
        if (h.compare(i, prefix.size(), prefix) == 0) return true;
        ++pos;
    }
    return false;
}

bool has_any_redirect(const std::string& h)
{
    return h.find('>') != std::string::npos;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

const Rule* match(std::string_view tool, std::string_view hint)
{
    const std::string h = lower(hint);
    const std::string t = lower(tool);

    static constexpr Rule kChmodWorldWritable{"", "world writable", Risk::Grave};
    static constexpr Rule kRawDeviceWrite{"", "raw device write", Risk::Grave};
    static constexpr Rule kOverwritesFile{"", "overwrites file", Risk::Elevated};

    if (contains_chmod_world_writable(h)) return &kChmodWorldWritable;
    if (has_redirect_to(h, "/dev/")) return &kRawDeviceWrite;

    for (const auto& r : kRules) {
        if (h.find(r.needle) != std::string::npos) return &r;
    }
    if (has_any_redirect(h)) return &kOverwritesFile;
    // 写类工具即使命令看着人畜无害也算 Elevated
    if (t == "write" || t == "edit" || t == "notebookedit") {
        static constexpr Rule kWrite{"", "modifies files", Risk::Elevated};
        return &kWrite;
    }
    return nullptr;
}

}  // namespace

Risk assess(std::string_view tool, std::string_view hint)
{
    const Rule* r = match(tool, hint);
    return r ? r->risk : Risk::Normal;
}

const char* risk_reason(std::string_view tool, std::string_view hint)
{
    const Rule* r = match(tool, hint);
    return r ? r->reason : "";
}

}  // namespace sinan
