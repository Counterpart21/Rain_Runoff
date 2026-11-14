#!/usr/bin/env python3
"""Create the original seamless rain streak DDS, with transparent horizontal edges.

No third-party textures are used. Only the Python standard library is required.
DDS is uncompressed RGBA8 with a complete mip chain, supported by Skyrim SE.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct


def make_pixels(width: int, height: int) -> bytes:
    pixels = bytearray()
    for y in range(height):
        v = y / height
        for x in range(width):
            u = (x + 0.5) / width
            edge = max(0.0, min(1.0, (u - 0.125) / 0.12, (0.875 - u) / 0.12))
            phase = u + 0.014 * math.sin(v * math.tau * 3 + u * 12)
            thread = (0.5 + 0.5 * math.cos(phase * math.tau * 19)) ** 7
            veins = (0.5 + 0.5 * math.sin(phase * math.tau * 7 + v * math.tau)) ** 5
            pulse = 0.56 + 0.44 * math.sin(v * math.tau * 5 + u * 23) ** 2
            sparkle = (0.5 + 0.5 * math.cos(v * math.tau * 11 + u * 17)) ** 16
            alpha = edge * (0.12 + 0.34 * thread + 0.18 * veins) * pulse
            luminance = 0.56 + 0.26 * thread + 0.13 * sparkle
            pixels.extend((round(luminance * 225), round(luminance * 239),
                           round(luminance * 255), round(alpha * 255)))
    return bytes(pixels)


def downsample(data: bytes, width: int, height: int) -> bytes:
    result = bytearray()
    for y in range(max(1, height // 2)):
        for x in range(max(1, width // 2)):
            for channel in range(4):
                samples = [data[((min(height - 1, y * 2 + j) * width
                                 + min(width - 1, x * 2 + i)) * 4) + channel]
                           for j in range(2) for i in range(2)]
                result.append(round(sum(samples) / 4))
    # Clamp sampling must remain transparent beyond U=[0,1], including distant
    # mip levels. An opaque one-pixel mip would cover the full hidden ribbon.
    new_width, new_height = max(1, width // 2), max(1, height // 2)
    for y in range(new_height):
        result[(y * new_width) * 4 + 3] = 0
        result[(y * new_width + new_width - 1) * 4 + 3] = 0
    return bytes(result)


def write_dds(file: Path, width: int = 128, height: int = 256) -> None:
    levels = []
    pixels = make_pixels(width, height)
    w, h = width, height
    while True:
        levels.append(pixels)
        if w == h == 1:
            break
        pixels = downsample(pixels, w, h)
        w, h = max(1, w // 2), max(1, h // 2)
    # DDS_HEADER + DDS_PIXELFORMAT, little-endian R8G8B8A8, complete mip chain.
    header = [124, 0x0002100F, height, width, width * 4, 0, len(levels)]
    header += [0] * 11
    header += [32, 0x41, 0, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000]
    header += [0x00401008, 0, 0, 0, 0]
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_bytes(b"DDS " + struct.pack("<31I", *header) + b"".join(levels))
    assert len(header) == 31
    assert all(levels[0][(y * width) * 4 + 3] == 0 for y in range(height))
    assert all(levels[0][(y * width + width - 1) * 4 + 3] == 0 for y in range(height))
    print(f"Generated {file}: {width}x{height} RGBA8, {len(levels)} mips, transparent S borders")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path, nargs="?", default=Path(__file__).resolve().parents[1] / "Data/textures/RainRunoff/runoff.dds")
    args = parser.parse_args()
    write_dds(args.file)
