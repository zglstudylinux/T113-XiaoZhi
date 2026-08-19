#
# T113 AI 小智 — 交叉编译 Makefile（基线复用 T113-bluetooth-speaker 验证过的 Makefile）
#
# 用法：
#   ./scripts/setup.sh        # 首次：复制 toolchain + 交叉编译 opus → _libs/
#   make                      # 构建 build/xiaozhi
#   ./scripts/deploy.sh       # adb 部署到板子
#
CC      := $(shell [ -x "$(CURDIR)/toolchain/bin/arm-openwrt-linux-gcc" ] && echo "$(CURDIR)/toolchain/bin/arm-openwrt-linux-gcc" || echo "arm-openwrt-linux-gcc")

# OpenWrt wrapper 编译器要求 STAGING_DIR
export STAGING_DIR := $(dir $(firstword $(CC)))
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= $(CURDIR)/third_party

# 板上自带（rootfs）：libssl/libcrypto 1.1.1、libasound 1.1.8、libspeexdsp 1.5.1
# 头文件从 Tina SDK staging 取（setup.sh 复制到 _libs/include/）
SDK_STAGING := /home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2/out/t113/zgl_board/openwrt/staging_dir/target/usr/include

CFLAGS  ?= -std=gnu99 -O2 -g \
	-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard \
	-I$(CURDIR)/src \
	-I$(CURDIR)/src/net -I$(CURDIR)/src/audio -I$(CURDIR)/src/wifi -I$(CURDIR)/src/ui \
	-I$(CURDIR)/_libs/include \
	-I$(SDK_STAGING) \
	-I$(LVGL_DIR)/ \
	-I$(CURDIR)/third_party/lvgl/src/extra/libs/freetype \
	-I$(CURDIR)/third_party/freetype/include \
	-DLV_USE_PNG=1 -DLV_USE_FS_POSIX=1 \
	-Wall -Wno-unused-function -Wno-unused-parameter -Wno-missing-prototypes \
	-Wno-sign-compare -Wno-format-nonliteral

# opus 静态链入（_libs/lib/libopus.a，setup.sh 交叉编译产物）
# speexdsp/ssl/asound 用板上 rootfs 的动态库（已验证存在）
LDFLAGS ?= -lm \
	-L$(CURDIR)/_libs/lib -lopus \
	-L$(CURDIR)/third_party/freetype/lib -lfreetype \
	-lssl -lcrypto \
	-lasound -lspeexdsp \
	-lz -lbz2 -lpthread -lrt -ldl \
	-Wl,-rpath,/usr/lib

BIN  = xiaozhi
BUILD = build

#Collect the files to compile
MAINSRC = ./src/main.c ./src/app_state.c \
	./src/net/ws_client.c ./src/net/xz_protocol.c \
	./src/audio/audio_cap.c ./src/audio/audio_play.c \
	./src/audio/opus_codec.c ./src/audio/resample.c \
	./src/wifi/wifi_manager.c \
	./src/ui/ui_main.c ./src/ui/ui_wifi_setup.c

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

OBJEXT ?= .o

AOBJS = $(patsubst %,$(BUILD)/%,$(ASRCS:.S=$(OBJEXT)))
COBJS = $(patsubst %,$(BUILD)/%,$(CSRCS:.c=$(OBJEXT)))
MAINOBJ = $(patsubst %,$(BUILD)/%,$(MAINSRC:.c=$(OBJEXT)))

OBJS = $(AOBJS) $(COBJS)

all: $(BUILD)/$(BIN)

$(BUILD)/$(BIN): $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $@ $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)
	@echo "===> $@"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

clean:
	rm -rf $(BUILD)

.PHONY: all clean
