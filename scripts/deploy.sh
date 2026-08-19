#!/bin/bash
#
# deploy.sh — 通过 adb 部署 T113 AI 小智到板子并启动
#
#   build/xiaozhi           → /mnt/UDISK/xiaozhi/xiaozhi
#   assets/fonts/*.otf      → /mnt/UDISK/xiaozhi/fonts/
#
# 启动用 start-stop-daemon（防 adb 会话退出杀进程）
# 与蓝牙音箱互斥：启动小智前杀掉 bt_speaker
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

ADB="${ADB:-adb}"
APP_PATH="/mnt/UDISK/xiaozhi/xiaozhi"

$ADB get-state >/dev/null 2>&1 || {
    echo "[deploy] 错误：adb 未连接（把 adb 连到虚拟机后再运行）"; exit 1; }

echo "[deploy] 停掉可能冲突的 bt_speaker（互斥：两个 app 抢 fb0/event4）..."
$ADB shell "/sbin/start-stop-daemon -K -p /tmp/bt_speaker.pid -x /mnt/UDISK/speaker/bt_speaker" 2>/dev/null || true
$ADB shell "killall bt_speaker" 2>/dev/null || true

echo "[deploy] 推送应用（→ UDISK，rootfs overlay 放不下）..."
$ADB shell mkdir -p /mnt/UDISK/xiaozhi
$ADB push "$PROJ_DIR/build/$(${ADB} shell echo ok >/dev/null 2>&1; echo xiaozhi)" "$APP_PATH" 2>/dev/null || $ADB push "$PROJ_DIR/build/xiaozhi" "$APP_PATH"
$ADB shell chmod +x "$APP_PATH"

echo "[deploy] 推送字体..."
$ADB shell mkdir -p /mnt/UDISK/xiaozhi/fonts
$ADB push "$PROJ_DIR/assets/fonts/." /mnt/UDISK/xiaozhi/fonts/

echo "[deploy] 推送 freetype 运行库（若板上已有则覆盖为同版本）..."
$ADB push "$PROJ_DIR/third_party/freetype/lib/libfreetype.so.6.17.0" /usr/lib/ 2>/dev/null || true
$ADB shell "cd /usr/lib && ln -sf libfreetype.so.6.17.0 libfreetype.so.6 && ln -sf libfreetype.so.6.17.0 libfreetype.so" 2>/dev/null || true

echo "[deploy] 推送 libspeexdsp（rootfs opkg 有记录但文件缺失，重烧后需重推）..."
SPEEXDSP_SRC="${TINA_SDK_PATH:-/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2}/out/t113/zgl_board/openwrt/staging_dir/target/usr/lib/libspeexdsp.so.1.5.1"
if [ -f "$SPEEXDSP_SRC" ]; then
    $ADB push "$SPEEXDSP_SRC" /usr/lib/
    $ADB shell "cd /usr/lib && ln -sf libspeexdsp.so.1.5.1 libspeexdsp.so.1"
else
    echo "  （找不到 $SPEEXDSP_SRC，跳过——M4 重采样才需要）"
fi

echo "[deploy] 启动..."
$ADB shell "/sbin/start-stop-daemon -K -p /tmp/xiaozhi.pid -x $APP_PATH" 2>/dev/null || true
$ADB shell "/sbin/start-stop-daemon -b -m -S -p /tmp/xiaozhi.pid -x $APP_PATH"
echo "[deploy] 完成。查看进程：adb shell ps | grep xiaozhi"
