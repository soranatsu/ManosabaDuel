"""Run in Unreal Editor to rebuild the bundled materials, textures, scans and rain loop."""

import hashlib
import json
from pathlib import Path
import struct

import unreal


ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE = ROOT / "SourceAssets"
MATERIALS = "/Game/DuelArt/Materials"
TEXTURES = "/Game/DuelArt/Textures"
AUDIO = "/Game/DuelArt/Audio"
UI = "/Game/DuelArt/UI"
SCANS = "/Game/DuelArt/Scans"
ART = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
GRAPH = unreal.MaterialEditingLibrary
PIN = unreal.MaterialProperty
SAMPLER = unreal.MaterialSamplerType


def load(path):
    return ART.load_asset(path) if ART.does_asset_exist(path) else None


def import_source(name, folder, *, replace=False):
    path = f"{folder}/{name}"
    asset = load(path)
    if asset and not replace:
        return asset, False
    candidates = [item for item in SOURCE.glob(f"{name}.*")
                  if item.suffix.lower() in {".png", ".jpg", ".jpeg", ".hdr", ".exr", ".wav"}]
    if len(candidates) != 1:
        raise RuntimeError(f"Missing or ambiguous texture source: {name}")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(candidates[0]))
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", replace)
    task.set_editor_property("save", False)
    TOOLS.import_asset_tasks([task])
    asset = load(path)
    if not asset:
        imported = task.get_editor_property("imported_object_paths")
        asset = ART.load_asset(imported[0]) if imported else None
    if not asset:
        raise RuntimeError(f"Could not import {name}")
    return asset, True


def texture(name, *, normal=False, linear=False):
    asset, created = import_source(name, TEXTURES)
    is_cube = isinstance(asset, unreal.TextureCube)
    expected_srgb = not (normal or linear or is_cube)
    changed = False
    if not is_cube:
        compression = unreal.TextureCompressionSettings
        expected = compression.TC_NORMALMAP if normal else (compression.TC_MASKS if linear else compression.TC_DEFAULT)
        if asset.get_editor_property("compression_settings") != expected:
            asset.set_editor_property("compression_settings", expected)
            changed = True
    if asset.get_editor_property("srgb") != expected_srgb:
        asset.set_editor_property("srgb", expected_srgb)
        changed = True
    if asset.get_editor_property("srgb") != expected_srgb:
        raise RuntimeError(f"Unexpected color space for {name}")
    if (created or changed) and not ART.save_loaded_asset(asset, False):
        raise RuntimeError(f"Could not save {name}")
    return asset


def import_scan(name, resolution="2k"):
    folder = f"{SCANS}/{name}"
    path = f"{folder}/{name}"
    existing = load(path)
    if existing:
        if ART.get_metadata_tag(existing, "ManosabaScan") == "1":
            return
        if not ART.delete_loaded_asset(existing):
            raise RuntimeError(f"Could not replace incomplete import {name}")
    filename = SOURCE / "Scans" / name / f"{name}_{resolution}.gltf"
    if not filename.is_file():
        raise RuntimeError(f"Missing scan source {name}")
    if name == "large_iron_gate":
        gltf = json.loads(filename.read_text(encoding="utf-8"))
        if len(gltf["scenes"]) != 1 or any(set(node) - {"mesh", "name"} for node in gltf["nodes"]):
            raise RuntimeError("The gate parts must share an identity transform")
        gltf["meshes"] = [{"name": name, "primitives": [
            primitive for mesh in gltf["meshes"] for primitive in mesh["primitives"]
        ]}]
        gltf["nodes"] = [{"name": name, "mesh": 0}]
        gltf["scenes"][0]["nodes"] = [0]
        filename = filename.with_name(f"{name}_combined.gltf")
        filename.write_text(json.dumps(gltf, separators=(",", ":")), encoding="utf-8")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(filename))
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    TOOLS.import_asset_tasks([task])
    meshes = [asset for asset in task.get_objects() if isinstance(asset, unreal.StaticMesh)]
    if len(meshes) != 1:
        raise RuntimeError(f"Expected one static mesh for {name}, got {len(meshes)}")
    mesh = meshes[0]
    if mesh.get_path_name() != f"{path}.{name}" and not ART.rename_loaded_asset(mesh, path):
        raise RuntimeError(f"Could not rename {name}")
    settings = mesh.get_editor_property("nanite_settings")
    settings.enabled = True
    mesh.set_editor_property("nanite_settings", settings)
    ART.set_metadata_tag(mesh, "ManosabaScan", "1")
    if not ART.save_directory(folder, False):
        raise RuntimeError(f"Could not save {name}")


