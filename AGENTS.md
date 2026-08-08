# 司南 SINAN — 单文件交付包

> M5Stack StopWatch (C152) 桌面 AI 智能体控制台固件。全部规格与源码都在这一份文件里。
> 生成于 2026-08-08。

> **v3.1 当前实现覆盖说明（2026-08-08）**：用户已明确推翻本文旧版 Fleet/Echo 方案。
> Work 只允许 Codex、PASEO、Grok Build；Happy/HappyCode、Claude、知识库状态不进入 Work。
> 独立 Echo/Voice 应用不注册也不编译。编程语音只在 Work 内进行：B 切工具、A 进入；
> 工具内 A 开始录音、A 再次停止并转写，屏幕显示转写后由 B 明确发送。旧文中“松手即发”、
> “待办/灵感记录”和 `auto_send` 均已废止。源码与 `README.md` 高于下文附录中的旧快照。

---

## Codex 请从这里开始

这份文件既是规格说明，也是源码包。三步：

**第一步，落盘。** 把下面这段存成 `unpack.py`，然后 `python3 unpack.py SINAN.md`：

```python
#!/usr/bin/env python3
"""把 SINAN.md 里的源码全部落盘。用法：python3 unpack.py SINAN.md [目标目录]"""
import pathlib, re, sys, stat

src = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'SINAN.md')
dst = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else 'sinan')
text = src.read_text(encoding='utf-8')

pat = re.compile(r'<!-- FILE: (?P<path>[^\s]+) -->\n```[a-z]*\n(?P<body>.*?)\n```', re.S)
n = 0
for m in pat.finditer(text):
    p = dst / m.group('path')
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(m.group('body') + '\n', encoding='utf-8')
    if p.suffix == '.sh' or p.name == 'fetch_repos.py':
        p.chmod(p.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    n += 1
    print('  ', p)

# AGENTS.md 就是本文件的规格部分，直接整份拷过去，Codex 会自动读
(dst / 'AGENTS.md').write_text(text, encoding='utf-8')
print(f'落盘 {n} 个源文件 + AGENTS.md -> {dst}/')
```

它会按附录里的 `<!-- FILE: 路径 -->` 标记把每个代码块写到对应位置，并把本文件整份复制为 `AGENTS.md`。

**第二步，补上游。** 本包不含官方 MIT 代码（HAL、launcher、common），必须先拉：

```bash
cd sinan
./scripts/sync_upstream.sh     # 拉 m5stack/M5StopWatch-UserDemo 的 hal/launcher/assets
python3 fetch_repos.py         # 拉 components/（LVGL 9.5 / mooncake / M5GFX ...）
```

**第三步，编译到能烧录。**

```bash
idf.py set-target esp32s3
idf.py build
```

第一次 build 一定有错。修就是了，但有三条不能碰：
- 不要改 `main/sinan/design.h` 里的任何数值
- 不要改 `main/hal/`（上游代码，跟着上游走）
- `photo_store.cpp` 里标了 `NOTE(codex)` 的解码段，对照 `components/lvgl/src/draw/lv_image_decoder.h` 修正签名，但保持"解一次、存 PSRAM、之后只 blit"的契约

---

# 规格

> M5Stack StopWatch (C152) 桌面 AI 智能体控制台固件。
> 你(Codex)在这个仓库里干活时，先读完本文件再动代码。

---

## 0. 这是什么

一台常插电、放在 Mac 旁边的圆形终端。它做四件事：

| 应用 | 单字 | 职责 | 数据通道 |
|---|---|---|---|
| Ward | 守 | 显示 Claude 桌面端的权限请求，物理键批准/拒绝 | BLE (官方 buddy 协议) |
| Fleet | 阵 | 多模型 worker 的额度与运行状态雷达 | WiFi WebSocket |
| Echo | 问 | 按键录音 → Mac 转写 → CLI 执行 → 语音播报结果 | WiFi WebSocket + 音频流 |
| Almanac | 历 | 每日号码 / 身体黄历 / 年轮三个表盘 | WiFi WebSocket |
| **Gaze** | **望** | **待机页：边牧团团的照片，缓慢呼吸与漂移** | 本地，无需联网 |

三条通道彼此独立。任何一条挂掉，其余应用必须照常工作 —— 这是硬性架构要求，不要写出跨通道的隐式依赖。

**形态是桌面终端，不是手表。** 常插电、可常亮、不需要抬腕唤醒。所以：
- 不要为省电牺牲刷新率或视觉表现
- 但**必须**做防烧屏（见 §4 岁差）
- 交互距离约 60cm，主数字用 96px 以上

---

## 1. 硬件与工具链

**设备**：M5Stack StopWatch，SKU C152
- ESP32-S3R8，16MB Flash，8MB PSRAM（Octal，80MHz）
- 1.75" 466×466 圆形 AMOLED，CST820 电容触摸
- BMI270 IMU、RX8130 RTC、麦克风 + 喇叭、震动马达
- M5PM1 电源管理、M5IOE1 IO 扩展、450mAh 电池
- 按键：A、B、Power

**工具链**：ESP-IDF v5.5.4（不要用 6.x，上游 UserDemo 锁在 5.5.4）

```bash
python3 ./fetch_repos.py          # 拉 components/（首次必跑）
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

**烧录前必须让设备进下载模式**：长按电源键约 2 秒，直到内部绿色 LED 亮起。上传失败十有八九是忘了这步，不是代码问题。

FQBN / 端口找不到时：`ls /dev/cu.usbmodem*`，拔插 USB 重试。

---

### 1.1 性能预算（先读这节再写任何渲染代码）

全项目最容易翻车的地方，数字必须记住：

| 项 | 数值 |
|---|---|
| 一帧全屏 RGB565 | 466 × 466 × 2 = **424 KB** |
| 内部 SRAM 总量 | 512 KB（装不下双缓冲，单缓冲都紧） |
| PSRAM | 8 MB（带宽远低于 SRAM，帧缓冲放这里更慢） |
| 现实全屏刷新率 | **15–25 FPS 量级**，不是 60 |

推论，不要试图绕过：

1. **全屏每帧重绘的动画做不动。** 想要"动态"靠的是分层刷新率，不是提高帧率。
2. **底层慢，叠加层快。** 照片以 8 FPS 漂移（每帧挪 1–2px），叠加的小点以 30 FPS 走，后者只脏 24×24 像素，成本可忽略。
3. **绝不在每帧做缩放或旋转。** `lv_image_set_scale` 是软件重采样，全屏尺寸下一帧吃掉全部预算。要平移就预先放大好、只改 offset。
4. **绝不在每帧解码 JPEG。** 开图时解一次到 PSRAM，之后只 blit。

sdkconfig 里这几条必须打开：

```
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y
CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y
CONFIG_LV_MEMCPY_MEMSET_STD=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

---

## 2. 代码基座与继承关系

本项目 fork 自 `m5stack/M5StopWatch-UserDemo`（MIT）。**HAL 层、launcher、构建系统原样保留，不要改**。我们只在 `main/apps/` 下加应用、在 `main/sinan/` 下加公共设施。

保留的上游资产：
- `main/hal/` —— 全部 HAL。用 `GetHAL()` 拿单例，接口见 `main/hal/hal.h`
- `main/apps/app_launcher/` —— 环形启动器，新应用自动出现在里面
- `main/apps/common/key_manager/` —— 按键事件抽象
- `components/` —— mooncake 2.3.3 / LVGL 9.5.0 / smooth_ui_toolkit 2.12.1 / M5GFX / ArduinoJson 7.4.3 / BMI270

### 应用生命周期（mooncake AppAbility）

每个应用继承 `mooncake::AppAbility`，实现四个回调：

```cpp
class AppWard : public mooncake::AppAbility {
public:
    AppWard();                      // 只设置 setAppInfo().name / .icon
    void onCreate() override;       // 安装时调用一次
    void onOpen() override;         // 打开：建 UI
    void onRunning() override;      // 每帧：轮询、更新
    void onClose() override;        // 关闭：销毁 UI、释放资源
};
```

在 `main/main.cpp` 里 `GetMooncake().installApp(std::make_unique<AppWard>());` 注册。

### 三条铁律

1. **任何 LVGL 操作必须持锁**。用 `LvglLockGuard lock;`（RAII，见 `hal.h`）。忘记加锁 = 随机崩溃，且崩溃点离现场很远，极难查。
2. **`onClose()` 里必须把所有 `lv_obj_t*` 置空、把所有 `unique_ptr` reset**。mooncake 会复用实例，残留指针会指向已销毁的对象。
3. **`onRunning()` 里不要阻塞**。网络 IO 全部在独立 FreeRTOS task 里做，通过 `sinan::State` 的快照传数据（见 §5）。

### UI 一律用原生 LVGL C API

不要用 `smooth_ui_toolkit` 的 `lvgl_cpp` 包装类写新代码。原因：包装类版本跟着组件走，接口会漂；原生 C API 对着 LVGL 9.5 文档写，稳定可查。
`smooth_ui_toolkit` 只用它的缓动/动画数值（`smooth_ui_toolkit::AnimateValue`），或者直接用 `lv_anim_t`。

---

## 3. 视觉系统「司南」

这块屏是一件圆形仪器，不是一块方屏。**整套 UI 里不允许出现矩形卡片、列表行、方形按钮。** 所有元素是弧、刻度、极坐标定位的文字。

### 3.1 色板（矿物颜料，非通用暗色主题）

定义在 `main/sinan/design.h`，**不要在别处硬编码颜色**。

| Token | Hex | 用途 |
|---|---|---|
| `SN_INK` | `#000000` | 底色。AMOLED 纯黑不发光，既省电又不烧屏。永远不要用深灰当背景 |
| `SN_LACQUER` | `#141210` | 弧的底槽，唯一允许的"接近黑但不是黑" |
| `SN_BRONZE` | `#C8A96E` | 鎏金。刻度、次级标签、静默态 |
| `SN_SILK` | `#F2EDE1` | 生宣白。主数字、主文本 |
| `SN_CINNABAR` | `#D6442F` | 朱砂。危险操作、拒绝 |
| `SN_MALACHITE` | `#4FA88A` | 石绿。健康、通过 |
| `SN_INDIGO` | `#3B4C8C` | 靛青。静默、待机、无事发生 |

一屏之内最多两个语义色 + 鎏金。同时出现三种以上语义色说明信息架构错了，回去重新分层。

### 3.2 四个半径带

屏幕 466×466，圆心 (233, 233)，最大半径 233。

| 带 | 半径 | 内容 |
|---|---|---|
| Rim 缘 | 210–228 | 状态光环。两米外唯一能看清的东西。弧长和颜色编码全局状态 |
| Orbit 轨 | 150–205 | 数据环。分段弧、刻度、进度 |
| Core 核 | 0–145 | 主内容。大字号数字或文本 |
| Chord 弦 | y > 355 | 上下文标签条。小字，永远居中 |

**留 20px 物理安全边**：任何元素不得超出半径 213，圆形屏边缘有可视裁切。

### 3.3 字体

| 角色 | 字体 | 场景 |
|---|---|---|
| 数字 | `CommissionerMedium108` / `64` | Core 带的主数字 |
| 数据/英文 | `lv_font_maple_mono_medium_24/28/48` | 命令、路径、token 数、等宽对齐场景 |
| 中文 | `lv_font_sinan_serif_28/40`（**需自行生成**） | 中文标签、黄历文案 |

中文字体上游没有，必须自己做子集：

```bash
# 用 LVGL 官方 font converter（Node）
npx lv_font_conv --font SourceHanSerifSC-Medium.otf \
  --size 28 --bpp 4 --format lvgl \
  --symbols "$(cat main/assets/fonts/sinan_cjk_subset.txt)" \
  -o main/assets/fonts/lv_font_sinan_serif_28.c
```

用**思源宋体**不是黑体 —— 宋体的横细竖粗在鎏金色上有金石感，跟司南的器物调性一致；黑体会让整个界面看起来像一个手机 App。
子集字表维护在 `main/assets/fonts/sinan_cjk_subset.txt`。加了新中文文案就往里加字，重新生成。**不要**全量嵌入中文字库，16MB Flash 撑得住但没必要。

### 3.4 运动

- 一切变化是**弧的生长与退让**，不是滑入滑出。没有 slide 动画。
- 缓动统一 `lv_anim_set_path_cb(&a, lv_anim_path_ease_out)`，时长 320ms。状态切换 480ms。
- 呼吸类动画周期 ≥ 2600ms。快过这个数就显得焦躁，不高级。
- 不要同时跑三个以上动画。

---

## 4. 岁差 Precession —— 防烧屏，也是签名设计

AMOLED 常亮必然烧屏。解法不是定时息屏（那会毁掉"常驻仪表"的核心价值），而是让整个 UI 缓慢自转。

`main/sinan/precession.cpp` 提供一个根容器，**所有应用的 UI 必须挂在它下面，不能直接挂 `lv_screen_active()`**：

```cpp
lv_obj_t* root = sinan::ui::precession_root();   // 拿根容器
lv_obj_t* my_arc = lv_arc_create(root);          // 挂上去
```

机制：
- 根容器每 60 秒绕圆心旋转 `0.3°`，20 小时转满一周
- 用 `lv_obj_set_style_transform_rotation()` + `transform_pivot_x/y = 233`
- 附加：整屏亮度按环境和状态在 25%–90% 之间浮动，静默态自动降到 25%
- 附加：Core 带的大字数字每 5 分钟额外做 ±3px 的亚像素抖动

**不要因为"看不出效果"就把它关掉。** 这是三个月后设备还能用的唯一原因。

---

## 5. 状态与并发模型

`main/sinan/state.h` 定义一个全局单例 `sinan::State`，是**唯一**的跨线程数据交换点。

- 网络任务（BLE / WS）在自己的 FreeRTOS task 里跑，只写 State
- 应用在 `onRunning()` 里读 State 的快照
- 用 `portMUX_TYPE` 自旋锁保护，临界区里只做结构体拷贝，不做任何 IO、不碰 LVGL

```cpp
auto snap = sinan::State::get().snapshot();   // 拿一份拷贝，之后随便用
if (snap.ble.has_prompt) { ... }
```

**绝对不要**在 BLE 回调或 WS 回调里直接操作 LVGL。回调里只能写 State。

---

## 6. 通道一：BLE（官方 buddy 协议）

实现在 `main/sinan/bridge_ble.cpp`。这是 Anthropic 官方协议，**不要自创字段**。权威规范：`anthropics/claude-desktop-buddy` 的 `REFERENCE.md`。

### 角色与传输

设备是 **GATT 外围端（peripheral）**，桌面端扫描并连接。

Nordic UART Service：
| | UUID |
|---|---|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX（桌面 → 设备，write） | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX（设备 → 桌面，notify） | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

- 广播名**必须以 `Claude` 开头**，否则桌面端的设备选择器过滤不到。我们用 `Claude-SINAN-XXXX`（末尾是 MAC 后两字节）。
- 线上全是 UTF-8 JSON，**一行一个对象，以 `\n` 结尾**。notify 会在 MTU 边界分片，接收端必须自己缓冲累积到 `\n` 再解析。
- **要求加密**：把 NUS 特征和 TX 的 CCCD 标记为 encrypted-only，广播 DisplayOnly IO 能力，首次 GATT 访问触发 OS 配对、屏幕显示 6 位配对码。transcript 片段和工具调用提示走这条链路，明文可被附近的 nRF dongle 嗅探。**不要为了省事跳过配对。**

### 桌面端 → 设备

**心跳快照**（状态变化时发，另外每 10 秒一次保活）：

```json
{"total":3,"running":1,"waiting":1,"msg":"approve: Bash",
 "entries":["10:42 git push","10:41 yarn test"],
 "tokens":184502,"tokens_today":31200,
 "prompt":{"id":"req_abc123","tool":"Bash","hint":"rm -rf /tmp/foo"}}
```

- `prompt` **只在需要决策时存在**。`prompt.id` 是回传时必须原样带回的凭据。
- 超过 30 秒没收到快照 → 判定连接已死，切静默态。

**回合事件**（一次性，>4KB 会被桌面端丢弃）：
```json
{"evt":"turn","role":"assistant","content":[{"type":"text","text":"..."}]}
```

**连接时的一次性消息**：
```json
{"time":[1775731234,-25200]}          // epoch 秒 + 时区偏移秒
{"cmd":"owner","name":"Felix"}
```

**第二项不能丢。** 早期版本只取了 epoch，系统时间被设成 UTC 而时区从没设过 —— 在上海，望页会显示比实际早 8 小时的时间，而且看起来一切正常：数字在动、秒针在跑，只是不对。

偏移量要转成 POSIX TZ 交给 `GetHAL().setTimezone()`，注意 **POSIX 的符号是反的**：UTC+8 要写成 `"UTC-8"`。

**命令**（每个带 `cmd` 的都要回 ack）：`status` / `name` / `owner` / `unpair`，以及文件夹推送的 `char_begin` / `file` / `chunk` / `file_end` / `char_end`。

ack 格式：`{"ack":"<同 cmd>","ok":true,"n":0}`

`status` 的 ack 要带数据，桌面端每隔几秒轮询它填充统计面板：
```json
{"ack":"status","ok":true,"data":{
  "name":"SINAN","sec":true,
  "bat":{"pct":87,"mV":4012,"mA":-120,"usb":true},
  "sys":{"up":8412,"heap":84200},
  "stats":{"appr":42,"deny":3,"vel":8,"nap":12,"lvl":5}}}
```
`bat.mA` 为负表示在充电。没有的字段可以省略。

### 设备 → 桌面：权限决策

```json
{"cmd":"permission","id":"req_abc123","decision":"once"}
{"cmd":"permission","id":"req_abc123","decision":"deny"}
```

`id` 必须与 `prompt.id` 完全一致 —— 这是防止误批准另一个请求的唯一保险。**发送前必须校验 id 与当前显示的 prompt 一致**，不一致就丢弃并重新拉取状态。

### 桌面端启用方式（写给用户，不是给你）

Help → Troubleshooting → Enable Developer Mode → Developer → Open Hardware Buddy… → Connect。
注意这条通道连的是 **Claude 桌面 App**（含其中的 Code / Cowork 会话），不是终端里的 `claude` CLI。终端场景走 §7 的 WebSocket + hooks。

---

## 7. 通道二：WiFi WebSocket（自建）

设备是 WS **客户端**，连 Mac 上的 `daemon/sinand.py`（默认 `ws://<mac-ip>:8790/sinan`）。

实现在 `main/sinan/bridge_ws.cpp`，用 `esp_websocket_client`（IDF 组件 `espressif/esp_websocket_client`）。

### 设备 → daemon

```json
{"t":"hello","dev":"sinan","ver":"0.1.0","mac":"..."}
{"t":"asr_begin","rate":16000}
{"t":"asr_chunk","seq":0,"pcm":"<base64 s16le mono>"}
{"t":"asr_end"}
{"t":"act","id":"daily_number"}          // 触发 daemon 白名单动作
{"t":"pong","ts":123456}
```

### daemon → 设备

```json
{"t":"fleet","ts":1775731234,"workers":[
  {"id":"codex","label":"CODEX","state":"run","quota":0.62,"task":"refactor hal"},
  {"id":"minimax","label":"MM","state":"idle","quota":0.91,"task":""},
  {"id":"grok","label":"GROK","state":"stall","quota":0.08,"task":"waiting api"},
  {"id":"claude","label":"CC","state":"run","quota":0.44,"task":"PLAN.md"}]}

{"t":"almanac","number":{"code":"0731","title":"..."},
 "huangli":{"trend":"up","yi":"深度工作","ji":"连续会议"},
 "ring":[{"doy":123,"tag":"盟史 #412"}]}

{"t":"say","text":"今天的号码是 0731","pcm":"<base64 16k s16le>"}
{"t":"asr_result","text":"帮我看一下今天的日报"}
{"t":"ping","ts":123456}
```

**约定**：`quota` 是 0.0–1.0 的剩余比例，`state` 只能是 `run` / `idle` / `stall` / `down` 四选一。daemon 负责把各家 CLI 的额度口径归一化，设备端不做业务判断。

断线策略：指数退避重连，上限 30 秒。断线期间 Fleet 和 Almanac 显示上次快照并把 Rim 环变靛青（表示数据陈旧），**不要显示错误弹窗** —— 桌面仪表出现弹窗是设计事故。

---

## 8. 通道三：音频

用 HAL 现成接口，不要自己碰 I2S：

```cpp
GetHAL().audioRecord(data, durationMs, gain);   // std::vector<int16_t>
GetHAL().audioPlay(data, async);
GetHAL().getAudioSampleRate();
GetHAL().updateAudioSpectrum();
GetHAL().getAudioSpectrum();     // 20 段频谱，正好沿圆周每 18° 一段
```

`AudioSpectrumFrame::bandCount == 20`。Echo 应用的电平环直接用这 20 段映射到圆周，一段 18°。这是上游白送的、参考方案完全没用的东西。

### 9.1 松手即发（产品契约，不要加"确认发送"）

```
按住 A  → 录音（listening）
松开 A  → 立刻发 asr_end → 转写 → 自动执行 → TTS 播报
```

**中间不允许有第二步确认。** 说完话还要再按一下确认，是把一句话的成本变成两个动作，这个功能就没人用了。只有 `config.toml` 里显式 `auto_send = false` 才会停在 `await_confirm`。

三条不发送的岔路，都写进 `State.voice.note` 给人看：

| 情况 | 表现 |
|---|---|
| 没有 WS 链路 | `begin()` 直接不开录，弦区 `no link`。**不要先录完再告诉人发不出去** |
| 录音短于 280ms | `too short`，多半是误碰 |
| 转写结果为空 | `didn't catch that`，不往下走 agent |

daemon 每一步回一帧 `voice_status`，设备弦区才能显示 `transcribing` / `running`：

```json
{"t":"voice_status","phase":"transcribing"}
{"t":"asr_result","text":"..."}
{"t":"voice_status","phase":"running"}
{"t":"say","text":"...","pcm":"<base64 s16le>"}
```

否则用户面对的是一个转圈的环，不知道卡在哪一步、还要不要等。

这条契约有可跑的测试守着：`python3 daemon/test_voice_autosend.py` 必须 ALL PASSED。**改语音链路后跑一次。**

### 9.2 音频接口

**不接小智**。语音只做本地链路：录音 → WS 上传 → Mac 端 whisper 转写 → 交给用户的 CLI → 结果 TTS 回传播放。数据不出内网，也不引入第二套协议栈。

---

## 9. 望 Gaze —— 团团待机页

这是设备 90% 的时间在显示的东西，也是唯一一个别人看见会问"这什么"的页面。做不好整台设备就是个工具；做好了它是一件物品。

**团团是一只红白毛的边境牧羊犬，睡觉时把自己蜷成一个正圆。** 而这块屏也是正圆。

所以这一页的正确形态不是"圆屏里放一张照片"，而是**团团本身就是那块表盘**：把他从背景里抠成一枚圆盘，边缘羽化，四周留纯黑。AMOLED 上黑像素物理熄灭，于是他没有边缘，像一件实物躺在表壳里。

顺带一提，他叫团团，而「团」本来就是圆、是蜷成一球的意思。整页设计的答案写在他名字里。

还有一件巧事：司南的色板取自漆器与矿物颜料，鎏金 `BRONZE` 和生宣 `SILK`，恰好就是团团身上那两种毛色。所以整机的配色天生就跟这只狗是一套的，**不要另配一套调色**。

### 9.1 设计意图（改代码前先理解，不要"优化"掉）

**照片要浮起来，不能有边。** 圆形照片直接放上去会露出一圈生硬的边缘，一眼廉价。做法是让照片外缘被同心弧堆成的晕影吃掉，最外层完全是纯黑 —— AMOLED 上纯黑意味着像素物理熄灭，所以照片不是"在一个圆里"，而是**悬浮在黑暗中，没有边界**。这一条是整个页面高级感的来源，其他都是加分项。

**它要呼吸。** 晕影半径以每分钟 18 次的频率做 ±6px 的收放。成年犬静息呼吸约每分钟 15–30 次，18 是个安稳的值。因为照片里他本来就在睡，这个效果不是隐喻——你看到的就是一只睡着的狗在起伏。这比任何粒子特效都有效，而且只重绘晕影那一环，成本几乎为零。

**秒针是团团在跑外圈。** 边牧的本职工作叫 outrun —— 绕着羊群跑一个大弧把它们收拢。所以这个页面没有传统秒针，取而代之的是一个鎏金小点沿 Rim 环每 60 秒跑一圈，身后拖一道会淡去的短痕，像草地上的跑道。这是签名元素，**不要换成常规秒针**。

**照片永远在缓慢漂移。** 走李萨如路径（x 与 y 周期取 14 分钟和 19 分钟，互质），所以轨迹准周期、肉眼看不出重复。这同时是防烧屏 —— 照片像素从不静止。

**倾斜有视差。** IMU 检测到倾斜时，照片层朝反方向偏 ±5px，时间层朝同方向偏 2px。幅度很小，但这是"活的"和"贴了张图"的分界线。

**默认不显示时间。** 团团蜷成的正圆本身就是钟面，上面不该压任何东西。默认版式「寐」只有他 + 外缘一道鎏金分钟弧（走满即一小时）+ 那颗跑外圈的点。想知道几点，敲一下桌子，时间露出六秒再退回去。

需要常显时间时切到「时」版式：数字锚在下三分之一，上面盖一层从透明到黑的竖向渐变作衬底。**绝不要把数字甩在正中央压住团团的头。**

### 9.2 分层与刷新率

| 层 | 内容 | 刷新率 | 每帧脏区 |
|---|---|---|---|
| 0 照片 | PSRAM 里的 RGB565，只改 offset | 8 FPS | 全屏（但每帧只挪 1–2px） |
| 1 晕影 | 24 道同心弧，只改半径与不透明度 | 呼吸时 10 FPS | 外圈 40px 环 |
| 1.5 分钟弧 | 鎏金细弧，寐版式专属 | 每秒 1 次 | 外缘一环 |
| 2 衬底 | 竖向渐变，静态 | 0 | 无 |
| 3 时间 | 大字数字 | 每分钟 1 次 | 数字外接框 |
| 4 外圈点 | 鎏金小点 + 拖痕 | 30 FPS | 约 24×24 px |

**这张表是性能约定，不是建议。** 任何让第 0 层跑到 30 FPS 的改动都会把总线打满，见 §1.1。

### 9.3 交互

| 输入 | 行为 |
|---|---|
| 触摸 | 换下一张照片，900ms 交叉淡入 |
| A 短按 | 切版式：寐（纯团团 + 分钟弧）/ 时（底部时间）/ 忆（好日子） |
| A 长按 | 锁定当前照片，不再自动轮换 |
| B | 返回 launcher |
| 敲桌面（IMU 冲击） | 亮度提到 60%，露出时间与日期，6 秒后回落。寐版式下这是唯一的看表方式 |
| 倾斜 | 视差 |

**Ward 收到 prompt 时自动从望切到守。** 待机页再好看，也不能挡住一个等着批准的请求。

### 9.4b 喜 —— 照片轮播与好日子的环

**这一页的概念不是"变老"，是"变化"，而且要落在快乐上。** 这条不是文案偏好，是结构约束：

早期设计是"圆周从他来家那天铺到今天"。那是一条**进度条**，而进度条有终点 —— 看久了是在数余额。**不要做这个。**

正确的形态是**收藏**：

- 环上是一颗一颗刻度，**每张照片一颗，一颗就是一个好日子**
- 刻度按 EXIF 拍摄日期分布在圆周上，但**只占 300°，留 60° 空着**。那 60° 是还没发生的好日子
- **加照片只增不减。环永远不会满，只会越来越密。**
- 弦区**不写"X 岁 Y 个月"**。年龄是减法。写那天是什么日子：海边、过年、抢飞盘。场景是加法
- 没有 caption 时退回显示日期（`2022.03.19`），也不要退回年龄

同一批照片、同一个圆，只换这一件事，情绪就反过来了。**改这一节前先把这段读完。**

### 9.4c 日期只能来自 EXIF

`manifest.json` 的 `items[].date` 由 `prep_photos.py` 从 EXIF `DateTimeOriginal` 读出，其次认文件名前缀 `YYYY-MM`。取不到就是 `null`。

**任何地方都不允许手打日期，包括示意图和预览。** 手打过一次就错过一次（把 2020.10 写成了 2019.05），而且错得很难发现 —— 渲染出来的图看着一切正常。设备端和预览端一律从 manifest 读。

`date` 为 `null` 的照片照常进轮播，只是不落在喜的环上。微信转发会剥掉 EXIF，所以实际到手的照片里有大半没有日期，这是常态不是异常。

### 9.5 团团点阵字形（守也要用）

`prep_photos.py` 会从第一张照片额外生成一张 `glyph.png`：**极坐标点阵半调**——同心圆环上撒点，点径跟着照片明暗走。

为什么不做剪影：团团蜷着的轮廓就是个圆，剪影出来认不出是狗。半调点阵能保住白毛的扫尾、蜷起的脸、圆滚的身子，而且"全是弧和点"正好是这套设计语言本身。

存成 **RGBA PNG（RGB 全白，alpha 存点的浓度）**，约 16KB。设备端用 `lv_obj_set_style_image_recolor` 按状态染色——只动 RGB，点的疏密原样保留。**不要改成往固件里塞 C 数组**：那是 68KB 的 flash，而且换照片时字形不会跟着更新。

用在三个地方：

1. **守 · 静默**：中心不是一颗光秃秃的鎏金点，是团团的幽灵（暗鎏金，26% 不透明）。这一屏本来就没别的信息。
2. **守 · 长按确认**（重点，见 §11）。
3. 往后的开机画面和阵的空态。

---

### 9.4 实现要点

完整源码见 `main/sinan/photo_store.{h,cpp}` 与 `main/apps/app_gaze/app_gaze.{h,cpp}`。几处不能改的契约：

- **照片解一次、存 PSRAM、之后只 blit。** 一张 536×536 RGB565 是 574KB，同时最多驻留两张（当前 + 交叉淡入的下一张）约 1.15MB，在 8MB PSRAM 里很宽裕。渲染循环里绝不解码。
- **漂移只改 `lv_image_set_offset_x/y`。** 源图 536×536 比屏幕大 70px，就是给漂移留的余量。改 offset 是纯 blit；用 `lv_image_set_scale` 会触发软件重采样，全屏尺寸下一帧就吃掉预算。
- **晕影用同心弧堆，不用带 alpha 的位图。** 三个好处：不占 flash、半径可运行时改（呼吸靠这个）、天然属于"一切都是弧"的设计语言。不透明度用平方曲线 `opa = 255 * t²`，线性过渡会看出台阶。
- **解码要宽容颜色格式。** TJPGD 在不同配置下会吐 RGB565 也会吐 RGB888/XRGB8888。早期版本只认 RGB565，遇到 888 就整张丢掉 —— 望页全黑，日志里只有一行 `decode failed`，非常难查。现在三种都接，一次性转成 RGB565 存进 PSRAM，并把真实的 `cf/w/h/stride` 打进日志。**尺寸必须严格是 536×536**（拿原图 memcpy 进 574KB 缓冲，崩点会离现场很远）。
- **`onSwapDone()` 必须是 public**，交叉淡入的 `lv_timer` 回调要调它。

---

## 10. 照片管线

### 10.1 准备照片（Mac 上跑一次）

```bash
python3 scripts/prep_photos.py ~/Pictures/团团/*.jpg
# 自动圈主体不准时手工指定，比调参快：
python3 scripts/prep_photos.py --center 1136,534 --radius 468 一张.jpg
```

只要 pillow + numpy，不需要 ImageMagick，也不需要抠图模型。流程是：

1. **圈出主体的外接圆**。默认按冷暖分割猜（地板毯子偏冷、狗偏暖），猜不准就用 `--center/--radius`。
2. **只在最外一圈环带上压残留背景**（半径 0.86–1.0）。这个半径限制不能去掉——不限半径的话，白毛的阴影会被当成地板一起啃掉，实测踩过。
3. **羽化的圆形遮罩**，高斯 12px。软边有两个作用：看起来像浮在黑里，而且不给 JPEG 的 DCT 留硬边去振铃。
4. **合成到纯黑画布**，主体直径占 72%，四周留黑给晕影。
5. **饱和度 +8%**。AMOLED 色域比显示器宽，原样放上去偏平；加一点在这块屏上刚好，多了就俗。

一张 536×536 q92 约 42KB，四十张才到 1.7MB 的推送上限。

### 10.1a 构图规矩（这几条决定这一页好不好看）

圆屏上的一张照片好不好看，不是"主体在不在框里"的问题。三条硬规矩：

