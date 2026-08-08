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
constexpr Rule kRules[] = {
    {"rm -rf",          "recursive delete",   Risk::Grave},
    {"rm -fr",          "recursive delete",   Risk::Grave},
    {"mkfs",            "format filesystem",  Risk::Grave},
    {"dd if=",          "raw disk write",     Risk::Grave},
    {"> /dev/",         "raw device write",   Risk::Grave},
    {"push --force",    "force push",         Risk::Grave},
    {"push -f",         "force push",         Risk::Grave},
    {"reset --hard",    "discards work",      Risk::Grave},
    {"clean -fdx",      "discards work",      Risk::Grave},
    {"sudo",            "privilege escalation", Risk::Grave},
    {"chmod 777",       "world writable",     Risk::Grave},
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
    {"> ",              "overwrites file",    Risk::Elevated},
};

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

    for (const auto& r : kRules) {
        if (h.find(r.needle) != std::string::npos) return &r;
    }
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