def import_castle_scan(name, parts=None):
    if ART.does_asset_exist(f"{SCANS}/{name}/{name}"):
        return
    path = SOURCE / "Scans" / name / f"{name}_2k.gltf"
    source = json.loads(path.read_text(encoding="utf-8"))
    nodes = [source["nodes"][index] for index in source["scenes"][0]["nodes"]]
    chosen = [node for node in nodes if not parts or node["name"] in parts]
    if not chosen or len(source["buffers"]) != 1:
        raise RuntimeError(f"Invalid castle scan geometry {name}")
    data = bytearray((path.parent / source["buffers"][0]["uri"]).read_bytes())
    primitives = []
    for item in chosen:
        offset = item.get("translation", (0, 0, 0)) if len(chosen) > 1 else (0, 0, 0)
        qx, qy, qz, qw = item.get("rotation", (0, 0, 0, 1))
        changed = set()
        for primitive in source["meshes"][item["mesh"]]["primitives"]:
            for channel in ("POSITION", "NORMAL"):
                index = primitive["attributes"][channel]
                if index in changed:
                    continue
                changed.add(index)
                accessor = source["accessors"][index]
                view = source["bufferViews"][accessor["bufferView"]]
                if accessor["componentType"] != 5126 or accessor["type"] != "VEC3":
                    raise RuntimeError(f"Expected floating-point vectors in {name}")
                first = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
                stride = view.get("byteStride", 12)
                low = [float("inf")] * 3
                high = [float("-inf")] * 3
                for vertex in range(accessor["count"]):
                    address = first + stride * vertex
                    x, y, z = struct.unpack_from("<3f", data, address)
                    tx, ty, tz = 2 * (qy * z - qz * y), 2 * (qz * x - qx * z), 2 * (qx * y - qy * x)
                    values = (x + qw * tx + qy * tz - qz * ty,
                              y + qw * ty + qz * tx - qx * tz,
                              z + qw * tz + qx * ty - qy * tx)
                    if channel == "POSITION":
                        values = tuple(v + d for v, d in zip(values, offset))
                    struct.pack_into("<3f", data, address, *values)
                    low = [min(a, b) for a, b in zip(low, values)]
                    high = [max(a, b) for a, b in zip(high, values)]
                if channel == "POSITION":
                    accessor["min"], accessor["max"] = low, high
            primitives.append(primitive)
    buffer = path.with_name(f"{name}_assembled.bin")
    buffer.write_bytes(data)
    source["buffers"][0]["uri"] = buffer.name
    source["meshes"] = [{"name": name, "primitives": primitives}]
    source["nodes"] = [{"mesh": 0, "name": name}]
    source["scenes"][0]["nodes"] = [0]
    derived = path.with_name(f"{name}_assembled.gltf")
    derived.write_text(json.dumps(source, separators=(",", ":")), encoding="utf-8")
    import_scan(name, "assembled")


def node(material, cls, x, y, **properties):
    expression = GRAPH.create_material_expression(material, cls, x, y)
    if not expression:
        raise RuntimeError(f"Could not create {cls.__name__}")
    for key, value in properties.items():
        expression.set_editor_property(key, value)
    return expression


