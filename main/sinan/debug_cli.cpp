/*
 * debug_cli.cpp
 *
 * 读行任务阻塞在自己的线程上，UI 线程无感。
 * 截屏走 LVGL 快照：435KB 的 RGB565 编 base64 要好几十秒，
 * 这是调试通道不是功能通道，慢可以接受，断不行 —— 所以分行输出。
 */
#include "debug_cli.h"
#include "beads.h"
#include "bridge_ble.h"
#include "bridge_hid.h"
#include "bridge_ws.h"
#include "gaze_fsm.h"
#include "haptics.h"
#include "precession.h"
#include "shell.h"
#include "state.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <mbedtls/base64.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <vector>

namespace sinan::debug {
namespace {

constexpr char TAG[] = "sinan.dbg";

const char* mood_name(gaze::Mood m)
{
    switch (m) {
        case gaze::Mood::Sleep:  return "Sleep";
        case gaze::Mood::Hear:   return "Hear";
        case gaze::Mood::Alert:  return "Alert";
        case gaze::Mood::Praise: return "Praise";
        case gaze::Mood::Guard:  return "Guard";
        case gaze::Mood::Pet:    return "Pet";
        case gaze::Mood::Peek:   return "Peek";
        default:                 return "Dream";
    }
}

void cmd_selftest()
{
    const auto s = State::get().snapshot();
    std::printf(
        "{\"heap\":%u,\"psram\":%u,\"ble\":%d,\"hid\":%d,\"ws\":%d,"
        "\"layer\":%d,\"mood\":\"%s\",\"bead\":%d,\"up_s\":%u}\n",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        s.ble.connected ? 1 : 0,
        s.hid.connected ? 1 : 0,
        s.ws_up ? 1 : 0,
        static_cast<int>(shell::current()),
        mood_name(gaze::mood()),
        beads::focus(),
        static_cast<unsigned>(GetHAL().millis() / 1000));
}

void cmd_shot()
{
    // 快照在锁内拍，打印在锁外 —— 435KB 编 base64 要几十秒，不能冻住 UI 线程
    lv_draw_buf_t* snap;
    {
        LvglLockGuard lock;
        snap = lv_snapshot_take(ui::precession_root(), LV_COLOR_FORMAT_RGB565);
    }
    if (!snap) {
        std::printf("SHOT FAIL\n");
        return;
    }
    const uint32_t w = snap->header.w;
    const uint32_t h = snap->header.h;
    const uint8_t* data = snap->data;
    const uint32_t len = snap->data_size;

    std::printf("SHOT BEGIN %u %u rgb565le\n", static_cast<unsigned>(w), static_cast<unsigned>(h));

    // 48 字节一组 → 64 个 base64 字符一行
    std::vector<unsigned char> b64(80);
    for (uint32_t off = 0; off < len; off += 48) {
        const uint32_t n = (len - off) < 48 ? (len - off) : 48;
        size_t olen = 0;
        mbedtls_base64_encode(b64.data(), b64.size(), &olen, data + off, n);
        b64[olen] = 0;
        std::printf("%s\n", reinterpret_cast<char*>(b64.data()));
    }
    std::printf("SHOT END\n");
    lv_draw_buf_destroy(snap);
}

void cmd_inject_prompt()
{
    BleState s = State::get().snapshot().ble;
    s.connected = true;
    s.last_beat = GetHAL().millis();
    s.has_prompt = true;
    s.prompt_id = "debug-1";
    s.prompt_tool = "Bash";
    s.prompt_hint = "rm -rf /tmp/sinan_test  # debug 注入的假审批";
    s.prompt_since = GetHAL().millis();
    State::get().setBle(s);
    std::printf("OK injected approval\n");
}

void cmd_inject_irq(const char* kind, const char* text)
{
    IrqEvent e;
    e.active = true;
    e.kind = std::strcmp(kind, "error") == 0 ? IrqKind::Error : IrqKind::Done;
    e.id = text && text[0] ? text : "debug";
    e.title = e.kind == IrqKind::Error ? "ERROR" : "DONE";
    e.body = text ? text : "";
    e.since_ms = GetHAL().millis();
    State::get().setIrq(e);
    std::printf("OK injected %s\n", kind);
}

void cmd_haptics(const char* name)
{
    struct { const char* n; haptics::Cue c; } map[] = {
        {"ok", haptics::Cue::Ok},       {"ng", haptics::Cue::Ng},
        {"warn", haptics::Cue::Warn},   {"tick", haptics::Cue::Tick},
        {"pet", haptics::Cue::Pet},     {"alert", haptics::Cue::Alert},
        {"pair", haptics::Cue::Pair},   {"soft", haptics::Cue::Soft},
    };
    for (auto& m : map) {
        if (std::strcmp(name, m.n) == 0) {
            haptics::play(m.c);
            std::printf("OK cue %s\n", name);
            return;
        }
    }
    std::printf("ERR unknown cue %s\n", name);
}

void cmd_set_time(const char* arg)
{
    int y, mo, d, h, mi, se;
    if (std::sscanf(arg, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
        std::printf("ERR time format\n");
        return;
    }
    tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = se;
    const time_t epoch = mktime(&t);
    timeval tv{epoch, 0};
    settimeofday(&tv, nullptr);
    GetHAL().syncSystemTimeToRtc();
    std::printf("OK time set\n");
}

void handle_line(char* line)
{
    // 去行尾
    size_t n = std::strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;

    switch (line[0]) {
        case 'P': cmd_shot(); break;
        case 'a': shell::inject_key('a'); break;
        case 'b': shell::inject_key('b'); break;
        case 'A': shell::inject_key('A'); break;
        case 'B': shell::inject_key('B'); break;
        case 'C': shell::inject_key('C'); break;
        case 'Q': shell::inject_layer(0); break;
        case 'W': shell::inject_layer(1); break;
        case 'X': shell::inject_layer(2); break;
        case 'S': cmd_selftest(); break;
        case 'G':
            std::printf("gaze %s · bead %d · layer %d\n",
                        mood_name(gaze::mood()), beads::focus(),
                        static_cast<int>(shell::current()));
            break;
        case '@': {
            int x, y;
            if (std::sscanf(line + 1, "%d,%d", &x, &y) == 2) {
                shell::inject_touch(x, y, true);
                shell::inject_touch(x, y, false);
            }
            break;
        }
        case 'H': cmd_haptics(line + 1 + (line[1] == ' ' ? 1 : 0)); break;
        case 'D': cmd_set_time(line + 2); break;
        case 'I': {
            char* arg = line + 1;
            while (*arg == ' ') arg++;
            if (std::strncmp(arg, "approval", 8) == 0)      cmd_inject_prompt();
            else if (std::strncmp(arg, "clear", 5) == 0) { State::get().clearPrompt(); std::printf("OK cleared\n"); }
            else if (std::strncmp(arg, "error", 5) == 0)   cmd_inject_irq("error", arg + 6);
            else if (std::strncmp(arg, "done", 4) == 0)    cmd_inject_irq("done", arg + 5);
            else std::printf("ERR unknown inject\n");
            break;
        }
        case 0:
            break;
        default:
            std::printf("ERR unknown cmd %c\n", line[0]);
            break;
    }
}

void cli_task(void*)
{
    char line[160];
    std::printf("sinan debug ready. S=selftest\n");
    while (fgets(line, sizeof(line), stdin)) {
        handle_line(line);
    }
    vTaskDelete(nullptr);
}

}  // namespace

void start()
{
    xTaskCreatePinnedToCore(cli_task, "sinan_dbg", 6144, nullptr, 2, nullptr, 0);
}

}  // namespace sinan::debug
