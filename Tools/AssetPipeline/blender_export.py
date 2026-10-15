"""
Stage 1 of the Metalization asset pipeline: Blender -> FBX/PNG + manifest.json

Run headless (see run_pipeline.ps1):
    blender.exe -b "<...>/Art/Graybox/Metalization_Graybox.blend" --python blender_export.py -- <output_dir>

Expected layout: Characters/{Heroes,Villains}/<character> (armature "<hero|villain>_<id>_ARM", body =
first sub-collection, then optional parts), Weapons/<weapon> and Grenades/<grenade>.

What it produces in <output_dir>:
    Characters/<id>.fbx            body skeletal mesh (armature + first sub-collection's meshes)
    Characters/<id>__<part>.fbx    optional parts (hand, backpack, visor...) skinned to the same armature
    Weapons/<weapon>__on__<char>.fbx  weapon skinned 100% to <char>'s weapon_bone (Source "bonemerge"),
                                   imported in UE onto <char>'s skeleton and driven as a leader-pose follower
    Props/<weapon>.fbx             weapon/grenade as a static mesh, pivot at its attachment bone (pickups)
    Textures/<image>.png           every image referenced by an exported material (none for the graybox)
    manifest.json                  materials, textures, characters, bones, parts, weapons

The .blend is never saved; all edits happen in memory.
"""
import bpy
import json
import os
import re
import sys
from mathutils import Matrix, Vector

BLEND_ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT_DIR = os.path.abspath(BLEND_ARGS[0] if BLEND_ARGS else os.path.join(os.path.dirname(bpy.data.filepath), "_export"))

# Weapons that the player and the enemy shooters can hold. Keys are the collection names in the .blend.
WEAPON_COLLECTIONS = {
    "Laser Level 1": "laser_l1",
    "Laser Level 2": "laser_l2",
    "Laser Level 3": "laser_l3",
    "Enemy Laser": "laser_enemy",
    "Repeater Level 1": "repeater_l1",
    "Repeater Level 3": "repeater_l3",
    "Shotgun Level 1": "shotgun_l1",
    "Shotgun Level 3": "shotgun_l3",
    "Rocket Launcher Level 1": "rocketlauncher_l1",
    "Rocket Launcher Level 3": "rocketlauncher_l3",
    "Sprayer Level 1": "sprayer_l1",
    "Sprayer Level 3": "sprayer_l3",
    "Flamethrower": "flamethrower",
    "Disc Launcher": "disclauncher",
    "Hack Tether": "hacktether",
}
GRENADE_COLLECTIONS = {
    "Cluster Grenade": "grenade_cluster",
    "EMP Grenade": "grenade_emp",
    "Fire Bomb": "grenade_fire",
    "Convert Grenade": "grenade_convert",
    "Drill Charge": "drillcharge",
    "Slingshot": "slingshot",
    "Wrench": "wrench",
}
# Which characters get which weapons exported as bonemerged followers.
WEAPON_HOLDERS = {
    "player": [w for w in WEAPON_COLLECTIONS.values() if w != "laser_enemy"],
    "shooter": ["laser_enemy", "repeater_l1", "shotgun_l1"],
}


def safe(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name).strip("_")


def log(*args):
    print("[MZExport]", *args, flush=True)


def child_collections(name):
    col = bpy.data.collections.get(name)
    return list(col.children) if col else []


def meshes_in(col):
    result = [o for o in col.all_objects if o.type == "MESH"]
    return sorted(result, key=lambda o: o.name)


def armature_in(col):
    for o in col.objects:
        if o.type == "ARMATURE":
            return o
    return None


# --------------------------------------------------------------------------------------------
# Materials and textures
# --------------------------------------------------------------------------------------------
materials = {}  # safe material name -> description
textures = {}   # safe image name -> {"file", "srgb"}


