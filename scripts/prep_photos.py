#!/usr/bin/env python3
"""
prep_photos.py — 把照片处理成设备能直接用的资产。

    python3 scripts/prep_photos.py ~/Pictures/团团/*.jpg
    python3 scripts/prep_photos.py --center 1136,534 --radius 468 一张.jpg

输出到 build/tuantuan/，然后把整个文件夹拖到 Claude 桌面端的
Hardware Buddy 窗口上，照片会经 BLE 流进设备。

日期一律从 EXIF DateTimeOriginal 读，其次认文件名前缀 YYYY-MM 或 YYYY-MM-DD。
**任何地方都不允许手打日期。** 手打过一次就错过一次（把 2020.10 写成了 2019.05），
而且错得很难发现——渲染出来的图看着一切正常。

设计前提：团团睡觉时把自己蜷成正圆，而屏幕也是正圆，所以做法不是
"圆屏里放一张照片"，而是把他抠成一枚悬浮的圆盘。四周是纯黑，
AMOLED 上黑像素熄灭，于是他没有边缘，像一件实物躺在表壳里。

只需要 pillow + numpy，不需要 ImageMagick，也不需要抠图模型。
"""
import argparse, json, pathlib, re, sys
from datetime import date, datetime
import numpy as np
from PIL import ExifTags, Image, ImageDraw, ImageEnhance, ImageFilter, ImageOps

SRC = 536          # 设备端画布，比屏幕大 70px，给漂移留余量
FRAC = 0.72        # 主体直径占画布的比例，四周留黑给晕影
BUDGET_KB = 1700   # Hardware Buddy 文件夹推送上限 1.8MB，留点余量


DATE_RE = re.compile(r'(20\d{2})[-_.]?(\d{2})(?:[-_.]?(\d{2}))?')


def photo_date(path: pathlib.Path):
    """EXIF 优先，文件名兜底。拿不到就返回 None，绝不猜、绝不用文件修改时间
    （那是下载时间，不是拍摄时间）。"""
    try:
        ex = Image.open(path).getexif()
        sub = ex.get_ifd(0x8769)
        for k, v in sub.items():
            if ExifTags.TAGS.get(k) == 'DateTimeOriginal' and v:
                return datetime.strptime(str(v)[:10], '%Y:%m:%d').date()
        for k, v in ex.items():
            if ExifTags.TAGS.get(k) == 'DateTime' and v:
                return datetime.strptime(str(v)[:10], '%Y:%m:%d').date()
    except Exception:
        pass
    m = DATE_RE.search(path.stem)
    if m:
        y, mo, d = m.group(1), m.group(2), m.group(3) or '15'
        try:
            return date(int(y), int(mo), int(d))
        except ValueError:
            pass
    return None


