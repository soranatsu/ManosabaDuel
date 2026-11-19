"""Import the bound models produced by SourceAssets/rig_characters.py."""

import hashlib
from pathlib import Path
import sys

import unreal


PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ASSETS = "/Game/DuelArt/Characters"
LIBRARY = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def import_form(name):
    source = PROJECT / "Intermediate" / "DuelCharacters" / f"{name}.glb"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    folder = f"{ASSETS}/{name}"
    path = f"{folder}/{name}"
    existing = LIBRARY.load_asset(path) if LIBRARY.does_asset_exist(path) else None
    if existing and LIBRARY.get_metadata_tag(existing, "ManosabaSource") == digest:
        return

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", bool(existing))
    task.set_editor_property("save", False)
    TOOLS.import_asset_tasks([task])
    meshes = [asset for asset in task.get_objects() if isinstance(asset, unreal.SkeletalMesh)]
    if len(meshes) != 1 or not meshes[0].get_editor_property("skeleton"):
        raise RuntimeError(f"{name}: expected one skeletal mesh and skeleton")
    mesh = meshes[0]
    if mesh.get_path_name() != f"{path}.{name}" and not LIBRARY.rename_loaded_asset(mesh, path):
        raise RuntimeError(f"{name}: could not rename the imported mesh")

    LIBRARY.set_metadata_tag(mesh, "ManosabaSource", digest)
    if not LIBRARY.save_directory(folder, False):
        raise RuntimeError(f"{name}: could not save the imported assets")
    unreal.log(f"Duel character imported {mesh.get_path_name()}")


FORMS = ("SK_HiroStand", "SK_EmaQuad", "SK_HiroQuad", "SK_EmaStand", "SK_YukiQuad")
requested = sys.argv[1:] or FORMS
for form in requested:
    if form not in FORMS:
        raise ValueError(f"Unknown Duel character form: {form}")
for form in requested:
    import_form(form)
