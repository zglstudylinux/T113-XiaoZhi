# platform/t113/lib — 预编译库说明

链接用库全部仓库内（`deps` 检查缺失即 configure 失败）。来源与重建方法：

## libopus.a（静态，opus 1.3.1，NEON + 硬浮点）

原 `scripts/setup.sh` 的交叉编译配方（脚本已随 R2 移除，留档在此）：

```bash
TINA_SDK_PATH=${TINA_SDK_PATH:-/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2}
CROSS=$TINA_SDK_PATH/prebuilt/rootfsbuilt/arm/toolchain-sunxi-glibc-gcc-830/toolchain/bin/arm-openwrt-linux-
export STAGING_DIR=$TINA_SDK_PATH/prebuilt/rootfsbuilt/arm/toolchain-sunxi-glibc-gcc-830/toolchain
TAR=$TINA_SDK_PATH/openwrt/dl/opus-1.3.1.tar.gz

mkdir -p opus-build && tar xzf $TAR -C opus-build --strip-components=1
cd opus-build
./configure --host=arm-openwrt-linux --disable-shared --enable-static \
    --disable-extra-programs --disable-doc \
    CFLAGS="-O2 -march=armv7-a -mfpu=neon -mfloat-abi=hard" \
    --prefix=/usr
make -j$(nproc) libopus.la
cp .libs/libopus.a ../libopus.a
```

注意：libopus.a 是**硬浮点（armhf）**ABI，与板端 rootfs 一致；换软浮点工具链须重编。

## 其余库

| 文件 | 来源 |
|---|---|
| libfreetype.so(.6/.6.17.0)、libz.so.1.2.11(+软链)、libbz2.so(.1.0) | third_party 预编译副本（R2 前即随仓库） |
| libssl.so.1.1、libcrypto.so.1.1、libasound.so.2.0.0、libspeexdsp.so.1.5.1(+软链) | Tina SDK `out/t113/zgl_board/openwrt/staging_dir/target/usr/lib`（与板端 /usr/lib 同版本，deploy.sh 只推 speexdsp，其余板上已有） |

`.so` 软链是给链接器（ld 按 `-l<name>` 找 `.so`），`.so.<N>` 实体决定 SONAME；
**链接必须用 `.so` 本体或未 strip 实体，`.so.6` 一类软链不能给 IMPORTED LOCATION**。
