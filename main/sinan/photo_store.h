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
