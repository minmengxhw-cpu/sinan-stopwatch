# 司南 SINAN — 可执行重构规格 v0.2

> 状态：可施工。依据 2026-08 对抗式审核结论。  
> 约束：**不改 `main/sinan/design.h` 里任何数值**（色、半径、时长、阈值一律沿用）。  
> 视觉语言不变：矿物色、弧与刻度、无矩形卡片、岁差根容器、原生 LVGL C API。  
> 本文是施工图，不是愿景文。验收以文末 Checklist 为准。

---

## 0. 一句话目标

把「五个并列 App 看板」收成**一件桌面决策仪**：

```
默认脸 = 团团（寐）
有事   = 全局中断（守语义）
干活   = 阵（Agent 环）+ Action 层（OK/NG/…）
冷启动 = BLE HID 即可用，不依赖 Mac daemon
```

---

## 1. 非目标（明确不做）

| 不做 | 原因 |
|------|------|
| 改 `design.h` 色/半径/时长数值 | 硬约束 |
| 放弃 LVGL 上粒子全屏引擎（Flux 路线） | 与照片栈/岁差/mooncake 冲突，毁漆器调性 |
| 腕戴形态 / 抄 vibewatch 霓虹键帽 UI | 产品是桌面常插电 |
| 期 1 实现 Codex Micro 完整 JSON-RPC | 期 2 可选；不挡主线 |
| 重做照片管线契约 | 仍「解一次、PSRAM、只 blit」 |
| 历三盘轮播继续当主功能 | 降级或删（见 §6.5） |
| 在网络回调里碰 LVGL | 铁律不变 |

---

## 2. 信息架构（四层壳）

所有 UI 挂在 `sinan::ui::precession_root()` 下。  
**壳（Shell）** 管层切换；旧 mooncake App 逐步变成层或深度页。

```
优先级高 → 低：

L3  Interrupt   全局中断：APPROVAL / ERROR / DONE（可抢占任何层）
L2  Action      FAST / NG / OK / PLAN / AI / 中央对讲
L1  Work        阵：≤6 agent 极坐标环 + focus
L0  Rest        寐：团团身体 + 灵魂点阵 + 项圈珠串   ← 默认
```

### 2.1 层切换状态机

```
enum class ShellLayer : uint8_t { Rest, Work, Action, Interrupt };

// 合法迁移（其余忽略并打 log）
Rest  --(A+B 同时短按 或 双击 B)--> Work
Work  --(A+B)--> Action
Action--(A+B 或 空闲 12s)--> Work
Work  --(B 长按 或 无操作 60s)--> Rest
*     --(有 prompt / ERROR / DONE 且未抑制)--> Interrupt
Interrupt --(决策完成 或 DONE 展示 1.2s 或 用户 B)--> 返回进入前的层
```

实现位置：`main/sinan/shell.{h,cpp}`  
- `shell::init()` / `shell::tick(now)` / `shell::goto_layer(L)`  
- `shell::current()` / `shell::push_interrupt(reason)` / `shell::pop_interrupt()`  
- 层 UI 是 shell 持有的 root 子树，**不是**五个互相 open/close 的 mooncake 实例抢屏。

### 2.2 与 mooncake 的过渡策略

| 阶段 | 策略 |
|------|------|
| PR-A（本规格） | 仍 install 旧 App，但 `SN_BOOT_APP` 改为 `Gaze`；Gaze 内嵌珠串与 FSM；新增 shell 协调中断 |
| PR-B | shell 接管 Rest/Work/Action；Fleet/Echo 逻辑迁入层；launcher 仅 Setup + 调试入口 |
| PR-C | Almanac 删除或缩成珠串数据源；Ward 深度读命令页挂在 Interrupt 详情 |

期 1 交付以 **PR-A 全部 + PR-B 的 Action/HID 最小集** 为验收线。

---

## 3. 全局输入与键位（全机唯一表）

文件：`main/sinan/input_map.h`（仅常量与注释，无逻辑）

