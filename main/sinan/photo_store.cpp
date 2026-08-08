#include "photo_store.h"
#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <strings.h>
#include <algorithm>
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
    lv_image_dsc_t dsc{};
    void* pixels = nullptr;
    int refs = 0;
};

std::vector<Slot> s_slots;

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

    // 按文件名排序，轮换顺序才可预期。用户给照片编号就是在排顺序
    std::sort(s_slots.begin(), s_slots.end(),
              [](const Slot& a, const Slot& b) { return a.file < b.file; });
    ESP_LOGI(TAG, "found %d photos", static_cast<int>(s_slots.size()));
    return static_cast<int>(s_slots.size());
}

void rescan() { init(); }

int count() { return static_cast<int>(s_slots.size()); }

const char* name_of(int i)
{
    return (i >= 0 && i < count()) ? s_slots[i].file.c_str() : "";
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
     * NOTE(codex): lv_image_decoder 的签名在 LVGL 9.x 各小版本间有过调整。
     * 编译不过时对照 components/lvgl/src/draw/lv_image_decoder.h 修正，
     * 但要保持"解一次、存 PSRAM、之后只 blit"这个契约不变。
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
    if (dec.decoded && dec.decoded->data) {
        const size_t n = std::min(bytes, static_cast<size_t>(dec.decoded->data_size));
        std::memcpy(s.pixels, dec.decoded->data, n);
    }
    lv_image_decoder_close(&dec);

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
