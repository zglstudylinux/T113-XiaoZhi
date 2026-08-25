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

### M0 — 骨架 + 板上录音自检（先测麦克风）✅ 完成（2026-08-20）
- [x] 建目录、复用 bt-speaker 的 third_party（lvgl/lv_drivers/freetype）+ toolchain setup
- [x] setup.sh 交叉编译 opus-1.3.1 → `_libs/libopus.a`（ARM ELF32 + NEON）
- [x] 最小 LVGL 界面上板验证（fb 抓帧确认）
- [x] 录音自检完成，**麦克风调通**，结论：
  - 硬件麦在 **MIC3/ADC3**（dts 注释正确）
  - **采集必须用 `default` 设备**（`arecord` 不带 `-D`）：板上 `/etc/asound.conf`
    已定义 `pcm.!default` capture → `CaptureMic`（plug + `ttable.0.2 1` 把硬件
    ch2=ADC3 映射到用户 ch0，hooks 自动开 MIC3 Switch）→ `CaptureDsnoop`
    （16k 3ch dsnoop 共享）。**直接 `-D hw:audiocodec,0 -c 1` 只会采到 ADC1（悬空底噪）**
  - 必开设置：`ADC HPF0/1 Mode=On`（滤直流，否则 DC≈550）、`ADC3 Gain=31`
    （MIC3 差分输入 `MIC3 Input Select=differ`）
  - 验证数据：说话 rms 87→824（9.5 倍起伏）、peak 4710；全双工（aplay 同时
    arecord）正常，喇叭回声可被麦采到（后续 AEC 可用 speexdsp）
- **发现**：板上 libspeexdsp.so opkg 有记录但文件缺失（烧录镜像问题），deploy.sh
  已加推送步骤

### M1 — WiFi 联网 + 配网 UI ✅ 完成（2026-08-21）
- wifi_manager：**自实现** wpa_supplicant ctrl 接口（unix DGRAM + 文本协议；板上无 libwpa_client.so）
  - 启动链：insmod 8723ds.ko（开机不自动加载）→ wpa_supplicant `-O /var/run/wpa_supplicant`
    （本板 conf 无 ctrl_interface 行，不带 -O 则 ctrl socket 不存在）→ ctrl 命令/事件双通道
  - API：scan（解析+去重+按 RSSI 排序）/connect（REMOVE→ADD→SET→ENABLE→SELECT→轮询）/dhcp（udhcpc）
- ui_wifi_setup：扫描线程 → AP 列表（flex column）→ 密码弹窗 + lv_keyboard → 连接线程 →
  lv_async_call 回 UI；成功存 `/mnt/UDISK/xiaozhi/wifi.cfg`
- main：启动过渡页 → wifi 后台线程（auto_connect 成功→主界面 / 失败→配网页）
- **板上验证**：扫描 3 AP → 选 zgl 输密码 → COMPLETED + DHCP 10.235.38.79 → ping 通 →
  重启 app 自动重连（"已处于连接状态，跳过重连"）→ 直接进主界面
- **坑**：
  1. ctrl_open 本地 bind 路径用 pid+tid 生成，同线程两次 open 路径相同 → 第二个 socket
     bind 失败返回 -1 → 命令通道静默丢（recv EAGAIN 超时）。改静态计数器后缀修复
  2. auto_connect 无条件 REMOVE 重连会断开已连接的网络；先 poll STATUS 已 COMPLETED
     且 ssid 匹配则直接复用（补 DHCP）
  3. connect 轮询不能只依赖事件线程状态（CONNECTED 事件可能在轮询开始前到达丢失），
     每轮主动发 STATUS

### M2 — WebSocket 连通（hello 握手）✅ 完成（2026-08-25）
- ws_client.c：RFC6455 over OpenSSL（原生 socket+poll 连接 → SSL_connect → BIO_f_ssl IO）
  - 客户端 mask、ping/pong、close 帧、**20s ping 保活**、断线重连指数退避 1→30s
  - 收包重组用 **parse_off 游标**（不搬移数据；最初两步 memmove 版本在"binary 帧后跟
    text 帧"时会把帧 2 长度读成 payload 字节值——x86 单测复现后重写）
- xz_protocol.c：**OTA 设备注册**（POST `api.tenclass.net/xiaozhi/ota/`）→ hello 握手
  - OTA 响应含 `websocket.token`、`activation.code`（未绑定设备的 6 位验证码，显示到屏幕）
  - cJSON 1.7.13（SDK aliyun-link-sdk 内 vendored，third_party/cjson/ 编入工程）
- **板上验证**：OTA 注册 → 验证码 959311 屏显 → 用户 xiaozhi.me 控制台绑定 →
  ws hello 握手成功（session_id 往返，下行 audio_params 24kHz opus 60ms）→
  空闲被踢自动重连恢复；UI 显示"服务器已连接 + session_id"（fb 抓帧确认）