def export_image(img, srgb):
    key = safe(img.name)
    if key in textures:
        return key
    path = os.path.join(OUT_DIR, "Textures", key + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not os.path.exists(path):
        copy = img.copy()
        copy.filepath_raw = path
        copy.file_format = "PNG"
        copy.save()
        bpy.data.images.remove(copy)
    textures[key] = {"file": os.path.relpath(path, OUT_DIR).replace("\\", "/"), "srgb": srgb}
    return key


def linked_image(node_input):
    if not node_input.is_linked:
        return None
    node = node_input.links[0].from_node
    return node.image if node.type == "TEX_IMAGE" and node.image else None


def describe_material(mat):
    """Translate a Blender material into a flat description for UE."""
    key = safe(mat.name)
    if key in materials:
        return key
    desc = {"kind": "lit", "base": None, "normal": None, "selfillum": None, "emissive_strength": 0.0, "masked": False,
            "color": None}
    nodes = mat.node_tree.nodes if mat.node_tree else []
    bsdf = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf and not any(n.type == "TEX_IMAGE" for n in nodes):
        # Graybox: flat colour, optionally glowing.
        desc["color"] = [round(c, 4) for c in bsdf.inputs["Base Color"].default_value]
        if bsdf.inputs["Emission Strength"].default_value > 0:
            desc["kind"] = "light"
    else:
        emission = next((n for n in nodes if n.type == "EMISSION"), None)
        transparent = any(n.type == "BSDF_TRANSPARENT" for n in nodes)
        image = linked_image(emission.inputs["Color"]) if emission else None
        if image is None:
            image = next((n.image for n in nodes if n.type == "TEX_IMAGE" and n.image), None)
        if transparent:
            # Glowing decals layered over the hull (Transparent + Emission): additive in UE.
            desc["kind"] = "decal"
        elif emission:
            # Opaque glowing light strips.
            desc["kind"] = "light"
        desc["base"] = export_image(image, True) if image else None
    materials[key] = desc
    return key


def sanitize_materials(objs):
    """Rename materials on the given meshes to safe names so UE slot names match manifest keys."""
    for o in objs:
        for slot in o.material_slots:
            if slot.material:
                key = describe_material(slot.material)
                if slot.material.name != key:
                    # Two blender materials may sanitize to the same key; reuse the first one.
                    existing = bpy.data.materials.get(key)
                    slot.material = existing if existing else slot.material
                    if slot.material.name != key:
                        slot.material.name = key


# --------------------------------------------------------------------------------------------
# FBX export helpers
# --------------------------------------------------------------------------------------------
def export_fbx(path, objects, armature=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.hide_set(False)
        o.hide_viewport = False
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"ARMATURE", "MESH"} if armature else {"MESH"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        global_scale=1.0,
        axis_forward="-Z",
        axis_up="Y",
        use_mesh_modifiers=False,
        mesh_smooth_type="FACE",
        use_tspace=False,
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        use_armature_deform_only=False,
        armature_nodetype="NULL",
        bake_anim=False,
        path_mode="STRIP",
        embed_textures=False,
    )
    log("wrote", os.path.relpath(path, OUT_DIR))


def make_all_visible():
    for lc in walk_layer_collections(bpy.context.view_layer.layer_collection):
        lc.exclude = False
        lc.hide_viewport = False
    for col in bpy.data.collections:
        col.hide_viewport = False
        col.hide_render = False


def walk_layer_collections(lc):
    yield lc
    for c in lc.children:
        yield from walk_layer_collections(c)


def bounds(objs):
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    if not pts:
        return [0, 0, 0], [0, 0, 0]
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


# --------------------------------------------------------------------------------------------
# Characters
# --------------------------------------------------------------------------------------------
CHARACTER_IDS = {}  # collection name -> id


def character_id(arm):
    # "hero_player_ARM" -> "player", "villain_shooter_ARM" -> "shooter"
    name = arm.name[:-4] if arm.name.endswith("_ARM") else arm.name
    return re.sub(r"^(hero|villain)_", "", name)


def export_characters(manifest):
    for faction in ("Heroes", "Villains"):
        for col in child_collections(faction):
            arm = armature_in(col)
            if arm is None:
                log("skip (no armature)", col.name)
                continue
            cid = character_id(arm)
            CHARACTER_IDS[col.name] = cid
            subs = [c for c in col.children if not c.name.endswith("_ATTACHMENTS")]
            if not subs:
                continue
            body = meshes_in(subs[0])
            sanitize_materials(body)
            export_fbx(os.path.join(OUT_DIR, "Characters", cid + ".fbx"), [arm] + body)
            parts = {}
            for sub in subs[1:]:
                pm = meshes_in(sub)
                if not pm:
                    continue
                sanitize_materials(pm)
                pid = safe(sub.name)
                export_fbx(os.path.join(OUT_DIR, "Characters", f"{cid}__{pid}.fbx"), [arm] + pm)
                parts[pid] = {"file": f"Characters/{cid}__{pid}.fbx", "materials": sorted({s.material.name for o in pm for s in o.material_slots if s.material})}
            mn, mx = bounds(body)
            bones = {}
            for b in arm.data.bones:
                h = arm.matrix_world @ b.head_local
                bones[b.name] = {"parent": b.parent.name if b.parent else None, "head": [round(v, 4) for v in h]}
            manifest["characters"][cid] = {
                "display_name": col.name,
                "faction": faction,
                "file": f"Characters/{cid}.fbx",
                "armature": arm.name,
                "materials": sorted({s.material.name for o in body for s in o.material_slots if s.material}),
                "parts": parts,
                "bones": bones,
                "bounds_min_m": [round(v, 4) for v in mn],
                "bounds_max_m": [round(v, 4) for v in mx],
            }


# --------------------------------------------------------------------------------------------
# Weapons
# --------------------------------------------------------------------------------------------
def weapon_root_bone(arm):
    return arm.data.bones[0]


def export_weapons(manifest):
    char_arms = {}
    for col_name, cid in CHARACTER_IDS.items():
        char_arms[cid] = armature_in(bpy.data.collections[col_name])

    all_props = dict(WEAPON_COLLECTIONS)
    all_props.update(GRENADE_COLLECTIONS)
    for col_name, wid in all_props.items():
        col = bpy.data.collections.get(col_name)
        if col is None:
            log("missing weapon collection", col_name)
            continue
        arm = armature_in(col)
        subs = [c for c in col.children if not c.name.endswith("_ATTACHMENTS")]
        meshes = meshes_in(subs[0]) if subs else [o for o in col.all_objects if o.type == "MESH"]
        if not meshes:
            continue
        sanitize_materials(meshes)
        wb_matrix = (arm.matrix_world @ weapon_root_bone(arm).matrix_local) if arm else Matrix.Identity(4)
        entry = {"materials": sorted({s.material.name for o in meshes for s in o.material_slots if s.material}),
                 "prop": f"Props/{wid}.fbx", "followers": {}}

        # Static prop: pivot moved to the attachment bone so pickups rotate around the grip.
        props = []
        for m in meshes:
            dup = m.copy()
            dup.data = m.data.copy()
            dup.parent = None
            dup.modifiers.clear()
            dup.data.transform(wb_matrix.inverted() @ m.matrix_world)
            dup.matrix_world = Matrix.Identity(4)
            dup.vertex_groups.clear()
            bpy.context.scene.collection.objects.link(dup)
            props.append(dup)
        export_fbx(os.path.join(OUT_DIR, entry["prop"]), props, armature=False)
        mn, mx = bounds(props)
        entry["prop_bounds_m"] = [[round(v, 4) for v in mn], [round(v, 4) for v in mx]]
        for p in props:
            bpy.data.objects.remove(p, do_unlink=True)

        # Bonemerged followers on each holder's skeleton.
        for cid, wlist in WEAPON_HOLDERS.items():
            if wid not in wlist or cid not in char_arms:
                continue
            carm = char_arms[cid]
            if "weapon_bone" not in carm.data.bones:
                continue
            cb_matrix = carm.matrix_world @ carm.data.bones["weapon_bone"].matrix_local
            followers = []
            for m in meshes:
                dup = m.copy()
                dup.data = m.data.copy()
                dup.modifiers.clear()
                dup.vertex_groups.clear()
                dup.parent = None
                dup.data.transform(carm.matrix_world.inverted() @ cb_matrix @ wb_matrix.inverted() @ m.matrix_world)
                dup.matrix_world = carm.matrix_world.copy()
                vg = dup.vertex_groups.new(name="weapon_bone")
                vg.add(list(range(len(dup.data.vertices))), 1.0, "REPLACE")
                mod = dup.modifiers.new("Armature", "ARMATURE")
                mod.object = carm
                dup.parent = carm
                bpy.context.scene.collection.objects.link(dup)
                followers.append(dup)
            rel = f"Weapons/{wid}__on__{cid}.fbx"
            export_fbx(os.path.join(OUT_DIR, rel), [carm] + followers)
            entry["followers"][cid] = rel
            for f in followers:
                bpy.data.objects.remove(f, do_unlink=True)
        manifest["weapons" if col_name in WEAPON_COLLECTIONS else "props"][wid] = entry


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    make_all_visible()
    # Show the rest pose regardless of any posing saved in the file.
    for o in bpy.data.objects:
        if o.type == "ARMATURE":
            o.data.pose_position = "REST"
    manifest = {"source": os.path.basename(bpy.data.filepath), "characters": {}, "weapons": {}, "props": {},
                "materials": materials, "textures": textures}
    export_characters(manifest)
    export_weapons(manifest)
    with open(os.path.join(OUT_DIR, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    log("DONE characters=%d weapons=%d props=%d materials=%d textures=%d" % (
        len(manifest["characters"]), len(manifest["weapons"]), len(manifest["props"]), len(materials), len(textures)))


main()
