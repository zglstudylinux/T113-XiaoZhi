# T113-XiaoZhi 架构重构方案（CMake / OSAL / UI-业务解耦 / PC 模拟器 / CI）

> 2026-08-30 与用户确认的方案。总原则：**行为零变化优先**——迁移与 OSAL 阶段是机械替换，
> 解耦阶段才动控制流；每阶段可独立编译，板测按风险安排（P2 后一次、P4/P5 后一次）。

## 1. 重构动机

M5 完成后（37ff08d，5521 行）暴露的结构问题：

| 问题 | 位置 | 后果 |
|---|---|---|
| UI 直调业务 | `ui_main.c` 调 `xz_protocol_talk_*`/`wifi_manager_state`；`ui_wifi_setup.c` 调 `wifi_manager_scan/connect/save_cfg`；`ui_menu.c` 调 `wifi_manager_state` | 换 UI 必须改业务代码 |
| 控制流倒挂 | `main.c` 实现 `xz_app_ensure_protocol`/`xz_app_wifi_setup_done` 被 ui/ 反向调用 | 加页面就要动 main.c |
| 业务直调 UI 且跨线程 | `app_state.c:28` 协议线程直调 `ui_main_on_state()` 摸 LVGL | 线程违规（现存 bug） |
| 配置散落 | URL/token/路径/设备名/采样率硬编码 7 个文件（`/mnt/UDISK/xiaozhi` ×6） | 改配置要全文搜索 |
| 手写 Makefile | 硬编码 SDK staging 绝对路径；lvgl 只剩 lvgl.mk；opus 源码不在仓库 | 无法 CI，无法主机构建 |
| 重复 OpenSSL 代码 | `ws_client.c` 与 `xz_protocol.c`（https_post）各一份 | 维护两份 TLS 逻辑 |

目标：**CMake 双平台构建（t113 交叉 / host PC）+ OSAL 分层（将来移植 MCU）+ UI↔业务双向解耦
+ PC SDL 模拟器（host 产物可直接跑 UI）+ GitHub Actions CI**。

## 2. 分层架构

```
┌────────────────────────────────────────────────┐
│ main/main.c        入口：port 初始化 → UI 启动 → app_start → 主循环 │
├────────────────────────────────────────────────┤
│ ui/   LVGL 界面（唯一允许 include lvgl）        │ app_events.h（业务→UI 事件）
│       ui_facade 订阅事件总线 → 各页 on_xxx      │ app_api.h（UI→业务 意图接口）
├────────────────────────────────────────────────┤
│ app/  业务层：纯 C99（target 不给 LVGL include，  │
│       编译期强制分层）                           │
│   app/app.c  app_start/stop（wifi boot+协议编排）│
│   app/controller.c  xz 事件→总线分发            │
│   app/app_state.c   状态机（只发事件不摸 UI）    │
│   app/app_api.c     UI 意图入口                 │
│   app/net/  ws_client / xz_protocol / http_client│
│   app/audio/ audio_cap/play / opus / resample   │
│   app/wifi/ wifi_manager                        │
├────────────────────────────────────────────────┤
│ osal/ 纯 C99 接口头(include/) + posix 实现(posix/)│
│       thread/mutex/cond/atomic/time/net/tls/file/log │
├────────────────────────────────────────────────┤
│ port/ 板级平台层：port/linux(sunxifb+evdev)      │
│       port/sdl(PC 模拟器)  port_tick             │
├────────────────────────────────────────────────┤
│ third_party/ lvgl8.3.1 / lv_drivers / cjson / freetype(so) │
└────────────────────────────────────────────────┘
```

**分层铁律（编译器强制）**：`xz_app` 静态库的 include 路径不含 LVGL → app 层
`#include "lvgl/lvgl.h"` 直接编译失败；CI 兜底。

**明确不进 OSAL**：ALSA（audio_cap.h/audio_play.h 头本身就是音频 HAL 边界，MCU 侧换
audio_hal）、opus/speexdsp（算法非 OS 设施）、`system()`（wifi_manager 整体是 Linux
service，MCU 移植时换 wifi HAL）。

## 3. 目标目录结构与迁移映射

