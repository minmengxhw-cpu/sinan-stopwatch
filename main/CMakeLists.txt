file(GLOB_RECURSE MY_SRCS
    "apps/*.c" "apps/*.cc" "apps/*.cpp"
    "assets/*.c" "assets/*.cc" "assets/*.cpp"
    "hal/*.c" "hal/*.cc" "hal/*.cpp"
    "sinan/*.c" "sinan/*.cc" "sinan/*.cpp"
)

idf_component_register(
    SRCS "main.cpp" ${MY_SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        bt
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        mbedtls
    EMBED_FILES "assets/sfx/boot_sfx.bin"
    EMBED_TXTFILES "hal/utils/config_ap/assets/badge_config_ap.html"
)

# 中文子集字体生成后由 scripts/build_cjk_font.sh 打开这个宏
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/fonts/lv_font_sinan_serif_28.c")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC SINAN_HAS_CJK_FONT=1)
endif()
