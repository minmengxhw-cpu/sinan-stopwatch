/*
 * config.h — 用户配置。这是唯一需要在烧录前改的文件。
 *
 * 注意 BLE 通道不需要任何配置：官方 buddy 协议靠桌面端主动扫描配对，
 * 不用填 IP、不用配网。WiFi 桥接改由设备上的 Connect 页面写入 NVS。
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
 * 默认 Photos：先看到真实的狗狗原图；守仍会被权限请求"拉起来" ——
 * 有请求时会自动从任何应用切过去，处理完再切回来，不需要你记得开它。
 * 可选 "Photos" / "Agarwood" / "Work" / "Buddy" / ""
 */
#define SN_BOOT_APP "Photos"
