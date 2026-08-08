file(GLOB_RECURSE MY_SRCS
    "apps/*.c" "apps/*.cc" "apps/*.cpp"
    "assets/*.c" "assets/*.cc" "assets/*.cpp"
    "hal/*.c" "hal/*.cc" "hal/*.cpp"
    "sinan/*.c" "sinan/*.cc" "sinan/*.cpp"
)

# v3.1 把语音并入 Work。旧 app_echo 源文件只作为历史参考保留，
# 不注册也不编进固件，避免出现第二套“语音记录”交互。
list(FILTER MY_SRCS EXCLUDE REGEX ".*/apps/app_echo/.*")
# 用户明确移除的入口：保留源文件作历史参考，但不编入最终固件。
list(FILTER MY_SRCS EXCLUDE REGEX ".*/apps/app_dog_play/.*")
list(FILTER MY_SRCS EXCLUDE REGEX ".*/apps/app_almanac/.*")
# 用户明确移除闹钟入口：保留源文件作历史参考，但不编入最终固件。
list(FILTER MY_SRCS EXCLUDE REGEX ".*/apps/app_alarm_clock/.*")

idf_component_register(
    SRCS "main.cpp" ${MY_SRCS}
    INCLUDE_DIRS "."
)

# 上游资产由 scripts/sync_upstream.sh 拉进来。缺文件时给一句人话，
# 而不是让 CMake 甩一段找不着北的报错
set(SINAN_EMBED "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/sfx/boot_sfx.bin")
    target_add_binary_data(${COMPONENT_LIB} "assets/sfx/boot_sfx.bin" BINARY)
else()
    message(WARNING "缺 assets/sfx/boot_sfx.bin —— 先跑 ./scripts/sync_upstream.sh")
endif()
# 两张狗狗原图直接编进固件，绕开旧版 FAT/LVGL JPEG 路径。
# 文件不存在时仍允许工程先编译诊断其它功能，但正式固件必须带上它们。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/dog/2020-puppy.jpg")
    target_add_binary_data(${COMPONENT_LIB} "assets/dog/2020-puppy.jpg" BINARY)
else()
    message(WARNING "缺 assets/dog/2020-puppy.jpg —— Photos 将无法显示")
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/dog/curled.jpg")
    target_add_binary_data(${COMPONENT_LIB} "assets/dog/curled.jpg" BINARY)
else()
    message(WARNING "缺 assets/dog/curled.jpg —— Photos 将无法显示")
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/hal/utils/config_ap/assets/badge_config_ap.html")
    target_add_binary_data(${COMPONENT_LIB} "hal/utils/config_ap/assets/badge_config_ap.html" TEXT)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/hal/hal.h")
    message(FATAL_ERROR "缺 main/hal/ —— 先跑 ./scripts/sync_upstream.sh 再 idf.py build")
endif()

# 诊断应用（IMU / 麦克风频谱）。上板自检时打开：
#   idf.py build -DSINAN_DIAG=1
option(SINAN_DIAG "install upstream IMU / FFT diagnostic apps" OFF)
target_compile_definitions(${COMPONENT_LIB} PUBLIC SINAN_DIAG=$<BOOL:${SINAN_DIAG}>)

# 中文子集字体生成后由 scripts/build_cjk_font.sh 打开这个宏
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/fonts/lv_font_sinan_serif_28.c")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC SINAN_HAS_CJK_FONT=1)
endif()