def wire(output, output_name, target, input_name):
    if not GRAPH.connect_material_expressions(output, output_name, target, input_name):
        raise RuntimeError(f"Could not connect {input_name}")


def output(expression, name, pin):
    if not GRAPH.connect_material_property(expression, name, pin):
        raise RuntimeError(f"Could not connect {pin}")


def scalar(material, value, x=-250, y=320):
    return node(material, unreal.MaterialExpressionConstant, x, y, r=value)


def sample(material, asset, sampler, x, y):
    return node(material, unreal.MaterialExpressionTextureSample, x, y,
                texture=asset, sampler_type=sampler)


def expression(material, code, kind, x, y, **inputs):
    sockets = []
    for name in inputs:
        socket = unreal.CustomInput()
        socket.set_editor_property("input_name", name)
        sockets.append(socket)
    custom = node(material, unreal.MaterialExpressionCustom, x, y,
                  code=code, output_type=kind, inputs=sockets)
    for name, source in inputs.items():
        wire(source, "", custom, name)
    return custom


def parameter(material, name, value, x, y):
    return node(material, unreal.MaterialExpressionScalarParameter, x, y,
                parameter_name=name, default_value=value)


def make(name, recipe, revision="1"):
    path = f"{MATERIALS}/{name}"
    material = load(path)
    if material and ART.get_metadata_tag(material, "ManosabaRecipe") == revision:
        return
    if material:
        GRAPH.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError(f"Could not create {name}")
    recipe(material)
    GRAPH.recompile_material(material)
    ART.set_metadata_tag(material, "ManosabaRecipe", revision)
    if not ART.save_loaded_asset(material, False):
        ART.remove_metadata_tag(material, "ManosabaRecipe")
        raise RuntimeError(f"Could not save {name}")


def stone(material, label, tint, rough_gain, *, wet=False):
    base = sample(material, texture(f"T_{label}Base"), SAMPLER.SAMPLERTYPE_COLOR, -820, -230)
    normal = sample(material, texture(f"T_{label}Normal", normal=True), SAMPLER.SAMPLERTYPE_NORMAL, -500, 50)
    arm = sample(material, texture(f"T_{label}ARM", linear=True), SAMPLER.SAMPLERTYPE_MASKS, -820, 340)
    color = node(material, unreal.MaterialExpressionVectorParameter, -820, -430,
                 parameter_name="Color", default_value=unreal.LinearColor(*tint, 1))
    color_product = node(material, unreal.MaterialExpressionMultiply, -425, -210)
    wire(base, "RGB", color_product, "A")
    wire(color, "", color_product, "B")
    vertex = node(material, unreal.MaterialExpressionVertexColor, -405, -443)
    weathered = node(material, unreal.MaterialExpressionMultiply, -185, -210)
    wire(color_product, "", weathered, "A")
    wire(vertex, "", weathered, "B")
    rough = node(material, unreal.MaterialExpressionMultiply, -420, 345, const_b=rough_gain)
    wire(arm, "G", rough, "A")
    if wet:
        fade = node(material, unreal.MaterialExpressionSmoothStep, -190, -410, const_min=.06, const_max=.65)
        wire(vertex, "A", fade, "Value")
        submerged = node(material, unreal.MaterialExpressionMultiply, 0, -210, const_b=.58)
        wire(weathered, "", submerged, "A")
        base_blend = node(material, unreal.MaterialExpressionLinearInterpolate, 220, -210)
        wire(weathered, "", base_blend, "A")
        wire(submerged, "", base_blend, "B")
        wire(fade, "", base_blend, "Alpha")
        flat = node(material, unreal.MaterialExpressionConstant3Vector, 0, 40,
                    constant=unreal.LinearColor(0, 0, 1, 1))
        normal_blend = node(material, unreal.MaterialExpressionLinearInterpolate, 220, 40)
        wire(normal, "RGB", normal_blend, "A")
        wire(flat, "", normal_blend, "B")
        wire(fade, "", normal_blend, "Alpha")
        rough_blend = node(material, unreal.MaterialExpressionLinearInterpolate, 220, 345, const_b=.125)
        wire(rough, "", rough_blend, "A")
        wire(fade, "", rough_blend, "Alpha")
        weathered, normal, rough = base_blend, normal_blend, rough_blend
    output(weathered, "", PIN.MP_BASE_COLOR)
    output(normal, "" if wet else "RGB", PIN.MP_NORMAL)
    output(arm, "R", PIN.MP_AMBIENT_OCCLUSION)
    output(rough, "", PIN.MP_ROUGHNESS)


