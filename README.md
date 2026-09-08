# T113 AI 小智（LVGL8 + WebSocket 语音助手）

基于 LVGL 8.3 的 Allwinner T113 AI 语音助手（参考虾哥 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)）：
WiFi 联网，点按开始说话，服务器 ASR/LLM/TTS，板子喇叭回答，480×640 屏显示对话。

- 硬件：T113-S3（zgl_board），D240N2501V1 屏（480×640），dx_touch 触摸，RTL8723DS WiFi/BT
- 音频链路：MIC3/ADC3 → ALSA default 设备采集(16k mono) → Opus 60ms 帧 → WebSocket(wss) 上行；
  下行 Opus → 解码 → 重采样 → hw:audiocodec 喇叭
- 屏显链路：sunxifb(/dev/fb0 32bpp) + evdev(/dev/input/event4)，FreeType 中文字体

## 目录结构（R2：component/platform 排版，对齐 app_sdk）

```
├── build.sh               # 唯一构建入口（t113|host|clean，export STAGING_DIR）
├── CMakeLists.txt         # 平台二选一 + third_party 库 + 链接契约
├── main/main.c            # 入口（纯装配：port → facade → 启动页 → 主循环）
├── app/                   # 业务编排（纯 C99，禁 LVGL——编译期强制分层）
│   ├── app_api.c/.h       # UI→业务唯一入口
│   ├── app_events.h + app_event_bus.c   # 业务→UI 事件总线
│   ├── app_state.c        # 状态机（发事件不摸 UI）
│   ├── controller.c       # 协议事件分发
│   └── app_config.c/.h    # 数据目录 + 协议常量收敛
├── component/             # 平台无关组件（各带 CMakeLists 出 STATIC 库）
│   ├── osal/              # 纯 C99 接口头(include/) + POSIX 实现(posix/)
│   ├── net/               # ws_client / xz_protocol / http_client
│   ├── audio/             # audio_cap / audio_play / opus_codec / resample
│   ├── wifi/              # wifi_manager（wpa_ctrl 封装）
│   └── hal/port_display.h # 显示/输入 HAL 接口头
├── platform/              # 平台层（按平台二选一编译）
│   ├── t113/              # t113.cmake 交叉工具链文件 + port/(sunxifb+evdev)
│   │   ├── toolchain/     # 裁剪版 gcc-8.3.0 glibc armhf（入库，克隆即编）
│   │   ├── include/ lib/  # openssl/alsa/opus/speex/freetype 头与预编译库
│   │   └── lib/README     # libopus.a 交叉编译配方留档
│   └── host/port/         # SDL 模拟器后端（PC/CI）
├── ui/                    # LVGL 界面（唯一允许 include lvgl）
│   ├── ui_facade.c        # 业务事件 → UI 分发（唯一订阅点）
│   └── ui_msg / ui_navigate / ui_main / ui_face / ui_menu / ui_wifi_setup / ui_res
├── third_party/           # lvgl 8.3.1 / lv_drivers / cjson（vendored 纯源码）
├── assets/                # fonts + images + config.ini（板上推到 /mnt/UDISK/xiaozhi）
├── scripts/deploy.sh      # adb 部署
├── tools/gen_assets.py    # PIL 素材生成器
└── docs/                  # plan.md（里程碑踩坑）+ architecture-refactor.md（重构方案）
```

## 构建（CMake，双平台）

```bash
./build.sh t113        # 交叉构建 → build-t113/xiaozhi（工具链/库全在仓库，克隆即编）
./build.sh host        # PC 模拟器（SDL 480×640，鼠标=触摸）→ build-host/xiaozhi
```

注意：必须经 `build.sh`（wrapper gcc 的 specs 要求 STAGING_DIR 环境变量，
脚本负责 export；裸跑 `cmake --build` 会报
`environment variable 'STAGING_DIR' not defined`）。

CI：GitHub Actions（`.github/workflows/build.yml`）双 job——
- `host-sim`：模拟器构建 + xvfb 冒烟，artifact `xiaozhi-sim-linux64.tar.gz` 解压即跑；
- `t113-cross`：交叉编译（零外部依赖）+ NEEDED 12 项依赖契约比对，artifact `xiaozhi-t113-armhf`。

## 部署运行

```bash
./scripts/deploy.sh    # adb push 产物/字体/素材/库 + start-stop-daemon 启动（与蓝牙音箱互斥）
```

## 里程碑状态

- [x] M0：骨架 + opus 交叉编译 + 最小 UI 上板 + **麦克风调通**
  - 麦在 **MIC3/ADC3**；采集必须用 `default` 设备（asound.conf 的 ttable 路由），
    `-D hw:audiocodec,0 -c 1` 只能采到悬空的 ADC1
  - 必开：`ADC HPF0/1=On`（滤直流 ~550）、`ADC3 Gain=31`
  - 板侧自测：`arecord -d 5 x.wav && aplay x.wav`（先按上面 amixer 设置）
  - 全双工验证 OK（aplay 同时 arecord，回声可采，AEC 可后续用 speexdsp）