1. **脸约占圆直径六成。** 更小显得空，更大会切耳朵。
2. **眼睛落在圆的四成高度，不是正中。** 正中会显得头重脚轻——这是人像的老规矩，圆形构图里更明显。
3. **背景越少越好。** 晕影只吃得掉四个角，中间那一大片灰地砖它救不了。宁可裁紧到有点软，也不要构图松垮：60cm 观看距离上，构图坏比轻微软化明显得多。

`scripts/crops-tuan.json` 是照这三条逐张定出来的，不是算出来的。**这类参数没有公式**，我按上面三条来回调了五轮才收敛，其中两轮还把左右方向弄反了（`cx` 增大是把主体往**左**推，因为裁框在往右移）。换照片就用 `pick_crops.html` 重来一遍，别指望自动。

**默认用居中方裁，不要猜圆心。** 手机拍宠物基本都是居中构图，所以

```bash
python3 scripts/prep_photos.py --exif-only --zoom 1.35 照片...
```

就够了：只收有 EXIF 拍摄日期的照片，取画面正中的方形区域，`--zoom` 控制收紧程度。**不猜圆心就不可能把主体裁偏**，这是它比任何检测都强的地方。

`--zoom` 实测：1.15 主体偏小，1.60 会切到头顶，**1.35 是最佳平衡**，已设为默认。

只有主体确实不在中心的照片才需要单独处理，用 `scripts/pick_crops.html` 圈一下写进 `crops.json`。

**不要再写自动检测。** 试过两套颜色启发式：按冷暖分割（木地板也是暖的，圈进半个客厅）、按白毛定位（白毛偏离头心，半径全靠蒙）。两套都在四成照片上失手。任意宠物照的主体定位不是颜色启发式能解的问题。

正确流程是 `scripts/pick_crops.html`：浏览器打开，拖进照片，每张拖一个圈（圈外实时压暗，所见即设备上的样子），导出 `crops.json`，再 `--crops crops.json` 喂给管线。十几张照片十分钟，每张都对。`find_subject()` 只是没给 crops 时的兜底，不要指望它。

**两种裁法**：`disc` 用于他蜷成球时（主体本身是圆的，占 72%，外环带清背景）；`portrait` 用于坐姿站姿（裁到头，占 90%，晕影收到 0.60，不清背景 —— 这类照片背景常是暖木色，跟毛色同色系，清了会把毛一起啃掉）。

**`ImageOps.exif_transpose` 不能省。** 手机竖拍的照片元数据里带旋转标记，不应用的话狗是躺着的。从 ImageMagick 换到 PIL 时把 `-auto-orient` 弄丢过一次。

### 10.2 送进设备：拖到 Hardware Buddy 上

官方 BLE 协议的文件夹推送原文就说传输内容不限、1.8MB 以内。所以把 `build/tuantuan/` 直接拖到 Claude 桌面端的 Hardware Buddy 窗口，照片就流进设备了。`manifest.json` 里的 `"name":"tuan"` 会覆盖文件夹名，设备据此写进 `/spiflash/tuan/`。

`bridge_ble.cpp` 里这套**已经实现**（`char_begin` 建目录并清空旧照片 → `file` 开文件 → `chunk` 解 base64 落盘 → `file_end` 关闭 → `char_end` 调 `photo::rescan()` 并震动一下）。协议严格串行（发一块等一个 ack），所以不缓冲整个文件，收到就写。

文件名经过 `sanitize_name()` 消毒：只取 basename，拒绝 `..`、绝对路径和任何分隔符。

### 10.3 备用路径

不想用 BLE 推送的话，官方固件自带的徽章上传也能用：`GetHAL().startBadgeEditModeViaAp()` 起一个配网 AP，手机连上网页上传，文件落在 `/spiflash/badge/slot_N.*`。把 `photo_store.cpp` 的 `DIR` 改成那个目录即可。

### 10.4 还没有照片时

`app_gaze` 会显示一行 `drop a folder of photos on Hardware Buddy`。**空态是邀请，不是错误** —— 不要改成红色警告或错误码。

---

## 10.5 前台应用路由

`main/sinan/route.h`。有权限请求进来时，用户不应该还要自己去 launcher 里点开守。

- `main.cpp` 在 `installApp` 之后用返回的 id 调 `route::register_app(name, id)`
- 望/阵/问/历 在 `onRunning` 开头调一次 `route::yield_to_ward_if_needed()`，返回 true 就立即 return
- 守处理完请求、落定态结束时调 `route::return_home_if_auto()` 回到开机应用
- 开机应用由 `config.h` 的 `SN_BOOT_APP` 决定，默认 `"Gaze"`；空字符串则停在 launcher

**不要把路由逻辑散进各个应用。** 判断在 route 里，应用只调用。

问在录音中不让位（先让这一段录完），其余应用立即让位。

---

## 11. 交互约定

全设备统一，不要在单个应用里发明新手势。

| 输入 | 行为 |
|---|---|
| A 短按 | 当前应用的主操作（Ward=批准 / Echo=录音开始 / Almanac=切表盘） |
| A 长按 0.8s | 危险确认。**只有危险操作走长按** |
| B 短按 | 次要操作（Ward=拒绝 / Fleet=切 worker） |
| B 长按 | 返回 launcher（等价 `KeyEvent::GoHome`） |
| 触摸滑动 | 环形导航 |
| 敲击桌面 | IMU 检测冲击 → 唤醒/亮屏。阈值见 `design.h` 的 `SN_TAP_G` |

用 `input::KeyManager` 拿 `GoHome` / `GoPrevious` / `GoNext`，直接读 `GetHAL().btnA/btnB` 拿长按短按。

**危险操作判定**在 `main/sinan/danger.h`：命中 `rm -rf`、`sudo`、`git push --force`、`> /dev/`、`dd if=`、`chmod 777`、`curl | sh` 等模式时，Ward 的 Rim 环变朱砂，且批准必须长按 A。这条规则宁可误报不可漏报。

---

## 12. 目录结构

```
sinan/
├── AGENTS.md                  # 本文件
├── CMakeLists.txt
├── fetch_repos.py             # 上游原样保留
├── repos.json                 # 组件清单
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── main.cpp               # 注册应用
│   ├── hal/                   # 上游 HAL，不要改
│   ├── assets/                # 上游资源 + 我们的中文字体
│   ├── sinan/                 # 公共设施
│   │   ├── design.h           # 色板 / 半径 / 字体 / 时长。改视觉只改这里
│   │   ├── ring.h/.cpp        # 圆形 UI 原语（弧、刻度、极坐标、大字）
│   │   ├── precession.h/.cpp  # 岁差根容器 + 防烧屏
│   │   ├── state.h/.cpp       # 全局状态快照
│   │   ├── danger.h/.cpp      # 危险命令判定（含 selftest 样本）
│   │   ├── route.h/.cpp       # 前台应用路由
│   │   ├── photo_store.h/.cpp # 照片解码与 PSRAM 缓存
│   │   ├── bridge_ble.h/.cpp  # 官方 buddy 协议
│   │   ├── bridge_ws.h/.cpp   # WebSocket 客户端
│   │   └── voice.h/.cpp       # 录音上传 / 播放
│   └── apps/
│       ├── apps.h
│       ├── app_ward/          # 守
│       ├── app_fleet/         # 阵
│       ├── app_echo/          # 问
│       └── app_almanac/       # 历
├── daemon/
│   ├── sinand.py              # Mac 端服务，纯标准库
│   ├── config.example.toml
│   └── install.sh
└── scripts/
    ├── sync_upstream.sh
    ├── build_cjk_font.sh
    └── prep_photos.sh         # 照片预处理
```

---

## 13. 分期与验收

> **当前状态（2026-08-07）**：全部条目**未验证**。代码写完了，但没有真机、没跑过 `idf.py build`。
> 下面每一条都要在硬件到手后亲手勾，不要因为"看起来实现了"就打勾。

**第 1 期**：骨架 + BLE + 守 + 望
- [ ] 项目能 build、能烧录、launcher 里出现五个应用图标
- [ ] 开机直接进望，不停在 launcher
- [ ] 配对时六位码在屏上，能在 macOS 弹窗里输完成配对
- [ ] 从望/阵/历里都能被 prompt 自动拉到守，处理完自动回望
- [ ] `sinan::selftest()` 危险规则样本全过
- [ ] 岁差根容器工作，肉眼不可见但 20 小时转满一周
- [ ] 设备以 `Claude-SINAN-XXXX` 广播，桌面端 Hardware Buddy 能扫到并配对成功（带加密）
- [ ] 收到 `prompt` 时震动 + Rim 环变琥珀 + 倒计时收缩
- [ ] A 批准 / B 拒绝，`id` 原样回传，桌面端确实执行
- [ ] 危险命令触发朱砂环 + 长按才能批准
- [ ] 断连 30 秒后自动切静默态，不崩
- [ ] 望：照片能显示、晕影没有硬边、呼吸和漂移肉眼可见但不烦人
- [ ] 望：照片层 8 FPS 时外圈点仍然顺滑（这是分层刷新率的验收点）
- [ ] 照片文件夹能从 Hardware Buddy 拖进设备并自动生效

**第 2 期**：WS 通道 + Fleet + Almanac
**第 3 期**：Echo 语音

每期结束跑一遍：连续运行 24 小时不重启、不内存泄漏（`heap_caps_get_free_size` 曲线平），断网断电各来一次。望要单独挂 8 小时看有无残影。

---

## 14. 常见坑

| 症状 | 原因 |
|---|---|
| 随机崩溃，栈回溯指向 LVGL 内部 | 忘了 `LvglLockGuard` |
| 上传失败 | 没进下载模式：长按电源键 2 秒到绿灯亮 |
| 桌面端扫不到设备 | 广播名没有以 `Claude` 开头 |
| JSON 解析失败 | notify 在 MTU 边界分片了，没做 `\n` 缓冲累积 |
| 批准了但桌面端没反应 | `id` 没原样回传，或多了空格 |
| 中文显示空白 | 字体子集里没有这个字，重新生成 |
| WiFi 连上但 BLE 断 | S3 单天线共存，WS 心跳间隔调到 ≥ 15 秒 |
| 屏幕出现残影 | 岁差被关掉了 |
| 望卡顿掉帧 | 照片层被提到了 30 FPS，或每帧在做缩放/解码 |
| 照片躺着显示 | 漏了 `ImageOps.exif_transpose` |
| 日期不对 | 某处手打了日期。一律从 manifest 读 |
| 主体太小或切了头 | 调 `--zoom`；个别照片主体不在中心的用 `pick_crops.html` 单独圈 |
| 照片有一圈硬边 | 晕影最外层没到纯黑，或没到 `VIG_OUTER` 236 |
| portrait 照片背景没吃干净 | 晕影内缘用了 disc 的 174，应该是 140 |
| 忆的文案和弦区叠在一起 | caption 必须让到 center+96，弦区固定在 y=372 |
| 朱砂到处都是 | 无害的失败用了朱砂。只有守的不可逆操作能用 |
| 刻度盖在文字上 | 写坐标时把半径当成了绝对 y。刻度在 r=171–185 |
| 弦区文字被切掉 | 超过 24 字符，顶出了 y=372 处的安全宽度 |
| 长按没有字形 | `/spiflash/tuan/glyph.png` 不在，重跑 `prep_photos.py` 并重新推送 |
| 开机一片全黑 | 岁差根容器没隐藏，或某个应用漏了 `root_release()` |
| 批过的请求又冒出来 | 写 State 时用了"读快照→改→写回"，没走 `mutate()` |
| 关中断期间崩在 malloc | State 退回自旋锁了。必须用互斥量 |
| 语音播放时 WS 断线 | `handle_say` 里同步播了音频，要走播放队列 |
| 呼吸像抽搐 | `breathe()` 被每帧重调。只在状态边沿调一次 |
| 开机就卡死不动 | `root_acquire` 与 `LvglLockGuard` 顺序错了，嵌套非递归锁 |
| 望页全黑但日志说解码成功 | 解出来是 RGB888，被当成格式错误丢掉了 |
| 说完话没反应 | daemon 没回 `voice_status`，或有人加了"确认发送"那一步 |
| daemon 关着时日志被刷爆 | WS 退避没生效，退回固定 3 秒重连了 |
| WS 一直连不上，日志像在"不停重连" | 退避任务把**正在握手**的连接 stop 掉了。必须给 `kHandshakeGrace` 的窗口 |
| 手动退出守之后望卡死不动 | 路由按 prompt id 去重了。必须用时间窗，否则 key 不变就永远不再切 |
| 一次成功的语音在两分钟后变成 timed out | 播完没把 `_wait_start` 清零 |
| 时间差了整数个小时 | 桌面端授时的时区偏移被丢了，或 POSIX TZ 符号没取反 |
| 关不了机 | 连续按两次电源键，不是长按 |
| 想真正关屏但找不到接口 | HAL 没暴露 PYG8 屏幕电源轨。设计上也不该关屏，靠岁差 |
| 加外设后 I2C 冲突 | 触摸/IMU/RTC/PMIC/IOE/音频全在 G47-G48 一条总线上 |
| 打开秒表/闹钟是一片黑 | 岁差根容器没在引用计数归零时隐藏，盖住了原厂应用 |
| 桌面端改名后扫不到 | 广播名丢了 `Claude` 前缀 |
| 换图时闪一下 | 交叉淡入没做完就换了 `_photo` 的 src |
| PSRAM 分配失败 | 同时驻留超过两张照片，检查 `release()` 有没有漏调 |

---

## 15. 写代码时的风格要求

- 注释写"为什么"，不写"是什么"。`// 自增 i` 这种删掉。
- 中文注释可以，但标识符和日志一律英文。
- 每个文件顶部一句话说清它的职责边界。
- 不要写 `TODO` 就交差。写不完就在 PR 描述里说明，不要在代码里埋。
- **改视觉只改 `design.h`。** 在应用里硬编码颜色或半径的 PR 一律打回。
- **不要"优化"掉 §9.1 里的设计意图。** 呼吸的频率、外圈点的隐喻、晕影收到纯黑，这些不是随手写的默认值，是这个页面区别于任何一个照片时钟的全部原因。要改先说清楚理由。



---

# 附录：全部源码

每块前面的 `<!-- FILE: -->` 标记就是落盘路径，unpack.py 靠它工作。


### `CMakeLists.txt`

<!-- FILE: CMakeLists.txt -->
```text
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sinan)
```


### `main/CMakeLists.txt`

<!-- FILE: main/CMakeLists.txt -->
```text
file(GLOB_RECURSE MY_SRCS
    "apps/*.c" "apps/*.cc" "apps/*.cpp"
    "assets/*.c" "assets/*.cc" "assets/*.cpp"
    "hal/*.c" "hal/*.cc" "hal/*.cpp"
    "sinan/*.c" "sinan/*.cc" "sinan/*.cpp"
)

idf_component_register(
    SRCS "main.cpp" ${MY_SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        bt
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        mbedtls
)

# 上游资产由 scripts/sync_upstream.sh 拉进来。缺文件时给一句人话，
# 而不是让 CMake 甩一段找不着北的报错
set(SINAN_EMBED "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/sfx/boot_sfx.bin")
    target_add_binary_data(${COMPONENT_LIB} "assets/sfx/boot_sfx.bin" BINARY)
else()
    message(WARNING "缺 assets/sfx/boot_sfx.bin —— 先跑 ./scripts/sync_upstream.sh")
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/hal/utils/config_ap/assets/badge_config_ap.html")
    target_add_binary_data(${COMPONENT_LIB} "hal/utils/config_ap/assets/badge_config_ap.html" TEXT)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/hal/hal.h")
    message(FATAL_ERROR "缺 main/hal/ —— 先跑 ./scripts/sync_upstream.sh 再 idf.py build")
endif()

# 诊断应用（IMU / 麦克风频谱）。上板自检时打开：
#   idf.py build -DSINAN_DIAG=1
option(SINAN_DIAG "install upstream IMU / FFT diagnostic apps" OFF)
target_compile_definitions(${COMPONENT_LIB} PUBLIC SINAN_DIAG=$<BOOL:${SINAN_DIAG}>)

# 中文子集字体生成后由 scripts/build_cjk_font.sh 打开这个宏
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/fonts/lv_font_sinan_serif_28.c")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC SINAN_HAS_CJK_FONT=1)
endif()
```


### `main/idf_component.yml`

<!-- FILE: main/idf_component.yml -->
```yaml
dependencies:
  idf: ">=5.5.0"
  espressif/esp_websocket_client: "^1.5.0"
  espressif/i2c_bus: "*"
```


### `sdkconfig.defaults`

<!-- FILE: sdkconfig.defaults -->
```ini
# This file was generated using idf.py save-defconfig. It can be edited manually.
# Espressif IoT Development Framework (ESP-IDF) 5.5.4 Project Minimal Configuration
#
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FREERTOS_HZ=1000
CONFIG_LV_USE_CLIB_MALLOC=y
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_USE_CLIB_SPRINTF=y
CONFIG_LV_CACHE_DEF_SIZE=1048576
CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=2
CONFIG_LV_FONT_MONTSERRAT_10=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_18=y
CONFIG_LV_FONT_MONTSERRAT_22=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_36=y
CONFIG_LV_USE_FS_STDIO=y
CONFIG_LV_FS_STDIO_LETTER=65
CONFIG_LV_FS_STDIO_CACHE_SIZE=1048576
CONFIG_LV_USE_LODEPNG=y
CONFIG_LV_USE_TJPGD=y
CONFIG_LV_USE_DEMO_BENCHMARK=y
CONFIG_LV_USE_DEMO_STRESS=y
CONFIG_LV_USE_DEMO_SMARTWATCH=y

# --- 司南 ---
# NimBLE：只做外围端，不需要 central 角色
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_SM_LEGACY=y
CONFIG_BT_NIMBLE_SM_SC=y
CONFIG_BT_NIMBLE_NVS_PERSIST=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=247
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=5120
# WS 上行 base64 音频帧偏大
CONFIG_WS_TRANSPORT=y
CONFIG_WS_BUFFER_SIZE=4096
# 望的照片层要靠这几条才跑得动（见 AGENTS.md §1.1）
CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y
CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y
CONFIG_LV_MEMCPY_MEMSET_STD=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```


### `partitions.csv`

<!-- FILE: partitions.csv -->
```csv
# ESP-IDF Partition Table
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,    0x4000,
otadata,  data, ota,     0xd000,    0x2000,
phy_init, data, phy,     0xf000,    0x1000,
ota_0,    app,  ota_0,   0x20000,   0x4f0000,
ota_1,    app,  ota_1,   ,          0x4f0000,
storage,  data, fat,     ,          4M,
coredump, data, coredump,,          0x10000,
```


### `repos.json`

<!-- FILE: repos.json -->
```json
[
    {
        "url": "https://github.com/Forairaaaaa/mooncake.git",
        "path": "components/mooncake",
        "branch": "v2.3.3"
    },
    {
        "url": "https://github.com/Forairaaaaa/mooncake_log.git",
        "path": "components/mooncake_log",
        "branch": "v1.5.0"
    },
    {
        "url": "https://github.com/Forairaaaaa/smooth_ui_toolkit.git",
        "path": "components/smooth_ui_toolkit",
        "branch": "v2.12.1"
    },
    {
        "url": "https://github.com/m5stack/M5GFX.git",
        "path": "components/M5GFX",
        "branch": "0.2.19"
    },
    {
        "url": "https://github.com/bblanchon/ArduinoJson.git",
        "path": "components/ArduinoJson",
        "branch": "v7.4.3"
    },
    {
        "url": "https://github.com/lvgl/lvgl.git",
        "path": "components/lvgl",
        "branch": "v9.5.0"
    },
    {
        "url": "https://github.com/m5stack/M5IOE1",
        "path": "components/M5IOE1",
        "branch": "1.0.8",
        "patch": "patches/M5IOE1.patch"
    },
    {
        "url": "https://github.com/m5stack/M5PM1",
        "path": "components/M5PM1",
        "branch": "1.0.6",
        "patch": "patches/M5PM1.patch"
    },
    {
        "url": "https://github.com/lbuque/BMI270_BMM150_Sensor.git",
        "path": "components/BMI270_BMM150_Sensor",
        "branch": "0.1.2",
        "with_submodules": true
    }
]
```


### `fetch_repos.py`

<!-- FILE: fetch_repos.py -->
```python
import os
import subprocess
import json


def clone_or_update_repo(
    repo_url, path, ref=None, with_submodules=False, patch_path=None
):
    import os

    if not os.path.exists(path):
        subprocess.run(["git", "clone", repo_url, path], check=True)
    else:
        subprocess.run(["git", "-C", path, "fetch"], check=True)

    if ref:
        subprocess.run(["git", "-C", path, "checkout", ref], check=True)

    if with_submodules:
        subprocess.run(
            ["git", "-C", path, "submodule", "update", "--init", "--recursive"],
            check=True,
        )

    # 应用 patch
    if patch_path:
        patch_full_path = (
            patch_path
            if os.path.isabs(patch_path)
            else os.path.join(os.getcwd(), patch_path)
        )
        # 使用 git apply --check 先检测补丁是否能应用，避免报错
        check_result = subprocess.run(
            ["git", "-C", path, "apply", "--check", patch_full_path]
        )
        if check_result.returncode == 0:
            subprocess.run(["git", "-C", path, "apply", patch_full_path], check=True)
            print(f"Applied patch {patch_path} to {path}")
        else:
            print(f"Patch {patch_path} cannot be applied cleanly to {path}, skipped.")


def fetch_dependencies():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "repos.json")

    with open(config_path) as f:
        repos = json.load(f)

    for repo in repos:
        repo_path = os.path.join(script_dir, repo["path"])
        branch = repo.get("branch")
        with_submodules = repo.get("with_submodules", False)
        patch = repo.get("patch")
        if patch and not os.path.isabs(patch):
            patch = os.path.join(script_dir, patch)
        clone_or_update_repo(repo["url"], repo_path, branch, with_submodules, patch)


if __name__ == "__main__":
    fetch_dependencies()
```


### `main/main.cpp`

<!-- FILE: main/main.cpp -->
```cpp
/*
 * main.cpp — 司南入口。
 *
 * 三条通道各起各的，任何一条起不来都不影响其他两条。
 * 这是架构约束，不要在这里加"等 WiFi 好了再起 BLE"之类的顺序依赖。
 */
#include <apps/apps.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <sinan/bridge_ble.h>
#include <sinan/bridge_ws.h>
#include <sinan/config.h>
#include <sinan/danger.h>
#include <sinan/precession.h>
#include <sinan/route.h>
#include <sinan/state.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    GetHAL().init();
    GetHAL().playBootSfx();   // 官方开机音，白送的仪式感

    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // 岁差根容器要在任何应用打开之前就位。它默认是隐藏的 ——
    // 全屏不透明纯黑，不隐藏会把 launcher 整个盖掉
    {
        LvglLockGuard lock;
        sinan::ui::precession_root();
    }

    // 危险规则表改坏了会静默失效 —— 漏报没有代价可言，所以开机自测一次。
    // 只 log 不阻塞启动：设备不能因为一条规则不对就开不了机
    if (const int bad = sinan::selftest(); bad > 0) {
        mclog::tagError("sinan", "danger selftest: {} fixture(s) failed", bad);
    }

    // BLE：不需要配网，桌面端扫到就能连
    sinan::ble::start();

    // WiFi + WS：连不上也无所谓，守照常工作
    sinan::ws::start(SN_WIFI_SSID, SN_WIFI_PASS, SN_WS_URI);

    GetMooncake().installApp(std::make_unique<AppLauncher>());
    sinan::route::register_app("Ward",    GetMooncake().installApp(std::make_unique<AppWard>()));
    sinan::route::register_app("Fleet",   GetMooncake().installApp(std::make_unique<AppFleet>()));
    sinan::route::register_app("Echo",    GetMooncake().installApp(std::make_unique<AppEcho>()));
    sinan::route::register_app("Almanac", GetMooncake().installApp(std::make_unique<AppAlmanac>()));
    sinan::route::register_app("Gaze",    GetMooncake().installApp(std::make_unique<AppGaze>()));
    GetMooncake().installApp(std::make_unique<AppSetup>());

    /*
     * 官方原厂应用。它们画在 lv_screen_active()，司南画在岁差根容器上，
     * 而根容器在引用计数归零时隐藏 —— 所以两套可以共存，互不遮挡。
     * 改动根容器可见性策略的人要保住这一点。
     */
    GetMooncake().installApp(std::make_unique<AppStopWatch>());    // 板子就叫这名字
    GetMooncake().installApp(std::make_unique<AppAlarmClock>());   // 硬件闹钟，司南没有
    GetMooncake().installApp(std::make_unique<AppBadge>());        // 没 BLE 时的照片入口
#if SINAN_DIAG
    // 上板自检用：麦克风是不是坏的、IMU 有没有数据。
    // 第一次点不亮时，先用它们排除硬件，再回来怪自己的代码
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppFft>());
#endif

    // 开机直接进待机页，别停在 launcher —— 这是一台常驻的桌面终端，
    // 不是一台等着你选功能的设备。SN_BOOT_APP 为空则保持 launcher
    sinan::route::open(SN_BOOT_APP);

    while (1) {
        GetHAL().feedTheDog();
        sinan::ui::precession_tick(GetHAL().millis());   // 内部自己持 LVGL 锁
        GetMooncake().update();
    }
}
```


### `main/apps/apps.h`

<!-- FILE: main/apps/apps.h -->
```cpp
#pragma once
// 上游保留。app_template / app_watch_face / app_lucky_wheel 留在树里作参考，
// 但不安装：望已经取代了表盘，转盘跟司南的视觉语言不搭
#include "app_launcher/app_launcher.h"
#include "app_setup/app_setup.h"
#include "app_stopwatch/app_stopwatch.h"       // 这块板出厂就叫 StopWatch
#include "app_alarm_clock/app_alarm_clock.h"   // 用了 HAL 的硬件闹钟，司南没有等价物
#include "app_badge/app_badge.h"               // 不走 BLE 的照片上传路径
#if SINAN_DIAG
#include "app_imu/app_imu.h"
#include "app_fft/app_fft.h"
#endif
// 司南
#include "app_ward/app_ward.h"
#include "app_fleet/app_fleet.h"
#include "app_echo/app_echo.h"
#include "app_almanac/app_almanac.h"
#include "app_gaze/app_gaze.h"
```


### `main/sinan/config.h`

<!-- FILE: main/sinan/config.h -->
```cpp
/*
 * config.h — 用户配置。这是唯一需要在烧录前改的文件。
 *
 * 注意 BLE 通道不需要任何配置：官方 buddy 协议靠桌面端主动扫描配对，
 * 不用填 IP、不用配网。下面这些只影响 WiFi 那条通道（Fleet / Almanac / Echo）。
 */
#pragma once

// WiFi。ESP32-S3 只支持 2.4GHz，别填 5G 的 SSID
#define SN_WIFI_SSID "YOUR_WIFI"
#define SN_WIFI_PASS "YOUR_PASSWORD"

// Mac 上 sinand.py 的地址。查本机 IP：ipconfig getifaddr en0
#define SN_WS_URI "ws://192.168.1.100:8790/sinan"

/*
 * 开机直接进入哪个应用。留空字符串则停在 launcher。
 *
 * 默认 Gaze：望占了设备九成的时间，而守是被权限请求"拉起来"的 ——
 * 有请求时会自动从任何应用切过去，处理完再切回来，不需要你记得开它。
 * 可选 "Gaze" / "Ward" / "Fleet" / "Almanac" / "Echo" / ""
 */
#define SN_BOOT_APP "Gaze"
```


### `main/sinan/design.h`

<!-- FILE: main/sinan/design.h -->
```cpp
/*
 * design.h — 司南视觉令牌。
 *
 * 职责边界：整个固件唯一允许定义颜色、半径、字号、动画时长的地方。
 * 应用代码里出现字面量颜色或半径 = 设计系统失效。
 */
#pragma once
#include <assets/assets.h>
#include <lvgl.h>
#include <cstdint>

namespace sinan::design {

/* ------------------------------ 矿物色板 ------------------------------ */
/* 不用通用暗色主题那一套。底是黑漆，字是生宣，刻度是鎏金，
   语义色取自朱砂、石绿、靛青三种传统矿物颜料。*/

constexpr uint32_t INK       = 0x000000;  // 真黑。AMOLED 不发光，省电且不烧屏
constexpr uint32_t LACQUER   = 0x141210;  // 漆地。唯一允许的"接近黑"，只用于弧底槽
constexpr uint32_t BRONZE    = 0xC8A96E;  // 鎏金。刻度、次级标签
constexpr uint32_t BRONZE_D  = 0x6E5C3A;  // 鎏金暗部
constexpr uint32_t SILK      = 0xF2EDE1;  // 生宣。主数字、主文本
constexpr uint32_t SILK_D    = 0x8A857B;  // 生宣暗部
/* 语义色是一条**严重度轴**，不是场景标签。按严重度选，不要按"这是什么功能"选 */
constexpr uint32_t MALACHITE = 0x4FA88A;  // 石绿。好 / 通过 / 健康
constexpr uint32_t AMBER     = 0xE8A33D;  // 琥珀。需要你注意：等待决策、额度将尽、状态偏低
constexpr uint32_t CINNABAR  = 0xD6442F;  // 朱砂。**只用于不可逆操作，全设备只有守能用**
constexpr uint32_t INDIGO    = 0x3B4C8C;  // 靛青。没有信息：静默、断链、陈旧、无害的失败

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

/* ------------------------------ 几何 ------------------------------ */

constexpr int SCREEN = 466;
constexpr int CENTER = 233;
constexpr int R_MAX  = 233;
constexpr int R_SAFE = 213;  // 物理安全边。任何元素不得越过

/* 晕影按照片形态走。蜷成球的主体本身就是圆的，晕影可以收在外圈；
   坐姿站姿的照片背景多，晕影要往里压才吃得掉 */
constexpr int VIG_INNER_DISC     = 174;
constexpr int VIG_INNER_PORTRAIT = 140;
constexpr int VIG_OUTER          = 236;   // 必须越过 R_MAX，否则四角露白
constexpr int VIG_RINGS          = 24;

constexpr int R_RIM       = 219;  // Rim 环中线半径
constexpr int W_RIM       = 10;
constexpr int R_ORBIT     = 178;  // Orbit 环中线半径
constexpr int W_ORBIT     = 6;
constexpr int R_ORBIT_IN  = 150;
constexpr int R_CORE      = 145;
constexpr int Y_CHORD     = 372;  // 弦区基线（绝对 y）

/* ------------------------------ 运动 ------------------------------ */

constexpr uint32_t T_FAST   = 320;
constexpr uint32_t T_STATE  = 480;
constexpr uint32_t T_BREATH = 2600;  // 呼吸周期下限。快过这个显得焦躁

/* ------------------------------ 岁差防烧屏 ------------------------------ */

constexpr uint32_t PRECESS_INTERVAL_MS = 60UL * 1000;
constexpr int PRECESS_STEP_DECIDEG     = 3;  // 0.3°/min，20 小时一周
constexpr uint32_t JITTER_INTERVAL_MS  = 5UL * 60 * 1000;
constexpr int JITTER_PX                = 3;

/* ------------------------------ 亮度 ------------------------------ */

constexpr int BL_QUIET  = 25;
constexpr int BL_NORMAL = 60;
constexpr int BL_ALERT  = 90;

/* ------------------------------ 交互 ------------------------------ */

constexpr uint32_t LONGPRESS_MS = 800;
constexpr float TAP_G           = 1.85f;
constexpr uint32_t STALE_MS     = 30000;

/* ------------------------------ 字体 ------------------------------ */
/* 数字用 Commissioner（几何无衬线，字宽稳定）
   数据用 Maple Mono（等宽，命令与路径不会跳动）
   中文用思源宋体子集（横细竖粗，鎏金上有金石感；黑体会像手机 App）*/

/* 字体由上游 assets/assets.h 用 LV_FONT_DECLARE 声明在全局命名空间。
   早期版本在这里 extern "C" 重声明了一遍 —— 同一批 C 链接符号在两个
   命名空间各声明一次，部分编译器会报冲突。直接 include 上游的头。*/

#define SN_FONT_NUM_XL (&CommissionerMedium108)
#define SN_FONT_NUM_L  (&CommissionerMedium64)
#define SN_FONT_MONO_L (&lv_font_maple_mono_medium_48)
#define SN_FONT_MONO_M (&lv_font_maple_mono_medium_28)
#define SN_FONT_MONO_S (&lv_font_maple_mono_medium_24)

/* 中文子集字体由 scripts/build_cjk_font.sh 生成。
   未生成时回落到 Montserrat，中文显示为空白 —— 刻意如此，
   空白比乱码更容易在自测时被发现。*/
#if defined(SINAN_HAS_CJK_FONT)
LV_FONT_DECLARE(lv_font_sinan_serif_28);
LV_FONT_DECLARE(lv_font_sinan_serif_40);
#define SN_FONT_CJK_M (&lv_font_sinan_serif_28)
#define SN_FONT_CJK_L (&lv_font_sinan_serif_40)
#else
#define SN_FONT_CJK_M (&lv_font_montserrat_28)
#define SN_FONT_CJK_L (&lv_font_montserrat_36)
#endif

}  // namespace sinan::design
```