| 输入 | 全局语义 | Rest | Work | Action | Interrupt |
|------|----------|------|------|--------|-----------|
| A 短 | 肯定 / 前进 | 无（或轻振忽略） | 对 focus 发 OK* | OK | 普通允许 |
| A 长 0.8s | 危险确认 / 深度 | 锁定照片（保留） | — | — | Grave 批准 |
| B 短 | 否定 / 切换 | — | 下一个 agent | NG | 拒绝 |
| B 长 | 回退 | → launcher（可选） | → Rest | → Work | 取消中断回下层 |
| A+B 同时 | 层切换 | → Work | → Action | → Work | 忽略 |
| 沿 Rim 圆弧滑 ≥40° | 盘珠 / 抚触 | 拨珠 or Pet | 拨 agent 环 | — | — |
| 敲击 `TAP_G` | 唤醒 | 露时 6s + 亮度 Normal | 同左 | 同左 | 忽略 |
| 触摸中心短按 | 上下文 | 灵魂 Peek（露眼/时间） | 选中 agent | 中央对讲开始判定 | — |
| 触摸中心长按 | 对讲 | 若已连 HID/WS 则对讲 | 对讲 | 对讲 | — |

\* Work 层 A 短：若当前 agent 无待批事项，则发 HID `OK` 快捷（可配置）；有 Buddy prompt 时进入 Interrupt，不在 Work 直接批。

**禁止**：各 App 再私自重定义 A/B 语义。迁移期旧 App 必须改调 `shell` 或共用 `input_map`。

---

## 4. 模块拆分与 PR 顺序

```
PR1  sinan/haptics     触感 + 方波音效语义表
PR2  sinan/bridge_hid  BLE HID 键盘最小集 + Action 发射
PR3  sinan/shell       四层壳 + 全局中断
PR4  sinan/beads       项圈珠串数据 + 绘制
PR5  gaze_fsm          团团状态机（改 app_gaze / 抽 sinan/gaze_fsm）
PR6  work_layer        阵逻辑并入 Work（可先包一层 AppFleet）
PR7  tools/*           串口 debug + web 原型 tokens
```

依赖：`PR1 → PR2 → PR3 → (PR4 ∥ PR5) → PR6`；`PR7` 可与 PR1 并行。

---

## 5. PR1 — `sinan/haptics`（多感官语义）

### 5.1 文件

```
main/sinan/haptics.h
main/sinan/haptics.cpp
```

### 5.2 API

```cpp
namespace sinan::haptics {

enum class Cue : uint8_t {
    Ok,          // 批准 / 正向确认
    Ng,          // 拒绝
    Warn,        // 危险出现、Grave 进入
    Tick,        // 轻反馈：切 agent、拨珠一格
    Pet,         // 呼噜：短序列
    Alert,       // 新 prompt / ERROR
    Pair,        // 配对成功
    Soft,        // 通用轻触
};

struct Settings {
    uint8_t vib_pct  = 80;   // 0–100
    uint8_t aud_pct  = 60;   // 0–100，0 = 静音
    bool state_vib   = true; // agent 状态变化是否振
};

void init();
void set(const Settings& s);
Settings get();

// 非阻塞：内部开短任务或复用已有 audio 缓冲；禁止在 LVGL 锁内阻塞播放
void play(Cue c);

// 仅震动（对讲开始等需要静音场景）
void vibe_only(Cue c);
}
```

### 5.3 语义映射（固定，不进 design.h 数值也可在 cpp 内用局部常量）

| Cue | 震动 | 声音（方波/短短语，HAL `audioPlay`） |
|-----|------|--------------------------------------|
| Ok | 90ms ×1 中等 | 上行两音（短-长） |
| Ng | 160ms ×1 偏强 | 下行两音 |
| Warn | 120ms ×2 间隔 80ms | 低长音 180ms |
| Tick | 30ms 轻 | 可选极短 click，aud&lt;30 则只振 |
| Pet | 40ms ×3 间隔 90ms 递减 | 无或极轻 |
| Alert | 180ms + 100ms（沿用现 vibrate 强度感） | 琥珀感中音 |
| Pair | 60ms ×2 | 上行三音 |
| Soft | 40ms | 无 |