def find_subject(im):
    """猜主体的外接圆。**这只是兜底，不要指望它。**

    试过两套判据：按冷暖分割（木地板也是暖的，圈进半个客厅）、
    按白毛定位（脸和胸的白毛偏离头心，半径全靠蒙）。
    两套都在四成左右的照片上失手。任意宠物照的主体定位不是颜色启发式
    能解的问题，别再往里加第三套规则。

    正确做法是 scripts/pick_crops.html：浏览器里拖一个圈，导出 crops.json，
    用 --crops 喂进来。十六张照片十分钟，而且每张都对。
    这跟守那一页是同一个原则 —— 判断交给人，机器只做机械活。
    """
    small = im.resize((im.width // 6, im.height // 6))
    a = np.asarray(small).astype(np.int16)
    lo, hi = a.min(2), a.max(2)
    blaze = (a.mean(2) > 175) & ((hi - lo) < 30)      # 亮且接近中性 = 白毛
    m = Image.fromarray((blaze * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(5))
    ys, xs = np.nonzero(np.asarray(m) > 127)

    if len(xs) < 150:
        return im.width // 2, im.height // 2, int(min(im.size) * 0.34)

    cx, cy = xs.mean() * 6, ys.mean() * 6
    spread = np.percentile(np.hypot(xs * 6 - cx, ys * 6 - cy), 88)
    r = int(np.clip(spread * 1.9, min(im.size) * 0.16, min(im.size) * 0.52))
    return int(cx), int(cy), r


def center_square(im, zoom):
    """纯居中方裁。主体在画面中心时这是唯一正确的做法 ——
    不猜圆心就不可能把主体裁偏。zoom 越大裁得越紧、主体越大。"""
    side = int(min(im.size) / zoom)
    cx, cy = im.width // 2, im.height // 2
    half = side // 2
    half = min(half, cx, cy)
    return cx, cy, half


def build(path, out, center=None, radius=None, mode='portrait', zoom=1.0):
    """两种裁法。

    disc：他蜷成球时用。主体本身就是圆的，占 72%，四周留黑给晕影，
          并在最外环带压掉残留背景。
    portrait：坐着站着时用。裁到头，占 90%，晕影往里收到 0.60 —— 地板和
          家具在外圈直接死掉。这种照片的背景常是暖木色，跟毛色同色系，
          冷暖分割会把毛一起啃掉，所以不清背景。
    """
    frac, clean = (FRAC, True) if mode == 'disc' else (0.90, False)

    # exif_transpose 必须有：手机竖拍的照片元数据里带旋转标记，
    # 不应用的话狗是躺着的。从 ImageMagick 换到 PIL 时把 -auto-orient 弄丢了
    im = ImageOps.exif_transpose(Image.open(path)).convert('RGB')
    if center and radius:
        cx, cy, r = center[0], center[1], radius
    elif zoom:
        cx, cy, r = center_square(im, zoom)
    else:
        cx, cy, r = find_subject(im)
    r = min(r, cx, cy, im.width - cx, im.height - cy)

    dog = im.crop((cx - r, cy - r, cx + r, cy + r))
    D = int(SRC * frac)
    dog = dog.resize((D, D), Image.LANCZOS)

    # 只在最外一圈环带上压残留背景。往里一步都不碰 ——
    # 不限半径的话，白毛的阴影会被当成地板一起啃掉
    if clean:
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
    ap.add_argument('--disc', nargs='*', default=[],
                    help='用 disc 模式（蜷成球）处理的文件名，其余一律 portrait')
    ap.add_argument('--glyph-from', default=None,
                    help='用哪张生成点阵字形。默认挑第一张 disc 模式的 —— '
                         '半调点阵在圆形主体上最好认，坐姿做出来会糊')
    ap.add_argument('--zoom', type=float, default=1.35,
                    help='居中方裁的收紧系数。1.0=整个短边，越大裁得越紧、主体越大。'
                         '主体在画面中心时用这个，不猜圆心就不会裁偏')
    ap.add_argument('--exif-only', action='store_true',
                    help='只收有 EXIF 拍摄日期的照片')
    ap.add_argument('--crops', default=None,
                    help='scripts/pick_crops.html 导出的 JSON：'
                         '{"文件名": {"cx":.., "cy":.., "r":.., "mode":"disc|portrait"}}')
    ap.add_argument('--captions', default=None,
                    help='可选的 JSON：{"文件名": "海边"}。写场景，不要写年龄')
    args = ap.parse_args()

    center = tuple(int(v) for v in args.center.split(',')) if args.center else None
    caps = json.loads(pathlib.Path(args.captions).read_text()) if args.captions else {}
    crops = json.loads(pathlib.Path(args.crops).read_text()) if args.crops else {}
    disc_set = set(args.disc)

    # 每次生成一个新目录，旧照片资产永不删除或覆盖。这样任何一次裁切结果
    # 都能回看，失败的批次也不会破坏上一次可用交付。
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
    out_dir = pathlib.Path(__file__).resolve().parent.parent / 'build' / f'tuantuan-{stamp}'
    out_dir.mkdir(parents=True, exist_ok=False)

    # 先按日期排。没日期的排在最后，保持给定顺序
    srcs = [pathlib.Path(s) for s in args.photos if pathlib.Path(s).is_file()]
    dated = [(photo_date(p), p) for p in srcs]
    if args.exif_only:
        dated = [x for x in dated if x[0]]
    dated.sort(key=lambda x: (x[0] is None, x[0] or date.min))

    items, undated, n = [], [], 0
    glyph_src = None
    for d, p in dated:
        n += 1
        c = crops.get(p.name)
        mode = c['mode'] if c and 'mode' in c else ('disc' if p.name in disc_set else 'portrait')
        ctr = (c['cx'], c['cy']) if c else center
        rad = c['r'] if c else args.radius
        dst = out_dir / f'{n:02d}.jpg'
        cx, cy, r = build(p, dst, ctr, rad, mode, args.zoom)
        if glyph_src is None and (args.glyph_from == p.name or
                                  (args.glyph_from is None and mode == 'disc')):
            glyph_src = dst
        items.append({'file': dst.name,
                      'date': d.isoformat() if d else None,
                      'caption': caps.get(p.name, ''),
                      'mode': mode})
        if d is None:
            undated.append(p.name)
        print(f'  {p.name:<24} -> {dst.name}  {dst.stat().st_size // 1024:>3}KB  '
              f'{mode:<8} {d.isoformat() if d else "日期未知"}')

    if not n:
        sys.exit('没有处理任何文件')

    # 点阵字形：守的长按确认要用
    make_glyph(glyph_src or (out_dir / '01.jpg'), out_dir / 'glyph.png')
    print(f"  glyph.png  {(out_dir / 'glyph.png').stat().st_size // 1024}KB   "
          f"点阵字形（源：{(glyph_src or out_dir / '01.jpg').name}）")

    (out_dir / 'manifest.json').write_text(
        json.dumps({'name': args.name, 'items': items}, ensure_ascii=False, indent=1))

    if undated:
        print(f'\n{len(undated)} 张没读到拍摄日期（微信转发会剥掉 EXIF）：')
        for u in undated:
            print(f'    {u}')
        print('  它们照样进轮播，只是不落在「喜」的环上。')
        print('  想让它们上环：文件名前面加 2022-06_ 这样的前缀，重跑一次即可。')
    total = sum(f.stat().st_size for f in out_dir.iterdir()) // 1024
    print(f'\n共 {n} 张，{total}KB')
    if total > BUDGET_KB:
        sys.exit(f'超过 Hardware Buddy 的 1.8MB 上限。减少张数，或把 quality 调到 85。')
    print(f'好了。把 {out_dir} 拖到 Hardware Buddy 窗口上。')
    print('自动猜的圆心不满意就用 --center x,y --radius r 手工指定，比调参快。')


if __name__ == '__main__':
    main()
