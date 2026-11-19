"""Bake the authored 3.4.6 night field once; runtime uses textures, not noise loops."""
from pathlib import Path
import hashlib
import json
import math
import numpy as np
from PIL import Image, ImageFilter

OUT = Path(__file__).resolve().parents[2] / "SourceAssets" / "NightSky346"
OUT.mkdir(parents=True, exist_ok=True)
SEED = 34663103
rng = np.random.default_rng(SEED)


def tile_noise(width, height, gx, gy):
    grid = rng.random((gy, gx), dtype=np.float32)
    x = np.arange(width, dtype=np.float32) * gx / width
    y = np.arange(height, dtype=np.float32) * gy / height
    ix, iy = x.astype(np.int32), y.astype(np.int32)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = grid[iy[:, None] % gy, ix[None, :] % gx]
    b = grid[iy[:, None] % gy, (ix[None, :] + 1) % gx]
    c = grid[(iy[:, None] + 1) % gy, ix[None, :] % gx]
    d = grid[(iy[:, None] + 1) % gy, (ix[None, :] + 1) % gx]
    return (a * (1 - fx) + b * fx) * (1 - fy[:, None]) + (c * (1 - fx) + d * fx) * fy[:, None]


def fbm(width, height, base_x, base_y, octaves):
    total = np.zeros((height, width), np.float32)
    weight = 0.
    for octave in range(octaves):
        gain = .53 ** octave
        total += tile_noise(width, height, base_x * 2**octave, base_y * 2**octave) * gain
        weight += gain
    return total / weight


def direction(yaw, pitch):
    y, p = np.radians([yaw, pitch])
    return np.array([np.cos(p)*np.cos(y), np.cos(p)*np.sin(y), np.sin(p)], np.float32)


width, height = 4096, 2048
longitude = (np.arange(width, dtype=np.float32) / width - .5) * (2 * np.pi)
latitude = (.5 - np.arange(height, dtype=np.float32) / height) * np.pi
cos_lat = np.cos(latitude)[:, None]
dx = cos_lat * np.cos(longitude)[None, :]
dy = cos_lat * np.sin(longitude)[None, :]
dz = np.broadcast_to(np.sin(latitude)[:, None], (height, width))
normal = np.cross(direction(-30, -4.0), direction(30, 38.))
normal /= np.linalg.norm(normal)
band_latitude = np.arcsin(np.clip(dx*normal[0] + dy*normal[1] + dz*normal[2], -1, 1))
large = fbm(width, height, 12, 8, 6)
fine = fbm(width, height, 78, 40, 4)
micro = fbm(width, height, 240, 120, 3)
warp = (large - .5) * .095 + np.sin(longitude[None, :] * 7) * .009
band = band_latitude + warp
halo = np.exp(-.5 * (band / .105)**2)
spine = np.exp(-.5 * (band / .039)**2)
core_dir = direction(-9, 15.2)
core_distance = np.arccos(np.clip(dx*core_dir[0] + dy*core_dir[1] + dz*core_dir[2], -1, 1))
core = np.exp(-.5 * (core_distance / .27)**2)
dust = np.exp(-.5 * ((band + (fine-.5)*.070) / .012)**2)
filaments = np.exp(-.5 * ((band + .030 + (micro-.5)*.10) / .007)**2)
structure = np.clip((large - .25) * 1.6, .06, 1.)
grain = np.clip((fine * 2.)**2.3, .12, 2.4) * (.70 + micro*.60)
horizon = np.exp(-np.abs(dz) / .14)
rgb = np.zeros((height, width, 3), np.float32)
for channel, (base, mist) in enumerate(zip((.008, .014, .030), (.012, .020, .032))):
    rgb[..., channel] = base + horizon * mist
glow = halo * .055 * structure + spine * .28 * grain * (.25 + .75*structure)
glow += core * halo * .19 * structure
glow *= (1. - dust * (.78 + fine*.18)) * (1. - filaments*.48)
glow += spine * np.clip(micro - .49, 0, .3) * .36
for channel, tint in enumerate((.79, .83, 1.)):
    rgb[..., channel] += glow * tint