实现注意：

- 音频用预生成小 PCM 表（编译期数组，每段 &lt; 4KB），**不要**运行时合成复杂波形拖爆 CPU。  
- `play` 重入：新 Cue 可打断旧播放。  
- 全项目逐步替换裸 `GetHAL().vibrate(...)`：Ward/Gaze/Fleet/Echo 的决策与切换点必须走 `haptics::play`。

### 5.4 验收

- [ ] `play(Ok)` / `play(Ng)` 人耳可辨、无需看屏  
- [ ] `aud_pct=0` 时无声仍有振  
- [ ] 在 `LvglLockGuard` 内调用 `play` 不卡死 UI 线程超过 2ms（只投递）

---

## 6. PR2 — `sinan/bridge_hid` + Action 发射

### 6.1 定位

第三通道：**BLE HID Keyboard**。  
与 Buddy NUS、WiFi WS **并列**；任一条挂掉不影响其他（`main.cpp` 继续各起各的）。

冷启动路径：系统蓝牙配对 → 设备当键盘 → Action 层按键立刻进焦点窗口。

### 6.2 文件

```
main/sinan/bridge_hid.h
main/sinan/bridge_hid.cpp
```

### 6.3 API

```cpp
namespace sinan::hid {

enum class Action : uint8_t {
    Ok,      // 默认：Enter  或 可配置复合键
    Ng,      // 默认：Esc
    Fast,    // 默认：Ctrl+Enter（或配置）
    Plan,    // 默认：发送字符串 "/plan\n" 或快捷键（配置）
    Ai,      // 默认：打开助手快捷（配置为自定义键序）
    Enter,   // 显式 Enter
    Cancel,  // Ctrl+Z 或 Esc（配置，默认 Ctrl+Z）
};

struct KeySeq {
    // 最多 6 个 HID usage 步进；实现用简单列表即可
    uint8_t mods;     // ctrl/shift/alt/gui bit
    uint8_t key;      // HID keycode
    uint16_t hold_ms; // 按下保持
};

void start();                 // NimBLE HID 设备，广播名 Claude-SINAN-XXXX-HID 或副服务
bool connected();
bool send(Action a);
bool send_text(const char* utf8);  // 对讲 ASR 后注入；ASCII 优先，非 ASCII 可降级为逐字或仅 WS

// 从 NVS 读键位映射；Setup 可改
void set_map(Action a, KeySeq seq);
}
```

### 6.4 与现有 BLE 共存

| 方案 | 选择 |
|------|------|
| 双 Peripheral 广播 | S3/NimBLE 吃力，**不选** |
| **单连接多服务**：NUS（Buddy）+ HID | **首选**。同一 `Claude-SINAN-XXXX`，系统当键盘，Buddy 仍走 NUS |
| HID 与 NUS 互斥模式 | 降级方案：Setup 切换「Buddy / HID」 |

实现要求：

- 保持 NUS 加密要求（现有 sm_mitm/sc）。  
- HID 报告键盘标准 Boot Protocol 即可。  
- `send` 失败（未连接）返回 false；UI 弦区提示 `no hid`，**不弹窗**。

### 6.5 Action 层 UI（视觉仍用 ring 原语）

挂在 shell L2，父节点 `precession_root()`：

```
Rim 整圈细弧：BRONZE 或当前 focus 语义色
Orbit 六等分触摸区（非矩形按钮，是弧段 hit-test）：
  0°附近 FAST | 60° NG | 120° OK | 180° PLAN | 240° AI | 中心对讲
弦区：当前 focus agent 名 或 "HID"
```

配色导轨（学 vibewatch 语义，用司南色）：

- NG 弧段 / 靠近 B 键侧：`CINNABAR` 短导轨（从屏缘向弧段）  
- OK 弧段 / 靠近 A 键侧：`MALACHITE` 短导轨  

