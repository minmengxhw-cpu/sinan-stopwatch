#include "photo_store.h"
#include <draw/lv_image_decoder_private.h>
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
        // Only the path is known at scan time.  The remaining fields keep their
        // declared defaults and captions are filled from manifest.json below.
        s_slots.emplace_back();
        s_slots.back().file = std::string(PHOTO_DIR) + "/" + e->d_name;
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
