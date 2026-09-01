# T113 AI 小智（LVGL8 + WebSocket 语音助手）

基于 LVGL 8.3 的 Allwinner T113 AI 语音助手（参考虾哥 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)）：
WiFi 联网，点按开始说话，服务器 ASR/LLM/TTS，板子喇叭回答，480×640 屏显示对话。

- 硬件：T113-S3（zgl_board），D240N2501V1 屏（480×640），dx_touch 触摸，RTL8723DS WiFi/BT
- 音频链路：MIC3/ADC3 → ALSA default 设备采集(16k mono) → Opus 60ms 帧 → WebSocket(wss) 上行；
  下行 Opus → 解码 → 重采样 → hw:audiocodec 喇叭
- 屏显链路：sunxifb(/dev/fb0 32bpp) + evdev(/dev/input/event4)，FreeType 中文字体

## 目录结构（P2 重构后分层）

```
├── CMakeLists.txt  cmake/  scripts/    # CMake 双平台构建（t113 交叉 / host PC）
├── main/main.c           # 入口（纯装配：port → facade → 启动页 → 主循环）
├── app/                  # 业务层（纯 C99，禁 LVGL——编译期强制分层）
│   ├── app_api.c/.h      # UI→业务唯一入口
│   ├── app_events.h + app_event_bus.c   # 业务→UI 事件总线
│   ├── app_state.c       # 状态机（发事件不摸 UI）
│   ├── controller.c      # 协议事件分发
│   ├── app_config.c/.h   # 数据目录 + 协议常量收敛（P5）
│   ├── net/              # ws_client / xz_protocol / http_client
│   ├── audio/            # audio_cap / audio_play / opus_codec / resample
│   └── wifi/             # wifi_manager（wpa_ctrl 封装）
├── ui/                   # LVGL 界面（唯一允许 include lvgl）
│   ├── ui_facade.c       # 业务事件 → UI 分发（唯一订阅点）
│   └── ui_msg / ui_navigate / ui_main / ui_face / ui_menu / ui_wifi_setup / ui_res
├── osal/                 # OSAL：纯 C99 接口头(include/) + POSIX 实现(posix/)
│                         # MCU 移植 = 换 posix/ 目录实现
├── port/                 # 板级平台层：linux/(sunxifb+evdev)  sdl/(PC 模拟器)
├── third_party/          # lvgl 8.3.1 / lv_drivers / freetype / cjson（vendored）
├── assets/               # fonts + images + config.ini（板上推到 /mnt/UDISK/xiaozhi）
├── tools/gen_assets.py   # PIL 素材生成器
└── docs/                 # plan.md（里程碑踩坑）+ architecture-refactor.md（重构方案）
```

## 构建（CMake，双平台）

```bash
# 板上产物（ARM 硬浮点，需先跑 scripts/setup.sh 准备 toolchain/ 与 _libs/）
TINA_SDK_PATH=<Tina SDK 路径> ./scripts/setup.sh
./scripts/build.sh t113        # → build-t113/xiaozhi
                               # 注意：必须经本脚本（wrapper gcc 要 STAGING_DIR env）

# PC 模拟器（SDL 480×640 窗口，鼠标=触摸）
./scripts/build.sh host        # → build-host/xiaozhi（资源自动找 ../assets）
```

CI：GitHub Actions（`.github/workflows/build.yml`）host 构建 + xvfb 冒烟，
artifact 下载 `xiaozhi-sim-linux64.tar.gz` 解压即跑（`tar xzf && ./xiaozhi-sim/xiaozhi`）。

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
  - **CMake 双平台**：`scripts/build.sh t113|host`，旧 Makefile 删除
  - **OSAL 分层**：osal/ 9 接口头 + POSIX 实现，业务层零 POSIX 直接调用
  - **UI↔业务解耦**：ui 只走 app_api（意图）+ app_event_bus（事件），
    ui/ 无业务头引用、app/ 无 LVGL 引用（编译期强制）
  - **PC SDL 模拟器**：host 产物 480×640 窗口，资源路径运行时解析（ui_res）
  - **修复存量 bug**：协议线程直调 ui_main_on_state（LVGL 线程违规）改事件总线

## 已知事项

- 板上 `libspeexdsp.so` opkg 有记录但文件缺失（烧录镜像问题），deploy.sh 每次自动推送
- 与蓝牙音箱项目互斥（抢 fb0/event4），deploy.sh 会先杀 bt_speaker
- 板侧 mixer 设置断电回 dts 默认（HPF Off / Gain 19），M3 的 audio_cap 会代码内自动设置

📖 **详细计划与踩坑记录**：[`docs/plan.md`](docs/plan.md)
