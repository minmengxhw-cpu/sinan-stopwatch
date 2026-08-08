/*
 * config.h — 用户配置。这是唯一需要在烧录前改的文件。
 *
 * BLE 两条通道（Buddy NUS + HID 键盘）不需要任何配置：
 * 桌面端扫描配对即用，不用填 IP、不用配网。
 * 下面这些只影响 WiFi 那条增强通道（阵的数据、对讲转写）。
 */
#pragma once

// WiFi。ESP32-S3 只支持 2.4GHz，别填 5G 的 SSID
#define SN_WIFI_SSID "YOUR_WIFI"
#define SN_WIFI_PASS "YOUR_PASSWORD"

// Mac 上 sinand.py 的地址。查本机 IP：ipconfig getifaddr en0
#define SN_WS_URI "ws://192.168.1.100:8790/sinan"

// BLE HID 键盘通道（Action 层 OK/NG/文本注入）。关掉则只剩 Buddy + WS
#define SN_HID_ENABLE 1

// WiFi/WS 增强通道。关掉则阵只有 BLE 心跳数据，对讲不可用
#define SN_WS_ENABLE 1

// 开机即壳（寐层）。启动器已退役，此项仅为文档标记
#define SN_BOOT_APP "Sinan"
