# 司南 SINAN v3.1.1

M5Stack StopWatch（C152）桌面 AI 编程终端，基于官方 `M5StopWatch-UserDemo`。

## 当前功能

| 应用 | 用途 | 通道 |
|---|---|---|
| Photos | 2 张团团照片待机页（2020 小时候、蜷缩睡觉） | 本地存储 |
| Work | Codex、PASEO、Grok Build 的真实状态与编程语音入口 | Wi‑Fi WebSocket |
| Agarwood | 沉香点燃、燃烧与余韵仪式 | 本地动画 |
| Buddy | Claude Desktop 硬件权限提示 | BLE 官方协议 |
| Tools / Connect | 设备状态与本地私有配网 | 本地 AP |
| Stopwatch / Badge | 官方硬件功能 | 本地 |

Work 不包含 Happy、HappyCode、独立 Voice、待办记录或灵感记录。

## Work 操作

Work 首页：

- B：依次选择 Codex、PASEO、Grok Build。
- A：进入当前编程工具。
- A+B 或向下滑：返回启动器。

进入工具后：

- A：开始录音。
- A 再按一次：停止录音并在 Mac 本地转写。
- 屏幕显示转写结果后，B：把这条指令发送给当前工具。
- 转写不对时按 A 重新录，不会误发。
- A+B 或向下滑：退回 Work 首页。

## Mac 桥接

桥接安装在 `~/.sinan/app/`，launchd 标签为 `com.sinan.daemon`，默认监听
`0.0.0.0:8790`。配对令牌只保存在 `~/.sinan/bridge.token`，不会写进源码。

默认只路由三个固定目标：

- Codex：连接本机常驻的 Codex app-server，会话复用并保留工作区沙箱。
- PASEO：`paseo run --provider claude`，由 PASEO 管理会话。
- Grok Build：`grok --single`。

PASEO 与 Grok 使用各自默认权限模式，不自动扩大权限。所有语音指令都作为独立
命令参数传入，不拼接成 shell 文本；桥接还会附加“禁止删除本地文件”的约束。

## 首次配网

没有桥接配置时，设备仍会进入 Photos，Work 显示离线。需要联网时从
Tools → MAC BRIDGE 是 Mac 桥接快捷入口；Connect 单独负责打开私有配网页：

1. Mac 或手机连接 `M5StopWatch-0AC1`。
2. 打开 `http://192.168.4.1`。
3. 填 2.4GHz Wi‑Fi、`ws://<Mac局域网IP>:8790/sinan` 和配对令牌。
4. 保存并关闭页面，设备自动重启。

配网是当前唯一需要用户输入 Wi‑Fi 密码的步骤；程序不会读取或显示该密码。

## 构建与烧录

使用 ESP‑IDF 5.5.4：

```bash
export IDF_TOOLS_PATH=/path/to/.espressif
. /path/to/esp-idf-v5.5.4/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem2101 app-flash
```

只刷应用分区不会擦除 NVS 或照片存储。

## 验收

```bash
python3 daemon/test_security_and_voice.py
python3 daemon/test_voice_autosend.py
python3 daemon/test_sources.py
```

`test_voice_autosend.py` 的历史文件名保留，但测试内容已经改为“A 转写、B 发送”。
