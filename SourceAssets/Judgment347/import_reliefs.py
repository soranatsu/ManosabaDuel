"""Run inside an Unreal editor Python commandlet with JUDGMENT_ART and JUDGMENT_MESHES set."""
import os
import json
from pathlib import Path

import unreal as u

art=Path(os.environ["JUDGMENT_ART"])
meshdir=Path(os.environ["JUDGMENT_MESHES"])
dest="/Game/DuelArt/Judgment"
lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
lib.make_directory(dest)

def bring(file,cls):
    target=dest+"/"+file.stem
    if lib.does_asset_exist(target):
        obj=lib.load_asset(target)
    elif lib.does_asset_exist(target+"/StaticMeshes/"+file.stem):
        assert lib.rename_asset(target+"/StaticMeshes/"+file.stem,target)
        obj=lib.load_asset(target)
    else:
        t=u.AssetImportTask()
        for k,v in dict(filename=str(file),destination_path=dest,destination_name=file.stem,automated=True,
                        save=True,replace_existing=True).items():
            t.set_editor_property(k,v)
        tools.import_asset_tasks([t])
        found=[ob for ob in t.get_objects() if isinstance(ob,cls)]
        if len(found)!=1:
            raise RuntimeError(f"{file.name}: {found} / {lib.list_assets(dest)}")
        obj=found[0]
        if obj.get_path_name().split(".")[0]!=target:
            assert lib.rename_asset(obj.get_path_name(),target)
            obj=lib.load_asset(target)
    if not isinstance(obj,cls):
        raise RuntimeError(target)
    return obj

t=bring(art/"T_JudgmentEnamel.png",u.Texture2D)
matpath=dest+"/M_JudgmentEnamel"
m=lib.load_asset(matpath) if lib.does_asset_exist(matpath) else tools.create_asset("M_JudgmentEnamel",dest,u.Material,u.MaterialFactoryNew())
edit.delete_all_material_expressions(m)
def node(cls,x,y):
    return edit.create_material_expression(m,cls,x,y)
def constant(v,x,y):
    n=node(u.MaterialExpressionConstant,x,y)
    n.set_editor_property("r",v)
    return n
def connect(a,b,port,output=""):
    assert edit.connect_material_expressions(a,output,b,port)
def output(a,prop,pin=""):
    assert edit.connect_material_property(a,pin,prop)
tex=node(u.MaterialExpressionTextureSample,-690,0)
tex.set_editor_property("texture",t)
output(tex,u.MaterialProperty.MP_BASE_COLOR,"RGB")
# Gilt and bare silver catch different highlights than the recessed blue enamel.
mask=node(u.MaterialExpressionSubtract,-420,230)
connect(tex,mask,"A","R");connect(tex,mask,"B","B")
boost=node(u.MaterialExpressionMultiply,-260,230)
connect(mask,boost,"A");connect(constant(3.4,-430,360),boost,"B")
metal=node(u.MaterialExpressionSaturate,-110,230)
connect(boost,metal,"")
output(metal,u.MaterialProperty.MP_METALLIC)
rough=node(u.MaterialExpressionLinearInterpolate,170,370)
connect(constant(.24,-60,425),rough,"A");connect(constant(.47,-65,490),rough,"B");connect(metal,rough,"Alpha")
output(rough,u.MaterialProperty.MP_ROUGHNESS)
light=node(u.MaterialExpressionMultiply,-100,-70)
connect(tex,light,"A","RGB");connect(constant(.50,-320,-150),light,"B")
output(light,u.MaterialProperty.MP_EMISSIVE_COLOR)
edit.recompile_material(m)
assert lib.save_loaded_asset(m,False)
receipt={}
for f in sorted(meshdir.glob("*.glb")):
    mesh=bring(f,u.StaticMesh)
    mesh.set_material(0,m)
    assert lib.save_loaded_asset(mesh,False)
    e=mesh.get_bounds().box_extent
    receipt[f.stem]={"asset":mesh.get_path_name(),"half_extent_cm":[e.x,e.y,e.z],"material":m.get_path_name()}
    assert 42<e.z<62 and min(e.x,e.y)<13 and 40<max(e.x,e.y)<62
assert len(receipt)==3
(art/"import_receipt.json").write_text(json.dumps(receipt,indent=2),encoding="utf8")
u.log("JUDGMENT_RELIEF_VERIFIED")
