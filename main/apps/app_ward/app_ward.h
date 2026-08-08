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