- [x] M1：WiFi 联网 + 配网 UI（扫描列表 + 键盘输密码 + 自动重连）
- [x] M2：WebSocket 连通 xiaozhi.me（OTA 设备注册 + 激活验证码 + hello 握手 + session_id）
  - 设备身份：Device-Id=**小写** wlan0 MAC、Client-Id=UUID、board.type=bread-compact-wifi
  - OTA 注册（POST /xiaozhi/ota/，必须带 User-Agent）→ 未绑定设备屏显 6 位验证码 →
    xiaozhi.me 控制台绑定 → ws hello 握手（服务器下行 24kHz opus）
- [x] M3：语音上行（采集→opus→ws，服务器 VAD 断句，stt 显示）
  - 采集：ALSA default 设备 16k mono（自动恢复 HPF/增益 mixer 设置）→ opus 60ms 帧上行
  - **SSL 并发坑**：SSL_read 与 SSL_write 并发会破坏 SSL 状态机（表现为假"断连"），
    所有 BIO 读写共持 io_lock 互斥（详见 plan.md M3）
  - 实测：对板说话 → stt 识别一字不差 → AI 回复文本气泡显示
- [x] M4：语音下行（TTS opus 解码→喇叭播放）
  - 下行链路：ws binary opus → 解码(24k) → speexdsp 重采样(→48k) → ALSA `default`
    （plug→softvol→dmix→hw，48k 立体声，mono 交织 L=R）→ 喇叭
  - **播放必须走 `default`**：直开 `hw:audiocodec` 16k mono 有可闻滋滋噪声（模拟
    通路问题，同数据同增益）；tts stop 后关设备消除空闲底噪
  - 实测：说话 → 喇叭清晰播出 AI 回复（无噪声）+ 屏幕气泡文本
- [x] M5：产品化 UI + 界面导航（2026-08-30）
  - **卡通表情界面**：夜空背景 + 机器人表情脸（`tools/gen_assets.py` PIL 生成，
    18 张脸：待命/聆听声波环/思考/说话嘴型动画 + 8 种情绪，随设备状态与
    服务器 llm 情绪实时切换）+ 半透明聊天气泡 + 胶囊说话按钮
  - **界面导航**：启动菜单页（AI 小智 / WiFi 设置，可扩展）；统一切屏管理
    `ui_navigate`（leave 回调 + auto_del，防界面重叠）；各页返回按钮
  - **状态感知**：无 WiFi 显示"未连接 WiFi"+一键去配网；未绑定设备大字显示
    6 位验证码；配网页键盘选中网络后才弹出
  - **稳定性大修**（gdb + core dump 定位）：`lv_async_call` 非线程安全（LVGL 堆
    并发分配损坏）→ 线程消息改走 `ui_msg` 互斥队列；修复扫描消息 free 静态缓存
    导致的堆损坏；修复菜单/配网路径协议线程未启动
- [ ] M5 剩余：与蓝牙音箱切换 + 开机自启
- [x] R1 架构重构（2026-09-01，CMake/OSAL/解耦/模拟器/CI，详篇见
  [`docs/architecture-refactor.md`](docs/architecture-refactor.md)）：
  - **CMake 双平台**：`build.sh t113|host`，旧 Makefile 删除
  - **OSAL 分层**：9 接口头 + POSIX 实现，业务层零 POSIX 直接调用
  - **UI↔业务解耦**：ui 只走 app_api（意图）+ app_event_bus（事件），
    ui/ 无业务头引用、app/ 无 LVGL 引用（编译期强制）
  - **PC SDL 模拟器**：host 产物 480×640 窗口，资源路径运行时解析（ui_res）
  - **修复存量 bug**：协议线程直调 ui_main_on_state（LVGL 线程违规）改事件总线
- [x] R2 目录重排 + 工具链入库 + 交叉编译 CI（2026-09-08，对齐 app_sdk 排版）：
  - **component/platform 排版**：osal/net/audio/wifi 出 STATIC 组件库；
    port 与预编译库归 platform/{t113,host}；源码 #include 零修改完成迁移
  - **工具链入库**：gcc-8.3.0 glibc armhf 裁剪 1.2G→148M 提交进
    `platform/t113/toolchain/`（wrapper 可重定位；TINA_SDK_PATH/staging 依赖全消灭）
  - **交叉编译 CI**：t113-cross job 零 apt 依赖，NEEDED 12 项依赖契约比对
  - 验证：NEEDED 与 R1 产物逐项一致、ELF32/ARM/VFPv3、host 模拟器冒烟过

## 已知事项

- 板上 `libspeexdsp.so` opkg 有记录但文件缺失（烧录镜像问题），deploy.sh 每次自动推送
  （R2 起推 `platform/t113/lib/libspeexdsp.so.1.5.1`，不再依赖 Tina SDK staging）
- 与蓝牙音箱项目互斥（抢 fb0/event4），deploy.sh 会先杀 bt_speaker
- 板侧 mixer 设置断电回 dts 默认（HPF Off / Gain 19），M3 的 audio_cap 会代码内自动设置

📖 **详细计划与踩坑记录**：[`docs/plan.md`](docs/plan.md)