导轨是 **2–3 条短弧或刻度点亮**，不是矩形条。半径 ≤ `R_SAFE`。

中央对讲：

1. 长按中心 → `haptics::Soft` + `voice::begin` 或本地缓存  
2. 松手 → `voice::end`；若 HID 已连且仅需文本注入：ASR 结果 `hid::send_text`  
3. 若 WS 可用且配置了 agent：保持现有 daemon 路径作增强  

Echo 独立 App **停止作为主入口**；逻辑函数化供 Action 调用。

### 6.6 验收

- [ ] macOS 蓝牙可发现并显示为键盘  
- [ ] Action OK → 焦点窗口收到 Enter（或配置键）  
- [ ] Action NG → Esc / 配置键  
- [ ] NUS Buddy 仍可收 prompt（同固件）  
- [ ] HID 断线时 Ward/Buddy 不受影响  

---

## 7. PR3 — `sinan/shell` + 全局中断

### 7.1 文件

```
main/sinan/shell.h
main/sinan/shell.cpp
main/sinan/interrupt_view.h
main/sinan/interrupt_view.cpp
```

### 7.2 中断类型

```cpp
enum class IrqKind : uint8_t {
    Approval,  // Buddy prompt 或外部审批
    Error,     // worker error / 显式 ERROR 事件
    Done,      // 任务完成，去重展示
};
```

### 7.3 去重规则（学 haosuo）

```
key = kind + source_id + (prompt_id | task_id | hash(msg))
同一 key 在 45s 内不重复全屏抢占；仅更新 Rim 收缩/文案
不同 key 可打断当前非 Grave 的 Done 展示
Approval(Grave) 不可被 Done 打断
```

### 7.4 Interrupt UI = 现 Ward 待决/落定的提取

- 复用 Ward 的 Rim 倒计时、Orbit 刻度密度、工具名、hint、危险长按填色团团  
- 从 `app_ward` **抽公共绘制**到 `interrupt_view`，Ward 深度页调用同一套  
- 静默幽灵团团只在 Rest/Interrupt 无文案时出现  

### 7.5 与 State 的衔接

`shell::tick` 每帧（主循环，已有 mooncake update 旁）：

```cpp
auto s = State::get().snapshot();
if (s.ble.has_prompt) shell::push_interrupt(IrqKind::Approval, s.ble.prompt_id);
// ERROR/DONE：State 增字段（见 §10），由 WS/HID/未来桥写入
```

Gaze `onRunning` 里「有 prompt 就 close」**删除**，改由 shell 抢占，Gaze 可保持挂载（隐藏）以免重载照片。

### 7.6 验收

- [ ] Rest 显示时来 prompt → 自动 Interrupt + Alert 触感  
- [ ] 批准/拒绝后 1.2s 内回原层，照片不重新 decode  
- [ ] 同一 prompt 心跳不重复震动（已有逻辑保留）  
- [ ] 45s 去重生效  

---

## 8. PR4 — `sinan/beads` 项圈珠串

### 8.1 隐喻（写进代码头注释）

> 团团项圈上的珠 = 今天你与 agent 之间的因果。  
> 不是装饰 sin()，不是秒针。

### 8.2 文件

```
main/sinan/beads.h
main/sinan/beads.cpp
```

### 8.3 数据模型

```cpp
namespace sinan::beads {

constexpr int kMax = 18;   // 视觉上限；过多变糊

enum class BeadKind : uint8_t {
    Empty,      // 漆色空槽
    Approve,    // 今日批准
    Deny,       // 今日拒绝
    AgentRun,   // 某 worker 在 run
    AgentStall, // stall
    QuotaLow,   // quota < 0.10
    Day,        // 日课/号码（可选）
    Event,      // 通用历史事件
};

struct Bead {
    BeadKind kind = BeadKind::Empty;
    uint32_t color;     // 必须来自 design 语义色
    char label[12];     // 拨到时弦区显示，英文或数字
    char detail[40];    // 次级
    uint8_t agent_idx;  // 0xff = 无
    uint32_t t_ms;      // 记录时间
};

// 由 shell/state 驱动重建；UI 只读 snapshot
void rebuild_from(const Snapshot& s);
int count();
Bead get(int i);
int focus();                 // 当前拨到的索引
void set_focus(int i);
void rotate(int delta);      // 盘珠 ±1

// 绘制：在 parent 上创建/更新 kMax 个小圆点，极坐标 R_RIM
void mount(lv_obj_t* parent);
void unmount();
void redraw();               // 只改颜色/opa/位置，不销毁
}
```

