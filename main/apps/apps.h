#pragma once
// 上游保留。app_template / app_watch_face / app_lucky_wheel 留在树里作参考，
// 但不安装：望已经取代了表盘，转盘跟司南的视觉语言不搭
#include "app_launcher/app_launcher.h"
#include "app_setup/app_setup.h"
#include "app_stopwatch/app_stopwatch.h"       // 这块板出厂就叫 StopWatch
#include "app_badge/app_badge.h"               // 不走 BLE 的照片上传路径
#if SINAN_DIAG
#include "app_imu/app_imu.h"
#include "app_fft/app_fft.h"
#endif
// 司南
#include "app_ward/app_ward.h"
#include "app_fleet/app_fleet.h"
#include "app_gaze/app_gaze.h"                  // 历史望页，保留源代码但不再注册
#include "app_dog_photo/app_dog_photo.h"        // 第一项：狗狗照片
#include "app_wenwan/app_wenwan.h"              // 沉香点香仪式
#include "app_tools/app_tools.h"
#include "app_connect/app_connect.h"