```
T113-XiaoZhi/
├── CMakeLists.txt  cmake/{toolchain-t113.cmake, deps.cmake}
├── scripts/{setup.sh, build.sh(新), deploy.sh}
├── main/main.c                     # 瘦身入口 ~120 行
├── osal/include/osal/*.h           # 9 个纯 C99 接口头
├── osal/posix/*.c                  # POSIX 参考实现（MCU 换实现不改业务）
├── app/                            # 业务层（纯 C99，禁 LVGL）
│   ├── app.c/.h  app_events.h  app_event_bus.c/.h
│   ├── app_api.c/.h  controller.c  app_state.c/.h  app_config.c/.h
│   ├── net/  ws_client / xz_protocol / http_client
│   ├── audio/ audio_cap / audio_play / opus_codec / resample
│   └── wifi/ wifi_manager
├── ui/                             # LVGL 层（唯一允许 lvgl）
│   ├── lv_conf.h                   # ← src/lv_conf.h
│   ├── ui_facade.c/.h  ui_res.c/.h（资源路径运行时化）
│   └── ui_msg / ui_navigate / ui_main / ui_face / ui_menu / ui_wifi_setup
├── port/
│   ├── port_display.h
│   ├── linux/ port_display.c（sunxifb+evdev+freetype） port_tick.c
│   └── sdl/  port_display.c（sdl_init+sdl_mouse_read+sdl_keyboard_read）
├── third_party/  assets/  tools/  docs/
└── .github/workflows/build.yml
```

| 现有文件 | 去向 |
|---|---|
| `src/main.c`(296) | 拆 3 份：`port/linux/port_display.c` + `port/linux/port_tick.c` + `main/main.c`；`xz_evt_async`/`wifi_boot_*`/`xiaozhi_start` → `app/controller.c`、`app/app.c` |
| `src/app_state.c/h` | `app/`（set() 改发事件，修跨线程 bug） |
| `src/lv_conf.h` | `ui/` |
| `src/net/*` | `app/net/`（pthread/poll/SSL→osal_*；https_post 抽 http_client） |
| `src/audio/*` | `app/audio/`（头签名不动，pthread/usleep→osal_*） |
| `src/wifi/wifi_manager` | `app/wifi/`（零依赖不动） |
| `src/ui/*` | `ui/`（直调点改 app_api/facade） |
| `Makefile` | P1 验证后删除 |
| `third_party/bt/`(22M 未链接) | 删除 |

## 4. CMake 设计

- **双平台**：`XZ_PLATFORM=t113|host`。t113 走 `cmake/toolchain-t113.cmake`
  （`toolchain/bin/arm-openwrt-linux-gcc`，`-march=armv7-a -mfpu=neon -mfloat-abi=hard`）；
  host 用系统 gcc。
- **STAGING_DIR**：OpenWrt wrapper gcc 编译期要读这个环境变量，`scripts/build.sh`
  export 后再调 cmake（唯一推荐入口，README 写明别裸跑 ninja）。
- **依赖解析**（cmake/deps.cmake，按平台）：
  | 依赖 | t113 | host(CI) |
  |---|---|---|
  | opus | `_libs/lib/libopus.a`（setup.sh 产物） | apt libopus-dev |
  | freetype | IMPORTED `third_party/freetype/lib/libfreetype.so` | find_package(Freetype) |
  | ssl/crypto/alsa/speexdsp | `XZ_STAGING_DIR`（默认从 `$ENV{TINA_SDK_PATH}` 推导） | 系统库（apt） |
  | lvgl | `file(GLOB_RECURSE third_party/lvgl/src/*.c)`，`-I ui` 让 lv_conf.h 生效（`__has_include` 自动 SIMPLE 模式） | 同左 |
  | lv_drivers | GLOB `display/*.c indev/*.c`；sdl 变体加 `sdl/sdl.c sdl_common.c`；`-DUSE_SDL/USE_SUNXIFB/USE_EVDEV` 切后端 | 同左 |
- **target 划分**：`lvgl` `lv_drivers` `cjson` `xz_osal` `xz_app` `xz_ui` STATIC +
  `xiaozhi` 可执行。`xz_app` 故意不给 LVGL include。
- **库顺序**沿用旧 Makefile：opus(静态)→freetype→ssl/crypto→asound/speexdsp→z/bz2/pthread/rt/dl→lm。
- **资源路径**：`XZ_RES_PATH` 编译定义（t113=`/mnt/UDISK/xiaozhi`，host=`${CMAKE_SOURCE_DIR}/assets`），
  由 `ui_res.c` 运行时拼接。

## 5. OSAL 接口（纯 C99，最少面）

```c
/* osal_log.h   */ XZ_LOGI/W/E(tag, fmt, ...) → osal_log(level, tag, ...)
/* osal_time.h  */ uint32_t osal_uptime_ms(void); uint64_t osal_uptime_ms64(void); void osal_sleep_ms(uint32_t ms);
/* osal_thread.h*/ typedef struct osal_thread *osal_thread_t; osal_thread_create/detach/join
/* osal_mutex.h */ osal_mutex_new/lock/unlock/free
/* osal_cond.h  */ osal_cond_new/wait(m, timeout_ms→1/0)/signal/free
/* osal_atomic.h*/ osal_atomic_set/get（替代 stdatomic，MCU 无 stdatomic.h）
/* osal_net.h   */ osal_net_tcp_connect(host,port,timeout_ms)/send/recv/poll(三态)/close/get_mac
/* osal_tls.h   */ osal_tls_connect(fd,host)/write(全量)/read/close   /* MCU 换 mbedtls 只改此文件 */
/* osal_file.h  */ osal_fopen/fread/fwrite/fclose
```

