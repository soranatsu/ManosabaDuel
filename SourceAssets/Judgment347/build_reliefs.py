"""Build three double-sided, bevelled champlevé plaques in metres from the original enamel atlas."""
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates


SOURCE = Path(__file__).resolve().parent
ATLAS = np.asarray(Image.open(SOURCE / "T_JudgmentEnamel.png").convert("RGB"), dtype=float) / 255.0
NAMES = ["SM_JudgmentEye", "SM_JudgmentScales", "SM_JudgmentCrown"]
SEGMENTS = 96
RINGS = 25


def plaque(index):
    tile = np.array_split(ATLAS, 3, axis=1)[index]
    # A washed highlight cannot alone define relief. A warm-metal mask keeps enamel below gold.
    warmth = np.clip((tile[:, :, 0] - tile[:, :, 2]) * 3.0 + .16, 0, 1)
    height = gaussian_filter((tile @ np.array([.3, .55, .15])) * (.5 + warmth), 3.6)
    positions, texcoords, faces = [], [], []

    def add_vertex(side, radius, angle, rim=False):
        sector = math.pi / 6
        polygon = math.cos(sector / 2) / math.cos(angle % sector - sector / 2)
        u = .5 + .488 * radius * math.sin(angle) * polygon
        v = .49 - .475 * radius * math.cos(angle) * polygon
        relief = map_coordinates(height, [[v*(tile.shape[0]-1)], [u*(tile.shape[1]-1)]], order=1)[0]
        depth = .045 + .025 * (1-radius*radius) + .032 * relief
        if rim:
            depth = .036
        x = (u - .5) * .993
        # Reverse texture orientation on the back of the pendant.
        texcoord_u = u if side > 0 else 1-u
        positions.append([x, (.49-v)*1.10, side * depth])
        texcoords.append([(index+texcoord_u)/3, v])
        return len(positions)-1

    rims = []
    for side in (1, -1):
        center = add_vertex(side, 0, 0)
        previous = None
        for ring in range(1, RINGS+2):
            radius = ring / RINGS * .986 if ring <= RINGS else 1
            current = [add_vertex(side, radius, j*math.tau/SEGMENTS, ring>RINGS) for j in range(SEGMENTS)]
            for j in range(SEGMENTS):
                n = (j+1) % SEGMENTS
                tris = [(center, current[n], current[j])] if previous is None else [
                    (previous[j], current[n], current[j]), (previous[j], previous[n], current[n])]
                faces.extend(tris if side > 0 else [tuple(reversed(t)) for t in tris])
            previous = current
        rims.append(previous)
    front, back = rims
    for j in range(SEGMENTS):
        n = (j+1) % SEGMENTS
        faces.extend([(front[j], front[n], back[n]), (front[j], back[n], back[j])])
    pos = np.array(positions, dtype="<f4")
    face = np.array(faces, dtype="<u2")
    normals = np.zeros_like(pos)
    cross = np.cross(pos[face[:, 1]]-pos[face[:, 0]], pos[face[:, 2]]-pos[face[:, 0]])
    for col in face.T:
        np.add.at(normals, col, cross)
    normals /= np.maximum(np.linalg.norm(normals,axis=1,keepdims=True), 1e-10)
    assert normals[0,2] > .75
    return pos, normals, np.array(texcoords,dtype="<f4"), face.reshape(-1)


def write_glb(name, arrays, output):
    buffer = bytearray()
    views, accessors = [], []
    for i, a in enumerate(arrays):
        buffer.extend(b"\0" * (-len(buffer)%4))
        raw = a.tobytes()
        views.append(dict(buffer=0,byteOffset=len(buffer),byteLength=len(raw)))
        buffer.extend(raw)
        acc = dict(bufferView=i,componentType=5123 if i==3 else 5126,count=len(a),
                   type=["VEC3","VEC3","VEC2","SCALAR"][i])
        if i==0:
            acc.update(min=a.min(axis=0).tolist(),max=a.max(axis=0).tolist())
        accessors.append(acc)
    model = dict(asset=dict(version="2.0"), scenes=[dict(nodes=[0])],scene=0,
                 nodes=[dict(mesh=0,name=name)], meshes=[dict(name=name,primitives=[
                     dict(attributes=dict(POSITION=0,NORMAL=1,TEXCOORD_0=2),indices=3)])],
                 buffers=[dict(byteLength=len(buffer))], bufferViews=views, accessors=accessors)
    meta=json.dumps(model,separators=(",",":")).encode()
    meta+=b" "*(-len(meta)%4)
    buffer.extend(b"\0"*(-len(buffer)%4))
    data=struct.pack("<I4s",len(meta),b"JSON")+meta+struct.pack("<I4s",len(buffer),b"BIN\0")+buffer
    (output/(name+".glb")).write_bytes(struct.pack("<4sII",b"glTF",2,len(data)+12)+data)


if __name__ == "__main__":
    import sys
    out = Path(sys.argv[1])
    out.mkdir(exist_ok=True,parents=True)
    for i, name in enumerate(NAMES):
        write_glb(name, plaque(i), out)
