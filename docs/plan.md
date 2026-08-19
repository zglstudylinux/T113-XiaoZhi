# T113 AI 小智（LVGL8 + WebSocket 语音助手）项目计划

> 2026-08-19 与用户确认的计划。里程碑制：每完成一个功能 → 用户上板确认 → commit/push。

## Context

在 `/home/zgl/SDK/T113_SDK/T113-XiaoZhi` 建独立项目，基于 LVGL8 做"AI 小智"语音助手
（参考虾哥 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)）：WiFi 联网、480×640 显示、
麦克风输入、喇叭音频输出。参考 `/home/zgl/SDK/app_sdk` 的代码。

**已确认的决策**：
- 服务器：**先接虾哥官方 xiaozhi.me**（token 官网获取）；自建 xiaozhi-esp32-server 后续可加
  （设备端只需改 config 里的 ws_url + token）
- 触发方式：**先做点按一次开始、服务器 VAD 自动断句（listen mode=auto）**；连续对话 realtime 后续可加
- 麦克风：**不确定板上有没有接**，M0 先录音自检（arecord 抓 5 秒分析），根据结果定采集设备
- 与蓝牙音箱项目共存：**两个 app 都常驻可切换**——初期互斥启动（跑谁推谁），切换器后期做

## 板上/SDK 现状（已探明）

| 依赖 | 状态 | 位置 |
|---|---|---|
| libssl/libcrypto 1.1.1n | ✅ rootfs 已有 | `/usr/lib/libssl.so.1.1`（wss 直接链接） |
| libasound + arecord/aplay/amixer | ✅ rootfs 已有 | alsa-lib 1.1.8 + alsa-utils 1.2.4 |
| libspeexdsp 1.5.1 | ✅ rootfs 已有 | AEC/降噪/重采样可用 |
| libopus | ❌ 无 | 源码包 `openwrt/dl/opus-1.3.1.tar.gz` 已在 SDK，gcc-830 交叉编译静态链入 |
| WiFi 配网 | ✅ rootfs 已有 | `wifi -c ssid passwd`（wifimanager）/ wpa_supplicant / wpa_cli |
| LVGL 8.3.1 + lv_drivers + toolchain gcc-830 | ✅ 从 T113-bluetooth-speaker 整体复用 | `third_party/` + `toolchain/` + Makefile |

**codec 采集能力**（`kernel/linux-5.4/sound/soc/sunxi/sun8iw20-codec.c`）：内置 codec 3 路 ADC，
capture 1–3 通道 8k–48k（16k 有专门配置），zgl_board dts `adc3` 注释"接麦克风"（实物待测）；
另有 snddmic 声卡（capture-only）。**播放** hw:audiocodec 已在蓝牙音箱项目验证出声。

## 小智 WebSocket 协议要点（[官方协议文档](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md)）

- **握手**：GET wss://… + headers `Authorization: Bearer <token>`、`Protocol-Version: 1`、
  `Device-Id: <MAC>`、`Client-Id: <UUID>`；连上后发 hello JSON，等服务器 hello（含 session_id）
- **hello（设备→服务器）**：
  `{"type":"hello","version":1,"features":{"mcp":false},"transport":"websocket","audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration":60}}`
- **上行**：`{"type":"listen","state":"start","mode":"auto","session_id":...}` 后持续发**裸 Opus 帧（v1）**，
  每帧 60ms（16kHz mono）；停止 `state:"stop"`；打断 `{"type":"abort","reason":"..."}`
- **下行**：tts start / sentence_start(text) / stop、stt(text)、llm(emotion)、二进制 opus 帧
  （按服务器 hello 的 audio_params 自适应采样率，可能 24k，需重采样到播放率）
- **状态机**：idle → connecting → listening → speaking → (auto)listening → … idle
- **鉴权**：Authorization Bearer token（xiaozhi.me 官网获取）

## 架构

```
src/
├── main.c                # 入口：LVGL 初始化（复用 bt-speaker 的 main）+ 主循环
├── lv_conf.h             # 基线=已验证配置（fb0 32bpp + FreeType + PNG + POSIX FS）
├── app_state.c/.h        # 小智设备状态机（idle/connecting/listening/speaking/error）+ 事件分发
├── net/
│   ├── ws_client.c/.h    # 手写 WebSocket 客户端（RFC6455：握手/分帧/mask/ping-pong）over OpenSSL BIO
│   └── xz_protocol.c/.h  # 小智协议层：hello/listen/tts/stt/llm/abort JSON + opus 帧收发 + session_id
├── audio/
│   ├── audio_cap.c/.h    # ALSA capture 线程（16k S16_LE mono，周期 60ms 对齐帧）
│   ├── audio_play.c/.h   # ALSA playback：解码 PCM 环形缓冲 + 播放线程（hw:audiocodec）
│   ├── opus_codec.c/.h   # libopus 封装（encoder/decoder，60ms 帧）
│   └── resample.c/.h     # speexdsp 重采样（下行 24k→16k 如需要）
├── wifi/
│   └── wifi_manager.c/.h # 扫描/连接/状态回调（wpa_ctrl 接口，参考 app_sdk wpa_manager.c）
└── ui/
    ├── ui_main.c         # 480×640 主界面：状态 + 对话气泡（stt/tts 文本）+ 按钮区
    └── ui_wifi_setup.c   # WiFi 配网页（扫描列表 + lv_keyboard 密码输入）
_libs/                    # opus 交叉编译产物（libopus.a + 头文件），setup 脚本自动构建
scripts/
├── setup.sh              # 复制 toolchain + 交叉编译 opus-1.3.1 → _libs/
├── deploy.sh             # adb push app/字体 → /mnt/UDISK/xiaozhi/ + 启动
└── test_alsa.sh          # 板上录音自检：arecord 5s 多设备/通道 → 拉回分析
```