### 8.4 重建规则（确定性）

顺序固定，便于测试：

1. 先放最多 6 颗 **agent 状态珠**（Fleet workers，run/stall/low quota 优先，idle 暗鎏金）  
2. 再放今日 **approve/deny** 各压缩为计数珠（如 `A12` / `D3`），超过则合并  
3. 若 `almanac.number_code` 非空，一颗 Day 珠  
4. 不足 kMax 的尾部 Empty 槽（半透明 LACQUER）  

空数据：18 槽全 Empty + 弦区 `beads sleep`——**邀请态，不是错误**。

### 8.5 交互

| 手势 | 行为 |
|------|------|
| Rim 上顺时针滑 | `rotate(+1)` + Tick + 弦区 label |
| 逆时针 | `rotate(-1)` |
| 长按 focus 珠 | 若有 agent_idx → shell Work 并 focus 该 agent |
| 无手势 | agent run 珠以 T_BREATH 下限做 opa 呼吸；Empty 不动 |

### 8.6 绘制约束

- 珠半径 5–7px 圆，`lv_obj` + `LV_RADIUS_CIRCLE`  
- 极坐标半径 = `R_RIM`（与现 outrun 点同轨）  
- **删除** Gaze 外圈 outrun 秒点与 trail（珠串取代其轨道）  
- 焦点珠：略大 1px 或 SILK 描边感（用更亮 BRONZE/SILK 填充）  

### 8.7 验收

- [ ] 无 daemon 时项圈为空槽，不报错红  
- [ ] 模拟 2 run worker → 2 颗石绿珠  
- [ ] 批准一次 → 出现/更新 Approve 珠  
- [ ] 拨珠弦区文案变化 + Tick 触感  
- [ ] 长按 agent 珠进入 Work 且 focus 正确  

---

## 9. PR5 — 团团状态机 `gaze_fsm`（赛博萌玩重构核心）

### 9.1 文件

```
main/sinan/gaze_fsm.h
main/sinan/gaze_fsm.cpp
// app_gaze.cpp 改为驱动 FSM + 照片层，去掉无语义 sin 叙事依赖
```

### 9.2 状态

```cpp
enum class GazeMood : uint8_t {
    Sleep,   // 默认
    Hear,    // 链路上有新事件但未全屏
    Alert,   // 即将/正在 Interrupt（壳层处理，Gaze 可降亮度）
    Praise,  // 刚批准
    Guard,   // 危险命令可见期
    Pet,     // 被抚触
    Peek,    // 敲击/点中露时
    Dream,   // 长时间无互动（可选，>20min）
};
```

### 9.3 转移表（唯一允许的「动」）

| 从 → 到 | 触发 | 表现（全用现有令牌） |
|---------|------|----------------------|
| * → Hear | State 新事件且未进 Interrupt | 点阵 opa 66→120，Rim 琥珀闪一次 |
| * → Alert | shell 进入 Interrupt | 照片 opa 降至 70，点阵隐藏或极淡 |
| * → Praise | 批准成功后 1.0s | 石绿 bloom 弧 + haptics::Ok；点阵 recolor MALACHITE |
| * → Guard | assess==Grave 展示中 | 点阵 CINNABAR，晕影略收 |
| * → Pet | Rim 滑且非拨珠模式（速度慢、弧长短于拨珠） | haptics::Pet；点阵短时 SILK |
| * → Peek | 敲击或中心轻点 | 现有 6s 时间逻辑 |
| * → Sleep | 超时回落 | 恢复 Quiet luma、点阵 26% |
| Sleep → Dream | 无互动 20min | 珠串极慢自转（每 60s 一格），非每帧 |

