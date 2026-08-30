#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_assets.py — 程序化生成小智 UI 素材（PIL 绘制，无版权素材）

输出 assets/images/：
  bg_main.png   480×640  夜空渐变背景（星光 + 底部微光晕）
  face_*.png    300×300  机器人表情脸
                状态帧：idle / listening_1..3 / thinking_1..3 / speaking_1..3
                情绪帧：happy laughing sad crying angry surprised loving sleepy

绘制用 2× 超采样 + LANCZOS 缩小抗锯齿；半透明元素用 ImageDraw(img, "RGBA")
模式直接混合（普通 RGBA fill 是像素替换不是混合）。
"""
import os
import random

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "images")

# ---- 配色（青色发光风，与 UI 主色 0x4FC3F7 一致） ----
C_FACE = (245, 247, 250, 255)    # 脸体暖白
C_EDGE = (203, 211, 222, 255)    # 描边
C_GLOW = (79, 195, 247)          # 青色发光部件（RGB，用时拼 alpha）
C_DARK = (55, 63, 75, 255)       # 闭眼线/眉毛等深色
C_MOUTH = (40, 50, 66, 255)      # 张嘴内部深色

SS = 2                            # 超采样倍率
FACE = 300                        # 脸图成品尺寸
S = FACE * SS                     # 绘制画布尺寸


def _new():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img, "RGBA")


def _save(img, name):
    img = img.resize((FACE, FACE), Image.LANCZOS)
    path = os.path.join(OUT, name)
    img.save(path, optimize=True)
    print("  %-22s %4d KB" % (name, os.path.getsize(path) // 1024))


# ================= 部件库（坐标均为成品 300 系，内部 ×SS） =================

def draw_base(g, dy=0.0):
    """脸体：发光天线 + 圆角方脸 + 腮红。dy 整体垂直微移（成品 px）"""
    d = dy * SS
    # 天线球（外圈光晕 + 实心）+ 杆
    g.ellipse([142*SS, (2+0)*SS+d, 158*SS, 18*SS+d], fill=C_GLOW + (70,))
    g.ellipse([145*SS, 5*SS+d, 155*SS, 15*SS+d], fill=C_GLOW + (255,))
    g.rectangle([147*SS, 15*SS+d, 153*SS, 46*SS+d], fill=C_EDGE)
    # 脸体
    g.rounded_rectangle([45*SS, 46*SS+d, 255*SS, 242*SS+d], radius=62*SS,
                        fill=C_FACE, outline=C_EDGE, width=5*SS)
    # 腮红
    for cx in (88, 212):
        g.ellipse([(cx-14)*SS, 168*SS+d, (cx+14)*SS, 190*SS+d],
                  fill=(247, 143, 167, 90))


def rings(g, phase):
    """聆听声波环（画在脸后面）：两侧圆弧，phase 1..3 幅度/亮度递增。
    几何保证最大外沿 ≤296px 不被画布裁剪（动画帧不能切边）"""
    for side in (-1, 1):
        cx = (150 + side * (105 + 5 * phase)) * SS
        cy = 138 * SS
        st, en = (-48, 48) if side > 0 else (132, 228)
        for k in (0, 1):
            r = (13 + 8 * k) * SS * (0.9 + 0.1 * phase)
            a = int(255 * (0.9 - 0.3 * k) * (0.5 + 0.17 * phase))
            g.arc([cx - r, cy - r, cx + r, cy + r], st, en,
                  fill=C_GLOW + (a,), width=6 * SS)


def eyes_open(g, r=16, dy=0):
    """睁眼（青色发光圆 + 高光）"""
    for ex in (108, 192):
        cx, cy = ex * SS, (132 + dy) * SS
        R = r * SS
        g.ellipse([cx-R-9*SS, cy-R-9*SS, cx+R+9*SS, cy+R+9*SS], fill=C_GLOW + (70,))
        g.ellipse([cx-R, cy-R, cx+R, cy+R], fill=C_GLOW + (255,))
        g.ellipse([cx-R*0.35-3*SS, cy-R*0.4-3*SS, cx-R*0.35+3*SS, cy-R*0.4+3*SS],
                  fill=(255, 255, 255, 255))


def eyes_joy(g):
    """开心眯眼（∩ 弧）"""
    for ex in (108, 192):
        g.arc([(ex-15)*SS, 122*SS, (ex+15)*SS, 148*SS], 200, 340,
              fill=C_DARK, width=7*SS)


def eyes_idle(g):
    """待命浅眯眼（浅 ∪ 弧）"""
    for ex in (108, 192):
        g.arc([(ex-15)*SS, 124*SS, (ex+15)*SS, 144*SS], 25, 155,
              fill=C_DARK, width=7*SS)


def eyes_lines(g):
    """睡觉直线眼"""
    for ex in (108, 192):
        g.line([(ex-14)*SS, 132*SS, (ex+14)*SS, 132*SS], fill=C_DARK, width=7*SS)


def eyes_rings(g, r=18):
    """惊讶圆环眼"""
    for ex in (108, 192):
        cx, cy = ex * SS, 132 * SS
        R = r * SS
        g.ellipse([cx-R, cy-R, cx+R, cy+R], outline=C_GLOW + (255,), width=6*SS)
        g.ellipse([cx-5*SS, cy-5*SS, cx+5*SS, cy+5*SS], fill=C_GLOW + (255,))


def eyes_look_up(g):
    """思考：眼珠上抬"""
    for ex in (108, 192):
        cx, cy = (ex + 3) * SS, 126 * SS
        R = 13 * SS
        g.ellipse([cx-R-8*SS, cy-R-8*SS, cx+R+8*SS, cy+R+8*SS], fill=C_GLOW + (60,))
        g.ellipse([cx-R, cy-R, cx+R, cy+R], fill=C_GLOW + (255,))
        g.ellipse([cx-6*SS, cy-7*SS, cx, cy-1*SS], fill=(255, 255, 255, 255))


def eyes_hearts(g):
    """爱心眼"""
    for ex in (108, 192):
        heart(g, ex, 132, 15, (240, 100, 130, 255))


def brows_up(g):
    g.line([(92*SS, 104*SS), (126*SS, 102*SS)], fill=C_DARK, width=6*SS)
    g.line([(208*SS, 104*SS), (174*SS, 102*SS)], fill=C_DARK, width=6*SS)


def brows_sad(g):
    g.line([(90*SS, 120*SS), (124*SS, 106*SS)], fill=C_DARK, width=7*SS)
    g.line([(210*SS, 120*SS), (176*SS, 106*SS)], fill=C_DARK, width=7*SS)


def brows_angry(g):
    g.line([(88*SS, 104*SS), (124*SS, 118*SS)], fill=C_DARK, width=7*SS)
    g.line([(212*SS, 104*SS), (176*SS, 118*SS)], fill=C_DARK, width=7*SS)


def m_smile(g, w=22, dy=0):
    g.arc([(150-w)*SS, (174+dy)*SS, (150+w)*SS, (196+dy)*SS], 20, 160,
          fill=C_GLOW + (255,), width=6*SS)


def m_frown(g, w=18, dy=2):
    g.arc([(150-w)*SS, (176+dy)*SS, (150+w)*SS, (200+dy)*SS], 195, 345,
          fill=C_GLOW + (255,), width=6*SS)


def m_flat(g, dy=0):
    g.line([(136*SS, (184+dy)*SS), (164*SS, (184+dy)*SS)],
           fill=C_GLOW + (255,), width=6*SS)


def m_open(g, w=24, h=20, dy=0):
    """张嘴（深色圆角矩形 + 舌头）"""
    cy = 184 + dy
    g.rounded_rectangle([(150-w)*SS, (cy-h//2)*SS, (150+w)*SS, (cy+h//2)*SS],
                        radius=9*SS, fill=C_MOUTH, outline=C_EDGE, width=2*SS)
    if h >= 16:
        g.ellipse([(150-w*0.55)*SS, (cy+h*0.05)*SS, (150+w*0.55)*SS, (cy+h*0.5)*SS],
                  fill=(240, 130, 140, 255))


def m_o(g, r=11, dy=0):
    g.ellipse([(150-r)*SS, (184-r+dy)*SS, (150+r)*SS, (184+r+dy)*SS],
              fill=C_MOUTH, outline=C_EDGE, width=2*SS)


def heart(g, cx, cy, r, color):
    cx, cy, r = cx*SS, cy*SS, r*SS
    g.ellipse([cx-r, cy-r*0.9, cx, cy+r*0.1], fill=color)
    g.ellipse([cx, cy-r*0.9, cx+r, cy+r*0.1], fill=color)
    g.polygon([(cx-r*0.96, cy-r*0.25), (cx+r*0.96, cy-r*0.25), (cx, cy+r)],
              fill=color)


def tear(g, cx, cy, r):
    cx, cy, r = cx*SS, cy*SS, r*SS
    c = (126, 212, 255, 230)
    g.polygon([(cx, cy-2*r), (cx-0.8*r, cy), (cx+0.8*r, cy)], fill=c)
    g.ellipse([cx-0.9*r, cy-r*0.7, cx+0.9*r, cy+r*1.3], fill=c)


def zzz(g, x=212, y=64):
    c = (150, 200, 240, 220)
    for i, u in enumerate((11, 9, 7)):
        ox = (x + i*11) * SS
        oy = (y - i*9) * SS
        us = u * SS
        g.line([(ox, oy), (ox+us, oy), (ox, oy+us), (ox+us, oy+us)],
               fill=c, width=4*SS, joint="curve")


def anger_mark(g):
    x, y = 216*SS, 86*SS
    c = (240, 113, 120, 230)
    L = 9*SS
    for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        g.line([(x, y), (x+dx*L, y+dy*L)], fill=c, width=5*SS)


def thought_dots(g, n):
    """思考气泡点（右上角，n=1..3 渐次出现）"""
    pts = [(228, 96, 4), (246, 76, 6), (266, 52, 8)]
    for i in range(n):
        x, y, r = pts[i]
        g.ellipse([(x-r)*SS, (y-r)*SS, (x+r)*SS, (y+r)*SS],
                  fill=(190, 205, 225, 200))


# ================= 表情定义 =================

def make_face(name, eyes_fn, mouth_fn, rings_phase=0, extras=(), dy=0.0):
    img, g = _new()
    if rings_phase:
        rings(g, rings_phase)
    draw_base(g, dy)
    eyes_fn(g)
    mouth_fn(g)
    for e in extras:
        e(g)
    _save(img, "face_%s.png" % name)


def gen_faces():
    # ---- 状态帧 ----
    make_face("idle", eyes_idle, lambda g: m_smile(g, 14))
    for ph in (1, 2, 3):
        make_face("listening_%d" % ph, eyes_open,
                  lambda g: m_smile(g, 18), rings_phase=ph)
    for ph in (1, 2, 3):
        make_face("thinking_%d" % ph, eyes_look_up, m_flat,
                  extras=(lambda g, n=ph: thought_dots(g, n),))
    make_face("speaking_1", eyes_open, lambda g: m_open(g, 15, 9))
    make_face("speaking_2", eyes_open, lambda g: m_open(g, 17, 25))
    make_face("speaking_3", eyes_open, lambda g: m_open(g, 16, 16))

    # ---- 情绪帧 ----
    make_face("happy", eyes_open, lambda g: m_smile(g, 24))
    make_face("laughing", eyes_joy, lambda g: m_open(g, 26, 28))
    make_face("sad", eyes_open,
              lambda g: m_frown(g), extras=(brows_sad,))
    make_face("crying", eyes_open, lambda g: m_frown(g),
              extras=(brows_sad, lambda g: (tear(g, 92, 156, 9), tear(g, 208, 156, 9))))
    make_face("angry", eyes_open, m_flat,
              extras=(brows_angry, anger_mark))
    make_face("surprised", eyes_rings, lambda g: m_o(g, 12), extras=(brows_up,))
    make_face("loving", eyes_hearts, lambda g: m_smile(g, 22))
    make_face("sleepy", eyes_lines, lambda g: m_o(g, 8), extras=(zzz,))


# ================= 背景 =================

def gen_bg():
    W, H = 480, 640
    top, mid, bot = (11, 16, 38), (19, 28, 64), (36, 36, 86)

    strip = Image.new("RGB", (1, H))
    for y in range(H):
        t = y / (H - 1)
        if t < 0.55:
            u = t / 0.55
            c = tuple(int(top[i] + (mid[i]-top[i]) * u) for i in range(3))
        else:
            u = (t - 0.55) / 0.45
            c = tuple(int(mid[i] + (bot[i]-mid[i]) * u) for i in range(3))
        strip.putpixel((0, y), c)
    img = strip.resize((W, H), Image.BILINEAR).convert("RGBA")

    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    g = ImageDraw.Draw(ov, "RGBA")

    rnd = random.Random(42)                    # 固定种子保证可复现
    for _ in range(70):
        x, y = rnd.randrange(W), rnd.randrange(int(H*0.62))
        r = rnd.uniform(0.6, 1.7)
        a = rnd.randint(50, 200)
        g.ellipse([x-r, y-r, x+r, y+r], fill=(235, 240, 255, a))
    for _ in range(6):                          # 少量大星 + 十字光
        x, y = rnd.randrange(W), rnd.randrange(int(H*0.5))
        g.line([(x-5, y), (x+5, y)], fill=(235, 240, 255, 120), width=1)
        g.line([(x, y-5), (x, y+5)], fill=(235, 240, 255, 120), width=1)

    # 底部中央青色光晕（同心椭圆，低 alpha 叠加）
    for r, a in ((230, 10), (180, 14), (130, 18), (85, 22)):
        g.ellipse([240-r, 660-r, 240+r, 660+r], fill=C_GLOW + (a,))

    img = Image.alpha_composite(img, ov)
    path = os.path.join(OUT, "bg_main.png")
    img.convert("RGB").save(path, optimize=True)
    print("  %-22s %4d KB" % ("bg_main.png", os.path.getsize(path) // 1024))


def gen_menu_icons():
    """菜单页图标 96×96：小智脸 + WiFi 扇形"""
    SZ, SSS = 96, 3   # 96 成品，3× 超采样
    W = SZ * SSS

    # 1) 小智图标：直接把 300px idle 脸缩到 96
    face = Image.open(os.path.join(OUT, "face_idle.png")).convert("RGBA")
    icon1 = face.resize((SZ, SZ), Image.LANCZOS)
    p1 = os.path.join(OUT, "menu_xiaozhi.png")
    icon1.save(p1, optimize=True)
    print("  %-22s %4d KB" % ("menu_xiaozhi.png", os.path.getsize(p1) // 1024))

    # 2) WiFi 图标：三层扇形圆弧 + 中心点
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    g = ImageDraw.Draw(img, "RGBA")
    cx, cy = W // 2, int(W * 0.82)
    for r, a in ((int(W*0.16), 255), (int(W*0.32), 230), (int(W*0.46), 205)):
        g.arc([cx - r, cy - r, cx + r, cy + r], 215, 325,
              fill=C_GLOW + (a,), width=7 * SSS // 2)
    g.ellipse([cx - 5*SSS, cy - 5*SSS, cx + 5*SSS, cy + 5*SSS],
              fill=C_GLOW + (255,))
    img = img.resize((SZ, SZ), Image.LANCZOS)
    p2 = os.path.join(OUT, "menu_wifi.png")
    img.save(p2, optimize=True)
    print("  %-22s %4d KB" % ("menu_wifi.png", os.path.getsize(p2) // 1024))


def main():
    os.makedirs(OUT, exist_ok=True)
    print("生成背景…")
    gen_bg()
    print("生成表情脸…")
    gen_faces()
    print("生成菜单图标…")
    gen_menu_icons()
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print("合计 %.1f KB，%d 个文件" % (total / 1024, len(os.listdir(OUT))))


if __name__ == "__main__":
    main()
