"""
Checks the imported Metalization assets: sizes, material assignments and skeletal-mesh usage flags
(the missing flag is what made every robot render with the default material in the first attempt).
Run: UnrealEditor-Cmd.exe Metalization.uproject -run=pythonscript -script=Tools/AssetPipeline/ue_validate.py
"""
import unreal

ROOT = "/Game/Metalization"
eal = unreal.EditorAssetLibrary
problems = []


def check_material(owner, slot, mi, skeletal):
    if mi is None or "WorldGridMaterial" in mi.get_path_name():
        problems.append(f"{owner}: slot {slot} has no material")
        return
    base = mi.get_base_material()
    if skeletal and not base.get_editor_property("used_with_skeletal_mesh"):
        problems.append(f"{owner}: {base.get_name()} lacks 'Used with Skeletal Mesh'")


for path in eal.list_assets(ROOT, recursive=True, include_folder=False):
    if not eal.does_asset_exist(path):
        continue
    asset = eal.load_asset(path)
    if isinstance(asset, unreal.SkeletalMesh):
        box = asset.get_bounds().box_extent
        for slot in asset.get_editor_property("materials"):
            check_material(asset.get_name(), slot.get_editor_property("material_slot_name"), slot.get_editor_property("material_interface"), True)
        if asset.get_name().count("_") == 1:
            print(f"[MZValidate] {asset.get_name():28} height={box.z * 2:6.1f}cm width={box.x * 2:6.1f}cm slots={len(asset.get_editor_property('materials'))}")
    elif isinstance(asset, unreal.StaticMesh) and "/Props/" in path:
        for i, slot in enumerate(asset.get_editor_property("static_materials")):
            check_material(asset.get_name(), slot.get_editor_property("material_slot_name"), slot.get_editor_property("material_interface"), False)

for p in problems:
    print("[MZValidate] PROBLEM " + p)
print(f"[MZValidate] {'PASSED' if not problems else 'FAILED'} ({len(problems)} problems)")