### 9.4 必须删除 / 降级的运动

| 旧机制 | 处置 |
|--------|------|
| 外圈 outrun 秒点 + trail | **删除**，轨道给珠串 |
| 李萨如漂移 | **保留**为防烧屏，注释改为 anti-burn-in，**禁止**在文案称「生命」 |
| 晕影 18bpm | **保留**，但幅度随 Mood：Sleep=6px，Peek=3px，Alert=0 |
| 视差 ±5px | **保留**，不加大 |
| 触摸=下一张 | 改为：**双指或 B+触摸** 才换图；单击中心 = Peek |
| A 短切三版式 | **删除轮询**；大字模式移到 Setup「无障碍」；默认只有寐+敲击看时 |

### 9.5 照片层契约（不改）

- `photo_store` 仍 acquire/release  
- 8 FPS offset，禁止 scale  
- 交叉淡入 public `onSwapDone`  
- 自动轮换仅 `_locked==false` 且 5min；Pet/Peek 不重置计时可接受  

### 9.6 灵魂点阵

- 路径仍 `A:/spiflash/tuan/glyph.png`  
- Rest 静默：显示 ghost（与 Ward 同视觉 26%）  
- 无 glyph 文件：回退小针点，不崩  

### 9.7 验收

- [ ] 每个 Mood 切换能指出触发源（单测或 debug 日志 `gaze mood X->Y reason`）  
- [ ] 无「纯装饰秒针」  
- [ ] 抚触与拨珠可区分（阈值：快速短滑=珠，慢长滑=Pet）  
- [ ] prompt 中断回来不重新 JPEG decode  
- [ ] 性能：照片层仍 ≤8 FPS 逻辑更新  

---

## 10. State 扩展（最小增量）

文件：`state.h` / `state.cpp`

```cpp
// 新增
struct IrqEvent {
    bool active = false;
    IrqKind kind = IrqKind::Done;
    char id[40];
    char title[32];
    char body[96];
    uint32_t since_ms = 0;
    uint32_t dedupe_hash = 0;
};

struct HidState {
    bool connected = false;
};

// Snapshot 增加：
IrqEvent irq;
HidState hid;
// tally 已有 approved/denied — beads 复用
```

写入方：bridge_ble / bridge_ws / bridge_hid / shell。  
读取方：shell、beads、gaze_fsm。  
**禁止** UI 直接写 State（决策发送后由 bridge clear）。

---

## 11. PR6 — Work 层（阵升级）

### 11.1 行为

- 显示逻辑基本复用 `app_fleet`：段弧=quota，色=state  
- B 短：cycle focus + Tick  
- A 短：`hid::send(Ok)` 若无 Buddy prompt  
- 有 prompt：shell 已进 Interrupt，Work 不截获 A  
- 弦区：`name_of(state) + task`，陈旧靛青  
- 空态：`no fleet` + 邀请，不红  

### 11.2 与珠串

- Work 内可隐藏项圈或降 opa，避免与段标签互抢  
- Rest 项圈 agent 珠与 Work focus **索引一致**  

### 11.3 验收

- [ ] 与现 Fleet 信息等价  
- [ ] focus 与 beads.agent_idx 联动  
- [ ] 断 WS 不弹窗，Rim/段转靛青  

---

## 12. 历 Almanac 处置

| 选项 | 规格选择 |
|------|----------|
| A. 删除 App，数据仅供 Day 珠 | **期 1 选 A** |
| B. 保留隐藏入口 | Setup 内「日课」 |

`app_almanac`：期 1 从 `main.cpp` uninstall，代码可留目录但 `apps.h` 不再暴露。  
daemon almanac 推送仍写入 State，供 beads Day 珠。

---

## 13. PR7 — 开发闭环

### 13.1 串口 Debug CLI