### `main/sinan/ring.h`

<!-- FILE: main/sinan/ring.h -->
```cpp
/*
 * ring.h — 圆形 UI 原语。
 *
 * 职责边界：把"在圆盘上画东西"这件事收敛成十来个函数，
 * 应用层不应该直接调 lv_arc_create / 手算极坐标。
 * 角度约定：0° = 12 点方向，顺时针为正。（LVGL 原生 0° 在 3 点，此处已换算）
 */
#pragma once
#include "design.h"
#include <lvgl.h>
#include <cstdint>
#include <vector>

namespace sinan::ui {

/* --------------------------- 极坐标 --------------------------- */

// 把元素放到以圆心为原点、半径 r、方位角 deg 的位置（元素自身居中对齐）
void place_polar(lv_obj_t* o, int r, float deg);

// 12 点为 0° 的角度转 LVGL 弧度角（3 点为 0°）
inline int to_lv_angle(float deg) { return static_cast<int>(deg) - 90; }

/* --------------------------- 弧 --------------------------- */

struct ArcSpec {
    int radius = design::R_RIM;
    int width  = design::W_RIM;
    uint32_t color = design::BRONZE;
    uint32_t track = design::LACQUER;  // 底槽色。传 design::INK 表示无底槽
    bool rounded   = true;
};

// 建一条弧。start/end 用 12 点为 0° 的角度制
lv_obj_t* arc(lv_obj_t* parent, const ArcSpec& spec, float start_deg, float end_deg);

void arc_set_range(lv_obj_t* a, float start_deg, float end_deg);
void arc_set_color(lv_obj_t* a, uint32_t hex);

// 从 12 点起、按比例 0.0–1.0 生长的进度弧
void arc_set_progress(lv_obj_t* a, float ratio);

// 带缓动地改变弧的终点角。用于"生长/退让"，替代所有滑入滑出
void arc_animate_to(lv_obj_t* a, float start_deg, float end_deg, uint32_t ms);

/* --------------------------- 刻度环 --------------------------- */

// 沿圆周均匀撒 count 个刻度。返回容器，刻度是它的子对象，按索引顺序排列
lv_obj_t* ticks(lv_obj_t* parent, int radius, int count, int len, int thickness, uint32_t color);

// 单独染色某一个刻度（索引从 12 点开始顺时针）
void tick_set_color(lv_obj_t* ring, int index, uint32_t hex);
void ticks_reset_color(lv_obj_t* ring, uint32_t hex);

/* --------------------------- 文字 --------------------------- */

lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color);

// Core 带的主数字。自动居中、自动按位数选字号
lv_obj_t* numeral(lv_obj_t* parent, const char* s);
void numeral_set(lv_obj_t* label, const char* s);

// 弦区标签：屏幕下方居中的一行小字
lv_obj_t* chord(lv_obj_t* parent, const char* s, uint32_t color = design::BRONZE);

// 等宽正文，自动折行并限宽在 Core 带内。用于命令、路径这类不能变形的内容
lv_obj_t* mono_block(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color, int width);

/* --------------------------- 动效 --------------------------- */

// 透明度呼吸。period 会被夹到 design::T_BREATH 以上
void breathe(lv_obj_t* o, uint32_t period_ms, int opa_lo = 60, int opa_hi = 255);
void stop_breathe(lv_obj_t* o);

// 弧从某个角度向两侧展开成整圈，用于决策落定的反馈
void bloom(lv_obj_t* a, float from_deg, uint32_t hex, uint32_t ms);

/* --------------------------- 屏幕基座 --------------------------- */

// 纯黑全屏底板。每个应用 onOpen 时挂在 precession_root() 上
lv_obj_t* stage(lv_obj_t* parent);

}  // namespace sinan::ui
```


### `main/sinan/ring.cpp`

<!-- FILE: main/sinan/ring.cpp -->
```cpp
/*
 * ring.cpp — 圆形 UI 原语实现。原生 LVGL 9.5 C API，不依赖包装类。
 */
#include "ring.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sinan::ui {

using namespace sinan::design;

static constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;

void place_polar(lv_obj_t* o, int r, float deg)
{
    // 12 点为 0°，顺时针。屏幕 y 轴向下，所以 y 用负 cos
    const float rad = (deg - 90.0f) * kDeg2Rad;
    const int x = static_cast<int>(std::lround(r * std::cos(rad)));
    const int y = static_cast<int>(std::lround(r * std::sin(rad)));
    lv_obj_align(o, LV_ALIGN_CENTER, x, y);
}

lv_obj_t* arc(lv_obj_t* parent, const ArcSpec& spec, float start_deg, float end_deg)
{
    lv_obj_t* a = lv_arc_create(parent);
    const int d = spec.radius * 2 + spec.width;
    lv_obj_set_size(a, d, d);
    lv_obj_center(a);

    // 弧是纯展示件：去掉旋钮、禁掉点击，否则触摸会误改数值
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(a, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(a, 0, LV_PART_MAIN);

    lv_obj_set_style_arc_width(a, spec.width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, spec.width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, spec.rounded, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, spec.rounded, LV_PART_INDICATOR);

    if (spec.track == INK) {
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
        lv_obj_set_style_arc_color(a, c(spec.track), LV_PART_MAIN);
    }
    lv_obj_set_style_arc_color(a, c(spec.color), LV_PART_INDICATOR);

    lv_arc_set_bg_angles(a, 0, 360);
    arc_set_range(a, start_deg, end_deg);
    return a;
}

void arc_set_range(lv_obj_t* a, float start_deg, float end_deg)
{
    int s = to_lv_angle(start_deg);
    int e = to_lv_angle(end_deg);
    while (s < 0) s += 360;
    while (e < 0) e += 360;
    lv_arc_set_angles(a, s, e);
}

void arc_set_color(lv_obj_t* a, uint32_t hex)
{
    lv_obj_set_style_arc_color(a, c(hex), LV_PART_INDICATOR);
}

void arc_set_progress(lv_obj_t* a, float ratio)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    // 359.9 而不是 360：LVGL 里 start==end 会被判成空弧，满圈会突然消失
    arc_set_range(a, 0.0f, ratio >= 1.0f ? 359.9f : ratio * 360.0f);
}

struct ArcAnimCtx {
    float start;
    float from;
    float to;
};

static void arc_anim_exec(void* obj, int32_t v)
{
    lv_obj_t* a = static_cast<lv_obj_t*>(obj);
    auto* ctx = static_cast<ArcAnimCtx*>(lv_obj_get_user_data(a));
    if (!ctx) return;
    const float e = ctx->from + (ctx->to - ctx->from) * (v / 1000.0f);
    arc_set_range(a, ctx->start, e);
}

// 对象销毁时把动画上下文一起收掉。早期版本只 new 不 delete，
// 每次开关应用泄一份，挂一天就不好看了
static void arc_ctx_free(lv_event_t* e)
{
    auto* ctx = static_cast<ArcAnimCtx*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    delete ctx;
}

void arc_animate_to(lv_obj_t* a, float start_deg, float end_deg, uint32_t ms)
{
    auto* ctx = static_cast<ArcAnimCtx*>(lv_obj_get_user_data(a));
    if (!ctx) {
        ctx = new ArcAnimCtx{};
        lv_obj_set_user_data(a, ctx);
        lv_obj_add_event_cb(a, arc_ctx_free, LV_EVENT_DELETE, nullptr);
    }
    // 从当前终点接着动，避免连续调用时的跳变
    int32_t cur_s = 0, cur_e = 0;
    cur_s = lv_arc_get_angle_start(a);
    cur_e = lv_arc_get_angle_end(a);
    (void)cur_s;
    ctx->start = start_deg;
    ctx->from  = static_cast<float>(cur_e) + 90.0f;
    ctx->to    = end_deg;

    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, a);
    lv_anim_set_exec_cb(&an, arc_anim_exec);
    lv_anim_set_values(&an, 0, 1000);
    lv_anim_set_time(&an, ms);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_start(&an);
}

lv_obj_t* ticks(lv_obj_t* parent, int radius, int count, int len, int thickness, uint32_t color)
{
    lv_obj_t* ring = lv_obj_create(parent);
    lv_obj_set_size(ring, SCREEN, SCREEN);
    lv_obj_center(ring);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 0, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < count; i++) {
        const float deg = 360.0f * i / count;
        lv_obj_t* t = lv_obj_create(ring);
        lv_obj_set_size(t, thickness, len);
        lv_obj_set_style_radius(t, thickness / 2, 0);
        lv_obj_set_style_bg_color(t, c(color), 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        // 刻度自身要沿半径方向立起来
        lv_obj_set_style_transform_rotation(t, static_cast<int32_t>(deg * 10), 0);
        lv_obj_set_style_transform_pivot_x(t, thickness / 2, 0);
        lv_obj_set_style_transform_pivot_y(t, len / 2, 0);
        place_polar(t, radius, deg);
    }
    return ring;
}

void tick_set_color(lv_obj_t* ring, int index, uint32_t hex)
{
    if (index < 0 || index >= static_cast<int>(lv_obj_get_child_count(ring))) return;
    lv_obj_set_style_bg_color(lv_obj_get_child(ring, index), c(hex), 0);
}

void ticks_reset_color(lv_obj_t* ring, uint32_t hex)
{
    const uint32_t n = lv_obj_get_child_count(ring);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_set_style_bg_color(lv_obj_get_child(ring, i), c(hex), 0);
    }
}

lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, s ? s : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

lv_obj_t* numeral(lv_obj_t* parent, const char* s)
{
    lv_obj_t* l = text(parent, s, SN_FONT_NUM_XL, SILK);
    lv_obj_center(l);
    numeral_set(l, s);
    return l;
}

void numeral_set(lv_obj_t* label, const char* s)
{
    if (!s) return;
    lv_label_set_text(label, s);
    // 位数多了自动降一档，避免撑破 Core 带
    lv_obj_set_style_text_font(label, std::strlen(s) > 4 ? SN_FONT_NUM_L : SN_FONT_NUM_XL, 0);
}

lv_obj_t* chord(lv_obj_t* parent, const char* s, uint32_t color)
{
    lv_obj_t* l = text(parent, s, SN_FONT_MONO_S, color);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, Y_CHORD);
    return l;
}

lv_obj_t* mono_block(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color, int width)
{
    lv_obj_t* l = text(parent, s, font, color);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(l, 6, 0);
    lv_obj_center(l);
    return l;
}

/*
 * 呼吸动画。重复调用是安全的，但会把周期从头重启 ——
 * 每秒重画一次的页面如果无脑重调，呼吸永远走不完一拍，看起来是抽搐。
 * 所以调用方要做边沿判断，只在"进入该呼吸的状态"时调一次。
 */
void breathe(lv_obj_t* o, uint32_t period_ms, int opa_lo, int opa_hi)
{
    if (period_ms < T_BREATH) period_ms = T_BREATH;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_style_arc_opa(static_cast<lv_obj_t*>(obj), v, LV_PART_INDICATOR);
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), v, 0);
    });
    lv_anim_set_values(&a, opa_lo, opa_hi);
    lv_anim_set_time(&a, period_ms / 2);
    lv_anim_set_playback_time(&a, period_ms / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void stop_breathe(lv_obj_t* o)
{
    lv_anim_delete(o, nullptr);
    lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_arc_opa(o, LV_OPA_COVER, LV_PART_INDICATOR);
}

void bloom(lv_obj_t* a, float from_deg, uint32_t hex, uint32_t ms)
{
    arc_set_color(a, hex);
    arc_set_range(a, from_deg - 2.0f, from_deg + 2.0f);

    // 从 from_deg 向两侧张开。早期版本把 from_deg 塞进 user_data 却从不读，
    // 结果永远从 12 点张开，跟当前弧的位置对不上
    auto* ctx = static_cast<ArcAnimCtx*>(lv_obj_get_user_data(a));
    if (!ctx) {
        ctx = new ArcAnimCtx{};
        lv_obj_set_user_data(a, ctx);
        lv_obj_add_event_cb(a, arc_ctx_free, LV_EVENT_DELETE, nullptr);
    }
    ctx->start = from_deg;

    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, a);
    lv_anim_set_exec_cb(&an, [](void* obj, int32_t v) {
        lv_obj_t* arc_obj = static_cast<lv_obj_t*>(obj);
        auto* c = static_cast<ArcAnimCtx*>(lv_obj_get_user_data(arc_obj));
        const float mid = c ? c->start : 0.0f;
        const float half = v / 10.0f;
        arc_set_range(arc_obj, mid - half, mid + half);
    });
    lv_anim_set_values(&an, 20, 1800);
    lv_anim_set_time(&an, ms);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_start(&an);
}

lv_obj_t* stage(lv_obj_t* parent)
{
    lv_obj_t* s = lv_obj_create(parent);
    lv_obj_set_size(s, SCREEN, SCREEN);
    lv_obj_center(s);
    lv_obj_set_style_bg_color(s, c(INK), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_set_style_radius(s, SCREEN / 2, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

}  // namespace sinan::ui
```


### `main/sinan/precession.h`

<!-- FILE: main/sinan/precession.h -->
```cpp
/*
 * precession.h — 岁差：防烧屏，同时是本机的签名视觉。
 *
 * 职责边界：提供所有应用共用的根容器，并让它极缓慢自转。
 * 应用不要直接挂 lv_screen_active()，否则该应用的画面会长期常亮同一批像素。
 */
#pragma once
#include <lvgl.h>
#include <cstdint>

namespace sinan::ui {

// 拿根容器。首次调用时创建，之后返回同一个
lv_obj_t* precession_root();

/*
 * 根容器是全屏不透明纯黑，会盖住上游的 launcher。
 * 所以它默认隐藏，由应用在 onOpen/onClose 成对 acquire/release。
 * 忘了 release 的后果是开机一片全黑，看起来像板子坏了。
 *
 * 【锁契约】这两个函数**要求调用方已经持有 LVGL 锁**，内部不再上锁。
 * 早期版本内部自己 LvglLockGuard，而四个应用是"先拿锁再调 acquire" ——
 * 非递归锁下这是死锁。契约统一成这样之后，两边写法都只有一种：
 *
 *   onOpen :  LvglLockGuard lock;  root_acquire();  建 UI
 *   onClose:  LvglLockGuard lock;  删 stage;  root_release();
 */
void root_acquire();
void root_release();

// 主循环里每帧调一次。内部自己持 LVGL 锁，调用方不用再包一层
void precession_tick(uint32_t now_ms);

// 亮度意图。由应用声明当前该多亮，实际下发做了平滑与去抖
enum class Luma { Quiet, Normal, Alert };
void set_luma(Luma l);

// 重置到零位。仅用于自测确认岁差确实在动
void precession_reset();

// 已累计转过的角度（0.1° 为单位），自测用
int32_t precession_offset_decideg();

}  // namespace sinan::ui
```


### `main/sinan/precession.cpp`

<!-- FILE: main/sinan/precession.cpp -->
```cpp
/*
 * precession.cpp
 *
 * AMOLED 常亮必然烧屏。定时息屏会毁掉"常驻仪表"的核心价值，
 * 所以改成让整个画面以肉眼不可察的速度自转：0.3°/分钟，20 小时一周。
 * 任何像素都不会长期承担同一个亮点。
 */
#include "precession.h"
#include "design.h"
#include <hal/hal.h>

namespace sinan::ui {

using namespace sinan::design;

static lv_obj_t* s_root          = nullptr;
static uint32_t s_last_precess   = 0;
static uint32_t s_last_jitter    = 0;
static int32_t s_offset_decideg  = 0;
static int s_jitter_phase        = 0;
static Luma s_luma               = Luma::Normal;
static Luma s_luma_applied       = Luma::Normal;
static uint32_t s_luma_changed   = 0;
static int s_refs                = 0;

lv_obj_t* precession_root()
{
    if (s_root) return s_root;

    s_root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_root, SCREEN, SCREEN);
    lv_obj_center(s_root);
    lv_obj_set_style_bg_color(s_root, c(INK), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // 绕屏幕中心转，不是绕自身左上角
    lv_obj_set_style_transform_pivot_x(s_root, CENTER, 0);
    lv_obj_set_style_transform_pivot_y(s_root, CENTER, 0);
    lv_obj_set_style_transform_rotation(s_root, s_offset_decideg, 0);
    // 默认藏起来，否则这块全屏黑板会把 launcher 整个盖掉
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}

void root_acquire()
{
    lv_obj_t* r = precession_root();
    if (s_refs++ == 0) {
        lv_obj_remove_flag(r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(r);
    }
}

void root_release()
{
    if (s_refs > 0 && --s_refs == 0 && s_root) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void precession_tick(uint32_t now_ms)
{
    if (!s_root) return;

    // 这里会动 LVGL 对象，而调用方是主循环，没有别处帮它上锁
    LvglLockGuard lock;

    if (now_ms - s_last_precess >= PRECESS_INTERVAL_MS) {
        s_last_precess = now_ms;
        s_offset_decideg = (s_offset_decideg + PRECESS_STEP_DECIDEG) % 3600;
        lv_obj_set_style_transform_rotation(s_root, s_offset_decideg, 0);
    }

    // 大字数字是最容易烧的区域，额外给一层慢抖动
    if (now_ms - s_last_jitter >= JITTER_INTERVAL_MS) {
        s_last_jitter = now_ms;
        s_jitter_phase = (s_jitter_phase + 1) % 4;
        static const int dx[4] = {JITTER_PX, 0, -JITTER_PX, 0};
        static const int dy[4] = {0, JITTER_PX, 0, -JITTER_PX};
        lv_obj_set_style_translate_x(s_root, dx[s_jitter_phase], 0);
        lv_obj_set_style_translate_y(s_root, dy[s_jitter_phase], 0);
    }

    // 亮度去抖：连续 600ms 保持同一意图才真正下发，避免闪烁
    if (s_luma != s_luma_applied) {
        if (s_luma_changed == 0) {
            s_luma_changed = now_ms;
        } else if (now_ms - s_luma_changed > 600) {
            int target = BL_NORMAL;
            if (s_luma == Luma::Quiet) target = BL_QUIET;
            else if (s_luma == Luma::Alert) target = BL_ALERT;
            GetHAL().setBackLightBrightness(target);
            s_luma_applied = s_luma;
            s_luma_changed = 0;
        }
    } else {
        s_luma_changed = 0;
    }
}

void set_luma(Luma l) { s_luma = l; }

void precession_reset()
{
    s_offset_decideg = 0;
    if (s_root) {
        lv_obj_set_style_transform_rotation(s_root, 0, 0);
        lv_obj_set_style_translate_x(s_root, 0, 0);
        lv_obj_set_style_translate_y(s_root, 0, 0);
    }
}

int32_t precession_offset_decideg() { return s_offset_decideg; }

}  // namespace sinan::ui
```


### `main/sinan/state.h`

<!-- FILE: main/sinan/state.h -->
```cpp
/*
 * state.h — 全局状态。
 *
 * 职责边界：网络线程与 UI 线程之间唯一的数据交换点。
 * 网络回调只写这里，应用只读这里的快照。任何一方都不要跨过它直接找对方。
 *
 * 并发模型：FreeRTOS 递归互斥量，不是自旋锁。
 * 早期版本用 portENTER_CRITICAL 保护，而临界区里要拷十几个 std::string ——
 * 关中断期间进堆分配器在 ESP32 上是教科书级的崩法。互斥量允许临界区里分配，
 * 代价是不能在 ISR 里用（我们也没有 ISR 写 State）。
 */
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace sinan {

/* ------------------------------ BLE 侧 ------------------------------ */

struct BleState {
    bool connected      = false;
    uint32_t last_beat  = 0;
    int total           = 0;
    int running         = 0;
    int waiting         = 0;
    uint32_t tokens_today = 0;
    std::string msg;
    std::array<std::string, 3> entries;

    bool has_prompt = false;
    std::string prompt_id;     // 回传时必须原样带回，这是防误批的唯一保险
    std::string prompt_tool;
    std::string prompt_hint;
    uint32_t prompt_since = 0;

    // 已决策但心跳还没跟上的请求。见 State::decide() 的说明
    std::string settled_id;

    std::string owner;
    uint32_t passkey = 0;      // 非 0 表示正在配对，UI 要把它显示出来
};

/* ------------------------------ WS 侧 ------------------------------ */

enum class WorkerState : uint8_t { Idle, Run, Stall, Down };

struct Worker {
    std::string id;
    std::string label;
    std::string task;
    WorkerState state = WorkerState::Down;
    float quota       = 0.0f;
};

struct FleetState {
    static constexpr int kMax = 6;
    std::array<Worker, kMax> workers;
    int count          = 0;
    uint32_t last_recv = 0;
    bool stale         = true;
};

struct AlmanacState {
    std::string number_code;
    std::string number_title;
    std::string huangli_trend;
    std::string huangli_yi;
    std::string huangli_ji;
    int ring_doy = 0;
    std::string ring_tag;
    uint32_t last_recv = 0;
};

/* ------------------------------ 语音侧 ------------------------------ */

enum class VoicePhase : uint8_t {
    Idle, Recording, Sending, Transcribing, Thinking, Speaking, Error
};

struct VoiceState {
    VoicePhase phase = VoicePhase::Idle;
    std::string heard;
    std::string reply;
    std::string note;   // 出错时给人看的一行英文，如 "no link" / "too short"
};

struct Tally {
    uint32_t approved = 0;
    uint32_t denied   = 0;
};

struct Snapshot {
    BleState ble;
    FleetState fleet;
    AlmanacState almanac;
    VoiceState voice;
    Tally tally;
    bool wifi_up = false;
    bool ws_up   = false;
};

class State {
public:
    static State& get();

    // 读：拿一份完整拷贝，之后随便用，不用再持锁
    Snapshot snapshot();

    /*
     * 写：在锁内对状态做原地修改。
     *
     * 所有写路径都必须走这个函数，不能"读快照 → 改 → 写回"——
     * 那是跨线程的读改写，会丢更新：你按下批准清掉 prompt 的同时，
     * BLE 任务正拿着批准前的旧副本准备写回，prompt 就诈尸了。
     */
    void mutate(const std::function<void(Snapshot&)>& fn);

    /*
     * 记下一个已决策的请求 id 并立刻清掉 prompt，全程在同一把锁内。
     * settled_id 用来吃掉后面几拍还带着同一个 prompt 的陈旧心跳 ——
     * 桌面端要过一两百毫秒才知道我们批过了。
     */
    void decide(const std::string& id, bool approved);

    void setLink(bool wifi, bool ws);

private:
    State();
    Snapshot _s;
    SemaphoreHandle_t _mtx = nullptr;

    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t m) : _m(m) { xSemaphoreTakeRecursive(_m, portMAX_DELAY); }
        ~Lock() { xSemaphoreGiveRecursive(_m); }
    private:
        SemaphoreHandle_t _m;
    };
};

}  // namespace sinan
```


### `main/sinan/state.cpp`

<!-- FILE: main/sinan/state.cpp -->
```cpp
#include "state.h"
#include <functional>

namespace sinan {

State::State()
{
    // 递归的：mutate 里再调 decide 不会自锁
    _mtx = xSemaphoreCreateRecursiveMutex();
}

State& State::get()
{
    static State inst;
    return inst;
}

Snapshot State::snapshot()
{
    Lock lk(_mtx);
    return _s;   // 互斥量下允许堆分配，所以这里拷 std::string 是安全的
}

void State::mutate(const std::function<void(Snapshot&)>& fn)
{
    Lock lk(_mtx);
    fn(_s);
}

void State::decide(const std::string& id, bool approved)
{
    Lock lk(_mtx);
    if (approved) _s.tally.approved++;
    else _s.tally.denied++;

    _s.ble.settled_id = id;      // 后续带同一个 id 的心跳一律忽略
    _s.ble.has_prompt = false;
    _s.ble.prompt_id.clear();
    _s.ble.prompt_tool.clear();
    _s.ble.prompt_hint.clear();
}

void State::setLink(bool wifi, bool ws)
{
    Lock lk(_mtx);
    _s.wifi_up = wifi;
    _s.ws_up   = ws;
}

}  // namespace sinan
```


### `main/sinan/danger.h`

<!-- FILE: main/sinan/danger.h -->
```cpp
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
```


### `main/sinan/danger.cpp`

<!-- FILE: main/sinan/danger.cpp -->
```cpp
#include "danger.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <esp_log.h>

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
    {"|sh",             "pipe to shell",      Risk::Grave},   // 无空格变体
    {"|bash",           "pipe to shell",      Risk::Grave},
    {"|zsh",            "pipe to shell",      Risk::Grave},
    {"rm -r ",          "recursive delete",   Risk::Grave},   // 不带 f 的一样能删光
    {"rm -rd",          "recursive delete",   Risk::Grave},
    {"--force-with-lease", "force push",      Risk::Grave},   // 比 --force 温和，但照样改写远端
    {"git checkout --", "discards work",      Risk::Grave},
    {"chown -R",        "ownership change",   Risk::Grave},
    {"killall",         "kills processes",    Risk::Elevated},
    {"launchctl",       "changes daemons",    Risk::Elevated},
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
    // 只认贴着路径的重定向，别把 "a > b" 这种比较写法一起抓进来
    {"> /",             "overwrites file",    Risk::Elevated},
    {"> ~",             "overwrites file",    Risk::Elevated},
    {">> /",            "appends to file",    Risk::Elevated},
};

/*
 * 自测样本。规则表改动后跑一次 selftest()，比在真机上试快得多。
 * 原则是宁可误报不可漏报：误报的代价是多按 0.8 秒，漏报没有代价可言。
 */
struct Fixture { const char* tool; const char* hint; Risk want; };

constexpr Fixture kFixtures[] = {
    {"Bash", "rm -rf ~/project",                 Risk::Grave},
    {"Bash", "rm -r build",                      Risk::Grave},
    {"Bash", "sudo launchctl unload x",          Risk::Grave},
    {"Bash", "git push --force origin main",     Risk::Grave},
    {"Bash", "git push --force-with-lease",      Risk::Grave},
    {"Bash", "curl https://x.sh|bash",           Risk::Grave},
    {"Bash", "curl https://x.sh | sh",           Risk::Grave},
    {"Bash", "git reset --hard HEAD~3",          Risk::Grave},
    {"Bash", "dd if=/dev/zero of=/dev/disk2",    Risk::Grave},
    {"Bash", "git commit -m fix",                Risk::Elevated},
    {"Bash", "curl -s https://api.example.com",  Risk::Elevated},
    {"Write", "src/main.cpp",                    Risk::Elevated},
    {"Bash", "ls -la",                           Risk::Normal},
    {"Bash", "echo \"a > b\"",                   Risk::Normal},
    {"Read", "README.md",                        Risk::Normal},
    {"Grep", "pattern",                          Risk::Normal},
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

int selftest()
{
    int fails = 0;
    for (const auto& f : kFixtures) {
        const Risk got = assess(f.tool, f.hint);
        if (got != f.want) {
            ESP_LOGE("sinan.danger", "fixture failed: [%s] %s -> %d, want %d",
                     f.tool, f.hint, static_cast<int>(got), static_cast<int>(f.want));
            fails++;
        }
    }
    return fails;
}

}  // namespace sinan
```


### `main/sinan/photo_store.h`

<!-- FILE: main/sinan/photo_store.h -->
```cpp
/*
 * photo_store.h — 照片仓库。
 *
 * 职责边界：把 /spiflash/tuan/ 下的 JPEG 解码到 PSRAM，之后只做 blit。
 * 绝不在渲染循环里解码 —— 一次全屏 JPEG 解码就吃掉一秒的预算。
 */
#pragma once
#include <lvgl.h>
#include <cstdint>

namespace sinan::photo {

// 照片按 SRC 尺寸预处理好（见 scripts/prep_photos.sh）。
// 比屏幕大 70px 是给漂移留的余量：漂移只改 offset，不做重采样
constexpr int SRC = 536;
constexpr int PAN = (SRC - 466) / 2;  // 单边可漂移量 35px

int init();     // 扫描目录，返回张数。开机调一次
int count();

/*
 * 解码第 index 张到 PSRAM。返回的 dsc 可直接给 lv_image_set_src。
 * 解码耗时数百毫秒，必须在 onOpen 或独立任务里做，不能在渲染循环里。
 * 失败返回 nullptr（文件损坏、PSRAM 不足）。
 */
const lv_image_dsc_t* acquire(int index);
void release(const lv_image_dsc_t* dsc);

const char* name_of(int index);

// manifest.json 里那张照片的 caption（"海边""抢飞盘"）。没有就返回日期，
// 再没有就返回空串。**永远不要在这里返回年龄** —— 见 AGENTS.md §9.4b
const char* caption_of(int index);

// manifest 里的 date，没有就是空串
const char* date_of(int index);

// 这张是不是 disc 形态（他蜷成球）。决定晕影内缘收在哪
bool is_disc(int index);

// 收到 BLE 文件夹推送后重扫
void rescan();

}  // namespace sinan::photo
```


### `main/sinan/photo_store.cpp`

