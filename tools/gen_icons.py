#!/usr/bin/env python3
"""从 resources/icons/*.svg 生成各平台的图标文件（生成结果已提交，CI 不需要运行它）。

依赖：vips 命令行（libvips，带 librsvg）和 Pillow。
  python tools/gen_icons.py [--vips 路径]
≤32px 用简化版 snappin-small.svg，更大的尺寸用 snappin.svg。
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, "resources", "icons")
SMALL_MAX = 32
PNG_SIZES = [256, 512]   # Linux 安装用
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]


def render(vips, size, out):
    small = size <= SMALL_MAX
    svg = os.path.join(ICONS, "snappin-small.svg" if small else "snappin.svg")
    base = 32 if small else 256
    subprocess.run([vips, "svgload", svg, out, "--scale", str(size / base)], check=True)
    im = Image.open(out).convert("RGBA")
    if im.size != (size, size):
        raise SystemExit(f"{svg} 渲染成 {im.size}，期望 {size}x{size}")
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vips", default=shutil.which("vips") or "vips")
    args = ap.parse_args()

    tmp = tempfile.mkdtemp()
    images = {}
    for s in sorted(set(PNG_SIZES + ICO_SIZES + ICNS_SIZES)):
        images[s] = render(args.vips, s, os.path.join(tmp, f"{s}.png"))

    png_dir = os.path.join(ICONS, "png")
    os.makedirs(png_dir, exist_ok=True)
    for s in PNG_SIZES:
        images[s].save(os.path.join(png_dir, f"snappin-{s}.png"), optimize=True)

    largest = images[ICO_SIZES[-1]]
    largest.save(os.path.join(ICONS, "snappin.ico"), sizes=[(s, s) for s in ICO_SIZES],
                 append_images=[images[s] for s in ICO_SIZES[:-1]])
    big = images[ICNS_SIZES[-1]]
    big.save(os.path.join(ICONS, "snappin.icns"),
             append_images=[images[s] for s in ICNS_SIZES[:-1]])
    shutil.copyfile(os.path.join(ICONS, "snappin.svg"),
                    os.path.join(ROOT, "resources", "linux", "snappin.svg"))
    shutil.rmtree(tmp)
    print("done")


if __name__ == "__main__":
    sys.exit(main())
