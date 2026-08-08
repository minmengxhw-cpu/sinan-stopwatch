#!/usr/bin/env python3
"""
prep_photos.py — 把照片处理成设备能直接用的资产。

    python3 scripts/prep_photos.py ~/Pictures/团团/*.jpg
    python3 scripts/prep_photos.py --center 1136,534 --radius 468 一张.jpg

输出到 build/tuantuan/，然后把整个文件夹拖到 Claude 桌面端的
Hardware Buddy 窗口上，照片会经 BLE 流进设备。

设计前提：团团睡觉时把自己蜷成正圆，而屏幕也是正圆，所以做法不是
"圆屏里放一张照片"，而是把他抠成一枚悬浮的圆盘。四周是纯黑，
AMOLED 上黑像素熄灭，于是他没有边缘，像一件实物躺在表壳里。

只需要 pillow + numpy，不需要 ImageMagick，也不需要抠图模型。
"""
import argparse, json, pathlib, sys
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

SRC = 536          # 设备端画布，比屏幕大 70px，给漂移留余量
FRAC = 0.72        # 主体直径占画布的比例，四周留黑给晕影
BUDGET_KB = 1700   # Hardware Buddy 文件夹推送上限 1.8MB，留点余量


def find_subject(im):
    """猜主体的外接圆。毯子/地板通常偏冷，主体偏暖，据此分割。
    猜不准就用 --center / --radius 手工指定，比调参快。"""
    a = np.asarray(im.resize((im.width // 4, im.height // 4))).astype(np.int16)
    R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    warm = (R > B + 10) | ((a.mean(2) > 170) & (G >= R - 6))
    m = Image.fromarray((warm * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(5))
    ys, xs = np.nonzero(np.asarray(m) > 127)
    if len(xs) < 500:
        return im.width // 2, im.height // 2, min(im.size) // 2
    cx, cy = xs.mean() * 4, ys.mean() * 4
    r = np.percentile(np.hypot(xs * 4 - cx, ys * 4 - cy), 93)
    return int(cx), int(cy), int(r)


def build(path, out, center=None, radius=None):
    im = Image.open(path).convert('RGB')
    cx, cy, r = center + (radius,) if center and radius else find_subject(im)
    r = min(r, cx, cy, im.width - cx, im.height - cy)

    dog = im.crop((cx - r, cy - r, cx + r, cy + r))
    D = int(SRC * FRAC)
    dog = dog.resize((D, D), Image.LANCZOS)

    # 只在最外一圈环带上压残留背景。往里一步都不碰 ——
    # 不限半径的话，白毛的阴影会被当成地板一起啃掉
    a = np.asarray(dog).astype(np.int16)
    Rc, Gc, Bc = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    yy, xx = np.mgrid[0:D, 0:D]
    edge = np.clip((np.hypot(xx - D / 2, yy - D / 2) / (D / 2) - 0.86) / 0.14, 0, 1)
    cool = (Bc > Rc + 14) & (Gc > Rc + 8)
    pale = (a.mean(2) > 150) & (Gc - Rc < 0)
    bg = Image.fromarray(((cool | pale) * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(7))
    bg = np.asarray(bg.filter(ImageFilter.GaussianBlur(5))).astype(np.float32) / 255.0
    dog = Image.fromarray((np.asarray(dog) * (1 - bg * edge)[..., None]).astype(np.uint8))

    # 羽化的圆形遮罩：软边既像浮在黑里，也不给 JPEG 的 DCT 留硬边去振铃
    mask = Image.new('L', (D, D), 0)
    ImageDraw.Draw(mask).ellipse((12, 12, D - 12, D - 12), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(12))

    canvas = Image.new('RGB', (SRC, SRC), (0, 0, 0))
    canvas.paste(dog, ((SRC - D) // 2,) * 2, mask)
    # AMOLED 色域比显示器宽，原样放上去偏平；+8% 饱和在这块屏上刚好，多了就俗
    canvas = ImageEnhance.Color(canvas).enhance(1.08)
    canvas.save(out, quality=92)
    return cx, cy, r


def make_glyph(asset_path, out, G=264, spacing=7.4):
    """从第一张照片生成极坐标点阵字形。

    团团蜷着的轮廓就是个圆，直接做剪影认不出是狗。改成半调点阵：
    同心圆环上撒点，点径跟着照片明暗走 —— 白毛的扫尾、蜷起的脸、
    圆滚的身子都读得出来，而且"全是弧和点"正好是这套设计语言本身。

    存成 RGBA PNG（RGB 全白，alpha 是点的浓度），设备端用
    lv_obj_set_style_image_recolor 按状态染色，比塞一个 68KB 的 C 数组干净。
    """
    im = Image.open(asset_path).convert('L')
    S = im.width
    r0 = int(S * FRAC / 2)
    im = im.crop((S//2-r0, S//2-r0, S//2+r0, S//2+r0))
    im = im.resize((G, G), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.0))

    lum = np.asarray(im).astype(np.float32)
    yy, xx = np.mgrid[0:G, 0:G]
    inside = np.hypot(xx - G/2, yy - G/2) < G/2 - 2
    # 只用主体内部的分位数归一化。用全图的话黑背景会把整体压暗，
    # 棕色身体就只剩下一片小点，认不出是狗
    lo, hi = np.percentile(lum[inside], [3, 97])
    v = np.clip((lum - lo) / max(hi - lo, 1), 0, 1) ** 0.62   # 提中间调
    v[~inside] = 0

    alpha = Image.new('L', (G, G), 0)
    d = ImageDraw.Draw(alpha)
    r = 4.0
    while r < G/2 - 3:
        n = max(6, int(round(2 * np.pi * r / spacing)))
        for i in range(n):
            # 每圈错开半格，否则会出现放射状的假条纹
            th = 2 * np.pi * (i + (0.5 if int(r/spacing) % 2 else 0)) / n
            x, y = G/2 + r*np.cos(th), G/2 + r*np.sin(th)
            val = float(v[int(np.clip(y, 0, G-1)), int(np.clip(x, 0, G-1))])
            if val < 0.04:
                continue
            rad = 0.85 + val * 3.0
            d.ellipse([x-rad, y-rad, x+rad, y+rad], fill=int(120 + 135*val))
        r += spacing

    glyph = Image.merge('RGBA', (Image.new('L', (G, G), 255),) * 3 + (alpha,))
    glyph.save(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('photos', nargs='+')
    ap.add_argument('--center', help='主体圆心 x,y（不给则自动猜）')
    ap.add_argument('--radius', type=int, help='主体半径')
    ap.add_argument('--name', default='tuan', help='设备端目录名 -> /spiflash/<name>')
    args = ap.parse_args()

    center = tuple(int(v) for v in args.center.split(',')) if args.center else None
    out_dir = pathlib.Path(__file__).resolve().parent.parent / 'build' / 'tuantuan'
    if out_dir.exists():
        for f in out_dir.iterdir():
            f.unlink()
    out_dir.mkdir(parents=True, exist_ok=True)

    n = 0
    for src in args.photos:
        p = pathlib.Path(src)
        if not p.is_file():
            continue
        n += 1
        dst = out_dir / f'{n:02d}.jpg'
        cx, cy, r = build(p, dst, center, args.radius)
        print(f'  {p.name:<26} -> {dst.name}  {dst.stat().st_size // 1024}KB'
              f'   主体 ({cx},{cy}) r={r}')

    if not n:
        sys.exit('没有处理任何文件')

    # 点阵字形：守的长按确认要用，从第一张生成
    make_glyph(out_dir / '01.jpg', out_dir / 'glyph.png')
    print(f"  glyph.png  {(out_dir / 'glyph.png').stat().st_size // 1024}KB   点阵字形")

    (out_dir / 'manifest.json').write_text(json.dumps({'name': args.name, 'count': n}))
    total = sum(f.stat().st_size for f in out_dir.iterdir()) // 1024
    print(f'共 {n} 张，{total}KB')
    if total > BUDGET_KB:
        sys.exit(f'超过 Hardware Buddy 的 1.8MB 上限。减少张数，或把 quality 调到 85。')
    print(f'好了。把 {out_dir} 拖到 Hardware Buddy 窗口上。')
    print('自动猜的圆心不满意就用 --center x,y --radius r 手工指定，比调参快。')


if __name__ == '__main__':
    main()