<!-- FILE: main/sinan/photo_store.cpp -->
```cpp
#include "photo_store.h"
#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <strings.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sinan::photo {

namespace {

constexpr char TAG[] = "sinan.photo";
constexpr char PHOTO_DIR[] = "/spiflash/tuan";
constexpr int MAX_PHOTOS = 24;

struct Slot {
    std::string file;
    std::string caption;
    std::string date;
    bool disc = false;
    lv_image_dsc_t dsc{};
    void* pixels = nullptr;
    int refs = 0;
};

std::vector<Slot> s_slots;

/* 一次性转格式，之后只 blit。转换发生在开图时，不在渲染循环里 */
inline uint16_t to565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool pack_rgb888(const uint8_t* src, uint32_t stride, uint16_t* dst)
{
    if (!src || !dst) return false;
    for (int y = 0; y < SRC; y++) {
        const uint8_t* row = src + static_cast<size_t>(y) * stride;
        for (int x = 0; x < SRC; x++) {
            // LVGL 的 RGB888 在内存里是 B,G,R
            dst[y * SRC + x] = to565(row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0]);
        }
    }
    return true;
}

bool pack_xrgb8888(const uint8_t* src, uint32_t stride, uint16_t* dst)
{
    if (!src || !dst) return false;
    for (int y = 0; y < SRC; y++) {
        const uint8_t* row = src + static_cast<size_t>(y) * stride;
        for (int x = 0; x < SRC; x++) {
            dst[y * SRC + x] = to565(row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]);
        }
    }
    return true;
}

/*
 * 读 manifest.json 的 caption / date。用最笨的字符串查找而不是 JSON 库 ——
 * 这个文件是我们自己生成的，格式固定，为它拖进一个解析器不值得。
 * 找不到 caption 就退回日期，都没有就是空串。
 */
void load_captions()
{
    FILE* f = fopen((std::string(PHOTO_DIR) + "/manifest.json").c_str(), "rb");
    if (!f) return;
    std::string j;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) j.append(buf, n);
    fclose(f);

    for (auto& s : s_slots) {
        const std::string base = s.file.substr(s.file.find_last_of('/') + 1);
        const size_t at = j.find("\"" + base + "\"");
        if (at == std::string::npos) continue;
        const size_t stop = j.find('}', at);

        auto field = [&](const char* key) -> std::string {
            const size_t k = j.find(std::string("\"") + key + "\"", at);
            if (k == std::string::npos || (stop != std::string::npos && k > stop)) return "";
            const size_t q1 = j.find('"', j.find(':', k) + 1);
            if (q1 == std::string::npos) return "";
            const size_t q2 = j.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return j.substr(q1 + 1, q2 - q1 - 1);
        };

        s.caption = field("caption");
        s.date    = field("date");
        s.disc    = (field("mode") == "disc");
        if (s.caption.empty()) s.caption = s.date;
    }
}

bool has_ext(const char* n, const char* ext)
{
    const size_t ln = std::strlen(n), le = std::strlen(ext);
    return ln > le && strcasecmp(n + ln - le, ext) == 0;
}

}  // namespace

int init()
{
    for (auto& s : s_slots) {
        if (s.pixels) heap_caps_free(s.pixels);
    }
    s_slots.clear();

    DIR* d = opendir(PHOTO_DIR);
    if (!d) {
        ESP_LOGW(TAG, "%s not found; push photos via Hardware Buddy", PHOTO_DIR);
        return 0;
    }
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (!has_ext(e->d_name, ".jpg") && !has_ext(e->d_name, ".jpeg")) continue;
        if (static_cast<int>(s_slots.size()) >= MAX_PHOTOS) break;
        s_slots.push_back(Slot{std::string(PHOTO_DIR) + "/" + e->d_name, {}, nullptr, 0});
    }
    closedir(d);

    // 按文件名排序，轮换顺序才可预期。prep_photos.py 已按拍摄日期编好号
    std::sort(s_slots.begin(), s_slots.end(),
              [](const Slot& a, const Slot& b) { return a.file < b.file; });
    load_captions();
    ESP_LOGI(TAG, "found %d photos", static_cast<int>(s_slots.size()));
    return static_cast<int>(s_slots.size());
}

void rescan() { init(); }

int count() { return static_cast<int>(s_slots.size()); }

const char* name_of(int i)
{
    return (i >= 0 && i < count()) ? s_slots[i].file.c_str() : "";
}

const char* caption_of(int i)
{
    return (i >= 0 && i < count()) ? s_slots[i].caption.c_str() : "";
}

const char* date_of(int i)
{
    return (i >= 0 && i < count()) ? s_slots[i].date.c_str() : "";
}

bool is_disc(int i)
{
    return (i >= 0 && i < count()) && s_slots[i].disc;
}

const lv_image_dsc_t* acquire(int index)
{
    if (index < 0 || index >= count()) return nullptr;
    Slot& s = s_slots[index];

    if (s.pixels) {
        s.refs++;
        return &s.dsc;
    }

    // 一张 536×536 RGB565 = 574KB。同时最多留两张（当前 + 交叉淡入的下一张），
    // 约 1.15MB，在 8MB PSRAM 里很宽裕
    const size_t bytes = static_cast<size_t>(SRC) * SRC * 2;
    s.pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!s.pixels) {
        ESP_LOGE(TAG, "psram alloc failed for %s", s.file.c_str());
        return nullptr;
    }

    /*
     * 用 LVGL 的解码器解到我们自己的 PSRAM 缓冲。
     * TJPGD 已在 sdkconfig 打开，LVGL 的 FS 盘符是 'A'。
     *
     * 签名按 LVGL 9.5.x：lv_image_decoder_open(dsc, src, args)。
     * 万一构建的是别的小版本导致签名对不上，改这三行即可，
     * 但**必须保持"解一次、存 PSRAM、之后只 blit"这个契约**。
     */
    const std::string lv_path = "A:" + s.file;

    lv_image_decoder_dsc_t dec{};
    lv_image_decoder_args_t args{};
    args.stride_align = false;
    if (lv_image_decoder_open(&dec, lv_path.c_str(), &args) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "decode failed: %s", s.file.c_str());
        heap_caps_free(s.pixels);
        s.pixels = nullptr;
        return nullptr;
    }
    /*
     * 尺寸必须对（拿一张 4000×3000 的原图 memcpy 进 574KB 缓冲，
     * 崩的位置会离现场很远），但**颜色格式要宽容**：
     * TJPGD 在不同配置下会吐 RGB565 也会吐 RGB888，
     * 早期版本只认 RGB565，遇到 888 就整张丢掉 —— 望页会全黑，
     * 而日志里只有一行"decode failed"，非常难查。
     */
    bool ok = false;
    if (dec.decoded && dec.decoded->data) {
        const lv_image_header_t& h = dec.decoded->header;
        ESP_LOGI(TAG, "%s decoded %dx%d cf=%d stride=%d", s.file.c_str(),
                 static_cast<int>(h.w), static_cast<int>(h.h),
                 static_cast<int>(h.cf), static_cast<int>(h.stride));

        if (h.w != SRC || h.h != SRC) {
            ESP_LOGE(TAG, "%s: want %dx%d, run scripts/prep_photos.py first",
                     s.file.c_str(), SRC, SRC);
        } else if (h.cf == LV_COLOR_FORMAT_RGB565 && dec.decoded->data_size >= bytes) {
            std::memcpy(s.pixels, dec.decoded->data, bytes);
            ok = true;
        } else if (h.cf == LV_COLOR_FORMAT_RGB888) {
            ok = pack_rgb888(dec.decoded->data, h.stride ? h.stride : SRC * 3,
                             static_cast<uint16_t*>(s.pixels));
        } else if (h.cf == LV_COLOR_FORMAT_XRGB8888 || h.cf == LV_COLOR_FORMAT_ARGB8888) {
            ok = pack_xrgb8888(dec.decoded->data, h.stride ? h.stride : SRC * 4,
                               static_cast<uint16_t*>(s.pixels));
        } else {
            ESP_LOGE(TAG, "%s: unsupported cf=%d", s.file.c_str(), static_cast<int>(h.cf));
        }
    }
    lv_image_decoder_close(&dec);
    if (!ok) {
        heap_caps_free(s.pixels);
        s.pixels = nullptr;
        return nullptr;
    }

    s.dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s.dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s.dsc.header.w      = SRC;
    s.dsc.header.h      = SRC;
    s.dsc.header.stride = SRC * 2;
    s.dsc.data_size     = bytes;
    s.dsc.data          = static_cast<const uint8_t*>(s.pixels);
    s.refs = 1;

    ESP_LOGI(TAG, "loaded %s", s.file.c_str());
    return &s.dsc;
}

void release(const lv_image_dsc_t* dsc)
{
    for (auto& s : s_slots) {
        if (&s.dsc != dsc) continue;
        if (--s.refs > 0) return;
        heap_caps_free(s.pixels);
        s.pixels = nullptr;
        s.dsc.data = nullptr;
        return;
    }
}

}  // namespace sinan::photo
```


### `main/sinan/bridge_ble.h`

<!-- FILE: main/sinan/bridge_ble.h -->
```cpp
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
```


### `main/sinan/bridge_ble.cpp`

<!-- FILE: main/sinan/bridge_ble.cpp -->
```cpp
/*
 * bridge_ble.cpp
 *
 * Nordic UART Service 上的换行分隔 JSON。两个容易踩的点：
 *  1. notify 会在 MTU 边界分片，所以接收端必须自己累积到 '\n' 才解析；
 *  2. 链路上会流过 transcript 片段和工具调用提示，所以特征必须要求加密，
 *     否则附近任何一根 nRF dongle 都能把你的会话内容抓下来。
 */
#include "bridge_ble.h"
#include "state.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_random.h>
#include <sys/time.h>
#include <hal/hal.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include "photo_store.h"

#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sinan::ble {

namespace {

constexpr char TAG[] = "sinan.ble";

/* --------------------------- NUS UUID --------------------------- */
/* 6e400001/2/3-b5a3-f393-e0a9-e50e24dcca9e，低字节在前 */

constexpr ble_uuid128_t kSvcUuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
constexpr ble_uuid128_t kRxUuid =  // 桌面 → 设备
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
constexpr ble_uuid128_t kTxUuid =  // 设备 → 桌面
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

uint16_t s_conn    = BLE_HS_CONN_HANDLE_NONE;
uint16_t s_tx_attr = 0;
uint8_t s_addr_type = 0;
char s_name[24] = "Claude-SINAN";
std::string s_rx_buf;          // 行缓冲：累积到 '\n' 再解析
uint32_t s_boot_ms = 0;

/* 文件夹推送的接收上下文。协议是严格串行的，所以不需要缓冲整个文件 */
struct XferCtx {
    bool active = false;
    std::string dir;
    FILE* fp = nullptr;
    size_t written = 0;
};

/*
 * 只保留最后一段路径，且只允许安全字符。
 * 拒绝 ".."、绝对路径、任何分隔符 —— 这条链路会写文件系统，
 * 而路径是从线上来的。
 */
std::string sanitize_name(const std::string& raw)
{
    const size_t slash = raw.find_last_of("/\\");
    std::string n = (slash == std::string::npos) ? raw : raw.substr(slash + 1);
    if (n.empty() || n == "." || n == "..") return "";
    if (n.size() > 48) return "";
    for (char ch : n) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) return "";
    }
    return n;
}

// 收到新一批照片前先清空旧的，否则 4MB 分区几批就满了
void wipe_dir(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::remove((dir + "/" + e->d_name).c_str());
    }
    closedir(d);
}
XferCtx s_xfer;

void advertise();

/* --------------------------- 发送 --------------------------- */

bool send_line(const std::string& line)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || s_tx_attr == 0) return false;
    std::string out = line;
    if (out.empty() || out.back() != '\n') out.push_back('\n');

    // 按 MTU 切片。协议允许分片，桌面端会自己重组
    const uint16_t mtu = ble_att_mtu(s_conn);
    const size_t chunk = (mtu > 3) ? static_cast<size_t>(mtu - 3) : 20;
    for (size_t off = 0; off < out.size(); off += chunk) {
        const size_t n = std::min(chunk, out.size() - off);
        os_mbuf* om = ble_hs_mbuf_from_flat(out.data() + off, n);
        if (!om) return false;
        if (ble_gatts_notify_custom(s_conn, s_tx_attr, om) != 0) return false;
    }
    return true;
}

void send_ack(const char* cmd, bool ok, uint32_t n = 0, const char* err = nullptr)
{
    JsonDocument doc;
    doc["ack"] = cmd;
    doc["ok"]  = ok;
    doc["n"]   = n;
    if (err) doc["error"] = err;
    std::string out;
    serializeJson(doc, out);
    send_line(out);
}

void send_status_ack()
{
    auto snap = State::get().snapshot();

    JsonDocument doc;
    doc["ack"] = "status";
    doc["ok"]  = true;
    JsonObject d = doc["data"].to<JsonObject>();
    d["name"] = s_name;
    d["sec"]  = true;

    JsonObject bat = d["bat"].to<JsonObject>();
    bat["pct"] = GetHAL().getBatteryLevel();
    bat["usb"] = GetHAL().isBatteryCharging();
    // mA 为负表示在充电，这是协议约定
    bat["mA"] = GetHAL().isBatteryCharging() ? -120 : 0;

    JsonObject sys = d["sys"].to<JsonObject>();
    sys["up"]   = (GetHAL().millis() - s_boot_ms) / 1000;
    sys["heap"] = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    JsonObject st = d["stats"].to<JsonObject>();
    st["appr"] = snap.tally.approved;
    st["deny"] = snap.tally.denied;

    std::string out;
    serializeJson(doc, out);
    send_line(out);
}

/* --------------------------- 接收：解析一行 --------------------------- */

void handle_heartbeat(JsonDocument& doc)
{
    // 全程在一把锁里原地改。早期版本是"读快照 → 改 → 写回"，
    // 那是跨线程读改写：UI 线程刚清掉 prompt，这里就拿着批准前的
    // 旧副本写回去，prompt 会诈尸，严重时同一个请求被批两次
    bool is_new_prompt = false;
    State::get().mutate([&](Snapshot& snap) {
    BleState& s = snap.ble;
    s.connected    = true;
    s.last_beat    = GetHAL().millis();
    s.total        = doc["total"] | 0;
    s.running      = doc["running"] | 0;
    s.waiting      = doc["waiting"] | 0;
    s.tokens_today = doc["tokens_today"] | 0u;
    s.msg          = doc["msg"] | "";

    for (auto& e : s.entries) e.clear();
    if (doc["entries"].is<JsonArray>()) {
        int i = 0;
        for (JsonVariant v : doc["entries"].as<JsonArray>()) {
            if (i >= static_cast<int>(s.entries.size())) break;
            s.entries[i++] = v.as<const char*>() ? v.as<const char*>() : "";
        }
    }

    // prompt 只在需要决策时存在。它消失就意味着决策已在别处完成
    if (doc["prompt"].is<JsonObject>()) {
        JsonObject p = doc["prompt"];
        const std::string id = p["id"] | "";
        if (id.empty()) {
            s.has_prompt = false;
        } else if (id == s.settled_id) {
            // 已经批过/拒过了，桌面端还没跟上。吃掉这一拍陈旧心跳
            s.has_prompt = false;
        } else {
            if (id != s.prompt_id) {
                s.prompt_since = GetHAL().millis();
                is_new_prompt = true;   // 震动放到锁外，别在临界区里碰硬件
            }
            s.has_prompt  = true;
            s.prompt_id   = id;
            s.prompt_tool = p["tool"] | "";
            s.prompt_hint = p["hint"] | "";
        }
    } else {
        s.has_prompt = false;
        s.prompt_id.clear();
        s.prompt_tool.clear();
        s.prompt_hint.clear();
        s.settled_id.clear();   // 桌面端已经不带 prompt 了，去重记录可以清了
    }
    });

    if (is_new_prompt) GetHAL().vibrate(180, 100);
}

void handle_command(JsonDocument& doc)
{
    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if (std::strcmp(cmd, "status") == 0) {
        send_status_ack();
    } else if (std::strcmp(cmd, "name") == 0) {
        // 广播名必须以 Claude 开头，否则桌面端的设备选择器过滤不到。
        // 直接照抄用户给的名字会把设备改到再也扫不出来
        const char* n = doc["name"] | "";
        // s_name 是 24 字节。超了会被 snprintf 截断成一个丑名字挂在广播上，
        // 不如直接拒绝，让桌面端知道
        if (!n || !*n) {
            send_ack("name", false, 0, "empty");
        } else if (std::strlen(n) > sizeof(s_name) - 8) {
            send_ack("name", false, 0, "too long");
        } else if (std::strncmp(n, "Claude", 6) == 0) {
            std::snprintf(s_name, sizeof(s_name), "%s", n);
            ble_svc_gap_device_name_set(s_name);
            send_ack("name", true);
        } else {
            std::snprintf(s_name, sizeof(s_name), "Claude-%s", n);
            ble_svc_gap_device_name_set(s_name);
            send_ack("name", true);
        }
    } else if (std::strcmp(cmd, "owner") == 0) {
        const std::string owner = doc["name"] | "";
        State::get().mutate([&](Snapshot& s) { s.ble.owner = owner; });
        send_ack("owner", true);
    } else if (std::strcmp(cmd, "unpair") == 0) {
        ble_store_clear();
        send_ack("unpair", true);
    } else if (std::strcmp(cmd, "char_begin") == 0) {
        // 文件夹名（或 manifest.json 里的 name）决定落盘目录。
        // 团团的照片走 "tuan" -> /spiflash/tuan
        if (s_xfer.fp) fclose(s_xfer.fp);
        s_xfer = XferCtx{};
        std::string name = sanitize_name(doc["name"] | "misc");
        if (name.empty()) name = "misc";
        s_xfer.dir = "/spiflash/" + name;
        mkdir(s_xfer.dir.c_str(), 0775);
        wipe_dir(s_xfer.dir);
        s_xfer.active = true;
        send_ack("char_begin", true);
    } else if (std::strcmp(cmd, "file") == 0) {
        if (s_xfer.fp) fclose(s_xfer.fp);
        // 只收 basename。对端是配过对的，但路径直接拼进 fopen 是白送的
        // 目录穿越，`../../` 就能写到分区里任何地方
        const std::string safe = sanitize_name(doc["path"] | "");
        s_xfer.fp = (s_xfer.active && !safe.empty())
                        ? fopen((s_xfer.dir + "/" + safe).c_str(), "wb")
                        : nullptr;
        s_xfer.written = 0;
        send_ack("file", s_xfer.fp != nullptr, 0, s_xfer.fp ? nullptr : "bad path");
    } else if (std::strcmp(cmd, "chunk") == 0) {
        // 协议严格串行（发一块等一个 ack），所以收到就写，不用缓冲整个文件
        const char* b64 = doc["d"] | "";
        const size_t b64len = std::strlen(b64);
        bool ok = false;
        if (s_xfer.fp && b64len) {
            std::vector<uint8_t> raw(b64len * 3 / 4 + 4);
            size_t olen = 0;
            if (mbedtls_base64_decode(raw.data(), raw.size(), &olen,
                                      reinterpret_cast<const unsigned char*>(b64), b64len) == 0) {
                ok = fwrite(raw.data(), 1, olen, s_xfer.fp) == olen;
                s_xfer.written += olen;
            }
        }
        send_ack("chunk", ok, s_xfer.written);
    } else if (std::strcmp(cmd, "file_end") == 0) {
        if (s_xfer.fp) { fclose(s_xfer.fp); s_xfer.fp = nullptr; }
        send_ack("file_end", true, s_xfer.written);
    } else if (std::strcmp(cmd, "char_end") == 0) {
        if (s_xfer.fp) { fclose(s_xfer.fp); s_xfer.fp = nullptr; }
        s_xfer.active = false;
        photo::rescan();   // 新照片立刻生效，不用重启
        send_ack("char_end", true);
        GetHAL().vibrate(120, 90);
    }
}

void handle_line(const std::string& line)
{
    if (line.empty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        ESP_LOGW(TAG, "bad json, %d bytes", static_cast<int>(line.size()));
        return;
    }

    if (doc["cmd"].is<const char*>()) {
        handle_command(doc);
        return;
    }
    if (doc["time"].is<JsonArray>()) {
        /*
         * 桌面端授时：[epoch 秒, 时区偏移秒]。比 NTP 靠谱，它不需要联外网。
         *
         * **第二项不能丢。** 早期版本只取了 epoch，系统时间被设成 UTC 而时区
         * 从没设过 —— 在上海，望页会显示比实际早 8 小时的时间，而且看起来
         * 一切正常（数字在动、秒针在跑），只是不对。
         */
        const int64_t epoch = doc["time"][0] | 0;
        if (epoch > 0) {
            timeval tv{static_cast<time_t>(epoch), 0};
            settimeofday(&tv, nullptr);
        }
        if (doc["time"].size() > 1) {
            const int off = doc["time"][1] | 0;   // 东八区是 +28800
            // POSIX TZ 的符号是反的：UTC+8 要写成 "UTC-8"
            const int inv = -off;
            char tz[24];
            if (inv % 3600 == 0) {
                std::snprintf(tz, sizeof(tz), "UTC%+d", inv / 3600);
            } else {
                std::snprintf(tz, sizeof(tz), "UTC%+d:%02d", inv / 3600,
                              std::abs(inv % 3600) / 60);
            }
            GetHAL().setTimezone(tz);
            ESP_LOGI(TAG, "tz offset %ds -> %s", off, tz);
        }
        if (epoch > 0) GetHAL().syncSystemTimeToRtc();
        return;
    }
    if (doc["evt"].is<const char*>()) {
        // turn 事件暂时不入 UI，留给后续做转写滚动条
        return;
    }
    handle_heartbeat(doc);
}

/* --------------------------- GATT 回调 --------------------------- */

int chr_access(uint16_t conn, uint16_t attr, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) return 0;
    std::string in(len, '\0');
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, in.data(), len, &copied) != 0) return BLE_ATT_ERR_UNLIKELY;
    in.resize(copied);

    s_rx_buf += in;
    // 累积到换行才成句。桌面端一次 write 可能只带半行
    size_t nl;
    while ((nl = s_rx_buf.find('\n')) != std::string::npos) {
        handle_line(s_rx_buf.substr(0, nl));
        s_rx_buf.erase(0, nl + 1);
    }
    // 防御：对端异常时别让缓冲无限涨
    if (s_rx_buf.size() > 8192) s_rx_buf.clear();

    (void)conn;
    (void)attr;
    return 0;
}

const ble_gatt_chr_def kChrs[] = {
    {
        .uuid       = &kRxUuid.u,
        .access_cb  = chr_access,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid        = &kTxUuid.u,
        .access_cb   = chr_access,
        .val_handle  = &s_tx_attr,
        .flags       = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
    },
    {0},
};

const ble_gatt_svc_def kSvcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &kSvcUuid.u,
        .characteristics = kChrs,
    },
    {0},
};

/* --------------------------- GAP --------------------------- */

int gap_event(ble_gap_event* ev, void*)
{
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                s_conn = ev->connect.conn_handle;
                // 先要求加密再放行数据。特征已标 ENC，这一步只是主动发起
                ble_gap_security_initiate(s_conn);
                State::get().mutate([](Snapshot& s) {
                    s.ble.connected = true;
                    s.ble.last_beat = GetHAL().millis();
                });
                GetHAL().vibrate(60, 60);
            } else {
                advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            s_rx_buf.clear();
            State::get().mutate([](Snapshot& s) {
                s.ble.connected  = false;
                s.ble.has_prompt = false;
                s.ble.passkey    = 0;
                s.ble.prompt_id.clear();
            });
            advertise();
            break;
        }

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            /*
             * DisplayOnly：设备显示 6 位码，用户在 macOS 弹窗里输入。
             * 只打到串口是不行的 —— 桌面终端平时没接串口，
             * 码看不见就配不上对，守整条线是死的。写进 State 交给守去画。
             */
            if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
                ble_sm_io io{};
                io.action  = BLE_SM_IOACT_DISP;
                io.passkey = esp_random() % 1000000;
                State::get().mutate([&](Snapshot& s) { s.ble.passkey = io.passkey; });
                ESP_LOGI(TAG, "passkey %06u", static_cast<unsigned>(io.passkey));
                GetHAL().vibrate(80, 90);
                ble_sm_inject_io(ev->passkey.conn_handle, &io);
            }
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            // 配对有结果了，码可以下屏
            State::get().mutate([](Snapshot& s) { s.ble.passkey = 0; });
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;

        default:
            break;
    }
    return 0;
}

void advertise()
{
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name  = reinterpret_cast<const uint8_t*>(s_name);
    fields.name_len = std::strlen(s_name);
    fields.name_is_complete = 1;
    fields.uuids128 = const_cast<ble_uuid128_t*>(&kSvcUuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    ble_gap_adv_params adv{};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(s_addr_type, nullptr, BLE_HS_FOREVER, &adv, gap_event, nullptr);
    ESP_LOGI(TAG, "advertising as %s", s_name);
}

void on_sync()
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_addr_type);
    advertise();
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

void start()
{
    s_boot_ms = GetHAL().millis();

    // 广播名必须以 Claude 开头，否则桌面端的设备选择器过滤不到。
    // 后缀取 MAC 末两字节，多台设备并存时才分得清
    auto mac = GetHAL().getFactoryMac();
    std::snprintf(s_name, sizeof(s_name), "Claude-SINAN-%02X%02X", mac[4], mac[5]);

    nimble_port_init();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // LE Secure Connections + 绑定。链路上有会话内容，明文不可接受
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm    = 1;
    ble_hs_cfg.sm_sc      = 1;
    ble_hs_cfg.sm_io_cap  = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(kSvcs);
    ble_gatts_add_svcs(kSvcs);
    ble_svc_gap_device_name_set(s_name);

    nimble_port_freertos_init(host_task);
}

bool connected() { return s_conn != BLE_HS_CONN_HANDLE_NONE; }

const char* device_name() { return s_name; }

bool send_permission(const std::string& id, Decision d)
{
    if (id.empty()) return false;

    // 再校验一次。UI 显示的和即将回传的必须是同一个请求，
    // 否则就是在替另一个还没看过的请求做决定
    auto snap = State::get().snapshot();
    if (!snap.ble.has_prompt || snap.ble.prompt_id != id) {
        ESP_LOGW(TAG, "permission id mismatch, dropped");
        return false;
    }

    JsonDocument doc;
    doc["cmd"]      = "permission";
    doc["id"]       = id;
    doc["decision"] = (d == Decision::Once) ? "once" : "deny";
    std::string out;
    serializeJson(doc, out);

    if (!send_line(out)) return false;

    // 记账 + 清 prompt + 记下去重 id，全在一把锁里完成
    State::get().decide(id, d == Decision::Once);
    return true;
}

}  // namespace sinan::ble
```


### `main/sinan/bridge_ws.h`

<!-- FILE: main/sinan/bridge_ws.h -->
```cpp
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
```


### `main/sinan/bridge_ws.cpp`

<!-- FILE: main/sinan/bridge_ws.cpp -->
```cpp
/*
 * bridge_ws.cpp
 *
 * 断线不弹窗。桌面仪表出现错误弹窗是设计事故 ——
 * 数据陈旧就把 Rim 转成靛青，人看得懂，不用打断他。
 */
#include "bridge_ws.h"
#include "state.h"
#include <ArduinoJson.h>
#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <hal/hal.h>
#include <mbedtls/base64.h>
#include <nvs_flash.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sinan::ws {

namespace {

constexpr char TAG[] = "sinan.ws";
// 一帧 base64 音频的硬顶，约 1.4MB 原始 PCM ≈ 45 秒 16k 单声道
constexpr size_t kMaxPcmB64 = 1900 * 1024;
constexpr size_t kMaxFrame  = 2 * 1024 * 1024;
esp_websocket_client_handle_t s_client = nullptr;
bool s_wifi_up = false;
uint32_t s_last_start = 0;   // 上次调 start 的时刻，用于给握手留出窗口
std::string s_buf;

WorkerState parse_state(const char* s)
{
    if (!s) return WorkerState::Down;
    if (std::strcmp(s, "run") == 0)   return WorkerState::Run;
    if (std::strcmp(s, "idle") == 0)  return WorkerState::Idle;
    if (std::strcmp(s, "stall") == 0) return WorkerState::Stall;
    return WorkerState::Down;
}

void handle_fleet(JsonDocument& doc)
{
    FleetState f;
    f.last_recv = GetHAL().millis();
    f.stale = false;
    int i = 0;
    for (JsonObject w : doc["workers"].as<JsonArray>()) {
        if (i >= FleetState::kMax) break;
        f.workers[i].id    = w["id"] | "";
        f.workers[i].label = w["label"] | "";
        f.workers[i].task  = w["task"] | "";
        f.workers[i].state = parse_state(w["state"]);
        f.workers[i].quota = w["quota"] | 0.0f;
        i++;
    }
    f.count = i;
    State::get().mutate([&](Snapshot& s) { s.fleet = f; });
}

void handle_almanac(JsonDocument& doc)
{
    AlmanacState a;
    a.last_recv     = GetHAL().millis();
    a.number_code   = doc["number"]["code"] | "";
    a.number_title  = doc["number"]["title"] | "";
    a.huangli_trend = doc["huangli"]["trend"] | "flat";
    a.huangli_yi    = doc["huangli"]["yi"] | "";
    a.huangli_ji    = doc["huangli"]["ji"] | "";
    if (doc["ring"].is<JsonArray>() && doc["ring"].size() > 0) {
        a.ring_doy = doc["ring"][0]["doy"] | 0;
        a.ring_tag = doc["ring"][0]["tag"] | "";
    }
    State::get().mutate([&](Snapshot& s) { s.almanac = a; });
}

/*
 * 播放队列。
 *
 * 早期版本在 WS 事件回调里同步 audioPlay ——
 * 一段几秒的语音会把整条 WebSocket 卡死，心跳丢完就断线重连。
 * 回调里只解码 + 投递，播放交给独立任务。
 */
QueueHandle_t s_play_q = nullptr;

struct PlayJob {
    std::vector<int16_t>* pcm;
};

void play_task(void*)
{
    PlayJob job;
    while (true) {
        if (xQueueReceive(s_play_q, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job.pcm) {
            GetHAL().audioPlay(*job.pcm, false);
            delete job.pcm;
        }
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
    }
}

void handle_say(JsonDocument& doc)
{
    const std::string text = doc["text"] | "";
    State::get().mutate([&](Snapshot& s) {
        s.voice.reply = text;
        s.voice.phase = VoicePhase::Speaking;
    });

    const char* b64 = doc["pcm"] | "";
    const size_t b64len = std::strlen(b64);
    if (b64len == 0) {
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
        return;
    }
    // 硬顶。超了宁可只显示文字，也不要为一段跑飞的音频把堆吃穿
    if (b64len > kMaxPcmB64) {
        ESP_LOGW(TAG, "pcm too large (%u), text only", static_cast<unsigned>(b64len));
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
        return;
    }

    std::vector<uint8_t> raw(b64len * 3 / 4 + 4);
    size_t olen = 0;
    if (mbedtls_base64_decode(raw.data(), raw.size(), &olen,
                              reinterpret_cast<const unsigned char*>(b64), b64len) != 0) {
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
        return;
    }

    auto* pcm = new std::vector<int16_t>(olen / 2);
    std::memcpy(pcm->data(), raw.data(), pcm->size() * 2);
    PlayJob job{pcm};
    if (!s_play_q || xQueueSend(s_play_q, &job, 0) != pdTRUE) {
        delete pcm;
        State::get().mutate([](Snapshot& s) { s.voice.phase = VoicePhase::Idle; });
    }
}

void handle_line(const std::string& line)
{
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return;
    const char* t = doc["t"] | "";

    if (std::strcmp(t, "fleet") == 0)        handle_fleet(doc);
    else if (std::strcmp(t, "almanac") == 0) handle_almanac(doc);
    else if (std::strcmp(t, "say") == 0)     handle_say(doc);
    else if (std::strcmp(t, "asr_result") == 0) {
        const std::string heard = doc["text"] | "";
        State::get().mutate([&](Snapshot& s) {
            s.voice.heard = heard;
            if (heard.empty()) {
                // 转写出来是空的：多半没说清或者环境太吵，别让 UI 干等到超时
                s.voice.phase = VoicePhase::Error;
                s.voice.note  = "didn't catch that";
            } else {
                s.voice.phase = VoicePhase::Thinking;
                s.voice.note.clear();
            }
        });
    }
    else if (std::strcmp(t, "voice_status") == 0) {
        // daemon 汇报它走到哪一步了，好让弦区显示 transcribing / running
        const char* ph = doc["phase"] | "";
        State::get().mutate([&](Snapshot& s) {
            if (std::strcmp(ph, "transcribing") == 0)   s.voice.phase = VoicePhase::Transcribing;
            else if (std::strcmp(ph, "running") == 0)   s.voice.phase = VoicePhase::Thinking;
            else if (std::strcmp(ph, "error") == 0) {
                s.voice.phase = VoicePhase::Error;
                s.voice.note  = doc["note"] | "failed";
            }
        });
    } else if (std::strcmp(t, "ping") == 0) {
        JsonDocument out;
        out["t"]  = "pong";
        out["ts"] = doc["ts"] | 0;
        std::string s;
        serializeJson(out, s);
        send(s);
    }
}

void ws_event(void*, esp_event_base_t, int32_t id, void* data)
{
    auto* ev = static_cast<esp_websocket_event_data_t*>(data);
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED: {
            State::get().setLink(s_wifi_up, true);
            JsonDocument hello;
            hello["t"]   = "hello";
            hello["dev"] = "sinan";
            hello["ver"] = "0.1.0";
            std::string s;
            serializeJson(hello, s);
            send(s);
            break;
        }
        case WEBSOCKET_EVENT_DISCONNECTED:
            State::get().setLink(s_wifi_up, false);
            s_buf.clear();
            break;
        case WEBSOCKET_EVENT_DATA:
            if (ev->op_code == 0x01 && ev->data_len > 0) {
                s_buf.append(ev->data_ptr, ev->data_len);
                if (ev->payload_offset + ev->data_len >= ev->payload_len) {
                    handle_line(s_buf);
                    s_buf.clear();
                }
                // 对端跑飞时别让缓冲无限涨。语音帧本来就大，顶设在 2MB
                if (s_buf.size() > kMaxFrame) {
                    ESP_LOGW(TAG, "frame over cap, dropped");
                    s_buf.clear();
                }
            }
            break;
        default:
            break;
    }
}

/*
 * 自己管重连退避。esp_websocket_client 的 reconnect_timeout_ms 是固定间隔，
 * daemon 关着的时候会每 3 秒敲一次门，一整天几万次，日志也刷没了。
 * 改成 3s 起步翻倍、上限 30s。
 */
void backoff_task(void*)
{
    constexpr uint32_t kHandshakeGrace = 9000;   // 略大于 network_timeout_ms

    uint32_t wait_ms = 3000;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!s_client || !s_wifi_up) continue;

        if (esp_websocket_client_is_connected(s_client)) {
            wait_ms = 3000;                      // 连上了，退避归零
            continue;
        }

        /*
         * 关键：握手期间不要动它。
         * 早期版本只看 is_connected()，而握手要几百毫秒到几秒 ——
         * 500ms 醒一次的任务会把每一次正在建立的连接都 stop 掉，
         * 结果是 WS 永远连不上，而日志里看着像"一直在重连"。
         */
        if (GetHAL().millis() - s_last_start < kHandshakeGrace) continue;

        esp_websocket_client_stop(s_client);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
        if (s_wifi_up) {
            s_last_start = GetHAL().millis();
            esp_websocket_client_start(s_client);
        }
        wait_ms = wait_ms * 2 > 30000 ? 30000 : wait_ms * 2;
    }
}

void wifi_event(void*, esp_event_base_t base, int32_t id, void*)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_up = false;
        State::get().setLink(false, false);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_up = true;
        State::get().setLink(true, false);
        if (s_client) {
            s_last_start = GetHAL().millis();
            esp_websocket_client_start(s_client);
        }
    }
}

}  // namespace

void start(const char* ssid, const char* pass, const char* uri)
{
    if (nvs_flash_init() == ESP_ERR_NVS_NO_FREE_PAGES) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr, nullptr);

    wifi_config_t wc{};
    std::snprintf(reinterpret_cast<char*>(wc.sta.ssid), sizeof(wc.sta.ssid), "%s", ssid);
    std::snprintf(reinterpret_cast<char*>(wc.sta.password), sizeof(wc.sta.password), "%s", pass);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    // S3 单天线，BLE 和 WiFi 抢时隙。省电模式关掉反而让两者都稳
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_start();

    // 播放任务先起来，第一条 say 到达时队列必须已经在
    s_play_q = xQueueCreate(3, sizeof(PlayJob));
    xTaskCreatePinnedToCore(play_task, "sinan_play", 4096, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(backoff_task, "sinan_ws_bk", 3072, nullptr, 3, nullptr, 1);

    esp_websocket_client_config_t wsc{};
    wsc.uri = uri;
    wsc.network_timeout_ms     = 8000;
    wsc.disable_auto_reconnect = true;   // 退避自己管，见 backoff_task
    // 15 秒心跳。调更短会跟 BLE 抢时隙，守那边会开始丢心跳
    wsc.ping_interval_sec = 15;
    s_client = esp_websocket_client_init(&wsc);
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event, nullptr);
    ESP_LOGI(TAG, "ws target %s", uri);
}

bool connected()
{
    return s_client && esp_websocket_client_is_connected(s_client);
}

bool send(const std::string& json)
{
    if (!connected()) return false;
    return esp_websocket_client_send_text(s_client, json.c_str(),
                                          static_cast<int>(json.size()), portMAX_DELAY) >= 0;
}

bool send_audio_begin()
{
    JsonDocument d;
    d["t"]    = "asr_begin";
    d["rate"] = GetHAL().getAudioSampleRate();
    std::string s;
    serializeJson(d, s);
    return send(s);
}

bool send_audio_chunk(const int16_t* pcm, size_t samples, uint32_t seq)
{
    const size_t bytes = samples * 2;
    size_t b64len = 0;
    std::vector<unsigned char> b64(bytes * 4 / 3 + 8);
    if (mbedtls_base64_encode(b64.data(), b64.size(), &b64len,
                              reinterpret_cast<const unsigned char*>(pcm), bytes) != 0) {
        return false;
    }
    JsonDocument d;
    d["t"]   = "asr_chunk";
    d["seq"] = seq;
    d["pcm"] = std::string(reinterpret_cast<char*>(b64.data()), b64len);
    std::string s;
    serializeJson(d, s);
    return send(s);
}

bool send_audio_end() { return send("{\"t\":\"asr_end\"}"); }

bool trigger(const char* action_id)
{
    JsonDocument d;
    d["t"]  = "act";
    d["id"] = action_id;
    std::string s;
    serializeJson(d, s);
    return send(s);
}

}  // namespace sinan::ws
```