def figure(material, surface=None):
    color = node(material, unreal.MaterialExpressionVectorParameter, -420, -110,
                 parameter_name="Color", default_value=unreal.LinearColor(1, 1, 1, 1))
    roughness = node(material, unreal.MaterialExpressionScalarParameter, -420, 105,
                     parameter_name="Roughness", default_value=.69)
    metal = node(material, unreal.MaterialExpressionScalarParameter, -420, 235,
                 parameter_name="Metallic", default_value=0.0)
    if surface:
        base = sample(material, texture(f"T_{surface}Base"), SAMPLER.SAMPLERTYPE_COLOR, -910, -180)
        normal = sample(material, texture(f"T_{surface}Normal", normal=True), SAMPLER.SAMPLERTYPE_NORMAL, -910, 80)
        arm = sample(material, texture(f"T_{surface}ARM", linear=True), SAMPLER.SAMPLERTYPE_MASKS, -910, 365)
        grain = node(material, unreal.MaterialExpressionLinearInterpolate, -660, -170,
                     const_a=.76, const_alpha=.55 if surface == "Forge" else .32)
        wire(base, "RGB", grain, "B")
        tinted = node(material, unreal.MaterialExpressionMultiply, -190, -120)
        wire(color, "", tinted, "A")
        wire(grain, "", tinted, "B")
        output(tinted, "", PIN.MP_BASE_COLOR)
        output(normal, "RGB", PIN.MP_NORMAL)
        rough = node(material, unreal.MaterialExpressionLinearInterpolate, -200, 180, const_alpha=.36)
        wire(roughness, "", rough, "A")
        wire(arm, "G", rough, "B")
        output(rough, "", PIN.MP_ROUGHNESS)
        output(arm, "R", PIN.MP_AMBIENT_OCCLUSION)
    else:
        output(color, "", PIN.MP_BASE_COLOR)
        output(roughness, "", PIN.MP_ROUGHNESS)
    output(metal, "", PIN.MP_METALLIC)
    output(scalar(material, .28, y=415), "", PIN.MP_SPECULAR)