**行为保持铁律**：usleep/超时数值逐处等值替换；poll 三态（>0/0/<0）语义保留；
`PTHREAD_MUTEX_INITIALIZER` 静态初始化（ui_msg.c、audio_play.c）改 `osal_mutex_new()`
必须补显式 init 调用点。

## 6. UI↔业务解耦

### 业务 → UI：事件总线（复用 ui_msg 泵）

```c
/* app/app_events.h */
typedef enum { APP_EVT_STATE=1, APP_EVT_CHAT_USER, APP_EVT_CHAT_ASSISTANT,
               APP_EVT_SERVER_INFO, APP_EVT_ACTIVATION, APP_EVT_EMOTION,
               APP_EVT_NAVIGATE, APP_EVT_SESSION_INFO } app_evt_t;
int app_event_subscribe(app_evt_handler_t fn);        /* ui_facade init 时注册 */
int app_event_post(app_evt_t evt, const char *data, const void *aux, size_t aux_sz);
```

- `app/controller.c` = 原 main.c 的 `xz_evt_cb`/`xz_evt_async` 整体搬入，
  `ui_main_*` 直调全换 `app_event_post`。
- **修 bug**：`app_state_set()` 删协议线程直调 `ui_main_on_state`，改发 `APP_EVT_STATE`。
- app 主动导航（wifi boot 完成/配网成功）走 `APP_EVT_NAVIGATE`（携带 ui_page_id_t），
  ui_facade 收到后 `ui_nav_go`。

### UI → 业务：意图 API

```c
/* app/app_api.h（从现有 UI 调用点反推，blocking 风格，线程模型零变化） */
int  app_api_wifi_connected(void);
int  app_api_talk_start(void);  int app_api_talking(void);  void app_api_talk_stop(void);
void app_api_ensure_started(void);      /* 原 xz_app_ensure_protocol，消解倒挂 */
void app_api_wifi_setup_done(int connected);
void app_api_get_net_info(char *ip, int, char *ssid, int);
int  app_api_wifi_scan(app_wifi_ap_t*, int max);
int  app_api_wifi_connect(const char *ssid, const char *psk, int timeout_ms);
void app_api_wifi_save(const char *ssid, const char *psk);
int  app_api_wifi_state(void);
```

此后 **ui/ 不再 include xz_protocol.h / wifi_manager.h**；换业务只换 app/，
换 UI 只换 ui/（ui_facade 只依赖 app_events.h）。

## 7. PC 模拟器与 CI

- host 构建：`cmake -B build-host -DXZ_PLATFORM=host`，port/sdl 后端 480×640 SDL 窗口
  （鼠标=触摸、键盘=输入）。lv_drv_conf.h 全 `#ifndef` 守卫，`-DUSE_SDL=1
  -DUSE_SUNXIFB=0` 即切。
- 模拟器行为：WiFi 打不开 → 走菜单页路径，UI 可导航；协议不启动；资源路径指向仓库
  `assets/`（ui_res 运行时化）。
- `.github/workflows/build.yml`：push+PR 触发；ubuntu-latest 装
  `libasound2-dev libssl-dev libspeexdsp-dev libfreetype-dev libopus-dev zlib1g-dev
  libbz2-dev libsdl2-dev`；host 构建；upload-artifact 上传 `xiaozhi-sim-linux64`。
- 不加 `-Werror`（gcc-8/gc-13 警告集差异）；ARM 交叉 CI 不做（本地 `build.sh t113`
  是发布必过门）。

## 8. 阶段划分与验证