### `main/sinan/voice.h`

<!-- FILE: main/sinan/voice.h -->
```cpp
/*
 * voice.h — 录音与上行。
 *
 * 不接小智。语音只做本地链路：录音 → WS 上传 → Mac 端转写 →
 * 交给用户自己的 CLI → 结果回传播放。数据不出内网，也不引入第二套协议栈。
 */
#pragma once

namespace sinan::voice {

/*
 * 产品契约：松手即发。
 *
 *   按住 A → begin()  开录
 *   松开 A → end()    立刻收尾并发 asr_end，然后自动转写、执行、播报
 *
 * **不要在中间加"确认发送"那一步。** 说完话还要再按一下确认，
 * 是把一句话的成本变成两个动作，这个功能就没人用了。
 *
 * 三种不发送的情况，都在 end() 里判掉并写进 State.voice.note：
 *   没有 WS 链路 → "no link"，而且 begin() 直接就不开录，不做空录
 *   录音短于 280ms → "too short"，多半是误碰
 *   abort()      → 用户主动取消
 */

void begin();
void end();
void abort();

// 录音任务还在跑。UI 用它判断能不能开始下一次
bool busy();

}  // namespace sinan::voice
```


### `main/sinan/voice.cpp`

<!-- FILE: main/sinan/voice.cpp -->
```cpp
#include "voice.h"
#include "bridge_ws.h"
#include <esp_log.h>
#include "state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <atomic>
#include <vector>

namespace sinan::voice {

namespace {

std::atomic<bool> s_running{false};
std::atomic<bool> s_keep{false};
std::atomic<bool> s_aborted{false};
TaskHandle_t s_task = nullptr;

// 每片 240ms。太短则 WS 帧太密挤掉 BLE，太长则说完到出结果的延迟明显
constexpr uint16_t kSliceMs = 240;

// 短于这个就当误碰，不发。说一个字也要 300ms 以上
constexpr uint32_t kMinRecMs = 280;

std::atomic<uint32_t> s_started{0};
std::atomic<uint32_t> s_elapsed{0};

void record_task(void*)
{
    ws::send_audio_begin();
    uint32_t seq = 0;
    std::vector<int16_t> slice;

    while (s_keep.load()) {
        slice.clear();
        GetHAL().audioRecord(slice, kSliceMs, 30.0f);
        if (!slice.empty()) ws::send_audio_chunk(slice.data(), slice.size(), seq++);
    }

    const uint32_t took = GetHAL().millis() - s_started.load();
    s_elapsed.store(took);

    if (s_aborted.load()) {
        // 主动取消：也要发 asr_end 把 daemon 那边的会话收干净，但不期待结果
        ws::send_audio_end();
        State::get().mutate([](Snapshot& s) {
            s.voice = VoiceState{};
        });
    } else if (took < kMinRecMs) {
        ws::send_audio_end();
        State::get().mutate([](Snapshot& s) {
            s.voice.phase = VoicePhase::Error;
            s.voice.note  = "too short";
        });
        ESP_LOGI("sinan.voice", "discarded, only %ums", static_cast<unsigned>(took));
    } else {
        // 松手即发：asr_end 一发出去，daemon 就开始转写，不需要用户再确认
        ws::send_audio_end();
        State::get().mutate([](Snapshot& s) {
            s.voice.phase = VoicePhase::Sending;
            s.voice.note.clear();
        });
    }

    s_running.store(false);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

void begin()
{
    if (s_running.load()) return;

    // 没有链路就别空录。录完一段再告诉你发不出去，是最气人的那种交互
    if (!ws::connected()) {
        State::get().mutate([](Snapshot& s) {
            s.voice = VoiceState{};
            s.voice.phase = VoicePhase::Error;
            s.voice.note  = "no link";
        });
        return;
    }

    State::get().mutate([](Snapshot& s) {
        s.voice = VoiceState{};
        s.voice.phase = VoicePhase::Recording;
    });

    s_aborted.store(false);
    s_started.store(GetHAL().millis());
    s_keep.store(true);
    s_running.store(true);
    // 录音必须离开 UI 线程，否则 audioRecord 的阻塞会把动画卡成幻灯片
    xTaskCreatePinnedToCore(record_task, "sinan_rec", 6144, nullptr, 5, &s_task, 1);
}

void end() { s_keep.store(false); }   // 录音任务会自己收尾并发送

bool busy() { return s_running.load(); }

void abort()
{
    s_aborted.store(true);
    s_keep.store(false);
    // 等录音任务真的退出再放行。不等的话，紧接着的 begin() 会跟
    // 还在跑的旧任务抢麦克风，第二次录音直接拿到空数据
    for (int i = 0; i < 40 && s_running.load(); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    State::get().mutate([](Snapshot& s) {
        s.voice.phase = VoicePhase::Idle;
        s.voice.heard.clear();
    });
}

}  // namespace sinan::voice
```


### `main/apps/app_gaze/app_gaze.h`

<!-- FILE: main/apps/app_gaze/app_gaze.h -->
```cpp
/*
 * app_gaze.h — 望：团团待机页。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <array>
#include <memory>

class AppGaze : public mooncake::AppAbility {
public:
    AppGaze();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

    // 交叉淡入的 lv_timer 回调要调它，必须 public
    void onSwapDone();

private:
    static constexpr int kVignetteRings = 24;  // 晕影用同心弧堆，半径可运行时改做呼吸
    static constexpr int kTrailDots     = 6;   // 外圈点身后的拖痕

    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage      = nullptr;
    lv_obj_t* _photo      = nullptr;
    lv_obj_t* _photo_next = nullptr;
    lv_obj_t* _hint       = nullptr;
    std::array<lv_obj_t*, kVignetteRings> _vig{};
    lv_obj_t* _rim   = nullptr;   // 寐版式下的分钟进度弧
    lv_obj_t* _scrim = nullptr;
    lv_obj_t* _time  = nullptr;
    lv_obj_t* _date  = nullptr;
    lv_obj_t* _dot   = nullptr;
    std::array<lv_obj_t*, kTrailDots> _trail{};

    const lv_image_dsc_t* _cur_dsc  = nullptr;
    const lv_image_dsc_t* _next_dsc = nullptr;

    int _index    = 0;
    int _vig_inner = 174;   // 晕影内缘，随照片形态变
    int _layout   = 0;   // 0 寐(纯团团+分钟弧) / 1 时(底部时间) / 2 忆(好日子)
    lv_obj_t* _joy_ring = nullptr;   // 忆：好日子的刻度环
    lv_obj_t* _joy_cap  = nullptr;   // 忆：那天是什么日子
    bool _locked  = false;
    int _last_min = -1;
    bool _touch_was_down = false;

    uint32_t _t_photo = 0;
    uint32_t _t_dot   = 0;
    uint32_t _t_swap  = 0;
    uint32_t _t_woke  = 0;

    float _tilt_x = 0.0f;
    float _tilt_y = 0.0f;

    void build_vignette();
    void apply_layout();
    void tick_photo(uint32_t now);
    void tick_dot(uint32_t now);
    void tick_clock(bool force);
    void tick_rim();
    void build_joy();
    void show_joy(bool on);
    void swap_photo();
};
```


### `main/apps/app_gaze/app_gaze.cpp`

<!-- FILE: main/apps/app_gaze/app_gaze.cpp -->
```cpp
/*
 * app_gaze.cpp — 望。
 *
 * 分层刷新率是这个页面能同时"好看"和"跑得动"的全部原因：
 * 照片层 8 FPS（每帧挪 1–2px，反正要的就是慢），外圈点 30 FPS（只脏 24×24）。
 * 不要为了"更流畅"把照片层提到 30 FPS —— 424KB 一帧，总线会被打满。
 */
#include "app_gaze.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/photo_store.h>
#include <sinan/state.h>
#include <sinan/route.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;
namespace photo = sinan::photo;

namespace {

constexpr uint32_t kPhotoPeriod = 125;   // 8 FPS
constexpr uint32_t kDotPeriod   = 33;    // 30 FPS
constexpr uint32_t kSwapAfter   = 5 * 60 * 1000;
constexpr uint32_t kWakeHold    = 6000;
constexpr uint32_t kFadeMs      = 900;

// 李萨如漂移。两个周期互质，轨迹准周期，肉眼看不出重复，
// 同时保证照片像素从不静止 —— 防烧屏是白送的
constexpr float kDriftAmp  = 22.0f;
constexpr float kDriftSecX = 14 * 60.0f;
constexpr float kDriftSecY = 19 * 60.0f;

// 成年犬静息呼吸每分钟 15–30 次，取 18。你不会意识到它在呼吸，
// 但你会觉得它是活的
constexpr float kBreathBpm = 18.0f;
constexpr float kBreathPx  = 6.0f;

// 视差幅度，别调大，大了就俗了
constexpr float kTiltPhoto = 5.0f;
constexpr float kTiltText  = 2.0f;

constexpr float kPi = 3.14159265358979f;

/*
 * 晕影的第 i 圈半径。inner 由照片形态决定 ——
 * 早期版本只有一套固定半径，portrait 的照片放上去背景吃不干净，
 * 而我给你看的预览用的是分档的值，代码和预览对不上。
 */
int vig_radius(int i, int shift, int inner)
{
    const float step = static_cast<float>(VIG_OUTER - inner) / (VIG_RINGS - 1);
    return inner + static_cast<int>(i * step) + shift;
}

// 时间只在「时」版式常显，寐是敲桌临时露出，忆不显示。所以只有一个位置
constexpr int kTimeY = 118;

}  // namespace

AppGaze::AppGaze() { setAppInfo().name = "Gaze"; }

void AppGaze::onCreate()
{
    photo::init();
    mclog::tagInfo(getAppInfo().name, "on create, {} photos", photo::count());
}

void AppGaze::onOpen()
{
    _key = std::make_unique<input::KeyManager>();
    _last_min = -1;
    _t_swap = GetHAL().millis();

    LvglLockGuard lock;
    ui::root_acquire();   // 与 onClose 的 root_release 成对，漏了会全屏黑

    _stage = ui::stage(ui::precession_root());

    /* ---- 层 0：照片 ---- */
    _photo = lv_image_create(_stage);
    lv_obj_set_size(_photo, SCREEN, SCREEN);
    lv_obj_center(_photo);
    lv_image_set_inner_align(_photo, LV_IMAGE_ALIGN_TOP_LEFT);

    _photo_next = lv_image_create(_stage);
    lv_obj_set_size(_photo_next, SCREEN, SCREEN);
    lv_obj_center(_photo_next);
    lv_image_set_inner_align(_photo_next, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_style_opa(_photo_next, LV_OPA_TRANSP, 0);

    _vig_inner = photo::is_disc(_index) ? VIG_INNER_DISC : VIG_INNER_PORTRAIT;
    _cur_dsc = photo::acquire(_index);
    if (_cur_dsc) {
        lv_image_set_src(_photo, _cur_dsc);
    } else {
        // 没有照片不是错误状态，是还没放照片。给一句能照做的话
        lv_obj_add_flag(_photo, LV_OBJ_FLAG_HIDDEN);
        _hint = ui::mono_block(_stage, "drop a folder of photos\non Hardware Buddy",
                               SN_FONT_MONO_S, BRONZE_D, 300);
        lv_obj_center(_hint);
    }

    /* ---- 层 1：晕影 ---- */
    build_vignette();

    /* ---- 层 1.5：Rim 分钟弧 ----
       寐版式里不显示时间，但一圈鎏金细弧走满即一小时。
       想知道几点就敲一下桌子，不想知道就当它是个装饰 */
    _rim = ui::arc(_stage, {R_RIM, 4, BRONZE, INK, true}, 0, 1);

    /* ---- 层 2：衬底 ----
       从透明到纯黑的竖向渐变，保证数字在任何照片上都读得清 */
    _scrim = lv_obj_create(_stage);
    lv_obj_set_size(_scrim, SCREEN, 190);
    lv_obj_align(_scrim, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(_scrim, 0, 0);
    lv_obj_set_style_radius(_scrim, 0, 0);
    lv_obj_set_style_pad_all(_scrim, 0, 0);
    lv_obj_set_style_bg_color(_scrim, c(INK), 0);
    lv_obj_set_style_bg_grad_color(_scrim, c(INK), 0);
    lv_obj_set_style_bg_grad_dir(_scrim, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(_scrim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(_scrim, LV_OPA_90, 0);
    lv_obj_remove_flag(_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(_scrim, LV_OBJ_FLAG_CLICKABLE);

    /* ---- 层 3：时间 ---- */
    _time = ui::text(_stage, "--:--", SN_FONT_NUM_L, SILK);
    lv_obj_align(_time, LV_ALIGN_CENTER, 0, 118);

    _date = ui::text(_stage, "", SN_FONT_MONO_S, BRONZE);
    lv_obj_align(_date, LV_ALIGN_CENTER, 0, 168);
    lv_obj_set_style_opa(_date, LV_OPA_TRANSP, 0);

    build_joy();

    /* ---- 层 4：外圈点 ----
       边牧的本职工作是 outrun —— 绕着羊群跑一个大弧把它们收拢。
       所以这个页面没有秒针，只有团团在跑外圈 */
    for (int i = 0; i < kTrailDots; i++) {
        lv_obj_t* t = lv_obj_create(_stage);
        const int sz = 5 - i / 2;
        lv_obj_set_size(t, sz, sz);
        lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(t, c(BRONZE_D), 0);
        lv_obj_set_style_bg_opa(t, 150 - i * 24, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        _trail[i] = t;
    }
    _dot = lv_obj_create(_stage);
    lv_obj_set_size(_dot, 7, 7);
    lv_obj_set_style_radius(_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_dot, c(BRONZE), 0);
    lv_obj_set_style_border_width(_dot, 0, 0);
    lv_obj_set_style_pad_all(_dot, 0, 0);
    lv_obj_remove_flag(_dot, LV_OBJ_FLAG_SCROLLABLE);

    apply_layout();
    tick_clock(true);
    ui::set_luma(ui::Luma::Quiet);
}

/*
 * 晕影用同心弧堆出来，不用带 alpha 的位图。三个好处：
 * 不占 flash、半径可运行时改（呼吸靠这个）、
 * 而且它天然属于"一切都是弧"的设计语言。
 */
void AppGaze::build_vignette()
{
    for (int i = 0; i < kVignetteRings; i++) {
        // 从内往外越来越不透明，最外一圈是纯黑 —— AMOLED 上像素直接熄灭，
        // 于是照片没有边缘，是悬浮在黑暗里的
        const float t = static_cast<float>(i) / (kVignetteRings - 1);
        const int opa = static_cast<int>(255.0f * t * t);  // 平方曲线，线性会看出台阶
        _vig[i] = ui::arc(_stage, {vig_radius(i, 0, _vig_inner), 3, INK, INK, false}, 0, 359.9f);
        lv_obj_set_style_arc_opa(_vig[i], opa, LV_PART_INDICATOR);
    }
}

/*
 * 三个版式：
 *
 *   寐  默认。团团蜷成的正圆本身就是钟面，上面不该压任何东西。
 *       想知道几点敲一下桌子就行。
 *   时  常显时间，数字锚在下三分之一，不压他的头。
 *   忆  好日子。外圈每一颗刻度是一张照片、一个日子，弦区写那天是什么日子。
 *       **不写年龄** —— 年龄是减法，日子是加法。环只占 300°，
 *       留 60° 是还没发生的好日子，所以它永远不会满，只会越来越密。
 */
void AppGaze::apply_layout()
{
    const bool sleep = (_layout == 0);
    const bool joy   = (_layout == 2);

    lv_obj_set_style_opa(_time,  sleep || joy ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_opa(_scrim, sleep ? LV_OPA_TRANSP : LV_OPA_90, 0);
    lv_obj_set_style_opa(_rim,   sleep ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(_time, SN_FONT_NUM_L, 0);
    lv_obj_set_style_opa(_photo, LV_OPA_COVER, 0);
    lv_obj_align(_time, LV_ALIGN_CENTER, 0, kTimeY);
    show_joy(joy);
}

/*
 * 忆的刻度环：一张照片一颗。
 *
 * 刻度只铺 300°，剩下 60° 空着 —— 那是还没发生的好日子。
 * 这一条不是装饰：一条铺满整圈的进度环暗示的是终点，而这一页要说的是
 * 「又多了一个好日子」。加照片只增不减，环只会越来越密。
 */
void AppGaze::build_joy()
{
    _joy_ring = ui::ticks(_stage, R_RIM, photo::count() > 0 ? photo::count() : 1,
                          9, 3, BRONZE_D);
    lv_obj_add_flag(_joy_ring, LV_OBJ_FLAG_HIDDEN);

    _joy_cap = ui::text(_stage, "", SN_FONT_CJK_L, SILK);
    // 弦区在 center+139，caption 必须让开，否则两行字叠在一起
    lv_obj_align(_joy_cap, LV_ALIGN_CENTER, 0, 96);
    lv_obj_add_flag(_joy_cap, LV_OBJ_FLAG_HIDDEN);
}

void AppGaze::show_joy(bool on)
{
    if (!_joy_ring || !_joy_cap) return;
    if (!on) {
        lv_obj_add_flag(_joy_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_joy_cap, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(_joy_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_joy_cap, LV_OBJ_FLAG_HIDDEN);
    ui::ticks_reset_color(_joy_ring, BRONZE_D);
    ui::tick_set_color(_joy_ring, _index, SILK);
    lv_label_set_text(_joy_cap, photo::caption_of(_index));
    // 大字写日子，弦区写日期 —— 跟预览里的层次一致
    lv_label_set_text(_chord, photo::date_of(_index));
    lv_obj_set_style_text_color(_chord, c(BRONZE_D), 0);
}

// 分钟进度：一圈走满即一小时。跟着外圈那颗点一起读
void AppGaze::tick_rim()
{
    if (_layout != 0) return;
    const auto t = GetHAL().getTimeHms();
    ui::arc_set_progress(_rim, (t.minute * 60.0f + t.second) / 3600.0f);
}

void AppGaze::tick_photo(uint32_t now)
{
    if (now - _t_photo < kPhotoPeriod) return;
    _t_photo = now;

    const float sec = now / 1000.0f;

    if (_cur_dsc) {
        // 漂移：只改 offset，纯 blit，不做任何重采样
        const float dx = kDriftAmp * std::sin(2 * kPi * sec / kDriftSecX);
        const float dy = kDriftAmp * std::sin(2 * kPi * sec / kDriftSecY);
        // 视差：照片朝倾斜的反方向走，才有"照片在窗后面"的错觉
        const int ox = static_cast<int>(photo::PAN + dx - _tilt_x * kTiltPhoto);
        const int oy = static_cast<int>(photo::PAN + dy - _tilt_y * kTiltPhoto);
        lv_image_set_offset_x(_photo, -std::clamp(ox, 0, photo::PAN * 2));
        lv_image_set_offset_y(_photo, -std::clamp(oy, 0, photo::PAN * 2));
    }

    // 呼吸：只动晕影半径，脏区就是外圈那一环
    const float br = std::sin(2 * kPi * sec * kBreathBpm / 60.0f);
    const int shift = static_cast<int>(br * kBreathPx);
    for (int i = 0; i < kVignetteRings; i++) {
        const int r = vig_radius(i, shift, _vig_inner);
        lv_obj_set_size(_vig[i], r * 2 + 3, r * 2 + 3);
        lv_obj_center(_vig[i]);
    }

    // 时间层朝倾斜的同方向轻移，与照片反向，视差才成立
    const int tx = static_cast<int>(_tilt_x * kTiltText);
    lv_obj_align(_time, LV_ALIGN_CENTER, tx, kTimeY);
}

void AppGaze::tick_dot(uint32_t now)
{
    if (now - _t_dot < kDotPeriod) return;
    _t_dot = now;

    const auto t = GetHAL().getTimeHms();
    const float frac = (t.second + (now % 1000) / 1000.0f) / 60.0f;
    const float deg = frac * 360.0f;

    ui::place_polar(_dot, R_RIM, deg);
    // 拖痕落在身后，像草地上的跑道
    for (int i = 0; i < kTrailDots; i++) {
        ui::place_polar(_trail[i], R_RIM, deg - (i + 1) * 2.4f);
    }
}

void AppGaze::tick_clock(bool force)
{
    const auto t = GetHAL().getTimeHms();
    if (!force && t.minute == _last_min) return;
    _last_min = t.minute;

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
    lv_label_set_text(_time, buf);

    const auto d = GetHAL().getDateYmd();
    char db[16];
    std::snprintf(db, sizeof(db), "%02d.%02d", d.month, d.day);
    lv_label_set_text(_date, db);
}

void AppGaze::swap_photo()
{
    if (photo::count() < 2 || _next_dsc) return;  // 上一次淡入还没完就别叠

    const int next = (_index + 1) % photo::count();
    const lv_image_dsc_t* dsc = photo::acquire(next);
    if (!dsc) return;

    // 交叉淡入：新图在上层从透明推到不透明，完事再交换指针
    lv_image_set_src(_photo_next, dsc);
    lv_image_set_offset_x(_photo_next, -photo::PAN);
    lv_image_set_offset_y(_photo_next, -photo::PAN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _photo_next);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(o), v, 0);
    });
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, kFadeMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    _next_dsc = _cur_dsc;   // 旧图留到淡入结束再释放
    _cur_dsc  = dsc;
    _index    = next;
    _t_swap   = GetHAL().millis();
    // 新图可能是另一种形态，晕影内缘跟着换
    _vig_inner = photo::is_disc(_index) ? VIG_INNER_DISC : VIG_INNER_PORTRAIT;
    if (_layout == 2) show_joy(true);   // 忆：换图时刻度和文案跟着走

    // 用定时器收尾，不要阻塞等待
    lv_timer_t* tm = lv_timer_create([](lv_timer_t* timer) {
        static_cast<AppGaze*>(lv_timer_get_user_data(timer))->onSwapDone();
        lv_timer_delete(timer);
    }, kFadeMs + 50, this);
    lv_timer_set_repeat_count(tm, 1);
}

void AppGaze::onSwapDone()
{
    if (!_photo) return;
    lv_image_set_src(_photo, _cur_dsc);
    lv_obj_set_style_opa(_photo_next, LV_OPA_TRANSP, 0);
    if (_next_dsc) {
        photo::release(_next_dsc);
        _next_dsc = nullptr;
    }
}

void AppGaze::onRunning()
{
    if (_key && _key->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    auto& hal = GetHAL();
    const uint32_t now = hal.millis();

    // 有请求时直接把守推到前台，不是 close 回 launcher ——
    // 那样用户还得自己去点开守，物理批准的主路径就断了
    if (sinan::route::yield_to_ward_if_needed()) return;

    hal.updateImuData();
    const auto& imu = hal.getImuData();
    // 低通，否则视差会抖
    _tilt_x = _tilt_x * 0.9f + imu.accelX * 0.1f;
    _tilt_y = _tilt_y * 0.9f + imu.accelY * 0.1f;

    const float g = std::sqrt(imu.accelX * imu.accelX + imu.accelY * imu.accelY +
                              imu.accelZ * imu.accelZ);
    if (g > TAP_G && now - _t_woke > 1500) {
        _t_woke = now;
        ui::set_luma(ui::Luma::Normal);
        LvglLockGuard lock;
        lv_obj_set_style_opa(_date, LV_OPA_COVER, 0);
        // 寐版式下平时不显示时间，敲一下才露出来
        if (_layout == 0) {
            lv_obj_set_style_opa(_time, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_grad_opa(_scrim, LV_OPA_90, 0);
        }
    }
    if (_t_woke && now - _t_woke > kWakeHold) {
        _t_woke = 0;
        ui::set_luma(ui::Luma::Quiet);
        LvglLockGuard lock;
        lv_obj_set_style_opa(_date, LV_OPA_TRANSP, 0);
        if (_layout == 0) {
            lv_obj_set_style_opa(_time, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_grad_opa(_scrim, LV_OPA_TRANSP, 0);
        }
    }

    hal.updateButtonStates();
    if (hal.btnA.pressedFor(LONGPRESS_MS)) {
        _locked = !_locked;
        hal.vibrate(_locked ? 120 : 60, 80);
    } else if (hal.btnA.wasClicked()) {
        _layout = (_layout + 1) % 3;
        hal.vibrate(40, 60);
        LvglLockGuard lock;
        apply_layout();
    }

    const auto tp = hal.getTouchPoint();
    const bool down = tp.num > 0;
    if (down && !_touch_was_down) {
        LvglLockGuard lock;
        swap_photo();
        hal.vibrate(30, 50);
    }
    _touch_was_down = down;

    LvglLockGuard lock;
    tick_photo(now);
    tick_dot(now);
    tick_clock(false);
    tick_rim();

    if (!_locked && now - _t_swap > kSwapAfter) swap_photo();
}

void AppGaze::onClose()
{
    _key.reset();

    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _photo = _photo_next = _hint = _rim = _scrim = _time = _date = _dot = nullptr;
    _joy_ring = _joy_cap = nullptr;
    _vig.fill(nullptr);
    _trail.fill(nullptr);

    if (_cur_dsc)  { photo::release(_cur_dsc);  _cur_dsc = nullptr; }
    if (_next_dsc) { photo::release(_next_dsc); _next_dsc = nullptr; }
    ui::set_luma(ui::Luma::Normal);
}
```


### `main/apps/app_ward/app_ward.h`

<!-- FILE: main/apps/app_ward/app_ward.h -->
```cpp
/*
 * app_ward.h — 守：权限决策终端。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <sinan/state.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <memory>
#include <string>

class AppWard : public mooncake::AppAbility {
public:
    AppWard();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage  = nullptr;
    lv_obj_t* _rim    = nullptr;
    lv_obj_t* _orbit  = nullptr;
    lv_obj_t* _tool   = nullptr;
    lv_obj_t* _hint   = nullptr;
    lv_obj_t* _reason = nullptr;
    lv_obj_t* _needle = nullptr;
    lv_obj_t* _ghost  = nullptr;   // 团团点阵，常驻的幽灵层
    lv_obj_t* _clip   = nullptr;   // 裁剪容器，高度即长按进度
    lv_obj_t* _fill   = nullptr;   // 同一张图，按状态色填
    lv_obj_t* _chord  = nullptr;
    lv_obj_t* _passkey = nullptr;   // 配对页的六位码

    int _face             = 0;
    bool _grave           = false;
    std::string _shown_id;
    uint32_t _settled_at  = 0;
    uint32_t _hold_start  = 0;

    void apply_quiet();
    void apply_pending(const sinan::Snapshot& s);
    void apply_settled(bool approved);
    void handle_keys(const sinan::Snapshot& s);
    void apply_pairing(uint32_t code);
    void build_glyph();
    void set_glyph(bool visible, float fill, uint32_t hue);
};
```


### `main/apps/app_ward/app_ward.cpp`