**线程模型**（沿用 app_sdk / bt-speaker 验证过的模式）：
- 主线程 = LVGL `lv_task_handler` 循环
- capture 线程：ALSA capture → opus 编码 → ws 发送（仅 listening 状态）
- playback 线程：opus 解码 → 环形缓冲 → ALSA 播放
- ws 线程：阻塞 recv + 分发（JSON → 状态机；binary → 解码播放队列）
- 跨线程到 UI 一律 `lv_async_call`（bt-speaker 已验证）

## 里程碑

### M0 — 骨架 + 板上录音自检（先测麦克风）← 当前
- [x] 建目录、复用 bt-speaker 的 third_party（lvgl/lv_drivers/freetype）+ toolchain setup
- [ ] setup.sh 交叉编译 opus-1.3.1 → `_libs/libopus.a`
- [ ] 最小 LVGL 界面："AI 小智 480×640" + 中文（FreeType）
- [ ] `scripts/test_alsa.sh`：arecord 5s（audiocodec 各通道 + snddmic）→ adb pull → 分析能量/底噪，确定采集设备
- **验证**：屏幕显示正常 + 录音分析结论 → 用户确认 → push 初版
- **分支**：若板上没接麦克风 → M0 停在"UI 骨架 + opus 编译通过"，等用户接麦再继续

### M1 — WiFi 联网 + 配网 UI
- wifi_manager（wpa_ctrl 封装）：扫描列表、连接、状态回调
- ui_wifi_setup：扫描列表 + 密码 textarea + lv_keyboard；连接成功存 `/mnt/UDISK/xiaozhi/wifi.cfg`，下次自动重连
- **验证**：屏幕选 WiFi 输密码 → 板子拿到 IP、ping 通外网 → 确认 → push

### M2 — WebSocket 连通（hello 握手）
- ws_client.c：RFC6455 over OpenSSL BIO（mask、ping/pong、close、fragment）
- 连 xiaozhi.me（URL+token 在 `/mnt/UDISK/xiaozhi/config.ini`：ws_url/token/device_id/mac）
- xz_protocol：发 hello、收服务器 hello（存 session_id）、断线重连（指数退避）
- **验证**：UI 显示已连接 + session_id；log 显示握手 JSON 往返 → 确认 → push

### M3 — 语音链路上行（说话 → 服务器识别）
- audio_cap：ALSA capture 线程（M0 确定的设备）16k mono → opus 60ms 帧
- listen start(auto) → 发 opus 帧 → 服务器 VAD 判停 → 收 stt 文本
- UI：说话按钮 + stt 文本气泡
- **验证**：对板说话 → 屏幕显示识别文字 → 确认 → push

### M4 — 语音链路下行（TTS 播放）
- audio_play：opus 解码（可能 24k）→ 重采样 → 环形缓冲 → ALSA 播放
- tts start/sentence_start/stop 状态机、auto 回 listening
- **验证**：说话 → 喇叭出声回答 + 文本显示 → 确认 → push

### M5 — 产品化 UI + 收尾
- 完整聊天界面（用户/AI 气泡、自动滚动、状态、音量、emoji）
- 与 bt_speaker 切换（app_switcher / 开机 chooser / rc.final 自启）
- README/docs 收尾、config.ini 参数化（可换自建服务器）
- **验证**：重启自动进小智界面，与蓝牙音箱可切换 → 确认 → push

## 关键风险与预案

- **麦克风没接**：M0 自检发现 → 等硬件；或临时用 DMIC
- **xiaozhi.me 激活码/排队**：官网获取激活码填 config.ini；排队不影响协议验证
- **libopus 交叉编译**：configure 注意 `--host` + `cc` 指向 gcc-830 wrapper（需 STAGING_DIR）
- **手写 ws over TLS**：先在 x86 对 python websockets 测通再上板
- **全双工冲突**：单 codec 同时 capture+playback 若驱动不支持 → 半双工（speaking 时停 capture）
- **线程安全**：opus encoder/decoder 各线程独享；ws send 互斥锁
- **RAM**：T113 64MB 够用；大文件放 /mnt/UDISK
- **两 app 抢 fb0/event4**：互斥启动，后期 chooser

## 验证方式（贯穿）

- 构建：`./scripts/setup.sh && make -j`
- 部署：`./scripts/deploy.sh`（adb push + start-stop-daemon）
- 板侧：用户肉眼/听感确认；log 由 Claude 经 adb 抓取；fb 抓帧分析（BGRX）复用 bt-speaker 经验
