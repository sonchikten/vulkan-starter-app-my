from PIL import Image
import random
import math
from perlin_noise import PerlinNoise   

# --- bricks_spec: серый 50% ---
Image.new("RGB", (256, 256), (128, 128, 128)).save("textures/bricks_spec.png")

# --- spec_checker: шахматка ---
img = Image.new("RGB", (256, 256))
px = img.load()
for y in range(256):
    for x in range(256):
        if ((x // 32) + (y // 32)) % 2 == 0:
            px[x, y] = (255, 255, 255)
        else:
            px[x, y] = (0, 0, 0)
img.save("textures/spec_checker.png")

# --- spec_spots: пятна ---
img = Image.new("RGB", (256, 256), (0, 0, 0))
px = img.load()
for _ in range(20):
    cx, cy = random.randint(0, 255), random.randint(0, 255)
    r = random.randint(10, 40)
    for y in range(max(0, cy - r), min(256, cy + r)):
        for x in range(max(0, cx - r), min(256, cx + r)):
            d = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
            if d < r:
                v = int(255 * (1 - d / r))
                px[x, y] = (v, v, v)
img.save("textures/spec_spots.png")

noise = PerlinNoise(octaves=4, seed=42)  # один раз

# --- bricks_emit (оранжевый) ---
img = Image.new("RGB", (256, 256))
px = img.load()
for y in range(256):
    for x in range(256):
        v = (noise([x / 256, y / 256]) + 1) / 2  # [-1, 1] → [0, 1]
        v = max(0.0, min(1.0, v))
        px[x, y] = (int(255 * v), int(60 * v), 0)
img.save("textures/bricks_emit.png")

# --- green_emit (зелёный) ---
img = Image.new("RGB", (256, 256))
px = img.load()
for y in range(256):
    for x in range(256):
        v = (noise([x / 256, y / 256]) + 1) / 2
        v = max(0.0, min(1.0, v))
        px[x, y] = (0, int(255 * v), 0)
img.save("textures/green_emit.png")

# --- zeepkats_emit (синий) ---
img = Image.new("RGB", (256, 256))
px = img.load()
for y in range(256):
    for x in range(256):
        v = (noise([x / 256, y / 256]) + 1) / 2
        v = max(0.0, min(1.0, v))
        px[x, y] = (0, 0, int(255 * v))
img.save("textures/zeepkats_emit.png")

print("done")