<!-- FILE: main/apps/app_ward/app_ward.cpp -->
```cpp
/*
 * app_ward.cpp — 守。
 *
 * 三种形态，共用同一圈 Rim 环：
 *   静默  纯黑，Rim 只剩一段靛青短弧在缓慢呼吸。AMOLED 上这几乎不耗电，
 *         也不会烧屏，可以就这样在桌上摆一整天。
 *   待决  Rim 整圈点亮并按剩余时间收缩，颜色由危险等级决定。
 *   落定  弧从中线向两侧张开成整圈，石绿或朱砂，0.48 秒后回到静默。
 *
 * 所有过渡都是弧的生长与退让，没有一处滑入滑出。
 */
#include "app_ward.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/state.h>
#include <sinan/bridge_ble.h>
#include <sinan/danger.h>
#include <sinan/route.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <algorithm>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

// 一个请求给 90 秒。到时不自动批准也不自动拒绝，只是收起催促，
// 让它回到静默 —— 超时替用户做决定是不可接受的
constexpr uint32_t kPromptWindowMs = 90000;

enum class Face { Quiet, Pending, Settled, Pairing };

}  // namespace

AppWard::AppWard()
{
    setAppInfo().name = "Ward";
}

void AppWard::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppWard::onOpen()
{
    _key = std::make_unique<input::KeyManager>();
    _face = static_cast<int>(Face::Quiet);
    _shown_id.clear();
    _settled_at = 0;

    LvglLockGuard lock;
    ui::root_acquire();   // 必须在持锁状态下调，见 precession.h 的锁契约
    _stage = ui::stage(ui::precession_root());

    // Rim：唯一一件两米外看得清的东西
    _rim = ui::arc(_stage, {R_RIM, W_RIM, INDIGO, LACQUER, true}, 0, 40);

    // Orbit：待决时显示危险等级的刻度，静默时熄灭
    _orbit = ui::ticks(_stage, R_ORBIT, 36, 14, 3, LACQUER);

    // Core：工具名（大字）+ 命令内容（等宽，可折行）
    _tool = ui::text(_stage, "", SN_FONT_MONO_L, SILK);
    lv_obj_align(_tool, LV_ALIGN_CENTER, 0, -62);

    _hint = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK_D, 250);
    lv_obj_align(_hint, LV_ALIGN_CENTER, 0, 6);

    _reason = ui::text(_stage, "", SN_FONT_MONO_S, CINNABAR);
    lv_obj_align(_reason, LV_ALIGN_CENTER, 0, 82);

    // 静默态中心：一枚司南浮针的意象，只是一个小圆点加一道短线
    _needle = lv_obj_create(_stage);
    lv_obj_set_size(_needle, 8, 8);
    lv_obj_set_style_radius(_needle, 4, 0);
    lv_obj_set_style_bg_color(_needle, c(BRONZE_D), 0);
    lv_obj_set_style_border_width(_needle, 0, 0);
    lv_obj_center(_needle);

    build_glyph();
    // 配对码：DisplayOnly 配对时唯一能让人完成配对的东西
    _passkey = ui::text(_stage, "", SN_FONT_NUM_XL, SILK);
    lv_obj_center(_passkey);
    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);

    _chord = ui::chord(_stage, "");

    apply_quiet();
}

/*
 * 团团点阵字形：两层同一张 RGBA PNG。
 * 底层是暗鎏金的幽灵，始终在；上层按状态染色，装在一个高度会变的
 * 裁剪容器里 —— 长按时容器从下往上长，看起来就是团团在被填满。
 *
 * 用 recolor 而不是塞一个 68KB 的 C 数组：图是 RGB 全白 + alpha 存点的浓度，
 * 染色只动 RGB，点的疏密原样保留。而且换照片时字形跟着一起换。
 */
void AppWard::build_glyph()
{
    static constexpr int GS = 264;
    const char* path = "A:/spiflash/tuan/glyph.png";

    _ghost = lv_image_create(_stage);
    lv_image_set_src(_ghost, path);
    lv_obj_center(_ghost);
    lv_obj_set_style_image_recolor(_ghost, c(BRONZE_D), 0);
    lv_obj_set_style_image_recolor_opa(_ghost, LV_OPA_COVER, 0);
    // 40% 而不是 26%：静默态屏幕亮度只有 25%，两者一乘就几乎看不见了。
    // 这个值要在真机上按环境光复核
    lv_obj_set_style_opa(_ghost, 102, 0);          // 40%
    lv_obj_add_flag(_ghost, LV_OBJ_FLAG_HIDDEN);

    _clip = lv_obj_create(_stage);
    lv_obj_set_size(_clip, GS, GS);
    lv_obj_center(_clip);
    lv_obj_set_style_bg_opa(_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_clip, 0, 0);
    lv_obj_set_style_pad_all(_clip, 0, 0);
    lv_obj_set_style_clip_corner(_clip, false, 0);
    lv_obj_remove_flag(_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_clip, LV_OBJ_FLAG_HIDDEN);

    _fill = lv_image_create(_clip);
    lv_image_set_src(_fill, path);
    lv_image_set_inner_align(_fill, LV_IMAGE_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_image_recolor_opa(_fill, LV_OPA_COVER, 0);
    lv_obj_set_size(_fill, GS, GS);
    lv_obj_align(_fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// fill 是 0..1 的填充比例，从下往上
void AppWard::set_glyph(bool visible, float fill, uint32_t hue)
{
    static constexpr int GS = 264;
    if (!_ghost || !_clip) return;

    if (!visible) {
        lv_obj_add_flag(_ghost, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_clip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(_ghost, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_clip, LV_OBJ_FLAG_HIDDEN);

    const int h = static_cast<int>(GS * std::clamp(fill, 0.0f, 1.0f));
    lv_obj_set_height(_clip, h > 0 ? h : 1);
    lv_obj_align(_clip, LV_ALIGN_CENTER, 0, (GS - h) / 2);
    lv_obj_set_style_image_recolor(_fill, c(hue), 0);
}

void AppWard::apply_quiet()
{
    _face = static_cast<int>(Face::Quiet);
    ui::set_luma(ui::Luma::Quiet);
    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);

    ui::arc_set_color(_rim, INDIGO);
    ui::arc_animate_to(_rim, 0, 40, T_STATE);
    ui::breathe(_rim, T_BREATH * 2, 40, 170);
    ui::ticks_reset_color(_orbit, LACQUER);

    lv_label_set_text(_tool, "");
    lv_label_set_text(_hint, "");
    lv_label_set_text(_reason, "");
    // 静默态中心不是一颗光秃秃的点，是团团的幽灵。反正这一屏没别的信息
    lv_obj_add_flag(_needle, LV_OBJ_FLAG_HIDDEN);
    set_glyph(true, 0.0f, BRONZE_D);
}

/*
 * 配对页。macOS 弹窗要你输一个六位码，而这台设备平时不接串口 ——
 * 码不上屏就配不上对，守整条线是死的。所以它优先级高于一切其他形态。
 */
void AppWard::apply_pairing(uint32_t code)
{
    _face = static_cast<int>(Face::Pairing);
    ui::set_luma(ui::Luma::Alert);

    ui::stop_breathe(_rim);
    ui::arc_set_color(_rim, INDIGO);
    ui::arc_set_range(_rim, 0, 359.9f);
    ui::ticks_reset_color(_orbit, LACQUER);
    set_glyph(false, 0.0f, 0);

    lv_obj_add_flag(_needle, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_tool, "");
    lv_label_set_text(_hint, "");
    lv_label_set_text(_reason, "");

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(code % 1000000));
    lv_obj_remove_flag(_passkey, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_passkey, buf);

    lv_label_set_text(_chord, "pair on Mac");
    lv_obj_set_style_text_color(_chord, c(INDIGO), 0);
}

void AppWard::apply_pending(const Snapshot& s)
{
    _face = static_cast<int>(Face::Pending);
    _shown_id = s.ble.prompt_id;
    ui::set_luma(ui::Luma::Alert);
    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);

    const Risk risk = assess(s.ble.prompt_tool, s.ble.prompt_hint);
    _grave = (risk == Risk::Grave);
    const uint32_t hue = _grave ? CINNABAR : (risk == Risk::Elevated ? AMBER : MALACHITE);

    ui::stop_breathe(_rim);
    ui::arc_set_color(_rim, hue);
    ui::arc_set_range(_rim, 0, 359.9f);

    // Orbit 刻度按危险等级点亮的密度不同：越危险，环上越"实"
    const int lit = _grave ? 36 : (risk == Risk::Elevated ? 18 : 9);
    ui::ticks_reset_color(_orbit, LACQUER);
    for (int i = 0; i < 36; i += (36 / lit)) {
        // 底部 60° 留空给弦区那行提示，否则刻度会从文字里穿过去。
        // 环在底部开一个口，看起来是有意的，实际是被文字逼出来的
        if (i >= 15 && i <= 21) continue;
        ui::tick_set_color(_orbit, i, hue);
    }

    lv_obj_add_flag(_needle, LV_OBJ_FLAG_HIDDEN);
    // 读命令的时候屏幕全让给文字，字形退场
    set_glyph(false, 0.0f, 0);
    lv_label_set_text(_tool, s.ble.prompt_tool.c_str());
    lv_obj_set_style_text_color(_tool, c(hue), 0);
    lv_label_set_text(_hint, s.ble.prompt_hint.c_str());

    const char* why = risk_reason(s.ble.prompt_tool, s.ble.prompt_hint);
    lv_label_set_text(_reason, why);
    lv_obj_set_style_text_color(_reason, c(hue), 0);

    // 危险操作明说需要长按。UI 不该让人猜为什么按了没反应
    // 弦区在 y=372，能用的宽度只有 ~310px。文案超过 24 字符会顶出安全圆
    lv_label_set_text(_chord, _grave ? "hold A \xc2\xb7 deny B" : "A allow \xc2\xb7 B deny");
    lv_obj_set_style_text_color(_chord, c(_grave ? CINNABAR : BRONZE), 0);
}

void AppWard::apply_settled(bool approved)
{
    _face = static_cast<int>(Face::Settled);
    _settled_at = GetHAL().millis();
    _shown_id.clear();

    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);
    ui::stop_breathe(_rim);
    // 从 12 点向两侧张开。bloom 现在真的会用 from_deg
    ui::bloom(_rim, 0, approved ? MALACHITE : CINNABAR, T_STATE);
    ui::ticks_reset_color(_orbit, approved ? MALACHITE : CINNABAR);
    // 批准的终态是团团被石绿填满 —— 跟长按过程的最后一帧接上，
    // 手指松开时画面不跳。拒绝就不留他，直接朱砂弧
    set_glyph(approved, 1.0f, MALACHITE);

    lv_label_set_text(_tool, approved ? "ALLOWED" : "DENIED");
    lv_obj_set_style_text_color(_tool, c(approved ? MALACHITE : CINNABAR), 0);
    lv_label_set_text(_hint, "");
    lv_label_set_text(_reason, "");
    lv_label_set_text(_chord, "");
    _hold_start = 0;

    GetHAL().vibrate(approved ? 90 : 160, 100);
}

void AppWard::onRunning()
{
    if (_key && _key->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    const uint32_t now = GetHAL().millis();
    const auto s = State::get().snapshot();

    // 按键状态每帧都要推进。早期版本只在 handle_keys 里推，
    // 其他形态下 wasClicked 的边沿会攒着，回到待决时凭空触发一次
    GetHAL().updateButtonStates();

    LvglLockGuard lock;

    // 配对压倒一切：配不上对，后面所有形态都没有意义
    if (s.ble.passkey != 0) {
        if (_face != static_cast<int>(Face::Pairing)) apply_pairing(s.ble.passkey);
        return;
    }
    if (_face == static_cast<int>(Face::Pairing)) {
        // 配对有结果了。当初是被配对拉过来的，就回待机页去
        apply_quiet();
        sinan::route::return_home_if_auto();
        return;
    }

    // 落定态只停留一瞬，然后自己退回静默
    if (_face == static_cast<int>(Face::Settled)) {
        if (now - _settled_at > T_STATE + 700) {
            apply_quiet();
            // 当初是被请求自动拉起来的，处理完就回待机页去
            sinan::route::return_home_if_auto();
        }
        return;
    }

    const bool link_dead = !s.ble.connected || (now - s.ble.last_beat > STALE_MS);

    if (link_dead) {
        if (_face != static_cast<int>(Face::Quiet)) apply_quiet();
        // 链路断了要让人看得出来，但不能弹窗。改成弦区一行小字
        lv_label_set_text(_chord, "no link");
        lv_obj_set_style_text_color(_chord, c(INDIGO), 0);
        return;
    }

    if (s.ble.has_prompt) {
        if (_shown_id != s.ble.prompt_id) {
            apply_pending(s);
        } else {
            // Rim 按剩余时间收缩。时间本身就是弧长，不需要额外的进度条
            const uint32_t elapsed = now - s.ble.prompt_since;
            const float left = elapsed >= kPromptWindowMs
                                   ? 0.0f
                                   : 1.0f - static_cast<float>(elapsed) / kPromptWindowMs;
            ui::arc_set_progress(_rim, left);
            handle_keys(s);
        }
        return;
    }

    // prompt 消失了：可能在别处批了，也可能超时。回静默，什么都不声张
    if (_face != static_cast<int>(Face::Quiet)) apply_quiet();

    char buf[48];
    if (s.ble.total == 0) {
        std::snprintf(buf, sizeof(buf), "idle");
    } else {
        std::snprintf(buf, sizeof(buf), "%d running / %d open", s.ble.running, s.ble.total);
    }
    lv_label_set_text(_chord, buf);
    lv_obj_set_style_text_color(_chord, c(BRONZE_D), 0);
}

void AppWard::handle_keys(const Snapshot& s)
{
    auto& hal = GetHAL();

    if (hal.btnB.wasClicked()) {
        if (ble::send_permission(s.ble.prompt_id, ble::Decision::Deny)) apply_settled(false);
        return;
    }

    if (_grave) {
        // 危险操作只认长按。这 0.8 秒是唯一挡在不可逆操作前面的东西。
        // 一旦手指按下去，屏幕就不再是"读命令"而是"看确认"：
        // 文字退场，团团从下往上被石绿填满，填满即放行。
        // 这样那 0.8 秒是一件正在发生的事，而不是一段没反应的延迟
        if (hal.btnA.pressedFor(LONGPRESS_MS)) {
            if (ble::send_permission(s.ble.prompt_id, ble::Decision::Once)) apply_settled(true);
        } else if (hal.btnA.isPressed()) {
            if (_hold_start == 0) _hold_start = hal.millis();
            const float p = std::clamp(
                static_cast<float>(hal.millis() - _hold_start) / LONGPRESS_MS, 0.0f, 1.0f);
            lv_obj_set_style_opa(_tool, LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(_hint, LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(_reason, LV_OPA_TRANSP, 0);
            set_glyph(true, p, MALACHITE);
            ui::arc_set_color(_rim, MALACHITE);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1fs", (LONGPRESS_MS * (1.0f - p)) / 1000.0f);
            lv_label_set_text(_chord, buf);
        } else if (_hold_start) {
            // 中途松手：撤回，回到读命令的状态
            _hold_start = 0;
            set_glyph(false, 0.0f, 0);
            lv_obj_set_style_opa(_tool, LV_OPA_COVER, 0);
            lv_obj_set_style_opa(_hint, LV_OPA_COVER, 0);
            lv_obj_set_style_opa(_reason, LV_OPA_COVER, 0);
            ui::arc_set_color(_rim, CINNABAR);
            lv_label_set_text(_chord, "hold A · deny B");
        }
        return;
    }

    if (hal.btnA.wasClicked()) {
        if (ble::send_permission(s.ble.prompt_id, ble::Decision::Once)) apply_settled(true);
    }
}

void AppWard::onClose()
{
    _key.reset();

    LvglLockGuard lock;
    // 一个都不能漏。mooncake 会复用实例，残留指针会指向已销毁对象
    if (_stage) lv_obj_delete(_stage);
    _stage = _rim = _orbit = _tool = _hint = _reason = _needle = _chord = nullptr;
    _passkey = _ghost = _clip = _fill = nullptr;
    ui::root_release();
    ui::set_luma(ui::Luma::Normal);
}
```


### `main/apps/app_fleet/app_fleet.h`

<!-- FILE: main/apps/app_fleet/app_fleet.h -->
```cpp
/*
 * app_fleet.h — 阵：多模型 worker 的额度与状态雷达。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <array>
#include <memory>

class AppFleet : public mooncake::AppAbility {
public:
    AppFleet();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr int kMaxSeg = 6;
    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage = nullptr;
    lv_obj_t* _rim   = nullptr;
    std::array<lv_obj_t*, kMaxSeg> _seg{};
    std::array<lv_obj_t*, kMaxSeg> _seg_bg{};
    std::array<lv_obj_t*, kMaxSeg> _seg_label{};
    std::array<bool, kMaxSeg> _breathing{};   // 呼吸的边沿状态
    lv_obj_t* _focus_pct  = nullptr;
    lv_obj_t* _focus_name = nullptr;
    lv_obj_t* _chord      = nullptr;

    int _focus = 0;
    uint32_t _last_draw = 0;

    void rebuild(const struct sinan::FleetState& f);
    void redraw();
};
```


### `main/apps/app_fleet/app_fleet.cpp`

<!-- FILE: main/apps/app_fleet/app_fleet.cpp -->
```cpp
/*
 * app_fleet.cpp — 阵。
 *
 * 圆周按 worker 数均分。每段弧的长度是该 worker 的剩余额度，颜色是它的状态。
 * 设计意图：额度快见底的那一段会明显变短，扫一眼就知道哪条线要断，
 * 不用等到编排真的挂掉才发现。
 */
#include "app_fleet.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/state.h>
#include <sinan/route.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

constexpr float kGapDeg = 6.0f;  // 段间留白。没有留白就看不出是几段

uint32_t hue_of(WorkerState s)
{
    switch (s) {
        case WorkerState::Run:   return MALACHITE;
        case WorkerState::Idle:  return BRONZE;
        case WorkerState::Stall: return AMBER;
        default:                 return INDIGO;
    }
}

const char* name_of(WorkerState s)
{
    switch (s) {
        case WorkerState::Run:   return "running";
        case WorkerState::Idle:  return "idle";
        case WorkerState::Stall: return "stalled";
        default:                 return "offline";
    }
}

}  // namespace

AppFleet::AppFleet() { setAppInfo().name = "Fleet"; }

void AppFleet::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

void AppFleet::onOpen()
{
    _key = std::make_unique<input::KeyManager>();
    _focus = 0;

    LvglLockGuard lock;
    ui::root_acquire();   // 与 onClose 的 root_release 成对，漏了会全屏黑

    _stage = ui::stage(ui::precession_root());

    // Rim 只做一件事：数据新鲜度。陈旧就整圈转靛青
    _rim = ui::arc(_stage, {R_RIM, 4, MALACHITE, INK, true}, 0, 359.9f);

    for (int i = 0; i < kMaxSeg; i++) {
        _seg_bg[i]    = ui::arc(_stage, {R_ORBIT, W_ORBIT + 2, LACQUER, INK, true}, 0, 1);
        _seg[i]       = ui::arc(_stage, {R_ORBIT, W_ORBIT, INDIGO, INK, true}, 0, 1);
        _seg_label[i] = ui::text(_stage, "", SN_FONT_MONO_S, BRONZE_D);
        lv_obj_add_flag(_seg_bg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_seg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_seg_label[i], LV_OBJ_FLAG_HIDDEN);
    }

    _focus_pct = ui::numeral(_stage, "--");
    lv_obj_align(_focus_pct, LV_ALIGN_CENTER, 0, -24);

    _focus_name = ui::text(_stage, "", SN_FONT_MONO_M, BRONZE);
    lv_obj_align(_focus_name, LV_ALIGN_CENTER, 0, 52);

    _chord = ui::chord(_stage, "B cycle");
    ui::set_luma(ui::Luma::Normal);
    redraw();
}

void AppFleet::rebuild(const FleetState& f)
{
    const int n = f.count > 0 ? f.count : 1;
    const float span = 360.0f / n;

    for (int i = 0; i < kMaxSeg; i++) {
        const bool live = i < f.count;
        if (!live) {
            lv_obj_add_flag(_seg_bg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_seg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_seg_label[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(_seg_bg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_seg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_seg_label[i], LV_OBJ_FLAG_HIDDEN);

        const auto& w = f.workers[i];
        const float start = i * span + kGapDeg / 2;
        const float full  = (i + 1) * span - kGapDeg / 2;
        const float used  = start + (full - start) * w.quota;

        ui::arc_set_range(_seg_bg[i], start, full);
        ui::arc_animate_to(_seg[i], start, used, T_FAST);
        ui::arc_set_color(_seg[i], hue_of(w.state));

        lv_label_set_text(_seg_label[i], w.label.c_str());
        lv_obj_set_style_text_color(_seg_label[i],
                                    c(i == _focus ? SILK : BRONZE_D), 0);
        ui::place_polar(_seg_label[i], R_ORBIT_IN - 22, (start + full) / 2);

        /*
         * 额度低于一成时让这一段呼吸。这是唯一允许的"催促"。
         * 只在进入/离开低额度的那一下动手 —— rebuild 每秒跑一次，
         * 无脑重调会把动画从头重启，呼吸永远走不完一拍，看起来是抽搐
         */
        const bool low = (w.quota < 0.10f && w.state != WorkerState::Down);
        if (low != _breathing[i]) {
            _breathing[i] = low;
            if (low) ui::breathe(_seg[i], T_BREATH, 80, 255);
            else ui::stop_breathe(_seg[i]);
        }
    }
}

void AppFleet::redraw()
{
    const auto s = State::get().snapshot();
    const auto& f = s.fleet;

    rebuild(f);

    const bool stale = f.stale || (GetHAL().millis() - f.last_recv > STALE_MS);
    ui::arc_set_color(_rim, stale ? INDIGO : MALACHITE);

    if (f.count == 0) {
        // 空态是邀请，不是错误
        ui::numeral_set(_focus_pct, "--");
        lv_label_set_text(_focus_name, "no fleet");
        lv_label_set_text(_chord, stale ? "daemon offline" : "waiting for daemon");
        return;
    }

    if (_focus >= f.count) _focus = 0;
    const auto& w = f.workers[_focus];

    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d", static_cast<int>(w.quota * 100 + 0.5f));
    ui::numeral_set(_focus_pct, pct);
    lv_obj_set_style_text_color(_focus_pct, c(hue_of(w.state)), 0);

    lv_label_set_text(_focus_name, w.label.c_str());

    char line[64];
    std::snprintf(line, sizeof(line), "%s  %s", name_of(w.state),
                  w.task.empty() ? "-" : w.task.c_str());
    lv_label_set_text(_chord, line);
    lv_obj_set_style_text_color(_chord, c(stale ? INDIGO : BRONZE_D), 0);
}

void AppFleet::onRunning()
{
    if (_key && _key->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }
    if (sinan::route::yield_to_ward_if_needed()) return;

    auto& hal = GetHAL();
    hal.updateButtonStates();

    bool dirty = false;
    if (hal.btnB.wasClicked()) {
        const auto s = State::get().snapshot();
        if (s.fleet.count > 0) {
            _focus = (_focus + 1) % s.fleet.count;
            hal.vibrate(40, 60);
            dirty = true;
        }
    }

    // 数据每秒最多重画一次。弧动画本身有 320ms，画太勤反而看着抖
    if (dirty || hal.millis() - _last_draw > 1000) {
        _last_draw = hal.millis();
        LvglLockGuard lock;
        redraw();
    }
}

void AppFleet::onClose()
{
    _key.reset();
    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _focus_pct = _focus_name = _chord = nullptr;
    _seg.fill(nullptr);
    _seg_bg.fill(nullptr);
    _seg_label.fill(nullptr);
    _breathing.fill(false);
}
```


### `main/apps/app_almanac/app_almanac.h`

<!-- FILE: main/apps/app_almanac/app_almanac.h -->
```cpp
/*
 * app_almanac.h — 历：三个日常表盘（号码 / 黄历 / 年轮），A 键轮播。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <memory>

class AppAlmanac : public mooncake::AppAbility {
public:
    AppAlmanac();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage  = nullptr;
    lv_obj_t* _rim    = nullptr;
    lv_obj_t* _wheel  = nullptr;  // 年轮的 73 段刻度环
    lv_obj_t* _big    = nullptr;
    lv_obj_t* _sub    = nullptr;
    lv_obj_t* _chord  = nullptr;

    int _dial = 0;
    uint32_t _last_draw = 0;

    void draw_number();
    void draw_huangli();
    void draw_yearring();
    void redraw();
};
```


### `main/apps/app_almanac/app_almanac.cpp`

<!-- FILE: main/apps/app_almanac/app_almanac.cpp -->
```cpp
/*
 * app_almanac.cpp — 历。
 *
 * 三个每天都会看一眼的盘，共用同一套环。切盘不是换页面，
 * 是同一块表盘换了一种读法 —— 所以过渡只改颜色和弧长，不重建对象。
 */
#include "app_almanac.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/state.h>
#include <sinan/route.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <ctime>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

constexpr int kDialCount  = 3;
constexpr int kWheelTicks = 73;  // 365 / 5，一格五天。365 个刻度在 466 屏上糊成一片

int day_of_year()
{
    const auto d = GetHAL().getDateYmd();
    int doy = d.day;
    for (int m = 1; m < d.month; m++) doy += DateYmd::daysInMonth(d.year, m);
    return doy;
}

}  // namespace

AppAlmanac::AppAlmanac() { setAppInfo().name = "Almanac"; }

void AppAlmanac::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

void AppAlmanac::onOpen()
{
    _key = std::make_unique<input::KeyManager>();
    _dial = 0;

    LvglLockGuard lock;
    ui::root_acquire();   // 与 onClose 的 root_release 成对，漏了会全屏黑

    _stage = ui::stage(ui::precession_root());

    _rim   = ui::arc(_stage, {R_RIM, W_RIM, BRONZE, LACQUER, true}, 0, 359.9f);
    _wheel = ui::ticks(_stage, R_ORBIT, kWheelTicks, 10, 2, LACQUER);
    lv_obj_add_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    _big = ui::numeral(_stage, "");
    lv_obj_align(_big, LV_ALIGN_CENTER, 0, -18);

    _sub = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK_D, 240);
    lv_obj_align(_sub, LV_ALIGN_CENTER, 0, 66);

    _chord = ui::chord(_stage, "A next dial");
    ui::set_luma(ui::Luma::Normal);
    redraw();
}

/* 号码盘：Rim 做一道缓慢扫描的短弧，中心是当日号码。
   这个盘的全部意义就是每天早上瞟一眼，所以中心只放一件东西。*/
void AppAlmanac::draw_number()
{
    const auto s = State::get().snapshot();
    lv_obj_add_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    ui::arc_set_color(_rim, MALACHITE);
    ui::arc_set_range(_rim, 0, 46);
    ui::breathe(_rim, T_BREATH * 2, 70, 255);

    const bool have = !s.almanac.number_code.empty();
    ui::numeral_set(_big, have ? s.almanac.number_code.c_str() : "----");
    lv_obj_set_style_text_color(_big, c(have ? SILK : SILK_D), 0);

    lv_label_set_text(_sub, have ? s.almanac.number_title.c_str() : "");
    lv_label_set_text(_chord, have ? "number" : "no number yet");
}

/* 黄历盘：HRV 相对基线做成一圈呼吸。高于基线是缓慢的石绿扩张，
   低于是急促的朱砂收缩 —— 呼吸的节奏本身就是读数，不需要写数字。*/
void AppAlmanac::draw_huangli()
{
    const auto s = State::get().snapshot();
    lv_obj_add_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    const std::string& t = s.almanac.huangli_trend;
    uint32_t hue = BRONZE;
    uint32_t period = T_BREATH * 2;
    if (t == "up")        { hue = MALACHITE; period = T_BREATH * 3; }
    // 琥珀而不是朱砂：状态偏低值得注意，但它不是危险
    else if (t == "down") { hue = AMBER; period = T_BREATH; }

    ui::arc_set_color(_rim, hue);
    ui::arc_set_range(_rim, 0, 359.9f);
    ui::breathe(_rim, period, 45, 220);

    char yi[64];
    std::snprintf(yi, sizeof(yi), "%s", s.almanac.huangli_yi.empty()
                                            ? "--" : s.almanac.huangli_yi.c_str());
    lv_label_set_text(_big, yi);
    lv_obj_set_style_text_font(_big, SN_FONT_CJK_L, 0);
    lv_obj_set_style_text_color(_big, c(SILK), 0);

    char ji[64];
    std::snprintf(ji, sizeof(ji), "%s", s.almanac.huangli_ji.empty()
                                            ? "" : s.almanac.huangli_ji.c_str());
    lv_label_set_text(_sub, ji);
    lv_obj_set_style_text_font(_sub, SN_FONT_CJK_M, 0);
    lv_label_set_text(_chord, "almanac");
}

/* 年轮盘：圆周即一年。今天所在的那一格点亮，
   已经过去的日子是暗鎏金，未来是漆色。一整年的进度是一个可以摸的实物。*/
void AppAlmanac::draw_yearring()
{
    const auto s = State::get().snapshot();
    lv_obj_clear_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    ui::stop_breathe(_rim);
    ui::arc_set_color(_rim, BRONZE_D);

    const int doy = s.almanac.ring_doy > 0 ? s.almanac.ring_doy : day_of_year();
    const int here = (doy * kWheelTicks) / 366;

    ui::ticks_reset_color(_wheel, LACQUER);
    for (int i = 0; i < here; i++) ui::tick_set_color(_wheel, i, BRONZE_D);
    ui::tick_set_color(_wheel, here, SILK);

    ui::arc_set_progress(_rim, static_cast<float>(doy) / 366.0f);

    char d[8];
    std::snprintf(d, sizeof(d), "%d", doy);
    ui::numeral_set(_big, d);
    lv_obj_set_style_text_font(_big, SN_FONT_NUM_XL, 0);
    lv_obj_set_style_text_color(_big, c(SILK), 0);

    lv_label_set_text(_sub, s.almanac.ring_tag.c_str());
    lv_obj_set_style_text_font(_sub, SN_FONT_CJK_M, 0);

    // 只说今天和年进度。转动翻任意一天还没做，界面上就别吹
    char c1[32];
    std::snprintf(c1, sizeof(c1), "day %d of %d", doy, 366);
    lv_label_set_text(_chord, c1);
}

void AppAlmanac::redraw()
{
    // 每次切盘先归位字体，否则上一个盘的中文字体会漏到下一个盘
    lv_obj_set_style_text_font(_big, SN_FONT_NUM_XL, 0);
    lv_obj_set_style_text_font(_sub, SN_FONT_MONO_S, 0);
    lv_obj_set_style_text_color(_chord, c(BRONZE_D), 0);

    switch (_dial) {
        case 0: draw_number(); break;
        case 1: draw_huangli(); break;
        default: draw_yearring(); break;
    }
}

void AppAlmanac::onRunning()
{
    if (_key && _key->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (sinan::route::yield_to_ward_if_needed()) return;

    auto& hal = GetHAL();
    hal.updateButtonStates();

    bool dirty = false;
    if (hal.btnA.wasClicked()) {
        _dial = (_dial + 1) % kDialCount;
        hal.vibrate(40, 70);
        dirty = true;
    }

    if (dirty || hal.millis() - _last_draw > 5000) {
        _last_draw = hal.millis();
        LvglLockGuard lock;
        redraw();
    }
}

void AppAlmanac::onClose()
{
    _key.reset();
    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _wheel = _big = _sub = _chord = nullptr;
}
```


### `main/apps/app_echo/app_echo.h`

<!-- FILE: main/apps/app_echo/app_echo.h -->
```cpp
/*
 * app_echo.h — 问：按住说话，Mac 转写并执行，结果语音回来。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <array>
#include <memory>

class AppEcho : public mooncake::AppAbility {
public:
    AppEcho();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    // HAL 的频谱正好 20 段，沿圆周每段 18°，不多不少刚好一圈
    static constexpr int kBands = 20;

    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage = nullptr;
    lv_obj_t* _rim   = nullptr;
    std::array<lv_obj_t*, kBands> _bars{};
    lv_obj_t* _glyph = nullptr;
    lv_obj_t* _text  = nullptr;
    lv_obj_t* _chord = nullptr;

    bool _recording      = false;
    uint32_t _rec_start  = 0;
    uint32_t _wait_start = 0;   // 松手之后开始等结果的时刻
    int _face            = 0;   // 当前画的是哪一态，避免每帧重建
    float _spin          = 0.0f;

    void set_face(int f, const char* line, uint32_t hue);
    void draw_levels();
};
```


### `main/apps/app_echo/app_echo.cpp`

