import os, glob
from PIL import Image

files = sorted(glob.glob(r"c:\OPENSNAR\DUANYAN-IDE\src-tauri\vid_frames\f_*.png"))
print("frames:", len(files))
rows = []
for fn in files:
    im = Image.open(fn).convert("RGB").resize((136, 240))
    px = list(im.getdata())
    w, h = im.size
    # 找最亮像素及其位置
    best = -1; bi = 0
    for i, (r, g, b) in enumerate(px):
        lum = r + g + b
        if lum > best:
            best = lum; bi = i
    x, y = bi % w, bi // w
    # 最亮点周围 5x5 均值
    rs = gs = bs = n = 0
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h:
                r, g, b = px[ny * w + nx]
                rs += r; gs += g; bs += b; n += 1
    t = int(fn[-7:-4]) - 1
    rows.append((t, x, y, rs // n, gs // n, bs // n))

# 每 5 秒打印一行
for t, x, y, r, g, b in rows:
    if t % 5 == 0:
        print(f"t={t:3d}s pos=({x},{y}) rgb=({r:3d},{g:3d},{b:3d})")
