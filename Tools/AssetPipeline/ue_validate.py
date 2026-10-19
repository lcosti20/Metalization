"""
Checks the imported Metalization assets: sizes, material assignments and skeletal-mesh usage flags
(the missing flag is what made every robot render with the default material in the first attempt),
and the ship pieces: collision, required sockets, hull size, and matching hangar portals.
Run: UnrealEditor-Cmd.exe Metalization.uproject -run=pythonscript -script=Tools/AssetPipeline/ue_validate.py
"""
import json
import os

import unreal

ROOT = "/Game/Metalization"
MANIFEST = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "AssetPipeline", "manifest.json")
# Sockets gameplay code looks up by name (AMetalCapitalShip, the hangar and portal setup).
SHIP_SOCKETS = {
    "SM_CapShip_Hull": ["hangar_portal", "engines", "shieldgen_L", "shieldgen_R", "comms", "turret_01"],
    "SM_CapShip_TurretBase": ["gun"],
    "SM_Hangar_Shell": ["hangar_portal", "corridor_door", "pad_1", "pad_2", "pad_3"],
}


def collision_count(mesh):
    """Simple collision shapes on a static mesh (editor subsystems aren't available in the commandlet)."""
    geom = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    return sum(len(geom.get_editor_property(k)) for k in ("convex_elems", "box_elems", "sphere_elems", "sphyl_elems"))
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
    elif isinstance(asset, unreal.StaticMesh) and "/Ships/" in path:
        name = asset.get_name()
        for slot in asset.get_editor_property("static_materials"):
            check_material(name, slot.get_editor_property("material_slot_name"), slot.get_editor_property("material_interface"), False)
        hulls = collision_count(asset)
        if hulls == 0:
            problems.append(f"{name}: no collision (UCX_ meshes missing from the export?)")
        for socket in SHIP_SOCKETS.get(name, []):
            if asset.find_socket(socket) is None:
                problems.append(f"{name}: missing socket '{socket}'")
        box = asset.get_bounds().box_extent
        print(f"[MZValidate] {name:28} size=({box.x * 0.02:5.0f},{box.y * 0.02:5.0f},{box.z * 0.02:5.0f})m hulls={hulls} sockets={sum(asset.find_socket(n) is not None for n in SHIP_SOCKETS.get(name, []))}/{len(SHIP_SOCKETS.get(name, []))}")
        if name == "SM_CapShip_Hull":
            if not 55000 <= box.x * 2 <= 70000:
                problems.append(f"{name}: hull is {box.x * 0.02:.0f} m long (expected ~600 m)")
            portal = asset.find_socket("hangar_portal")
            if portal and portal.get_editor_property("relative_location").y <= 0:
                problems.append(f"{name}: hangar mouth is not on the starboard (+Y) side - axis conversion changed?")

# Both ends of the hangar portal must be the same size for the virtual camera to line up.
if os.path.exists(MANIFEST):
    with open(MANIFEST) as f:
        ships = json.load(f).get("ships", {})
    ends = [ships.get(p, {}).get("sockets", {}).get("hangar_portal") for p in ("CapShip_Hull", "Hangar_Shell")]
    if None in ends:
        problems.append("manifest: hangar_portal socket missing on the hull or the hangar shell")
    elif (ends[0].get("width"), ends[0].get("height")) != (ends[1].get("width"), ends[1].get("height")):
        problems.append(f"hangar portals differ: hull {ends[0].get('width')}x{ends[0].get('height')} m, shell {ends[1].get('width')}x{ends[1].get('height')} m")

for p in problems:
    print("[MZValidate] PROBLEM " + p)
print(f"[MZValidate] {'PASSED' if not problems else 'FAILED'} ({len(problems)} problems)")
