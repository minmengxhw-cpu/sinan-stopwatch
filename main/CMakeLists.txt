file(GLOB_RECURSE MY_SRCS
    "apps/*.c" "apps/*.cc" "apps/*.cpp"
    "assets/*.c" "assets/*.cc" "assets/*.cpp"
    "hal/*.c" "hal/*.cc" "hal/*.cpp"
    "sinan/*.c" "sinan/*.cc" "sinan/*.cpp"
)

idf_component_register(
    SRCS "main.cpp" ${MY_SRCS}
    INCLUDE_DIRS "."
    # 不写 REQUIRES：显式收窄会关掉"依赖全部组件"的默认语义，
    # lvgl/mooncake/M5GFX 的 include 会全部找不到
)

# 上游资产由 scripts/sync_upstream.sh 拉进来。缺文件时给一句人话，
# 而不是让 CMake 甩一段找不着北的报错
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

# 中文子集字体生成后由 scripts/build_cjk_font.sh 打开这个宏
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/fonts/lv_font_sinan_serif_28.c")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC SINAN_HAS_CJK_FONT=1)
endif()