<!-- FILE: main/apps/app_echo/app_echo.cpp -->
```cpp
/*
 * app_echo.cpp — 问。
 *
 * 电平环用的是 HAL 白送的 20 段频谱，沿圆周每段 18°。
 * 参考方案里麦克风完全没接，这块板一半的表现力就浪费在那儿。
 */
#include "app_echo.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/state.h>
#include <sinan/route.h>
#include <sinan/voice.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {
constexpr uint32_t kMaxRecMs  = 12000;   // 说太久转写会慢到不像话
constexpr uint32_t kWaitMaxMs = 120000;  // 等结果的上限。CLI 跑长任务是常事
constexpr int kBarMin = 6;
constexpr int kBarMax = 46;

// 画面形态。跟 VoicePhase 不是一一对应 —— UI 只关心"看起来该是哪样"
enum Face { FaceIdle = 0, FaceRec, FaceWait, FaceSpeak, FaceError };
}  // namespace

AppEcho::AppEcho() { setAppInfo().name = "Echo"; }

void AppEcho::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

void AppEcho::onOpen()
{
    _key = std::make_unique<input::KeyManager>();
    _recording = false;

    LvglLockGuard lock;
    ui::root_acquire();   // 与 onClose 的 root_release 成对，漏了会全屏黑

    _stage = ui::stage(ui::precession_root());
    _rim   = ui::arc(_stage, {R_RIM, 4, INDIGO, INK, true}, 0, 359.9f);

    for (int i = 0; i < kBands; i++) {
        lv_obj_t* b = lv_obj_create(_stage);
        lv_obj_set_size(b, 5, kBarMin);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_set_style_bg_color(b, c(BRONZE_D), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        const float deg = 360.0f * i / kBands;
        lv_obj_set_style_transform_rotation(b, static_cast<int32_t>(deg * 10), 0);
        lv_obj_set_style_transform_pivot_x(b, 2, 0);
        lv_obj_set_style_transform_pivot_y(b, kBarMin / 2, 0);
        ui::place_polar(b, R_ORBIT, deg);
        _bars[i] = b;
    }

    // 静默时中心是一个小圆环，像一只闭着的耳朵
    _glyph = ui::arc(_stage, {58, 3, BRONZE_D, INK, true}, 0, 359.9f);
    _text  = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK, 250);
    lv_obj_align(_text, LV_ALIGN_CENTER, 0, 0);

    _chord = ui::chord(_stage, "hold A to speak");
    _face = -1;
    set_face(FaceIdle, "hold A to speak", BRONZE_D);
}

/*
 * 只在形态真的变了的时候重画。弦区那行字每帧都可能不同（倒计时），
 * 所以 line 单独更新，不触发整套重建
 */
void AppEcho::set_face(int f, const char* line, uint32_t hue)
{
    if (line) {
        lv_label_set_text(_chord, line);
        lv_obj_set_style_text_color(_chord, c(hue), 0);
    }
    if (f == _face) return;
    _face = f;

    switch (f) {
        case FaceIdle:
            ui::set_luma(ui::Luma::Quiet);
            ui::arc_set_color(_rim, INDIGO);
            ui::arc_set_range(_rim, 0, 359.9f);
            ui::breathe(_rim, T_BREATH * 2, 30, 120);
            lv_obj_clear_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_text, "");
            break;
        case FaceRec:
            ui::set_luma(ui::Luma::Alert);
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, MALACHITE);
            lv_obj_add_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_text, "");
            GetHAL().vibrate(50, 80);
            break;
        case FaceWait:
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, AMBER);
            lv_obj_clear_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            // 松手的这一下要有反馈 —— 用户需要知道"已经发出去了"
            GetHAL().vibrate(40, 70);
            break;
        case FaceSpeak:
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, SILK);
            ui::arc_set_range(_rim, 0, 359.9f);
            break;
        case FaceError:
            // 靛青而不是朱砂：没链路、没听清都不是危险，是"什么都没发生"。
            // 朱砂只留给守那一页的不可逆操作 —— 用滥了它就吓不住人了
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, INDIGO);
            ui::arc_set_range(_rim, 0, 359.9f);
            lv_obj_clear_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_text, "");
            GetHAL().vibrate(120, 80);
            break;
        default:
            break;
    }
}

void AppEcho::draw_levels()
{
    auto& hal = GetHAL();
    hal.updateAudioSpectrum();
    const auto& f = hal.getAudioSpectrum();

    for (int i = 0; i < kBands; i++) {
        const float v = std::clamp(f.bands[i], 0.0f, 1.0f);
        const int h = kBarMin + static_cast<int>((kBarMax - kBarMin) * v);
        lv_obj_set_height(_bars[i], h);
        lv_obj_set_style_transform_pivot_y(_bars[i], h / 2, 0);
        // 从暗鎏金推向石绿，越响越亮。颜色只有一条轴，不搞彩虹
        lv_obj_set_style_bg_color(_bars[i], c(v > 0.55f ? MALACHITE : BRONZE), 0);
        ui::place_polar(_bars[i], R_ORBIT, 360.0f * i / kBands);
    }
}

void AppEcho::onRunning()
{
    if (_key && _key->update() == input::KeyEvent::GoHome) {
        if (_recording) voice::abort();
        close();
        return;
    }
    if (!_recording && sinan::route::yield_to_ward_if_needed()) return;

    auto& hal = GetHAL();
    hal.updateButtonStates();
    const uint32_t now = hal.millis();
    const auto s = State::get().snapshot();

    LvglLockGuard lock;

    /* ---------- 录音中 ---------- */
    if (!_recording && hal.btnA.isPressed() && !voice::busy()) {
        _recording = true;
        _rec_start = now;
        voice::begin();
        set_face(FaceRec, "listening", MALACHITE);
    }

    if (_recording) {
        // begin() 可能因为没链路直接判错，这时不该继续画录音态
        if (s.voice.phase == VoicePhase::Error) {
            _recording = false;
            set_face(FaceError, s.voice.note.c_str(), INDIGO);
            _wait_start = now;
            return;
        }
        draw_levels();
        const uint32_t used = now - _rec_start;
        ui::arc_set_progress(_rim, 1.0f - std::min(1.0f, static_cast<float>(used) / kMaxRecMs));

        // 松开即发。没有第二步确认 —— 说完话还要再按一下，这功能就没人用了
        if (!hal.btnA.isPressed() || used > kMaxRecMs) {
            _recording  = false;
            _wait_start = now;
            voice::end();
            set_face(FaceWait, "sent", AMBER);
        }
        return;
    }

    /* ---------- 等结果 ---------- */
    if (_face == FaceWait || _face == FaceSpeak) {
        // B 键随时复位，不必等超时
        if (hal.btnB.wasClicked()) {
            set_face(FaceIdle, "hold A to speak", BRONZE_D);
            ui::set_luma(ui::Luma::Normal);
            return;
        }
        _spin += 3.0f;
        if (_spin >= 360.0f) _spin -= 360.0f;

        switch (s.voice.phase) {
            case VoicePhase::Sending:
                ui::arc_set_range(_rim, _spin, _spin + 50.0f);
                set_face(FaceWait, "sent", AMBER);
                break;
            case VoicePhase::Transcribing:
                ui::arc_set_range(_rim, _spin, _spin + 50.0f);
                set_face(FaceWait, "transcribing", AMBER);
                break;
            case VoicePhase::Thinking:
                ui::arc_set_range(_rim, _spin, _spin + 50.0f);
                if (!s.voice.heard.empty()) lv_label_set_text(_text, s.voice.heard.c_str());
                set_face(FaceWait, "running", AMBER);
                break;
            case VoicePhase::Speaking:
                lv_label_set_text(_text, s.voice.reply.c_str());
                set_face(FaceSpeak, "", SILK);
                break;
            case VoicePhase::Error:
                set_face(FaceError, s.voice.note.c_str(), INDIGO);
                _wait_start = now;
                break;
            case VoicePhase::Idle:
                if (!s.voice.reply.empty()) {
                    lv_label_set_text(_text, s.voice.reply.c_str());
                    set_face(FaceSpeak, "hold A to speak", BRONZE_D);
                    ui::set_luma(ui::Luma::Normal);
                    // 已经播完了，退出等待计时。不清零的话 120 秒后
                    // 一次成功的对话会被超时判定改写成 timed out
                    _wait_start = 0;
                }
                break;
            default:
                break;
        }

        if (_wait_start && now - _wait_start > kWaitMaxMs) {
            set_face(FaceError, "timed out", INDIGO);
            _wait_start = now;
        }
        return;
    }

    /* ---------- 错误：停留几秒自己退回 ---------- */
    if (_face == FaceError) {
        if (now - _wait_start > 4000 || hal.btnB.wasClicked()) {
            set_face(FaceIdle, "hold A to speak", BRONZE_D);
            ui::set_luma(ui::Luma::Normal);
        }
        return;
    }
}

void AppEcho::onClose()
{
    if (_recording) voice::abort();
    _recording = false;
    _key.reset();

    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _glyph = _text = _chord = nullptr;
    _bars.fill(nullptr);
    ui::set_luma(ui::Luma::Normal);
}
```


### `main/assets/fonts/sinan_cjk_subset.txt`

<!-- FILE: main/assets/fonts/sinan_cjk_subset.txt -->
```text
守阵问历司南
宜忌今日号码黄历年轮
运行空闲卡住离线额度任务
批准拒绝危险确认长按
连接断开陈旧同步
深度工作会议休息拉伸喝水散步阅读
早中晚上午下午夜
第天周月年
盟史课堂研究整理
零一二三四五六七八九十百千万
```


### `daemon/sinand.py`

<!-- FILE: daemon/sinand.py -->
```python
#!/usr/bin/env python3
"""
sinand — 司南 Mac 端服务。

只用标准库。装 venv、装依赖、依赖版本冲突这些事一件都不会发生，
放进 launchd 就能一直跑。

职责：
  1. 采集各家 CLI 的额度与运行状态，归一化后推给设备（Fleet）
  2. 每天推一次号码 / 黄历 / 年轮（Almanac）
  3. 收设备上传的音频，转写，交给白名单动作，把结果 TTS 回去（Echo）

它不碰 BLE。Ward 那条线是设备直连 Claude 桌面端的，跟这里无关。
"""

from __future__ import annotations

import base64
import hashlib
import ipaddress
import json
import logging
import os
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
try:
    import tomllib                      # 3.11+
except ModuleNotFoundError:             # 3.10 及以下
    import tomli as tomllib             # type: ignore
import wave
from datetime import date, datetime
from pathlib import Path

LOG = logging.getLogger("sinand")
HERE = Path(__file__).resolve().parent
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  # RFC 6455 magic

# ---------------------------------------------------------------- 工具


def is_private(addr: str) -> bool:
    """RFC1918 + 回环。用标准库判，别自己切字符串。"""
    try:
        ip = ipaddress.ip_address(addr)
    except ValueError:
        return False
    return ip.is_private or ip.is_loopback or ip.is_link_local


# ---------------------------------------------------------------- 配置


def load_config() -> dict:
    path = HERE / "config.toml"
    if not path.exists():
        shutil.copy(HERE / "config.example.toml", path)
        LOG.info("created %s from example", path)
    with path.open("rb") as f:
        return tomllib.load(f)


# ---------------------------------------------------------------- WebSocket


class WSError(Exception):
    pass


class WSConn:
    """一个极简的 RFC 6455 服务端连接。够用就好，不追求完整。"""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.lock = threading.Lock()
        self.alive = True

    def handshake(self) -> None:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WSError("closed during handshake")
            data += chunk
            if len(data) > 16384:
                raise WSError("header too large")

        headers = {}
        for line in data.split(b"\r\n")[1:]:
            if b":" in line:
                k, v = line.split(b":", 1)
                headers[k.strip().lower()] = v.strip()

        key = headers.get(b"sec-websocket-key")
        if not key:
            raise WSError("no ws key")
        accept = base64.b64encode(hashlib.sha1(key + GUID.encode()).digest()).decode()
        self.sock.sendall(
            b"HTTP/1.1 101 Switching Protocols\r\n"
            b"Upgrade: websocket\r\n"
            b"Connection: Upgrade\r\n"
            b"Sec-WebSocket-Accept: " + accept.encode() + b"\r\n\r\n"
        )

    def _recv_exact(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise WSError("closed")
            buf += chunk
        return buf

    def recv(self) -> str | None:
        """读一帧文本。控制帧内部消化掉，返回 None 表示对端要走了。"""
        while True:
            head = self._recv_exact(2)
            fin = head[0] & 0x80
            opcode = head[0] & 0x0F
            masked = head[1] & 0x80
            length = head[1] & 0x7F

            if length == 126:
                length = struct.unpack(">H", self._recv_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._recv_exact(8))[0]
            if length > 4 * 1024 * 1024:
                raise WSError("frame too large")

            mask = self._recv_exact(4) if masked else b""
            payload = self._recv_exact(length)
            if masked:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

            if opcode == 0x8:
                return None
            if opcode == 0x9:
                self._send_frame(payload, 0xA)
                continue
            if opcode == 0xA:
                continue
            if opcode == 0x1 and fin:
                return payload.decode("utf-8", "replace")
            # 分片文本：设备端不会发，忽略即可
            if opcode == 0x1:
                acc = payload
                while True:
                    h2 = self._recv_exact(2)
                    f2 = h2[0] & 0x80
                    l2 = h2[1] & 0x7F
                    m2 = h2[1] & 0x80
                    if l2 == 126:
                        l2 = struct.unpack(">H", self._recv_exact(2))[0]
                    elif l2 == 127:
                        l2 = struct.unpack(">Q", self._recv_exact(8))[0]
                    mk = self._recv_exact(4) if m2 else b""
                    p2 = self._recv_exact(l2)
                    if m2:
                        p2 = bytes(b ^ mk[i % 4] for i, b in enumerate(p2))
                    acc += p2
                    if f2:
                        break
                return acc.decode("utf-8", "replace")

    def _send_frame(self, payload: bytes, opcode: int = 0x1) -> None:
        n = len(payload)
        if n < 126:
            head = struct.pack(">BB", 0x80 | opcode, n)
        elif n < 65536:
            head = struct.pack(">BBH", 0x80 | opcode, 126, n)
        else:
            head = struct.pack(">BBQ", 0x80 | opcode, 127, n)
        with self.lock:
            self.sock.sendall(head + payload)

    def send_json(self, obj: dict) -> None:
        try:
            self._send_frame(json.dumps(obj, ensure_ascii=False).encode())
        except OSError:
            self.alive = False

    def close(self) -> None:
        self.alive = False
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------- 数据源


class Fleet:
    """把各家 CLI 的额度口径归一化成 0.0–1.0。

    设备端不做业务判断 —— 它只负责把弧画短，判断在这里做完。
    """

    def __init__(self, cfg: dict):
        self.cfg = cfg

    def _probe(self, w: dict) -> dict:
        state, quota, task = "down", 0.0, ""
        probe = w.get("probe")
        if probe:
            try:
                out = subprocess.run(
                    ["/bin/bash", "--norc", "--noprofile", "-c", probe],
                    capture_output=True, text=True, timeout=w.get("timeout", 8),
                    env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
                ).stdout.strip()
                # 约定：探针输出一行 JSON，字段 state / quota / task
                got = json.loads(out) if out.startswith("{") else {}
                state = got.get("state", "idle")
                quota = float(got.get("quota", 1.0))
                task = got.get("task", "")
            except Exception as e:  # 探针挂了就是 down，不要让它拖垮整个推送
                LOG.debug("probe failed for %s: %s", w.get("id"), e)
        return {
            "id": w.get("id", "?"),
            "label": w.get("label", w.get("id", "?"))[:8],
            "state": state,
            "quota": max(0.0, min(1.0, quota)),
            "task": task[:48],
        }

    def snapshot(self) -> dict:
        workers = [self._probe(w) for w in self.cfg.get("worker", [])]
        return {"t": "fleet", "ts": int(time.time()), "workers": workers}


class Almanac:
    def __init__(self, cfg: dict):
        self.cfg = cfg

    def snapshot(self) -> dict:
        doy = date.today().timetuple().tm_yday
        out = {
            "t": "almanac",
            "number": {"code": "", "title": ""},
            "huangli": {"trend": "flat", "yi": "", "ji": ""},
            "ring": [{"doy": doy, "tag": ""}],
        }
        src = self.cfg.get("almanac", {}).get("source")
        if not src:
            return out
        try:
            raw = subprocess.run(
                ["/bin/bash", "--norc", "--noprofile", "-c", src],
                capture_output=True, text=True, timeout=30,
                env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
            ).stdout.strip()
            got = json.loads(raw)
            for k in ("number", "huangli", "ring"):
                if k in got:
                    out[k] = got[k]
        except Exception as e:
            LOG.warning("almanac source failed: %s", e)
        return out


# ---------------------------------------------------------------- 语音


class VoiceSession:
    """一次录音的生命周期。攒够整段再转写，比流式简单得多，
    而且十来秒的语音也没必要流式。"""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.pcm = bytearray()
        self.rate = 16000

    def begin(self, rate: int) -> None:
        self.pcm = bytearray()
        self.rate = rate or 16000

    def chunk(self, b64: str) -> None:
        try:
            self.pcm += base64.b64decode(b64)
        except Exception:
            pass

    def _write_wav(self) -> Path:
        path = Path("/tmp/sinan_asr.wav")
        with wave.open(str(path), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(self.rate)
            w.writeframes(bytes(self.pcm))
        return path

    def transcribe(self) -> str:
        if len(self.pcm) < 4000:
            return ""
        wav = self._write_wav()
        cmd = self.cfg["voice"]["asr_cmd"].replace("{wav}", str(wav))
        try:
            return subprocess.run(
                ["/bin/bash", "--norc", "--noprofile", "-c", cmd],
                capture_output=True, text=True, timeout=90,
                env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
            ).stdout.strip()
        except Exception as e:
            LOG.warning("asr failed: %s", e)
            return ""

    def answer(self, text: str) -> str:
        cmd = self.cfg["voice"]["agent_cmd"].replace("{text}", json.dumps(text))
        try:
            return subprocess.run(
                ["/bin/bash", "--norc", "--noprofile", "-c", cmd],
                capture_output=True, text=True,
                timeout=self.cfg["voice"].get("agent_timeout", 180),
                env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
            ).stdout.strip()[:400]
        except Exception as e:
            LOG.warning("agent failed: %s", e)
            return "agent timed out"

    def synth(self, text: str) -> str:
        """macOS 自带 say 就够用，不用装 TTS。转 16k 单声道 s16le 回传。

        走 WAV 而不是 CAF：CAF 的头不是定长，早期版本硬跳 4096 字节，
        运气不好就把音频前半秒切掉、或者把头当成采样播出一声爆音。
        """
        if not text:
            return ""
        aiff = Path("/tmp/sinan_tts.aiff")
        wav = Path("/tmp/sinan_tts.wav")
        voice = self.cfg["voice"].get("tts_voice", "Tingting")
        try:
            subprocess.run(["say", "-v", voice, "-o", str(aiff), text],
                           check=True, timeout=60)
            subprocess.run(
                ["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1",
                 str(aiff), str(wav)], check=True, timeout=60)
            with wave.open(str(wav), "rb") as w:
                if w.getnchannels() != 1 or w.getsampwidth() != 2:
                    LOG.warning("unexpected tts format, skipping audio")
                    return ""
                pcm = w.readframes(w.getnframes())
            return base64.b64encode(pcm).decode()
        except Exception as e:
            LOG.warning("tts failed: %s", e)
            return ""


# ---------------------------------------------------------------- 主服务


class Server:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.fleet = Fleet(cfg)
        self.almanac = Almanac(cfg)
        self.clients: list[WSConn] = []
        self.clients_lock = threading.Lock()

    def broadcast(self, obj: dict) -> None:
        with self.clients_lock:
            targets = list(self.clients)
        for c in targets:
            if c.alive:
                c.send_json(obj)

    def pusher(self) -> None:
        """周期推送。Fleet 勤一点，Almanac 一天几次就够。"""
        last_almanac = 0.0
        while True:
            try:
                self.broadcast(self.fleet.snapshot())
                if time.time() - last_almanac > self.cfg["daemon"].get("almanac_interval", 3600):
                    last_almanac = time.time()
                    self.broadcast(self.almanac.snapshot())
            except Exception as e:
                LOG.warning("pusher: %s", e)
            time.sleep(self.cfg["daemon"].get("fleet_interval", 20))

    def run_action(self, action_id: str) -> str:
        for a in self.cfg.get("action", []):
            if a.get("id") == action_id:
                try:
                    return subprocess.run(
                        ["/bin/bash", "--norc", "--noprofile", "-c", a["cmd"]],
                        capture_output=True, text=True, timeout=a.get("timeout", 120),
                        env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
                    ).stdout.strip()[:400]
                except Exception as e:
                    return f"failed: {e}"
        return f"unknown action: {action_id}"

    def handle(self, conn: WSConn) -> None:
        session = VoiceSession(self.cfg)
        with self.clients_lock:
            self.clients.append(conn)
        LOG.info("device connected (%d total)", len(self.clients))

        # 一连上先给一份现状，别让设备干等一个推送周期
        conn.send_json(self.fleet.snapshot())
        conn.send_json(self.almanac.snapshot())

        try:
            while conn.alive:
                line = conn.recv()
                if line is None:
                    break
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue

                t = msg.get("t")
                if t == "hello":
                    LOG.info("hello from %s v%s", msg.get("dev"), msg.get("ver"))
                elif t == "asr_begin":
                    session.begin(msg.get("rate", 16000))
                elif t == "asr_chunk":
                    session.chunk(msg.get("pcm", ""))
                elif t == "asr_end":
                    threading.Thread(target=self._finish_voice,
                                     args=(conn, session), daemon=True).start()
                    session = VoiceSession(self.cfg)
                elif t == "act":
                    out = self.run_action(msg.get("id", ""))
                    conn.send_json({"t": "say", "text": out, "pcm": ""})
                elif t == "pong":
                    pass
        except (WSError, OSError) as e:
            LOG.debug("conn ended: %s", e)
        finally:
            conn.close()
            with self.clients_lock:
                if conn in self.clients:
                    self.clients.remove(conn)
            LOG.info("device disconnected")

    def _finish_voice(self, conn: WSConn, session: VoiceSession) -> None:
        """松手即发：设备一发 asr_end 就走完整条链，不等第二次确认。

        每一步都回一帧 voice_status，设备弦区才能显示 transcribing / running ——
        否则用户面对的是一个转圈的环，不知道它卡在哪一步、还要不要等。
        """
        auto = self.cfg.get("voice", {}).get("auto_send", True)

        conn.send_json({"t": "voice_status", "phase": "transcribing"})
        heard = session.transcribe()
        conn.send_json({"t": "asr_result", "text": heard})
        if not heard:
            # 设备端收到空 asr_result 会自己判错，这里不再多发一帧
            return

        LOG.info("heard: %s", heard)
        if not auto:
            # 只有显式关掉 auto_send 才停在这一步等确认
            conn.send_json({"t": "voice_status", "phase": "await_confirm"})
            return

        conn.send_json({"t": "voice_status", "phase": "running"})
        try:
            reply = session.answer(heard)
        except Exception as e:
            LOG.warning("agent failed: %s", e)
            conn.send_json({"t": "voice_status", "phase": "error", "note": "agent failed"})
            return
        conn.send_json({"t": "say", "text": reply, "pcm": session.synth(reply)})

    def serve(self) -> None:
        host = self.cfg["daemon"].get("bind", "0.0.0.0")
        port = self.cfg["daemon"].get("port", 8790)

        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(4)
        LOG.info("listening on %s:%d", host, port)

        threading.Thread(target=self.pusher, daemon=True).start()

        while True:
            sock, addr = srv.accept()
            # 只接内网。这台机器上跑着你的 CLI，不该对公网开口。
            # 注意 172 段：私网只有 172.16-172.31，172.32 是公网地址，
            # 用 startswith("172.") 会把公网一起放进来
            if not is_private(addr[0]):
                LOG.warning("rejected %s", addr[0])
                sock.close()
                continue
            conn = WSConn(sock)
            try:
                conn.handshake()
            except (WSError, OSError):
                conn.close()
                continue
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)-5s %(message)s",
        datefmt="%H:%M:%S",
    )
    cfg = load_config()
    Server(cfg).serve()
    return 0


if __name__ == "__main__":
    sys.exit(main())
```


### `daemon/config.example.toml`

<!-- FILE: daemon/config.example.toml -->
```toml
# sinand 配置。首次运行 sinand.py 会自动复制这份为 config.toml。

[daemon]
bind = "0.0.0.0"
port = 8790
# 命令在干净环境执行，不受你 shell 配置污染。按需补 PATH
path = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
fleet_interval   = 20     # Fleet 推送间隔（秒）
almanac_interval = 3600   # Almanac 推送间隔（秒）

# ---------------------------------------------------------------- worker
#
# 每个 worker 一个探针。约定：探针在 stdout 输出一行 JSON：
#   {"state":"run|idle|stall","quota":0.62,"task":"当前在干嘛"}
# 探针挂了不会拖垮推送，那一段会显示成 offline。
#
# quota 的口径由你自己定义 —— 设备端只管把弧画短，不做业务判断。

[[worker]]
id    = "claude"
label = "CC"
probe = "~/.sinan/probe_claude.sh"

[[worker]]
id    = "codex"
label = "CODEX"
probe = "~/.sinan/probe_codex.sh"

[[worker]]
id    = "minimax"
label = "MM"
probe = "~/.sinan/probe_minimax.sh"

[[worker]]
id    = "grok"
label = "GROK"
probe = "~/.sinan/probe_grok.sh"

# ---------------------------------------------------------------- almanac
#
# 输出一行 JSON：
#   {"number":{"code":"0731","title":"..."},
#    "huangli":{"trend":"up|flat|down","yi":"深度工作","ji":"连续会议"},
#    "ring":[{"doy":218,"tag":"盟史 #412"}]}

[almanac]
source = "~/.sinan/almanac.sh"

# ---------------------------------------------------------------- voice
#
# 全本地链路，不接任何云端语音服务。
# {wav} 会被替换成录音文件路径，{text} 会被替换成转写结果（已 JSON 转义）。

[voice]
# 松手即发。设备一松开 A 键就转写并直接执行，不再要求第二次确认。
# 改成 false 会退回"转写完停下等确认"，交互多一步，一般不要开。
auto_send = true
asr_cmd   = "whisper-cli -m ~/models/ggml-large-v3-turbo.bin -f {wav} -l zh --no-timestamps -np 2>/dev/null | tail -1"
agent_cmd = "claude -p {text} 2>&1 | tail -20"
agent_timeout = 180
tts_voice = "Tingting"

# ---------------------------------------------------------------- action
#
# 设备上按键直接触发的白名单命令。只有这里列出的能执行。

[[action]]
id      = "daily_number"
label   = "NUMBER"
cmd     = "~/.sinan/almanac.sh | python3 -c 'import sys,json;print(json.load(sys.stdin)[\"number\"][\"title\"])'"
timeout = 30

[[action]]
id      = "brief"
label   = "BRIEF"
cmd     = "claude -p '用三句话说说过去 24 小时值得注意的事' 2>&1 | tail -10"
timeout = 180
```


### `daemon/install.sh`

<!-- FILE: daemon/install.sh -->
```bash
#!/usr/bin/env bash
# 把 sinand 装成 launchd 常驻服务。开机自启，崩了自动拉起。
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLIST="$HOME/Library/LaunchAgents/com.sinan.daemon.plist"
PY="$(command -v python3)"

if [[ "$($PY -c 'import sys;print(sys.version_info>=(3,11))')" != "True" ]]; then
  echo "need python 3.11+ (tomllib)"; exit 1
fi

mkdir -p "$HOME/.sinan" "$HOME/Library/LaunchAgents"

cat > "$PLIST" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>com.sinan.daemon</string>
  <key>ProgramArguments</key><array>
    <string>$PY</string><string>$HERE/sinand.py</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/tmp/sinand.log</string>
  <key>StandardErrorPath</key><string>/tmp/sinand.log</string>
</dict></plist>
PLIST_EOF

launchctl unload "$PLIST" 2>/dev/null || true
launchctl load "$PLIST"
echo "loaded. log: /tmp/sinand.log"
echo "本机 IP（填进固件 config.h 的 SN_WS_URI）: $(ipconfig getifaddr en0 2>/dev/null || echo '?')"
```


### `scripts/sync_upstream.sh`

<!-- FILE: scripts/sync_upstream.sh -->
```bash
#!/usr/bin/env bash
# 从官方 UserDemo 同步 HAL / launcher / assets。这些文件我们不改，只跟着上游走。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git clone --depth 1 https://github.com/m5stack/M5StopWatch-UserDemo.git "$TMP/up"

rsync -a --delete "$TMP/up/main/hal/"    "$ROOT/main/hal/"
rsync -a          "$TMP/up/main/assets/" "$ROOT/main/assets/"
# 官方应用一并同步。早期版本只拿 launcher 和 setup，等于把秒表、闹钟、
# 徽章上传、以及 IMU/麦克风自检全丢了 —— 而这块板出厂就叫 StopWatch。
# 保留它们不冲突：官方应用画在 lv_screen_active()，司南画在岁差根容器上，
# 根容器在引用计数归零时是隐藏的。
for a in common app_launcher app_setup app_stopwatch app_alarm_clock \
         app_badge app_imu app_fft app_watch_face app_lucky_wheel app_template; do
  mkdir -p "$ROOT/main/apps/$a"
  rsync -a --delete "$TMP/up/main/apps/$a/" "$ROOT/main/apps/$a/"
done
cp "$TMP/up/repos.json" "$TMP/up/fetch_repos.py" "$TMP/up/partitions.csv" "$ROOT/"
[ -d "$TMP/up/patches" ] && rsync -a "$TMP/up/patches/" "$ROOT/patches/"

echo "synced. 别忘了跑 python3 fetch_repos.py"
```


### `scripts/build_cjk_font.sh`

<!-- FILE: scripts/build_cjk_font.sh -->
```bash
#!/usr/bin/env bash
# 生成中文子集字体。
#
# 用思源宋体不是黑体：宋体的横细竖粗在鎏金色上有金石感，
# 跟司南的器物调性一致；黑体会让整个界面看起来像一个手机 App。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONTS="$ROOT/main/assets/fonts"
SRC="${1:-$HOME/Library/Fonts/SourceHanSerifSC-Medium.otf}"

[ -f "$SRC" ] || { echo "找不到字体：$SRC"; echo "用法：$0 /path/to/SourceHanSerifSC-Medium.otf"; exit 1; }
command -v npx >/dev/null || { echo "需要 node/npx"; exit 1; }

SYMBOLS="$(tr -d '\n ' < "$FONTS/sinan_cjk_subset.txt")"

for SIZE in 28 40; do
  npx -y lv_font_conv \
    --font "$SRC" --size "$SIZE" --bpp 4 --format lvgl \
    --symbols "$SYMBOLS" --no-compress \
    --lv-font-name "lv_font_sinan_serif_${SIZE}" \
    -o "$FONTS/lv_font_sinan_serif_${SIZE}.c"
  echo "生成 lv_font_sinan_serif_${SIZE}.c"
done

echo "重新 idf.py build 即可自动启用（CMakeLists 检测到 .c 文件会打开 SINAN_HAS_CJK_FONT）"
```


### `README.md`

<!-- FILE: README.md -->
```text
# 司南 SINAN

一台放在 Mac 边上的圆形终端。看得见智能体在干什么，按一下就能决定放不放行。

基于 M5Stack StopWatch（C152），fork 自官方 `M5StopWatch-UserDemo`。

---

## 它做四件事

| 应用 | 干什么 | 靠哪条线 |
|---|---|---|
| **守** Ward | Claude 桌面端要权限时震动提醒，A 批准 B 拒绝 | BLE，官方协议 |
| **阵** Fleet | 四个模型 worker 的额度和状态，哪条线要断一眼看见 | WiFi |
| **问** Echo | 按住说话，Mac 转写并执行，结果念回来 | WiFi + 音频 |
| **历** Almanac | 每日号码 / 身体黄历 / 年轮三个盘 | WiFi |

三条线各走各的。WiFi 断了守照常工作，Mac 关了阵显示上次快照——不会弹窗，只是把外圈转成靛青，你看得懂。

---

## 十五分钟上手

### 1. 拉依赖并编译

```bash
python3 ./fetch_repos.py        # 拉 components/，首次必跑
idf.py set-target esp32s3
idf.py build
```

需要 ESP-IDF **v5.5.4**。别用 6.x，上游锁在 5.5.4。

### 2. 填配置

只有一个文件要改，`main/sinan/config.h`：

```c
#define SN_WIFI_SSID "你家WiFi"       // 2.4G，S3 不支持 5G
#define SN_WIFI_PASS "密码"
#define SN_WS_URI    "ws://192.168.1.100:8790/sinan"
```

Mac 的 IP：`ipconfig getifaddr en0`。

**守不需要任何配置**——BLE 靠桌面端主动扫描，不填 IP 不配网。

### 3. 烧录

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

烧录前**长按电源键约 2 秒，直到内部绿色 LED 亮起**。上传失败十有八九是漏了这步。

### 4. 起 Mac 端服务

```bash
cd daemon && ./install.sh
tail -f /tmp/sinand.log
```

装成 launchd 常驻，开机自启，崩了自动拉起。纯标准库，不用 venv 不用 pip。

改 `daemon/config.toml` 接上你自己的探针脚本（见文件内注释）。

### 5. 配对 Claude 桌面端

Claude for Mac → Help → Troubleshooting → Enable Developer Mode → Developer → Open Hardware Buddy… → Connect，选 `Claude-SINAN-XXXX`。

屏幕会显示 6 位配对码，在 macOS 弹窗里输入。之后自动重连，不用再开这个窗口。

> 这条线连的是 **Claude 桌面 App**（含其中的 Code / Cowork 会话），不是终端里的 `claude` CLI。终端场景走 WiFi 那条线加 hooks。

---

## 视觉

黑漆地、鎏金刻度、生宣白的字，朱砂石绿靛青三种矿物色做语义。整套 UI 里没有一个矩形卡片——全是弧、刻度和极坐标定位的文字。

**岁差**：整个画面每分钟绕圆心转 0.3°，20 小时走完一周。你看不出来，但没有一个像素长期承担同一个亮点。AMOLED 常亮要么烧屏要么定时息屏，我们选第三条路——把防烧屏做成设计本身。

**危险要看得出来**：`rm -rf`、`sudo`、`push --force` 这类命令会让外圈变朱砂，而且必须长按 0.8 秒才能批准。那 0.8 秒是唯一挡在不可逆操作前面的东西。

改视觉只改 `main/sinan/design.h`。别的地方出现颜色字面量就是设计系统失效了。

中文字体要自己生成一次：

```bash
./scripts/build_cjk_font.sh /path/to/SourceHanSerifSC-Medium.otf
```

没生成之前中文显示为空白——刻意的，空白比乱码更容易在自测时发现。

---

## 目录

```
main/sinan/     公共设施：设计令牌、圆形 UI 原语、岁差、状态、三条通道
main/apps/      四个应用，各自一个目录
daemon/         Mac 端服务
scripts/        上游同步、字体生成
AGENTS.md       给 Codex 的完整上下文，改代码前先读
```

`main/hal/`、`main/apps/app_launcher/`、`main/apps/common/` 是上游的，不要改。要跟上游更新跑 `./scripts/sync_upstream.sh`。

---

## 遇到问题

| 症状 | 原因 |
|---|---|
| 上传失败 | 没进下载模式：长按电源键 2 秒到绿灯亮 |
| 桌面端扫不到 | 广播名必须以 `Claude` 开头，检查 `bridge_ble.cpp` |
| 随机崩溃，栈指向 LVGL | 漏了 `LvglLockGuard` |
| 批准了没反应 | `prompt.id` 没原样回传 |
| 中文空白 | 字体子集没生成，或这个字不在 `sinan_cjk_subset.txt` 里 |
| WiFi 连上但 BLE 老断 | S3 单天线共存，把 WS 心跳调到 15 秒以上 |
| 屏幕有残影 | 岁差被关掉了 |

---

## 上游

- 固件基座 [m5stack/M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)（MIT）
- BLE 协议 [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) 的 `REFERENCE.md`
- 渲染思路参考 [robustonian/M5StopWatch-Flux](https://github.com/robustonian/M5StopWatch-Flux)
```


