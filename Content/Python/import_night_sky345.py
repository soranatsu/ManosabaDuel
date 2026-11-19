"""Run with UE 5.8 -run=pythonscript -script=...; edits only uniquely named 345 sky assets."""
from pathlib import Path
import json
import unreal as u

root = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
source = root / "SourceAssets" / "NightSky345"
tools = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
folder = "/Game/DuelArt/NorthTower/NightSky345"


def texture(filename, name, srgb):
    task = u.AssetImportTask()
    task.set_editor_property("filename", str(source / filename))
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    tex = u.load_asset(folder + "/" + name)
    assert tex, name
    tex.set_editor_property("srgb", srgb)
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("lod_group", u.TextureGroup.TEXTUREGROUP_SKYBOX)
    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_DEFAULT)
    tex.set_editor_property("address_x", u.TextureAddress.TA_WRAP)
    tex.set_editor_property("address_y", u.TextureAddress.TA_WRAP)
    u.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


stars = texture("MilkyWay345.png", "T_NorthMilkyWay345", True)
clouds = texture("CloudField345.png", "T_NorthCloudField345", False)
moon = u.load_asset("/Game/DuelArt/NorthTower/Rescue/Textures/T_NorthLRO_Moon")
assert moon, "Retain the credited NASA/LRO moon map"


def material(name, code, inputs, transparent=False, sky=False):
    path = folder + "/" + name
    mat = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name, folder, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT if transparent else u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("is_sky", sky)
    custom = lib.create_material_expression(mat, u.MaterialExpressionCustom, 0, 0)
    custom.set_editor_property("output_type", u.CustomMaterialOutputType.CMOT_FLOAT4 if transparent else u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for index, (pin_name, node_type, asset, srgb) in enumerate(inputs):
        pin = u.CustomInput(); pin.set_editor_property("input_name", pin_name); pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", code)
    for index, (pin_name, node_type, asset, srgb) in enumerate(inputs):
        node = lib.create_material_expression(mat, node_type, -400, index*140)
        if asset:
            node.set_editor_property("parameter_name", pin_name)
            node.set_editor_property("texture", asset)
            node.set_editor_property("sampler_type", u.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb else u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        assert lib.connect_material_expressions(node, "", custom, pin_name), pin_name
    if transparent:
        for channels, prop in (("rgb", u.MaterialProperty.MP_EMISSIVE_COLOR), ("a", u.MaterialProperty.MP_OPACITY)):
            mask = lib.create_material_expression(mat, u.MaterialExpressionComponentMask, 270, 0 if channels=="rgb" else 140)
            for c in "rgba": mask.set_editor_property(c, c in channels)
            assert lib.connect_material_expressions(custom, "", mask, "")
            assert lib.connect_material_property(mask, "", prop)
    else:
        assert lib.connect_material_property(custom, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    assert u.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


position = ("P", u.MaterialExpressionWorldPosition, None, False)
time = ("T", u.MaterialExpressionTime, None, False)
sky = material("M_NorthNightSky345", r"""
float3 d=normalize(P-float3(0,0,-2125));
float2 uv=float2(atan2(d.y,d.x)*.159154943+.5, acos(clamp(d.z,-1,1))*.318309886);
// Fixed bake LOD avoids the discontinuous atan2 derivative at the longitude seam.
float3 field=Texture2DSampleLevel(Stars,StarsSampler,uv,0).rgb;
float haze=exp(-abs(d.z)*9.);
float3 horizon=float3(.005,.012,.026)*haze;
return (field*1.50+horizon)*View.OneOverPreExposure;
""", [position, ("Stars", u.MaterialExpressionTextureObjectParameter, stars, True)], sky=True)

cloud = material("M_NorthMoonlitClouds345", r"""
float3 d=normalize(P-float3(0,0,-2125));
float2 uv=float2(atan2(d.y,d.x)*.159154943+.5, acos(clamp(d.z,-1,1))*.318309886);
float n=Texture2DSampleLevel(Density,DensitySampler,uv*float2(3.,2.)+float2(T*.00150,T*.000035),0).r;
float m=Texture2DSampleLevel(Density,DensitySampler,uv*float2(7.,4.5)+float2(-T*.00050,T*.00006)+.37,0).r;
float billow=smoothstep(.48,.82,n*.72+m*.28);
float band=smoothstep(-.025,.045,d.z)*(1.-smoothstep(.75,.98,d.z));
float silver=pow(saturate(dot(d,normalize(float3(380000,-38000,19125)))),12.);
float3 blue=lerp(float3(.008,.016,.030),float3(.025,.043,.074),billow);
blue+=float3(.045,.065,.095)*silver*(.35+billow*.65);
return float4(blue*View.OneOverPreExposure,billow*.32*band);
""", [position, time, ("Density", u.MaterialExpressionTextureObjectParameter, clouds, False)], transparent=True)

lunar = material("M_NorthMoon345", r"""
float3 albedo=Texture2DSample(Moon,MoonSampler,UV).rgb;
float rim=smoothstep(.035,.75,saturate(dot(normalize(N),normalize(V))));
return albedo*float3(.92,.98,1.08)*(1.16+rim*.38)*View.OneOverPreExposure;
""", [
    ("Moon", u.MaterialExpressionTextureObjectParameter, moon, bool(moon.get_editor_property("srgb"))),
    ("UV", u.MaterialExpressionTextureCoordinate, None, False),
    ("N", u.MaterialExpressionPixelNormalWS, None, False),
    ("V", u.MaterialExpressionCameraVectorWS, None, False),
])

report = {"assets": [a.get_path_name() for a in (stars, clouds, sky, cloud, lunar)],
          "moon_source": moon.get_path_name(),
          "runtime_draws": "one opaque night sphere, one moon sphere, one translucent cloud sphere"}
(source / "unreal_import345.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
u.log("NORTH_NIGHT_SKY_345_IMPORTED " + json.dumps(report))