def arc_light(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    color = node(material, unreal.MaterialExpressionVectorParameter, -410, -105,
                 parameter_name="Color", default_value=unreal.LinearColor(1, .08, .24, .9))
    output(color, "", PIN.MP_EMISSIVE_COLOR)
    output(color, "A", PIN.MP_OPACITY)


def breath(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    uv = node(material, unreal.MaterialExpressionTextureCoordinate, -900, -90)
    center = node(material, unreal.MaterialExpressionConstant2Vector, -900, 70, r=.5, g=.5)
    radius = node(material, unreal.MaterialExpressionDistance, -680, 0)
    wire(uv, "", radius, "A")
    wire(center, "", radius, "B")
    fade = node(material, unreal.MaterialExpressionSmoothStep, -450, 0, const_min=.04, const_max=.48)
    wire(radius, "", fade, "Value")
    soft = node(material, unreal.MaterialExpressionOneMinus, -200, 0)
    wire(fade, "", soft, "")
    opacity = node(material, unreal.MaterialExpressionScalarParameter, -200, 200,
                   parameter_name="Opacity", default_value=0)
    cloud = node(material, unreal.MaterialExpressionMultiply, 0, 0)
    wire(soft, "", cloud, "A")
    wire(opacity, "", cloud, "B")
    tone = node(material, unreal.MaterialExpressionConstant3Vector, 0, -190,
                constant=unreal.LinearColor(.56, .65, .74, 1))
    output(tone, "", PIN.MP_EMISSIVE_COLOR)
    output(cloud, "", PIN.MP_OPACITY)


def sacred_light(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    kind = unreal.CustomMaterialOutputType
    uv = node(material, unreal.MaterialExpressionTextureCoordinate, -1250, -330)
    origin = node(material, unreal.MaterialExpressionVectorParameter, -1250, -175,
                  parameter_name="TileOrigin", default_value=unreal.LinearColor(0, 0, 0, 0))
    coords = expression(material,
        "return frac(UV * float2(Repeat, 1)) * TileSize + TileOrigin.rg;",
        kind.CMOT_FLOAT2, -925, -325, UV=uv, TileOrigin=origin,
        TileSize=parameter(material, "TileSize", 1, -1250, 0),
        Repeat=parameter(material, "RepeatX", 1, -1250, 90))
    pattern = sample(material, texture("T_SacredFx", linear=True), SAMPLER.SAMPLERTYPE_MASKS, -665, -330)
    wire(coords, "", pattern, "UVs")
    color = node(material, unreal.MaterialExpressionVectorParameter, -625, -97,
                 parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    vertex = node(material, unreal.MaterialExpressionVertexColor, -645, 165)
    tone = expression(material,
        "float core = saturate(Pattern.g * 1.32);"
        "return lerp(Tint.rgb * Vertex.rgb, float3(1, .91, .73), core) * Gain;",
        kind.CMOT_FLOAT3, -178, -210, Pattern=pattern, Tint=color, Vertex=vertex,
        Gain=parameter(material, "Gain", 5.1, -635, 60))
    alpha = node(material, unreal.MaterialExpressionMultiply, -160, 145)
    wire(pattern, "R", alpha, "A")
    wire(vertex, "A", alpha, "B")
    envelope = node(material, unreal.MaterialExpressionMultiply, 33, 147)
    wire(alpha, "", envelope, "A")
    wire(parameter(material, "Opacity", 1, -145, 256), "", envelope, "B")
    depth = node(material, unreal.MaterialExpressionDepthFade, 235, 116)
    wire(envelope, "", depth, "Opacity")
    wire(parameter(material, "SoftDepth", 22, 17, 304), "", depth, "FadeDistance")
    output(tone, "", PIN.MP_EMISSIVE_COLOR)
    output(depth, "", PIN.MP_OPACITY)


def blade_wake(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    kind = unreal.CustomMaterialOutputType
    uv = node(material, unreal.MaterialExpressionTextureCoordinate, -790, -245)
    vertex = node(material, unreal.MaterialExpressionVertexColor, -800, 20)
    tint = node(material, unreal.MaterialExpressionVectorParameter, -800, 200,
                parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    profile = expression(material, """
        float width = UV.y;
        float edge = exp(-pow((width - .94) / .026, 2));
        float lip = exp(-pow((width - .82) / .14, 2));
        float filament = .5 + .5 * sin(width * 44 + UV.x * 11);
        float body = smoothstep(.12, .58, width) * (1 - smoothstep(.91, 1, width));
        float taper = smoothstep(0, .17, UV.x) * (1 - smoothstep(.93, 1, UV.x));
        return float3(edge, lip + filament * body * .26, taper * saturate(body * .66 + edge));
        """, kind.CMOT_FLOAT3, -535, -187, UV=uv)
    tone = expression(material, """
        float3 chroma = Tint.rgb * Vertex.rgb;
        float3 hot = lerp(chroma, float3(1, .84, .65), .18);
        return chroma * (.018 + Profile.y * .24) + hot * Profile.x * 5.2;
        """, kind.CMOT_FLOAT3, -195, -187, Profile=profile, Tint=tint, Vertex=vertex)
    profile_mask = expression(material, "return Profile.z * Opacity;",
                      kind.CMOT_FLOAT1, -372, 85, Profile=profile,
                      Opacity=parameter(material, "Opacity", 1, -540, 220))
    mask = node(material, unreal.MaterialExpressionMultiply, -155, 115)
    wire(profile_mask, "", mask, "A")
    wire(vertex, "A", mask, "B")
    depth = node(material, unreal.MaterialExpressionDepthFade, 80, 125, fade_distance_default=10.0)
    wire(mask, "", depth, "Opacity")
    output(tone, "", PIN.MP_EMISSIVE_COLOR)
    output(depth, "", PIN.MP_OPACITY)


def combat_mist(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    kind = unreal.CustomMaterialOutputType
    uv = node(material, unreal.MaterialExpressionTextureCoordinate, -990, -210)
    time = node(material, unreal.MaterialExpressionTime, -990, 10)
    vertex = node(material, unreal.MaterialExpressionVertexColor, -680, 180)
    coords = expression(material, """
        float2 p = UV * 4 - float2(2, 2);
        p.x += .09 * sin(p.y * 6 + Time * 2.2) * sin(p.x * 3.14159);
        p.y += .07 * sin(p.x * 7 - Time * 2.9) * sin(p.y * 3.14159);
        return (saturate(p) * .247 + float2(.5015, .5015));
        """, kind.CMOT_FLOAT2, -767, -242, UV=uv, Time=time)
    pattern = sample(material, texture("T_SacredFx", linear=True), SAMPLER.SAMPLERTYPE_MASKS, -550, -175)
    wire(coords, "", pattern, "UVs")
    tone = expression(material, "return Vertex.rgb * (.18 + .73 * pow(Pattern.r, 1.8));",
                      kind.CMOT_FLOAT3, -191, -175, Vertex=vertex, Pattern=pattern)
    pattern_mask = expression(material, "return pow(saturate(Pattern.r), 1.36);",
                              kind.CMOT_FLOAT1, -330, 20, Pattern=pattern)
    mask = node(material, unreal.MaterialExpressionMultiply, -140, 75)
    wire(pattern_mask, "", mask, "A")
    wire(vertex, "A", mask, "B")
    depth = node(material, unreal.MaterialExpressionDepthFade, 60, 70, fade_distance_default=48.0)
    wire(mask, "", depth, "Opacity")
    output(tone, "", PIN.MP_EMISSIVE_COLOR)
    output(depth, "", PIN.MP_OPACITY)


def evidence_docket(material):
    kind = unreal.CustomMaterialOutputType
    map_texture = sample(material, texture("T_EvidenceDocket", linear=True), SAMPLER.SAMPLERTYPE_MASKS, -940, -220)
    stone = sample(material, texture("T_WallBase"), SAMPLER.SAMPLERTYPE_COLOR, -940, 50)
    metal = node(material, unreal.MaterialExpressionMultiply, -450, 290, const_b=.72)
    wire(map_texture, "R", metal, "A")
    base = expression(material,
        "return lerp(Stone.rgb * float3(.14, .21, .24), float3(.40, .26, .093), Mask.r);",
        kind.CMOT_FLOAT3, -442, -180, Mask=map_texture, Stone=stone)
    glow = expression(material, "return Mask.g * float3(1.44, .87, .39);",
                      kind.CMOT_FLOAT3, -439, 58, Mask=map_texture)
    output(base, "", PIN.MP_BASE_COLOR)
    output(glow, "", PIN.MP_EMISSIVE_COLOR)
    output(metal, "", PIN.MP_METALLIC)
    output(scalar(material, .54, y=440), "", PIN.MP_ROUGHNESS)


def witness_glass(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    kind = unreal.CustomMaterialOutputType
    pane = sample(material, texture("T_WitnessGlass"), SAMPLER.SAMPLERTYPE_COLOR, -820, -180)
    output(pane, "A", PIN.MP_OPACITY_MASK)
    base = expression(material, "return Pane.rgb * .32;", kind.CMOT_FLOAT3, -420, -170, Pane=pane)
    light = expression(material, "return Pane.rgb * smoothstep(.022, .28, max(Pane.r, max(Pane.g, Pane.b))) * 2.65;",
                       kind.CMOT_FLOAT3, -420, 30, Pane=pane)
    output(base, "", PIN.MP_BASE_COLOR)
    output(light, "", PIN.MP_EMISSIVE_COLOR)
    output(scalar(material, .33, y=220), "", PIN.MP_ROUGHNESS)


def fog_gate(material):
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    kind = unreal.CustomMaterialOutputType
    uv = node(material, unreal.MaterialExpressionTextureCoordinate, -1150, -150)
    time = node(material, unreal.MaterialExpressionTime, -1150, 105)
    mist_uv = expression(material,
                         "float2 wind = float2(sin(UV.y * 4.1 + T * .42) * .23, sin(UV.x * 7 + T * .31) * .17);"
                         "return frac(UV * float2(1.27, 1.07) + wind + float2(T * .048, T * -.062)) * .25 + float2(.5, .5);",
                         kind.CMOT_FLOAT2, -850, -180, UV=uv, T=time)
    mist = sample(material, texture("T_SacredFx", linear=True), SAMPLER.SAMPLERTYPE_MASKS, -550, -160)
    wire(mist_uv, "", mist, "UVs")
    density = expression(material,
        "float2 p = UV * 2 - 1;"
        "float arch = length(float2(p.x, max(0, -p.y - .33) * 1.56));"
        "float rim = 1 - smoothstep(.84, .994, arch);"
        "float foot = 1 - smoothstep(.78, 1, p.y);"
        "float tendril = sin(p.x * 13 + sin(p.y * 6 - T * .72) * 1.8 + p.y * 3.4);"
        "float broad = sin(p.x * 5.9 + p.y * 4 + T * .39) * .12;"
        "return rim * foot * saturate(.29 + Mist.r * .59 + tendril * .09 + broad);",
        kind.CMOT_FLOAT1, -216, 68, UV=uv, Mist=mist, T=time)
    tint = node(material, unreal.MaterialExpressionConstant3Vector, -360, -240,
                constant=unreal.LinearColor(.54, .70, .92, 1))
    light = node(material, unreal.MaterialExpressionMultiply, -90, -180, const_b=.72)
    wire(tint, "", light, "A")
    output(light, "", PIN.MP_EMISSIVE_COLOR)
    output(density, "", PIN.MP_OPACITY)


def overcast(material):
    cubemap = texture("T_Overcast", linear=True)
    if not isinstance(cubemap, unreal.TextureCube):
        raise RuntimeError("Overcast HDR did not import as a TextureCube")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("is_sky", False)
    direction = node(material, unreal.MaterialExpressionCameraVectorWS, -930, 0)
    inverse = node(material, unreal.MaterialExpressionMultiply, -685, 0, const_b=-1.0)
    wire(direction, "", inverse, "A")
    sky = sample(material, cubemap, SAMPLER.SAMPLERTYPE_LINEAR_COLOR, -455, 0)
    wire(inverse, "", sky, "UVs")
    brightness = node(material, unreal.MaterialExpressionMultiply, -185, 0, const_b=.39)
    wire(sky, "RGB", brightness, "A")
    output(brightness, "", PIN.MP_EMISSIVE_COLOR)


def prepare():
    make("M_Figure", figure, revision="2")
    make("M_ForgedSteel", lambda material: figure(material, "Forge"))
    make("M_WovenCloth", lambda material: figure(material, "Textile"), revision="3")
    make("M_ArcLight", arc_light, revision="2")
    make("M_Overcast", overcast, revision="2")
    make("M_Breath", breath)
    make("M_SacredFx", sacred_light)
    make("M_BladeWake", blade_wake, revision="3")
    make("M_CombatMist", combat_mist, revision="2")
    make("M_EvidenceDocket", evidence_docket)
    make("M_WitnessGlass", witness_glass)
    make("M_FogGate", fog_gate, revision="2")
    court_tint = (.34, .40, .46)
    make("M_CourtStone", lambda mat: stone(mat, "Court", court_tint, .61), revision="3")
    make("M_Puddle", lambda mat: stone(mat, "Court", court_tint, .61, wet=True), revision="3")
    make("M_LowCourt", lambda mat: stone(mat, "LowCourt", (.55, .61, .47), .84))
    make("M_LowCourtWater", lambda mat: stone(mat, "LowCourt", (.52, .59, .47), .82, wet=True))
    make("M_RuinedWall", lambda mat: stone(mat, "RuinedWall", (.79, .73, .60), .93))
    make("M_Steps", lambda mat: stone(mat, "Wall", (.62, .61, .52), .86))
    make("M_ArchiveStone", lambda mat: stone(mat, "Wall", (.75, .62, .46), .88))
    make("M_AppealSlate", lambda mat: stone(mat, "AppealSlate", (.64, .57, .47), .83))
    for name, recipe in (
        ("M_WallStone", lambda mat: stone(mat, "Wall", (.83, .89, .93), .88)),
        ("M_TrimStone", lambda mat: stone(mat, "Wall", (.77, .80, .83), .91)),
    ):
        make(name, recipe, revision="2")
    chrome_digest = hashlib.sha256((SOURCE / "T_HudChrome.png").read_bytes()).hexdigest()
    chrome = load(f"{UI}/T_HudChrome")
    if not chrome or ART.get_metadata_tag(chrome, "ManosabaAtlasDigest") != chrome_digest:
        chrome, _ = import_source("T_HudChrome", UI, replace=bool(chrome))
        chrome.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        chrome.set_editor_property("srgb", True)
        chrome.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        chrome.set_editor_property("never_stream", True)
        ART.set_metadata_tag(chrome, "ManosabaAtlasDigest", chrome_digest)
        if not ART.save_loaded_asset(chrome, False):
            raise RuntimeError("Could not save HUD ornaments")
    for name in ("coastal_cliff_01", "coastal_cliff_02", "boulder_01"):
        import_scan(name)
    import_scan("gothic_statue", "4k")
    import_scan("large_iron_gate")
    import_scan("tree_stump_02")
    for name in ("large_castle_door", "gate_latch_01", "wooden_lantern_01"):
        import_castle_scan(name)
    import_castle_scan("modular_fort_01", {"modular_fort_01_tower_round"})
    for name in ("S_RainCourt", "S_EmaTrial", "S_EmaWitch", "S_HiroTrial", "S_HiroWitch",
                 "S_YukiTrial", "S_YukiWitch", "S_LaVI_Bavellabion", "S_MAdEaR_MAdEaR"):
        digest = hashlib.sha256((SOURCE / f"{name}.wav").read_bytes()).hexdigest()
        loop = load(f"{AUDIO}/{name}")
        changed = not loop or ART.get_metadata_tag(loop, "ManosabaAudioDigest") != digest
        if changed:
            loop, _ = import_source(name, AUDIO, replace=bool(loop))
        if not isinstance(loop, unreal.SoundWave):
            raise RuntimeError(f"Could not import {name}")
        if changed or not loop.get_editor_property("looping"):
            loop.set_editor_property("looping", True)
            ART.set_metadata_tag(loop, "ManosabaAudioDigest", digest)
            if not ART.save_loaded_asset(loop, False):
                raise RuntimeError(f"Could not save {name}")


try:
    prepare()
except Exception as error:
    unreal.log_error(f"Manosaba Duel art setup: {error}")
    raise