### `daemon/test_voice_autosend.py`

<!-- FILE: daemon/test_voice_autosend.py -->
```python
#!/usr/bin/env python3
"""
松手即发的契约自测。不需要设备、不需要 whisper。

    python3 daemon/test_voice_autosend.py

验的是「一次松手 = 一条完整链路」，以及三条不发送的岔路。
这个契约很容易在后续改动里被无意破坏（比如有人为了"更稳"加一步确认），
所以它需要一个能跑的测试守着，而不是只写在文档里。
"""
from __future__ import annotations

import pathlib
import sys
import types

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sinand  # noqa: E402


class FakeConn:
    def __init__(self):
        self.sent: list[dict] = []
        self.alive = True

    def send_json(self, obj):
        self.sent.append(obj)

    def phases(self):
        return [m.get("phase") for m in self.sent if m.get("t") == "voice_status"]

    def types(self):
        return [m.get("t") for m in self.sent]


def make_server(auto_send=True, heard="今天的号码", reply="0731"):
    cfg = {
        "daemon": {"path": ""},
        "voice": {"auto_send": auto_send, "asr_cmd": "", "agent_cmd": ""},
    }
    srv = sinand.Server.__new__(sinand.Server)
    srv.cfg = cfg
    session = types.SimpleNamespace(
        transcribe=lambda: heard,
        answer=lambda t: reply,
        synth=lambda t: "",
    )
    return srv, session


FAILED = []


def check(name, cond, detail=""):
    if cond:
        print(f"  PASS  {name}")
    else:
        print(f"  FAIL  {name}  {detail}")
        FAILED.append(name)


def main() -> int:
    print("松手即发契约")

    # 1. 正常路径：一次松手走完 transcribing -> asr_result -> running -> say
    srv, sess = make_server()
    conn = FakeConn()
    srv._finish_voice(conn, sess)
    check("完整链路只需一次松手",
          conn.types() == ["voice_status", "asr_result", "voice_status", "say"],
          conn.types())
    check("状态帧顺序正确", conn.phases() == ["transcribing", "running"], conn.phases())
    check("没有 await_confirm", "await_confirm" not in conn.phases())

    # 2. 空转写：不往下走，交给设备端判错
    srv, sess = make_server(heard="")
    conn = FakeConn()
    srv._finish_voice(conn, sess)
    check("空转写不触发 agent", "say" not in conn.types(), conn.types())
    check("空转写仍回 asr_result", "asr_result" in conn.types())

    # 3. 显式关掉 auto_send 才停下等确认
    srv, sess = make_server(auto_send=False)
    conn = FakeConn()
    srv._finish_voice(conn, sess)
    check("auto_send=false 时停在 await_confirm",
          conn.phases() == ["transcribing", "await_confirm"], conn.phases())
    check("auto_send=false 时不执行", "say" not in conn.types())

    # 4. agent 抛异常要回 error，不能静默吞掉
    srv, sess = make_server()
    sess.answer = lambda t: (_ for _ in ()).throw(RuntimeError("boom"))
    conn = FakeConn()
    srv._finish_voice(conn, sess)
    check("agent 失败回 error 帧", "error" in conn.phases(), conn.phases())

    # 5. 配置默认必须是 true
    cfg_text = (pathlib.Path(__file__).parent / "config.example.toml").read_text()
    check("config.example.toml 默认 auto_send = true", "auto_send = true" in cfg_text)

    print()
    if FAILED:
        print(f"{len(FAILED)} FAILED: {', '.join(FAILED)}")
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```


### `main/sinan/route.cpp`

<!-- FILE: main/sinan/route.cpp -->
```cpp
#include "route.h"
#include "state.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstring>
#include <string>
#include <vector>

namespace sinan::route {

namespace {

constexpr char TAG[] = "sinan.route";

struct Entry {
    std::string name;
    int id;
};

std::vector<Entry> s_apps;
bool s_auto_opened = false;
std::string s_home;
uint32_t s_last_open = 0;   // 上次把守推到前台的时刻

int id_of(const char* name)
{
    if (!name || !*name) return -1;
    for (const auto& e : s_apps) {
        if (e.name == name) return e.id;
    }
    return -1;
}

}  // namespace

void register_app(const char* name, int id)
{
    s_apps.push_back({name, id});
}

bool open(const char* name)
{
    const int id = id_of(name);
    if (id < 0) return false;
    if (s_home.empty()) s_home = name;   // 第一个被显式打开的就是开机应用
    return mooncake::GetMooncake().openApp(id);
}

bool yield_to_ward_if_needed()
{
    const auto s = State::get().snapshot();

    /*
     * 两件事都要把守推到前台：
     *   1. 有待决请求
     *   2. 正在配对（六位码只有守画得出来，看不见码就配不上对）
     * 用 key 去重，否则每帧都 openApp 一次，动画永远重来。
     */
    const bool want = (s.ble.passkey != 0) || s.ble.has_prompt;
    if (!want) return false;

    const int id = id_of("Ward");
    if (id < 0) return false;

    /*
     * 去重用时间窗，不要用 prompt id 做 key。
     *
     * 按 key 去重看起来更精确，但会造成死局：用户在请求待决时手动从守退回
     * launcher 再进望，望调到这里发现 key 没变就直接 return true —— 望每帧
     * 什么都不做，画面卡住，而守也没被拉起来。
     *
     * 而调用方永远只会是非守应用，它还在跑就说明守不在前台，那就该切。
     * 时间窗只是防止 openApp 尚未生效的那一两帧里重复调用。
     */
    const uint32_t now = GetHAL().millis();
    if (now - s_last_open < 1000) return true;

    s_last_open   = now;
    s_auto_opened = true;
    mooncake::GetMooncake().openApp(id);
    mclog::tagInfo(TAG, "switching to Ward");
    return true;
}

void return_home_if_auto()
{
    s_last_open = 0;
    if (!s_auto_opened) return;
    s_auto_opened = false;
    const int id = id_of(s_home.empty() ? "Gaze" : s_home.c_str());
    if (id >= 0) mooncake::GetMooncake().openApp(id);
}

}  // namespace sinan::route
```


### `main/sinan/route.h`

<!-- FILE: main/sinan/route.h -->
```cpp
/*
 * route.h — 前台应用路由。
 *
 * 职责边界：只回答"现在该让哪个应用在前台"，不碰 UI。
 *
 * 存在的理由：有权限请求进来时，用户不应该还要自己去 launcher 里点开守。
 * 望/阵/问/历 在 onRunning 里各自调一次 yield_to_ward_if_needed()，
 * 请求处理完再回到开机应用。
 */
#pragma once

namespace sinan::route {

// main.cpp 在 installApp 之后登记，id 就是 installApp 的返回值
void register_app(const char* name, int id);

// 按名字打开。名字不存在或为空则什么都不做，返回 false
bool open(const char* name);

/*
 * 有待决请求且当前不在守里 -> 打开守，返回 true（调用方应立即 return）。
 * 会记下"这次是自动切过去的"，好在请求处理完之后回得来。
 */
bool yield_to_ward_if_needed();

// 守在请求处理完后调。只有当初是自动切过来的才回开机应用
void return_home_if_auto();

}  // namespace sinan::route
```


### `scripts/crops-tuan.json`

<!-- FILE: scripts/crops-tuan.json -->
```json
{
 "_comment": "逐张定的构图，不是算出来的。规矩：脸约占圆直径六成，眼睛落在四成高度（正中会显得头重脚轻），背景越少越好——晕影只吃得掉四个角。换照片用 pick_crops.html 重圈，别去调自动检测。",
 "12529.jpeg": {
  "cx": 1062,
  "cy": 1060,
  "r": 545,
  "mode": "portrait"
 },
 "15693.jpeg": {
  "cx": 440,
  "cy": 355,
  "r": 225,
  "mode": "portrait"
 },
 "11118.jpeg": {
  "cx": 1690,
  "cy": 2008,
  "r": 1290,
  "mode": "portrait"
 },
 "13844.jpeg": {
  "cx": 900,
  "cy": 600,
  "r": 215,
  "mode": "portrait"
 },
 "16112.jpeg": {
  "cx": 2568,
  "cy": 1310,
  "r": 450,
  "mode": "portrait"
 },
 "12092.jpeg": {
  "cx": 1333,
  "cy": 1968,
  "r": 880,
  "mode": "portrait"
 },
 "12246.jpeg": {
  "cx": 2240,
  "cy": 1258,
  "r": 300,
  "mode": "portrait"
 },
 "12494.jpeg": {
  "cx": 1904,
  "cy": 1221,
  "r": 784,
  "mode": "portrait"
 },
 "12930.jpeg": {
  "cx": 1030,
  "cy": 1035,
  "r": 1000,
  "mode": "portrait"
 },
 "13575.jpeg": {
  "cx": 1309,
  "cy": 2349,
  "r": 796,
  "mode": "portrait"
 },
 "13867.jpeg": {
  "cx": 1136,
  "cy": 534,
  "r": 468,
  "mode": "disc"
 },
 "13869.jpeg": {
  "cx": 825,
  "cy": 577,
  "r": 395,
  "mode": "portrait"
 },
 "8762.jpeg": {
  "cx": 1960,
  "cy": 1718,
  "r": 523,
  "mode": "portrait"
 },
 "9154.jpeg": {
  "cx": 1333,
  "cy": 2397,
  "r": 560,
  "mode": "portrait"
 },
 "IMG_5997.jpeg": {
  "cx": 1400,
  "cy": 1829,
  "r": 747,
  "mode": "portrait"
 },
 "IMG_8655.jpeg": {
  "cx": 1579,
  "cy": 2210,
  "r": 411,
  "mode": "portrait"
 }
}
```


### `scripts/pick_crops.html`

<!-- FILE: scripts/pick_crops.html -->
```text
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>司南 · 圈主体</title>
<!--
  为什么有这个页面：自动圈主体解不了。
  试过按冷暖分割（木地板也是暖的）、按白毛定位（半径全靠蒙），
  两套都在四成照片上失手。任意宠物照的主体定位不是颜色启发式能解的问题。

  所以判断交给人，机器只做机械活 —— 跟守那一页是同一个原则。
  十六张照片十分钟，而且每张都对。

  用法：浏览器打开本文件 → 拖进照片 → 每张拖一个圈 → 导出 crops.json
        python3 scripts/prep_photos.py --crops crops.json 照片...
-->
<style>
  :root{--ink:#0B0B0C;--panel:#141416;--line:#26262A;--bronze:#C8A96E;
        --silk:#F2EDE1;--dim:#8A857B;--malachite:#4FA88A;--cinnabar:#D6442F}
  *{box-sizing:border-box}
  body{margin:0;background:var(--ink);color:var(--silk);
       font:400 14px/1.6 -apple-system,BlinkMacSystemFont,"PingFang SC",sans-serif}
  header{padding:20px 24px;border-bottom:1px solid var(--line);
         display:flex;align-items:center;gap:18px;flex-wrap:wrap;
         position:sticky;top:0;background:var(--ink);z-index:10}
  h1{margin:0;font:500 17px/1 inherit;letter-spacing:.16em}
  .hint{color:var(--dim);font-size:13px;flex:1;min-width:260px}
  button{background:var(--panel);color:var(--silk);border:1px solid var(--line);
         border-radius:8px;padding:9px 16px;font:500 13px/1 inherit;cursor:pointer}
  button:hover{border-color:var(--bronze)}
  button.go{background:var(--bronze);color:#141210;border-color:var(--bronze)}
  #drop{margin:24px;padding:56px;border:1px dashed var(--line);border-radius:16px;
        text-align:center;color:var(--dim)}
  #drop.over{border-color:var(--bronze);color:var(--silk)}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));
        gap:22px;padding:24px}
  .cell{background:var(--panel);border:1px solid var(--line);border-radius:14px;
        padding:12px;position:relative}
  .cell canvas{width:100%;display:block;border-radius:8px;cursor:crosshair;touch-action:none}
  .row{display:flex;align-items:center;gap:10px;margin-top:10px;font-size:12px}
  .name{color:var(--dim);flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
        font-family:ui-monospace,Menlo,monospace}
  .date{color:var(--bronze);font-family:ui-monospace,Menlo,monospace}
  .mode{background:transparent;color:var(--dim);border:1px solid var(--line);
        border-radius:6px;padding:3px 8px;font-size:11px;cursor:pointer}
  .mode.disc{color:var(--malachite);border-color:var(--malachite)}
  .cell.unset{outline:1px solid var(--cinnabar);outline-offset:-1px}
  textarea{width:calc(100% - 48px);margin:0 24px 40px;height:180px;background:var(--panel);
           color:var(--silk);border:1px solid var(--line);border-radius:10px;padding:14px;
           font:12px/1.5 ui-monospace,Menlo,monospace}
</style>
</head>
<body>

<header>
  <h1>司南 · 圈主体</h1>
  <span class="hint">在每张图上<b>按住拖动</b>画一个圈，把团团圈进去。蜷成球的点一下右边切成 disc。</span>
  <button id="auto">全部套用默认圈</button>
  <button class="go" id="out">导出 crops.json</button>
</header>

<div id="drop">把照片拖到这里，或点击选择<input id="file" type="file" multiple accept="image/*" hidden></div>
<div class="grid" id="grid"></div>
<textarea id="json" placeholder="导出的 JSON 会出现在这里，存成 crops.json"></textarea>

<script>
const grid = document.getElementById('grid');
const drop = document.getElementById('drop');
const fileIn = document.getElementById('file');
const items = [];   // {name, img, cx, cy, r, mode, canvas, set}

drop.onclick = () => fileIn.click();
fileIn.onchange = e => load([...e.target.files]);
drop.ondragover = e => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = e => { e.preventDefault(); drop.classList.remove('over'); load([...e.dataTransfer.files]); };

function load(files){
  files.filter(f => f.type.startsWith('image/')).forEach(f => {
    const img = new Image();
    img.onload = () => addCell(f.name, img);
    img.src = URL.createObjectURL(f);
  });
  drop.style.display = 'none';
}

function addCell(name, img){
  const cell = document.createElement('div');
  cell.className = 'cell unset';
  const cv = document.createElement('canvas');
  const W = 560, H = Math.round(W * img.height / img.width);
  cv.width = W; cv.height = H;
  cell.appendChild(cv);

  const row = document.createElement('div');
  row.className = 'row';
  row.innerHTML = `<span class="name">${name}</span><span class="date"></span>`;
  const modeBtn = document.createElement('button');
  modeBtn.className = 'mode'; modeBtn.textContent = 'portrait';
  row.appendChild(modeBtn);
  cell.appendChild(row);
  grid.appendChild(cell);

  // 默认给一个居中的保守圈，直接导出也不会是空的
  const it = { name, img, cx: img.width/2, cy: img.height/2,
               r: Math.round(Math.min(img.width, img.height) * 0.34),
               mode: 'portrait', cv, cell, set: false, scale: img.width / W };
  items.push(it);
  draw(it);

  modeBtn.onclick = () => {
    it.mode = it.mode === 'disc' ? 'portrait' : 'disc';
    modeBtn.textContent = it.mode;
    modeBtn.classList.toggle('disc', it.mode === 'disc');
    draw(it);
  };

  let dragging = false, sx = 0, sy = 0;
  const pos = e => {
    const b = cv.getBoundingClientRect();
    const k = cv.width / b.width;
    return [(e.clientX - b.left) * k, (e.clientY - b.top) * k];
  };
  cv.onpointerdown = e => { cv.setPointerCapture(e.pointerId); dragging = true; [sx, sy] = pos(e); };
  cv.onpointermove = e => {
    if (!dragging) return;
    const [x, y] = pos(e);
    it.cx = Math.round((sx + x) / 2 * it.scale);
    it.cy = Math.round((sy + y) / 2 * it.scale);
    it.r  = Math.round(Math.hypot(x - sx, y - sy) / 2 * it.scale);
    it.set = true; cell.classList.remove('unset');
    draw(it);
  };
  cv.onpointerup = () => { dragging = false; };
}

function draw(it){
  const g = it.cv.getContext('2d');
  const W = it.cv.width, H = it.cv.height;
  g.clearRect(0, 0, W, H);
  g.drawImage(it.img, 0, 0, W, H);

  // 圈外压暗，圈内保持原样 —— 这就是设备上晕影之后的样子
  g.save();
  g.beginPath(); g.rect(0, 0, W, H);
  g.arc(it.cx / it.scale, it.cy / it.scale, it.r / it.scale, 0, Math.PI * 2, true);
  g.fillStyle = 'rgba(0,0,0,.72)'; g.fill('evenodd');
  g.restore();

  g.beginPath();
  g.arc(it.cx / it.scale, it.cy / it.scale, it.r / it.scale, 0, Math.PI * 2);
  g.strokeStyle = it.mode === 'disc' ? '#4FA88A' : '#C8A96E';
  g.lineWidth = 2; g.stroke();
}

document.getElementById('auto').onclick = () => {
  items.forEach(it => { it.set = true; it.cell.classList.remove('unset'); });
};

document.getElementById('out').onclick = () => {
  const o = {};
  items.forEach(it => { o[it.name] = { cx: it.cx, cy: it.cy, r: it.r, mode: it.mode }; });
  const txt = JSON.stringify(o, null, 1);
  document.getElementById('json').value = txt;
  navigator.clipboard?.writeText(txt);
  const b = new Blob([txt], {type:'application/json'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(b); a.download = 'crops.json'; a.click();
};
</script>
</body>
</html>
```


### `scripts/prep_photos.py`

<!-- FILE: scripts/prep_photos.py -->
```python
#!/usr/bin/env python3
"""
prep_photos.py — 把照片处理成设备能直接用的资产。

    python3 scripts/prep_photos.py ~/Pictures/团团/*.jpg
    python3 scripts/prep_photos.py --center 1136,534 --radius 468 一张.jpg

输出到 build/tuantuan/，然后把整个文件夹拖到 Claude 桌面端的
Hardware Buddy 窗口上，照片会经 BLE 流进设备。

日期一律从 EXIF DateTimeOriginal 读，其次认文件名前缀 YYYY-MM 或 YYYY-MM-DD。
**任何地方都不允许手打日期。** 手打过一次就错过一次（把 2020.10 写成了 2019.05），
而且错得很难发现——渲染出来的图看着一切正常。

设计前提：团团睡觉时把自己蜷成正圆，而屏幕也是正圆，所以做法不是
"圆屏里放一张照片"，而是把他抠成一枚悬浮的圆盘。四周是纯黑，
AMOLED 上黑像素熄灭，于是他没有边缘，像一件实物躺在表壳里。

只需要 pillow + numpy，不需要 ImageMagick，也不需要抠图模型。
"""
import argparse, json, pathlib, re, sys
from datetime import date, datetime
import numpy as np
from PIL import ExifTags, Image, ImageDraw, ImageEnhance, ImageFilter, ImageOps

SRC = 536          # 设备端画布，比屏幕大 70px，给漂移留余量
FRAC = 0.72        # 主体直径占画布的比例，四周留黑给晕影
BUDGET_KB = 1700   # Hardware Buddy 文件夹推送上限 1.8MB，留点余量


DATE_RE = re.compile(r'(20\d{2})[-_.]?(\d{2})(?:[-_.]?(\d{2}))?')


def photo_date(path: pathlib.Path):
    """EXIF 优先，文件名兜底。拿不到就返回 None，绝不猜、绝不用文件修改时间
    （那是下载时间，不是拍摄时间）。"""
    try:
        ex = Image.open(path).getexif()
        sub = ex.get_ifd(0x8769)
        for k, v in sub.items():
            if ExifTags.TAGS.get(k) == 'DateTimeOriginal' and v:
                return datetime.strptime(str(v)[:10], '%Y:%m:%d').date()
        for k, v in ex.items():
            if ExifTags.TAGS.get(k) == 'DateTime' and v:
                return datetime.strptime(str(v)[:10], '%Y:%m:%d').date()
    except Exception:
        pass
    m = DATE_RE.search(path.stem)
    if m:
        y, mo, d = m.group(1), m.group(2), m.group(3) or '15'
        try:
            return date(int(y), int(mo), int(d))
        except ValueError:
            pass
    return None


def find_subject(im):
    """猜主体的外接圆。**这只是兜底，不要指望它。**

    试过两套判据：按冷暖分割（木地板也是暖的，圈进半个客厅）、
    按白毛定位（脸和胸的白毛偏离头心，半径全靠蒙）。
    两套都在四成左右的照片上失手。任意宠物照的主体定位不是颜色启发式
    能解的问题，别再往里加第三套规则。

    正确做法是 scripts/pick_crops.html：浏览器里拖一个圈，导出 crops.json，
    用 --crops 喂进来。十六张照片十分钟，而且每张都对。
    这跟守那一页是同一个原则 —— 判断交给人，机器只做机械活。
    """
    small = im.resize((im.width // 6, im.height // 6))
    a = np.asarray(small).astype(np.int16)
    lo, hi = a.min(2), a.max(2)
    blaze = (a.mean(2) > 175) & ((hi - lo) < 30)      # 亮且接近中性 = 白毛
    m = Image.fromarray((blaze * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(5))
    ys, xs = np.nonzero(np.asarray(m) > 127)

    if len(xs) < 150:
        return im.width // 2, im.height // 2, int(min(im.size) * 0.34)

    cx, cy = xs.mean() * 6, ys.mean() * 6
    spread = np.percentile(np.hypot(xs * 6 - cx, ys * 6 - cy), 88)
    r = int(np.clip(spread * 1.9, min(im.size) * 0.16, min(im.size) * 0.52))
    return int(cx), int(cy), r


def center_square(im, zoom):
    """纯居中方裁。主体在画面中心时这是唯一正确的做法 ——
    不猜圆心就不可能把主体裁偏。zoom 越大裁得越紧、主体越大。"""
    side = int(min(im.size) / zoom)
    cx, cy = im.width // 2, im.height // 2
    half = side // 2
    half = min(half, cx, cy)
    return cx, cy, half


def build(path, out, center=None, radius=None, mode='portrait', zoom=1.0):
    """两种裁法。

    disc：他蜷成球时用。主体本身就是圆的，占 72%，四周留黑给晕影，
          并在最外环带压掉残留背景。
    portrait：坐着站着时用。裁到头，占 90%，晕影往里收到 0.60 —— 地板和
          家具在外圈直接死掉。这种照片的背景常是暖木色，跟毛色同色系，
          冷暖分割会把毛一起啃掉，所以不清背景。
    """
    frac, clean = (FRAC, True) if mode == 'disc' else (0.90, False)

    # exif_transpose 必须有：手机竖拍的照片元数据里带旋转标记，
    # 不应用的话狗是躺着的。从 ImageMagick 换到 PIL 时把 -auto-orient 弄丢了
    im = ImageOps.exif_transpose(Image.open(path)).convert('RGB')
    if center and radius:
        cx, cy, r = center[0], center[1], radius
    elif zoom:
        cx, cy, r = center_square(im, zoom)
    else:
        cx, cy, r = find_subject(im)
    r = min(r, cx, cy, im.width - cx, im.height - cy)

    dog = im.crop((cx - r, cy - r, cx + r, cy + r))
    D = int(SRC * frac)
    dog = dog.resize((D, D), Image.LANCZOS)

    # 只在最外一圈环带上压残留背景。往里一步都不碰 ——
    # 不限半径的话，白毛的阴影会被当成地板一起啃掉
    if clean:
        a = np.asarray(dog).astype(np.int16)
        Rc, Gc, Bc = a[:, :, 0], a[:, :, 1], a[:, :, 2]
        yy, xx = np.mgrid[0:D, 0:D]
        edge = np.clip((np.hypot(xx - D / 2, yy - D / 2) / (D / 2) - 0.86) / 0.14, 0, 1)
        cool = (Bc > Rc + 14) & (Gc > Rc + 8)
        pale = (a.mean(2) > 150) & (Gc - Rc < 0)
        bg = Image.fromarray(((cool | pale) * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(7))
        bg = np.asarray(bg.filter(ImageFilter.GaussianBlur(5))).astype(np.float32) / 255.0
        dog = Image.fromarray((np.asarray(dog) * (1 - bg * edge)[..., None]).astype(np.uint8))

    # 羽化的圆形遮罩：软边既像浮在黑里，也不给 JPEG 的 DCT 留硬边去振铃
    mask = Image.new('L', (D, D), 0)
    ImageDraw.Draw(mask).ellipse((12, 12, D - 12, D - 12), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(12))

    canvas = Image.new('RGB', (SRC, SRC), (0, 0, 0))
    canvas.paste(dog, ((SRC - D) // 2,) * 2, mask)
    # AMOLED 色域比显示器宽，原样放上去偏平；+8% 饱和在这块屏上刚好，多了就俗
    canvas = ImageEnhance.Color(canvas).enhance(1.08)
    canvas.save(out, quality=92)
    return cx, cy, r


def make_glyph(asset_path, out, G=264, spacing=7.4):
    """从第一张照片生成极坐标点阵字形。

    团团蜷着的轮廓就是个圆，直接做剪影认不出是狗。改成半调点阵：
    同心圆环上撒点，点径跟着照片明暗走 —— 白毛的扫尾、蜷起的脸、
    圆滚的身子都读得出来，而且"全是弧和点"正好是这套设计语言本身。

    存成 RGBA PNG（RGB 全白，alpha 是点的浓度），设备端用
    lv_obj_set_style_image_recolor 按状态染色，比塞一个 68KB 的 C 数组干净。
    """
    im = Image.open(asset_path).convert('L')
    S = im.width
    r0 = int(S * FRAC / 2)
    im = im.crop((S//2-r0, S//2-r0, S//2+r0, S//2+r0))
    im = im.resize((G, G), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.0))

    lum = np.asarray(im).astype(np.float32)
    yy, xx = np.mgrid[0:G, 0:G]
    inside = np.hypot(xx - G/2, yy - G/2) < G/2 - 2
    # 只用主体内部的分位数归一化。用全图的话黑背景会把整体压暗，
    # 棕色身体就只剩下一片小点，认不出是狗
    lo, hi = np.percentile(lum[inside], [3, 97])
    v = np.clip((lum - lo) / max(hi - lo, 1), 0, 1) ** 0.62   # 提中间调
    v[~inside] = 0

    alpha = Image.new('L', (G, G), 0)
    d = ImageDraw.Draw(alpha)
    r = 4.0
    while r < G/2 - 3:
        n = max(6, int(round(2 * np.pi * r / spacing)))
        for i in range(n):
            # 每圈错开半格，否则会出现放射状的假条纹
            th = 2 * np.pi * (i + (0.5 if int(r/spacing) % 2 else 0)) / n
            x, y = G/2 + r*np.cos(th), G/2 + r*np.sin(th)
            val = float(v[int(np.clip(y, 0, G-1)), int(np.clip(x, 0, G-1))])
            if val < 0.04:
                continue
            rad = 0.85 + val * 3.0
            d.ellipse([x-rad, y-rad, x+rad, y+rad], fill=int(120 + 135*val))
        r += spacing

    glyph = Image.merge('RGBA', (Image.new('L', (G, G), 255),) * 3 + (alpha,))
    glyph.save(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('photos', nargs='+')
    ap.add_argument('--center', help='主体圆心 x,y（不给则自动猜）')
    ap.add_argument('--radius', type=int, help='主体半径')
    ap.add_argument('--name', default='tuan', help='设备端目录名 -> /spiflash/<name>')
    ap.add_argument('--disc', nargs='*', default=[],
                    help='用 disc 模式（蜷成球）处理的文件名，其余一律 portrait')
    ap.add_argument('--glyph-from', default=None,
                    help='用哪张生成点阵字形。默认挑第一张 disc 模式的 —— '
                         '半调点阵在圆形主体上最好认，坐姿做出来会糊')
    ap.add_argument('--zoom', type=float, default=1.35,
                    help='居中方裁的收紧系数。1.0=整个短边，越大裁得越紧、主体越大。'
                         '主体在画面中心时用这个，不猜圆心就不会裁偏')
    ap.add_argument('--exif-only', action='store_true',
                    help='只收有 EXIF 拍摄日期的照片')
    ap.add_argument('--crops', default=None,
                    help='scripts/pick_crops.html 导出的 JSON：'
                         '{"文件名": {"cx":.., "cy":.., "r":.., "mode":"disc|portrait"}}')
    ap.add_argument('--captions', default=None,
                    help='可选的 JSON：{"文件名": "海边"}。写场景，不要写年龄')
    args = ap.parse_args()

    center = tuple(int(v) for v in args.center.split(',')) if args.center else None
    caps = json.loads(pathlib.Path(args.captions).read_text()) if args.captions else {}
    crops = json.loads(pathlib.Path(args.crops).read_text()) if args.crops else {}
    disc_set = set(args.disc)

    out_dir = pathlib.Path(__file__).resolve().parent.parent / 'build' / 'tuantuan'
    if out_dir.exists():
        for f in out_dir.iterdir():
            f.unlink()
    out_dir.mkdir(parents=True, exist_ok=True)

    # 先按日期排。没日期的排在最后，保持给定顺序
    srcs = [pathlib.Path(s) for s in args.photos if pathlib.Path(s).is_file()]
    dated = [(photo_date(p), p) for p in srcs]
    if args.exif_only:
        dated = [x for x in dated if x[0]]
    dated.sort(key=lambda x: (x[0] is None, x[0] or date.min))

    items, undated, n = [], [], 0
    glyph_src = None
    for d, p in dated:
        n += 1
        c = crops.get(p.name)
        mode = c['mode'] if c and 'mode' in c else ('disc' if p.name in disc_set else 'portrait')
        ctr = (c['cx'], c['cy']) if c else center
        rad = c['r'] if c else args.radius
        dst = out_dir / f'{n:02d}.jpg'
        cx, cy, r = build(p, dst, ctr, rad, mode, args.zoom)
        if glyph_src is None and (args.glyph_from == p.name or
                                  (args.glyph_from is None and mode == 'disc')):
            glyph_src = dst
        items.append({'file': dst.name,
                      'date': d.isoformat() if d else None,
                      'caption': caps.get(p.name, ''),
                      'mode': mode})
        if d is None:
            undated.append(p.name)
        print(f'  {p.name:<24} -> {dst.name}  {dst.stat().st_size // 1024:>3}KB  '
              f'{mode:<8} {d.isoformat() if d else "日期未知"}')

    if not n:
        sys.exit('没有处理任何文件')

    # 点阵字形：守的长按确认要用
    make_glyph(glyph_src or (out_dir / '01.jpg'), out_dir / 'glyph.png')
    print(f"  glyph.png  {(out_dir / 'glyph.png').stat().st_size // 1024}KB   "
          f"点阵字形（源：{(glyph_src or out_dir / '01.jpg').name}）")

    (out_dir / 'manifest.json').write_text(
        json.dumps({'name': args.name, 'items': items}, ensure_ascii=False, indent=1))

    if undated:
        print(f'\n{len(undated)} 张没读到拍摄日期（微信转发会剥掉 EXIF）：')
        for u in undated:
            print(f'    {u}')
        print('  它们照样进轮播，只是不落在「喜」的环上。')
        print('  想让它们上环：文件名前面加 2022-06_ 这样的前缀，重跑一次即可。')
    total = sum(f.stat().st_size for f in out_dir.iterdir()) // 1024
    print(f'\n共 {n} 张，{total}KB')
    if total > BUDGET_KB:
        sys.exit(f'超过 Hardware Buddy 的 1.8MB 上限。减少张数，或把 quality 调到 85。')
    print(f'好了。把 {out_dir} 拖到 Hardware Buddy 窗口上。')
    print('自动猜的圆心不满意就用 --center x,y --radius r 手工指定，比调参快。')


if __name__ == '__main__':
    main()
```