- **坑（重要）**：
  1. **Device-Id 必须小写 MAC**：服务器 OTA 注册按小写记账（响应里 mqtt client_id
     就是 `34_75_63_29_c5_55`），握手发大写 → 未注册设备 → 101 后立即空 close。
     PC 端 systematic 探测才定位到（doc 示例 MAC `11:22:33:44:55:66` 存活、我们的
     大写 MAC 被踢、同一 MAC 小写存活）
  2. **OTA POST 必须带 User-Agent**：缺失返回 400 "Invalid board type or app
     version"（误导性报错，实为 UA 校验）
  3. **board.type 必须官方已注册板型**：用 `bread-compact-wifi`（面包板），
     `zgl_board`/自拟名 400
  4. **Client-Id 必须 UUID 格式**：非 UUID（如 "t113-xiaozhi-0001"）→ "Invalid client ID"
  5. **未绑定设备的 ws 会被立即 close**：必须先 OTA 拿验证码 → 用户控制台绑定 →
     才允许会话。绑定后 OTA 响应不再含 activation 字段（可作为绑定状态判据）
  6. **TLS IO 必须走 BIO_f_ssl 包装的 SSL BIO**：拿 `SSL_get_wbio()`（socket BIO）
     直接读写会绕过加密层发明文，服务器握手成功后收到明文 HTTP 直接断连
  7. 服务器空闲 ~60-90s 踢连接（即使 ping 保活也踢 hello-only 的会话，可能
     要求 listen 开启音频会话）；重连 1-2s 内恢复，M3 接入音频后按需重连

### M3 — 语音链路上行（说话 → 服务器识别）✅ 完成（2026-08-25）
- audio_cap：ALSA `default` 设备 capture 线程（16k S16_LE mono，60ms=960 样本/帧）
  - mixer 自动恢复（snd_ctl API，**必须设 SND_CTL_ELEM_IFACE_MIXER**，否则找不到控件）：
    `ADC HPF0/1 Mode=On` + `ADC3 Gain=31`（断电回 dts 默认 HPF Off/Gain 19）
  - 请求 period 320 样本被 plug 协商到 640（asound.conf CaptureDsnoop 固定值），
    readi 按 320 读，攒 3 个周期 = 960 样本回调一帧
- opus_codec：libopus encoder（VOIP/32kbps/complexity 5），每次 talk_start 创建、stop 销毁
- xz_protocol talk 会话：按钮 → listen start(auto) → capture 起 → 回调编码+binary 上行；
  stt 到达或再点按钮 → talk_stop（停采集 + listen stop）
- UI：说话按钮（聆听中变红/STOP 图标）、stt 绿色气泡（右对齐）、tts 白色气泡、
  气泡自动滚底、上限 8 条防内存涨
- **板上验证**：说"喂喂喂，你好，小四能听到吗？喂喂喂"→ stt 一字不差 →
  AI 回复"听得见听得见啦～刚才信号好像有点卡，吓我一跳。怎么了吗？"（含 llm 表情）；
  fb 抓帧确认气泡显示正确
- **坑（重要，排查一晚上）**：
  1. **SSL 对象并发读写是根因大坑**：ws 线程 SSL_read（1s SO_RCVTIMEO 阻塞中）+
     capture 线程 SSL_write 并发 → OpenSSL SSL 对象状态机被破坏 → SSL_read 返回
     `SSL_ERROR_SYSCALL` + errno=EAGAIN（实际只是读超时！）→ 被当成"对端关闭"
     退出收包循环 → 主动关 socket。表现为：**listen start 后 ~0.5-1s 必断**，
     与音频内容无关（发假帧也断）。修复：所有 BIO 读/写共持 `io_lock` 互斥；
     读路径先 poll socket 可读（不持锁）再持锁短读，避免饿死写线程
  2. **诊断路径**：PC 端纯标准库 raw-socket ws 探测脚本（/tmp/probe_listen.py 模式）
     对照排除法定位——PC 同协议同假帧不断 → 板上假帧也断 → 排除服务器/协议/音频内容
     → 加 errno 日志抓到 EAGAIN → 锁定 SSL 并发
  3. **make CFLAGS=... 覆盖会污染 .o**：临时诊断宏（DIAG_FAKE_FRAMES）编进
     xz_protocol.o 后，普通 make 因 .c 未变不重编 → 正式版继续发假帧。
     诊断构建要用独立产物名或 touch 源文件
  4. UDISK 空间紧张（36MB 分区，speaker 12.3M + xiaozhi 二进制 4.2M×2 份满了）：
     字体 8.4MB 与 speaker 重复 → xiaozhi/fonts 软链到 speaker/fonts；
     deploy.sh 加了失败回退软链逻辑；诊断二进制用完即删
  5. 服务器 listen 后 ~20s 无音频会断会话（假帧 45s 匀速发送验证存活）
  6. 服务器对 hello-only 空闲会话 ~60-90s 踢（M2 已知），M3 修复 SSL 并发后
     重连稳定，stt/tts 流程正常触发

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