| 阶段 | 内容 | 验证 | 板测 |
|---|---|---|---|
| P0 ✅ | 本文档 | — | — |
| P1 CMake 化 ✅ | CMakeLists + toolchain/deps + build.sh；**src/ 布局不动**；删 Makefile；删 third_party/bt | t113 产物 `readelf -d` 12 项依赖与旧产物逐一一致 | 冒烟 |
| P2 重组+OSAL ✅ | git mv 迁移映射；osal 9 头+posix 实现；app 层机械替换；port 拆出 | 编译过 | **🧪 板测 1 ✅ 通过（2026-08-31）**：WiFi 自动重连（zgl 热点）、OTA、TLS+ws hello 握手、状态机驱动 UI、表情脸渲染（fb 抓帧）、**全链路语音对话**（说话→STT→回复→TTS 播放）全部正常。期间修复聊天区自动贴底 bug（见风险 9/10） |
| P3 模拟器+CI ✅ | port/sdl + ui_res + build.yml | 本机 SDL 窗口可导航（480×640 菜单页渲染正常，PC 无网卡走菜单页路径）；Actions 见仓库 | 不需 |
| P4 解耦 ✅ | 事件总线、controller、app_api、app_state 修 bug（协议线程不再直调 LVGL，见风险 11）、ui_facade、main 瘦身（205→113 行） | grep：ui/∩业务头=空（唯一命中是注释）；app 层无 lvgl include；双平台编译过 | **🧪 板测 2 ✅ 对话链路通过（2026-09-01）**：WiFi 重连→OTA→hello→说话→STT→情绪→两句 TTS 播放，事件经新总线分发行为与解耦前一致；气泡自动滚动无回归。配网全流程（扫密码错/超时分支）未单独重测——逻辑零改动仅换 API 名 |
| P5 配置统一 ✅ | app_config（数据目录 XZ_DATA_DIR 收敛：config.ini/wifi.cfg/diag dump 3 处归 1）；协议常量（OTA URL/UA/板型/默认 ws）进 app_config.h；ini 解析抽 app_config_load_ini；http_client.c 抽出（https_post 平移，行为逐行等价）；README 重写构建/目录段 | 改 config.ini 生效（路径解析逻辑未变，编译期验证 + 双平台构建过） | 复用板测 2 结论（config.ini 读取路径经 app_config_path，板上已验证同一文件） |

push 节奏：✅ P2/P3/P4 均已按节奏 push；P5 完成后 push。
push 走代理：`git -c http.proxy=http://192.168.1.4:7897 push origin main`。

## 9. 风险与坑（已核实）

1. **lv_conf.h**：必须 `-I ui`；漏配捡默认值（COLOR_DEPTH 16）→ 编译不报错但花屏。
2. **include 顺序**：`_libs/include`（openssl 1.1.1 头）必须先于 SDK staging 头。
3. **STAGING_DIR**：wrapper gcc 只认环境变量；build.sh export 为准。
4. **freetype**：IMPORTED 指 `.so` 本体（`.so.6` 软链不能给链接器）；`-lz -lbz2` 别丢。
5. **静态 mutex**（ui_msg.c/audio_play.c 的 PTHREAD_MUTEX_INITIALIZER）改
   osal_mutex_new 漏 init = 启动崩。
6. **事件时序**：解耦后成对事件合并为一条 post（ui_msg 队列深 32 丢最旧，防风暴丢面大）。
7. **gcc-13 vs gcc-8** 新警告：host 平台单独 `-Wno-*`，不污染 t113 flag。
8. **remote URL 内嵌 PAT**：建议后续吊销换 SSH/credential helper（本次不动）。
9. **LVGL 滚动贴底**（板测 1 踩中）：`lv_obj_get_scroll_bottom()` 走子对象 `coords.y2`
   路径，coords 随容器滚动被平移 → 滚过一次后每次算出的贴底量带偏差（表现为只自动滚
   前一两条）。正确做法：`update_layout` 后用**滚动不变量** `内容高 − 视口净高` 算贴底量；
   首条消息时容器刚从 HIDDEN 转可见、同拍几何是旧值，需再 `lv_async_call` 延迟一拍幂等重滚。
10. **app 自动重连窗口 vs wpa 首次扫描竞争**（板测 1 踩中）：app 启动时若 wpa_supplicant
    还在 SCANNING/ASSOCIATING，重连窗口（~20s）耗尽即判失败进配网页——即使热点可见。
    绕过：先等 WiFi 连上再起 app。根治（P4/P5 候选）：重连改持续后台重试或加长窗口。
11. **lv_drv_conf.h 的 SDL_HOR/VER_RES 无守卫**：-D 覆盖会撞重定义——已加 `#ifndef`
    （本项目改动，升级 lv_drivers 时注意保留）。显示后端宏（USE_SDL/USE_SUNXIFB/
    USE_EVDEV/SDL_HOR_RES）必须**全局** `add_compile_definitions`：port_display.c 与
    lv_drivers 库要看到同一套宏，只给 lv_drivers target 加会 sdl.h 声明全被守卫裁掉。
12. **静态初始化器不能放函数调用**：`static lv_ft_info_t ft = {.name = ui_res_font(...)}`
    非常量初始化非法（gcc 会拒）。ui_res 运行时化后改为函数体内赋值（port_display.c）。
13. **ui_res 的两条取径 API**：`ui_res_img()` 返回带 `S:` 盘符的 LVGL 路径（走 lv_fs），
    `ui_res_font()` 返回真实路径（FreeType 走标准 fopen，不能带盘符）。且两者共用单
    静态缓冲——**别在同一次调用里混用两个返回值**（后调覆盖前调）。