rgb[..., 0] += core * spine * .034
rgb[..., 1] += core * spine * .010
rgb[..., 2] += halo * fine * .012
del dx, dy, dz, core_distance, core, dust, filaments, grain, structure, fine, micro, large

# The point spread is baked: one texture fetch produces thousands of stars.
for _ in range(35000):
    x = int(rng.integers(width))
    z = rng.uniform(-1, 1)
    y = min(height - 1, int(math.acos(z) / math.pi * height))
    concentration = float(halo[y, x])
    if rng.random() > .22 + .78 * concentration:
        continue
    brightness = .09 + float(rng.random()**4) * .83
    temperature = rng.random()
    colour = np.array((.77, .88, 1.) if temperature < .58 else
                      (1., .84, .65) if temperature > .88 else (1., .98, .91), np.float32)
    rgb[y, x] += colour * brightness
    if brightness > .45:
        for yy, xx, amplitude in ((0, 1, .12), (0, -1, .12), (1, 0, .12), (-1, 0, .12)):
            if 0 <= y+yy < height:
                rgb[y+yy, (x+xx) % width] += colour * brightness * amplitude
    if brightness > .84:
        for yy in range(-3, 4):
            if not 0 <= y+yy < height: continue
            for xx in range(-3, 4):
                rgb[y+yy, (x+xx) % width] += colour * .032 * math.exp(-(xx*xx+yy*yy)/3.)

sky = Image.fromarray(np.uint8(np.clip(rgb, 0, 1) * 255), "RGB")
sky.save(OUT / "MilkyWay346.png")
sky.resize((1536, 768), Image.Resampling.LANCZOS).save(OUT / "MilkyWay346-preview.png")
cloud = fbm(1024, 1024, 5, 4, 7)
cloud = np.clip((cloud - .26) / .52, 0, 1)
Image.fromarray(np.uint8(cloud * 255), "L").save(OUT / "CloudField346.png")

# A normal 68-degree horizontal / 16:9 garden view, yaw zero, +2 degree pitch.
vw, vh = 1280, 720
xx = ((np.arange(vw) + .5) / vw * 2 - 1) * math.tan(math.radians(34))
yy = (1 - (np.arange(vh) + .5) / vh * 2) * math.tan(math.radians(34)) * vh / vw
ray_x = np.ones((vh, vw), np.float32)
ray_y = np.broadcast_to(xx[None, :], (vh, vw))
ray_z = np.broadcast_to(yy[:, None], (vh, vw))
pitch = math.radians(2.)
ray_x, ray_z = ray_x*math.cos(pitch)-ray_z*math.sin(pitch), ray_x*math.sin(pitch)+ray_z*math.cos(pitch)
length = np.sqrt(ray_x**2 + ray_y**2 + ray_z**2)
u = (np.arctan2(ray_y, ray_x) / (2*np.pi) + .5) % 1.
v = np.arccos(ray_z / length) / np.pi
preview = np.array(sky)[np.minimum(height-1, (v*height).astype(int)), (u*width).astype(int) % width]
Image.fromarray(preview, "RGB").save(OUT / "LevelView346-preview.png")
manifest = {
    "seed": SEED,
    "galaxy_plane_endpoints_yaw_elevation": [[-30,-4],[30,38]],
    "galaxy_core_yaw_elevation": [-9,15.2],
    "garden_default_pitch_degrees": 2,
    "authorship": "Original deterministic procedural game art; no observed star catalogue or external galaxy photograph.",
    "runtime": "4K equirectangular star field; 1K periodic cloud density; one opaque sky sphere and one translucent cloud sphere; no runtime noise octaves or volumetric clouds.",
    "files": {name: hashlib.sha256((OUT/name).read_bytes()).hexdigest()
              for name in ("MilkyWay346.png", "CloudField346.png")},
}
(OUT / "source_manifest346.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
print(OUT)
