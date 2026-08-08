# 司南 SINAN

一台放在 Mac 边上的圆形 AI 决策仪。默认是一只蜷成正圆睡觉的边牧（团团），
有权限请求时朱砂/琥珀抢屏，干活时 A+B 切到 agent 环与 Action 层，
配对后它就是一把蓝牙键盘 —— 不回 Mac 也能 OK / NG / 说话进焦点窗口。

基于 M5Stack StopWatch（C152），fork 自官方 `M5StopWatch-UserDemo`。

---

## 它是一件东西，不是五个 App

```
L3  Interrupt  审批 / 配对码 / ERROR / DONE（可抢占任何层，45s 去重）
L2  Action     FAST · NG · OK · PLAN · AI · 中央对讲（BLE HID 发键）
L1  Work       阵：≤6 agent 极坐标环，段弧=额度，颜色=状态
L0  Rest       寐：团团照片（身体）+ 点阵（灵魂）+ 项圈珠串（今天的因果）  ← 默认
```

切层只是显隐，照片不重新解码。键位全机唯一（`main/sinan/input_map.h`）：

| 输入 | 语义 |
|---|---|
| A 短 / A 长 | 肯定 / 危险确认（Grave 长按 0.8s，团团填色） |
| B 短 / B 长 | 否定·切换 / 回退（Rest 里 B 长 = Settings） |
| A+B | 层切换：Rest→Work→Action |
| Rim 快滑 / 慢滑 | 盘珠·切 agent / 抚触团团（呼噜） |
| 敲桌子 / 点中心 | 露时间 6 秒 |
| 中心长按 | 对讲（录音→daemon 转写→可直注焦点窗口） |

## 三条通道，各走各的

| 通道 | 干什么 | 依赖 |
|---|---|---|
| BLE NUS | Claude Desktop Buddy 权限审批、照片推送 | 官方协议 |
| BLE HID | Action 层按键进焦点窗口，**冷启动零依赖** | 系统蓝牙配对即可 |
| WiFi WS | 阵的多厂额度、对讲转写、DONE/ERROR 通知 | `daemon/sinand.py` |

任一条挂掉不影响其他。配对时 6 位码直接显示在屏上（DisplayOnly）。

## 十五分钟上手

```bash
./scripts/sync_upstream.sh     # 拉官方 HAL/launcher/assets（MIT）
python3 fetch_repos.py         # 拉组件（mooncake/lvgl/...）
# 改 main/sinan/config.h 填 WiFi 与 daemon 地址（不填也能用 BLE 两条线）
idf.py flash monitor
```

Mac 端：`cd daemon && ./install.sh && python3 sinand.py`

## 开发闭环

| 工具 | 干什么 |
|---|---|
| `tools/ctl.py` | 串口控制台：`S` 自测、`I approval` 注假审批、`a/A/C` 注按键 |
| `tools/screenshot.py` | `P` 截屏存 PNG |
| `web/prototype/` | 浏览器评审四层 UI，`python3 -m http.server -d web/prototype` |
| `scripts/export_design_tokens.py` | 从 `design.h` 导出 tokens.json 给 web 原型 |

重构规格与验收清单：`docs/REFACTOR_SPEC.md`。设计令牌唯一来源：`main/sinan/design.h`。
