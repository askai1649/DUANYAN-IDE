import glob
from PIL import Image

files = sorted(glob.glob(r"c:\OPENSNAR\DUANYAN-IDE\src-tauri\vid_frames\f_*.png"))
rows = []
for fn in files:
    im = Image.open(fn).convert("RGB").resize((136, 240))
    px = list(im.getdata())
    w, h = im.size
    best = -1; bi = 0
    for i, (r, g, b) in enumerate(px):
        lum = r + g + b
        if lum > best:
            best = lum; bi = i
    x, y = bi % w, bi // w
    rs = gs = bs = n = 0
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h:
                r, g, b = px[ny * w + nx]
                rs += r; gs += g; bs += b; n += 1
    t = int(fn[-7:-4]) - 1
    R, G, B = rs // n, gs // n, bs // n
    mx = max(R, G, B); mn = min(R, G, B)
    # 分类: W=近白, C=饱和彩色
    tag = "W" if (mn > 100 and mx > 200) else "C"
    rows.append((t, x, y, R, G, B, tag))

for t, x, y, R, G, B, tag in rows:
    print(f"t={t:3d} {tag} ({R:3d},{G:3d},{B:3d}) pos=({x},{y})")