```
main/sinan/debug_cli.{h,cpp}
tools/screenshot.py
tools/ctl.py
```

命令（单字节或行）：

| 命令 | 作用 |
|------|------|
| `P` | 截屏：读面板或 LVGL 快照 → base64 行协议 |
| `a` / `b` | 注入 A/B click |
| `A` / `B` | 注入 A/B long |
| `@x,y,ms` | 注入触摸 |
| `Q` | shell → Rest |
| `W` / `X` | shell → Work / Action |
| `I approval` | 注入假 prompt 到 State |
| `I clear` | clear prompt |
| `G` | 打印 gaze mood + beads focus |
| `H ok` / `H ng` | haptics |
| `S` | selftest：heap / psram / ble / hid / ws 一行 JSON |
| `D YYYY-MM-DD HH:MM:SS` | 对时 |

### 13.2 Web 原型

```
web/prototype/index.html
web/prototype/tokens.json    # 由 scripts/export_design_tokens.py 从 design.h 正则导出
web/prototype/shell.js       # 四层 + 珠串 + Action 可点
```

要求：

- tokens 与固件色/半径一致  
- 可演示：Rest 抚触、拨珠、Interrupt 倒计时、Action 导轨  
- README 一节：`python3 -m http.server -d web/prototype`  

### 13.3 验收

- [ ] 无硬件可评审 Rest/Work/Action/Interrupt  
- [ ] 有硬件：`S` 与 `P` 可用  
- [ ] CI 可选：tokens.json 与 design.h 哈希一致性脚本  

---

## 14. 启动与配置变更

### 14.1 `config.h`

```cpp
#define SN_BOOT_APP "Gaze"          // 原 Ward → Gaze（寐）
#define SN_HID_ENABLE 1
#define SN_WS_ENABLE 1              // 仍默认可关
// 可选键位后续用 NVS，不强求进 config.h
```

### 14.2 `main.cpp` 顺序

```
HAL init
precession_root
haptics::init
ble::start          // NUS
hid::start          // 同主机或紧随
ws::start           // 允许失败
shell::init         // 创建 Rest 为默认
debug_cli::start
// mooncake：过渡期仍 install；shell 主导时 launcher 仅 Setup
loop: feedDog / precession_tick / shell::tick / mooncake.update / debug poll
```

---

## 15. 文件级变更清单（期 1）

| 路径 | 动作 |
|------|------|
| `main/sinan/haptics.{h,cpp}` | 新建 |
| `main/sinan/bridge_hid.{h,cpp}` | 新建 |
| `main/sinan/shell.{h,cpp}` | 新建 |
| `main/sinan/interrupt_view.{h,cpp}` | 新建（抽 Ward） |
| `main/sinan/beads.{h,cpp}` | 新建 |
| `main/sinan/gaze_fsm.{h,cpp}` | 新建 |
| `main/sinan/debug_cli.{h,cpp}` | 新建 |
| `main/sinan/input_map.h` | 新建 |
| `main/sinan/state.{h,cpp}` | 扩字段 |
| `main/sinan/config.h` | BOOT/开关 |
| `main/apps/app_gaze/*` | FSM+珠串；删 outrun；改触摸 |
| `main/apps/app_ward/*` | 调 haptics；深度 UI 共用 interrupt_view |
| `main/apps/app_fleet/*` | 过渡期保留；键位对齐 input_map |
| `main/apps/app_echo/*` | 函数化供 Action；可从 launcher 隐藏 |
| `main/apps/app_almanac/*` | uninstall |
| `main/main.cpp` | 启动序 |
| `main/CMakeLists.txt` | 已 GLOB，无需改若放 sinan/ |
| `tools/*` `web/prototype/*` | 新建 |
| `docs/REFACTOR_SPEC.md` | 本文 |
| `AGENTS.md` | 期 1 结束后同步架构段落（另 PR） |
| **`main/sinan/design.h`** | **禁止改数值** |

---

## 16. 测试计划（可脚本化）

### 16.1 无硬件

