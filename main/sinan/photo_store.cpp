#include "photo_store.h"
#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <strings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <cstring>
#include <memory>
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

// s_slots：当前目录清单，rescan() 时整体替换。
// s_retiring：rescan 发生时仍被 UI 侧持有（refs>0）的旧 slot ——
// 不能跟着立刻释放，否则 LVGL 渲染线程下一帧还在读的像素缓冲已经被 free，
// 是use-after-free。挪到这里等对应 release() 把 refs 减到 0 才真正释放。
// 用 unique_ptr 存放：即使 vector 扩容搬迁了内部指针数组，Slot 对象本身地址
// 不变，之前发给 layer_rest.cpp 的 &slot->dsc 依旧有效。
std::vector<std::unique_ptr<Slot>> s_slots;
std::vector<std::unique_ptr<Slot>> s_retiring;

SemaphoreHandle_t s_mutex = nullptr;

// rescan() 来自 BLE 主机任务，acquire()/release() 来自 UI/LVGL 任务，
// 两边都会改 s_slots/s_retiring，必须互斥。用可能阻塞的 FreeRTOS 互斥量
// 而不是 portENTER_CRITICAL 自旋锁 —— 临界区里有 JPEG 解码/malloc，
// 拿自旋锁关中断做这些事会让别的任务（含 BLE 协议栈）卡住。
struct Lock {
    Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_mutex); }
};

void free_slot(Slot& s)
{
    if (s.pixels) heap_caps_free(s.pixels);
    s.pixels   = nullptr;
    s.dsc.data = nullptr;
}

bool has_ext(const char* n, const char* ext)
{
    const size_t ln = std::strlen(n), le = std::strlen(ext);
    return ln > le && strcasecmp(n + ln - le, ext) == 0;
}

}  // namespace

int init()
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    Lock lock;

    // 旧 slot：没人用（refs==0）的直接释放；还被引用的（正在渲染）挪去
    // retiring，交给对应的 release() 在引用数归零时释放，绝不抢先释放
    for (auto& s : s_slots) {
        if (s->refs > 0) {
            s_retiring.push_back(std::move(s));
        } else {
            free_slot(*s);
        }
    }
    s_slots.clear();

    DIR* d = opendir(PHOTO_DIR);
    if (!d) {
        ESP_LOGW(TAG, "%s not found; push photos via Hardware Buddy", PHOTO_DIR);
        return 0;
    }
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (!has_ext(e->d_name, ".jpg") && !has_ext(e->d_name, ".jpeg")) continue;
        if (static_cast<int>(names.size()) >= MAX_PHOTOS) break;
        names.push_back(std::string(PHOTO_DIR) + "/" + e->d_name);
    }
    closedir(d);

    // 按文件名排序，轮换顺序才可预期。用户给照片编号就是在排顺序
    std::sort(names.begin(), names.end());
    for (auto& n : names) {
        auto s = std::make_unique<Slot>();
        s->file = std::move(n);
        s_slots.push_back(std::move(s));
    }
    ESP_LOGI(TAG, "found %d photos", static_cast<int>(s_slots.size()));
    return static_cast<int>(s_slots.size());
}

void rescan() { init(); }

int count()
{
    Lock lock;
    return static_cast<int>(s_slots.size());
}

const char* name_of(int i)
{
    Lock lock;
    return (i >= 0 && i < static_cast<int>(s_slots.size())) ? s_slots[i]->file.c_str() : "";
}

const lv_image_dsc_t* acquire(int index)
{
    Lock lock;
    if (index < 0 || index >= static_cast<int>(s_slots.size())) return nullptr;
    Slot& s = *s_slots[index];

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
    Lock lock;
    for (auto& s : s_slots) {
        if (&s->dsc != dsc) continue;
        if (--s->refs > 0) return;
        free_slot(*s);
        return;
    }
    // 没在当前列表里找到，说明是 rescan 时挪去 retiring 的旧 slot：
    // 引用数归零后真正释放像素缓冲，并把这个 Slot 对象本身也清掉
    for (auto it = s_retiring.begin(); it != s_retiring.end(); ++it) {
        if (&(*it)->dsc != dsc) continue;
        if (--(*it)->refs > 0) return;
        free_slot(**it);
        s_retiring.erase(it);
        return;
    }
}

}  // namespace sinan::photo
