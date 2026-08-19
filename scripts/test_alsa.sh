#!/bin/bash
#
# test_alsa.sh — 板上录音自检（M0：确定麦克风采集设备/通道）
#
# 在宿主机跑（需要 adb）。对每个候选设备录 5 秒拉回分析：
#   1. hw:audiocodec,0  默认通道
#   2. snddmic          数字麦声卡
# 也打印声卡/混音器现状辅助判断。
#
set -e

ADB="${ADB:-adb}"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/build/alsa_test"

$ADB get-state >/dev/null 2>&1 || { echo "adb 未连接"; exit 1; }
mkdir -p "$OUT_DIR"

echo "=== 声卡列表 ==="
$ADB shell cat /proc/asound/cards
echo; echo "=== PCM 设备（capture 即 Capture） ==="
$ADB shell cat /proc/asound/pcm
echo; echo "=== codec 控件（mic 相关） ==="
$ADB shell amixer -c 0 contents 2>/dev/null | head -80 || true

echo; echo "=== 开始录音测试（每项 5 秒，请对着板子说话） ==="
declare -a TESTS=(
    "audiocodec:arecord -D hw:audiocodec,0 -c 1 -r 16000 -f S16_LE -d 5 /tmp/t1.wav"
    "snddmic:arecord -D hw:snddmic,0 -c 1 -r 16000 -f S16_LE -d 5 /tmp/t2.wav"
)
for t in "${TESTS[@]}"; do
    name="${t%%:*}"; cmd="${t#*:}"
    echo "--- [$name] $cmd"
    if $ADB shell "$cmd"; then
        $ADB pull /tmp/t"$([ "$name" = audiocodec ] && echo 1 || echo 2)".wav "$OUT_DIR/${name}.wav" && echo "已拉回 $OUT_DIR/${name}.wav"
    else
        echo "[$name] 录音命令失败"
    fi
done
echo; echo "完成。分析：python3 分析能量/底噪（由 Claude 执行）"