1. Web 原型走通四层  
2. `export_design_tokens.py` 与 `tokens.json` 一致  

### 16.2 有硬件（serial）

```text
S                          # selftest ok
I approval                 # 假 prompt → Interrupt + Alert
a                          # 若 Normal → 批准
G                          # mood Praise→Sleep
W                          # Work
b                          # 切 agent + Tick
X                          # Action
a                          # HID OK（需系统已配对）
Q                          # Rest
# 慢滑 Rim → Pet；快滑 → beads rotate
```

### 16.3 人感验收（产品）

| # | 场景 | 通过标准 |
|---|------|----------|
| 1 | 桌上一整天只看寐 | 像器物不是屏保；珠有则有故事，无则安静 |
| 2 | prompt 来 | 不靠说明：振+声+抢屏；A/B 语义稳定 |
| 3 | Grave | 朱砂+长按填色团团；松手可撤 |
| 4 | 无 Mac daemon | HID 仍能 OK/NG |
| 5 | 无照片 | 邀请文案，不红不崩 |
| 6 | 闭眼辨 OK/NG | 仅靠声/振 |

---

## 17. 风险与缓解

| 风险 | 缓解 |
|------|------|
| NimBLE NUS+HID 同主机不稳定 | 先 HID 最小键盘；失败则 Setup 互斥模式 |
| 照片常驻 + shell 不卸载导致内存 | 继续最多 2 张 PSRAM；Interrupt 不 decode |
| 拨珠 vs Pet 误触 | 角速度阈值：`Δθ/Δt` 高=珠，低且持续=Pet |
| 范围膨胀 | 严格按 PR1→7；Almanac/Codex Micro 不进期 1 |
| 审美走样 | 任何 PR 禁止新色值；导轨只用 CINNABAR/MALACHITE/BRONZE |

---

## 18. 期 1 完成定义（DoD）

同时满足：

1. `design.h` 数值零 diff（允许新增 **注释** 或 `#include` 守卫外无数值的引用说明，但禁止改 constexpr）  
2. 开机默认寐（团团）  
3. 全局中断审批可用（Buddy）  
4. Action 层 + HID OK/NG 在至少一台 Mac 上验证  
5. 珠串 + GazeMood 日志可解释每一次动效  
6. 无 outrun 秒针；历不在主路径  
7. `haptics` 覆盖 OK/NG/Alert/Tick  
8. debug `S`/`I`/`G` 可用或 web 原型可演示同流  

---

## 19. 给施工 agent 的施工口令

按序开 PR，每个 PR 只做清单内文件：

```
/implement PR1 haptics from docs/REFACTOR_SPEC.md §5
/implement PR2 bridge_hid + Action UI §6
/implement PR3 shell + interrupt_view §7
/implement PR4 beads §8
/implement PR5 gaze_fsm + strip outrun §9
/implement PR6 work layer glue §11
/implement PR7 debug_cli + web prototype §13
```

每个 PR 描述必须包含：

- 对应规格章节  
- 验收 Checklist 勾选结果  
- 确认 `design.h` 无数值变更  

---

## 20. 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| v0.2 | 2026-08-08 | 首版可执行规格：四层壳、HID、珠串、Gaze FSM、PR 序 |
| v1.0 | 2026-08-08 | **期 1 已实施**。与规格的实施偏差：① interrupt_view 未单抽 —— Ward 已删，审批绘制只此一份，直接落在 layer_interrupt；② 直接落到 PR-B 形态 —— 层不是 mooncake App，AppSinan 是唯一常驻应用，launcher 退役（Setup 独立安装、Rest 层 B 长按直达）；③ SN_BOOT_APP 失去意义，开机即壳；④ 历 App 删除，数据仍经 WS 写入 State 供 Day 珠；⑤ 对讲为壳级覆盖层 talk_overlay，任意层中心长按可达；⑥ daemon 增 `event` 广播（DONE/ERROR）与 voice.inject（ASR 直注焦点窗口）。验收见 §16/§18 |
