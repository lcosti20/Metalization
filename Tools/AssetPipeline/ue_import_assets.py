"""
Stage 2 of the Metalization asset pipeline: manifest.json + FBX/PNG -> Unreal assets.

Run through the editor commandlet (see run_pipeline.ps1):
    UnrealEditor-Cmd.exe Metalization.uproject -run=pythonscript -script="<this file>"

Creates, under /Game/Metalization:
    Textures/T_*                      every texture referenced by the models (none for the graybox)
    Materials/M_MZ_Base(_Masked)      master materials (flat Tint colour, optional packed textures)
    Materials/M_MZ_Light, M_MZ_Decal  glowing strips / additive decals
    Materials/M_FX_Glow               effects (tracers, explosions, markers)
    Materials/M_Env_Metal + MI_Env_*  world-projected prototype grid for level geometry
    Materials/Instances/MI_*          one instance per model material
    Characters/<Name>/SK_<Name>       skeletal meshes (+ skeleton, physics asset, parts)
    Weapons/<Name>/SK_<Name>_On<Holder>  weapons bonemerged onto a holder's skeleton
    Props/SM_<Name>                   weapons and grenades as static meshes (pickups)

Safe to re-run: existing assets are updated in place. run_pipeline.ps1 clears the generated folders first,
because reimporting over a mesh keeps its old material slots (which breaks switching models).
"""
import json
import os

import unreal

PIPELINE_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "AssetPipeline")
ROOT = "/Game/Metalization"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    print("[MZImport] " + msg, flush=True)


def pascal(text):
    return "".join(p[:1].upper() + p[1:] for p in text.replace("-", "_").split("_") if p)


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


# --------------------------------------------------------------------------------------------
# Textures
# --------------------------------------------------------------------------------------------
def import_textures(manifest):
    tasks = []
    for key, tex in manifest["textures"].items():
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(PIPELINE_DIR, tex["file"]))
        task.set_editor_property("destination_path", ROOT + "/Textures")
        task.set_editor_property("destination_name", "T_" + key)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)
    for key, tex in manifest["textures"].items():
        texture = load(f"{ROOT}/Textures/T_{key}")
        if texture is None:
            unreal.log_error(f"[MZImport] texture failed: {key}")
            continue
        if not tex["srgb"]:
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    log(f"textures: {len(tasks)}")


# --------------------------------------------------------------------------------------------
# Master materials
# --------------------------------------------------------------------------------------------
def fresh_material(name):
    path = f"{ROOT}/Materials/{name}"
    mat = load(path)
    if mat is None:
        mat = asset_tools.create_asset(name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    else:
        mel.delete_all_material_expressions(mat)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    mat.set_editor_property("used_with_static_lighting", False)
    return mat


def expr(mat, cls, x, y, **props):
    node = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        node.set_editor_property(k, v)
    return node


def tex_param(mat, name, default, x, y, normal=False):
    node = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=default)
    if normal:
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    return node


def scalar(mat, name, value, x, y):
    return expr(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vector(mat, name, color, x, y):
    return expr(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*color))


def binop(mat, cls, a, a_out, b, b_out, x, y):
    node = expr(mat, cls, x, y)
    mel.connect_material_expressions(a, a_out, node, "A")
    mel.connect_material_expressions(b, b_out, node, "B")
    return node


def build_base(name, masked):
    """Packed-texture base: metallic = 1-R, roughness = 1-G of the diffuse, emission = diffuse * selfillum mask."""
    mat = fresh_material(name)
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    black = unreal.load_asset("/Engine/EngineResources/Black")
    flat = unreal.load_asset("/Engine/EngineMaterials/DefaultNormal")

    base = tex_param(mat, "BaseColor", white, -900, -200)
    normal = tex_param(mat, "Normal", flat, -900, 200, normal=True)
    selfillum = tex_param(mat, "SelfIllum", black, -900, 500)
    # Graybox materials have no texture: Tint carries their flat colour.
    tint = vector(mat, "Tint", (1.0, 1.0, 1.0, 1.0), -900, -380)
    tinted = binop(mat, unreal.MaterialExpressionMultiply, base, "RGB", tint, "", -650, -250)
    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    inv_r = expr(mat, unreal.MaterialExpressionOneMinus, -550, -50)
    mel.connect_material_expressions(base, "R", inv_r, "")
    metal_scale = scalar(mat, "MetallicScale", 0.75, -550, 30)
    metallic = binop(mat, unreal.MaterialExpressionMultiply, inv_r, "", metal_scale, "", -350, -50)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)

    inv_g = expr(mat, unreal.MaterialExpressionOneMinus, -550, 100)
    mel.connect_material_expressions(base, "G", inv_g, "")
    rough = expr(mat, unreal.MaterialExpressionClamp, -350, 100, min_default=0.3, max_default=0.85)
    mel.connect_material_expressions(inv_g, "", rough, "")
    mel.connect_material_expressions(scalar(mat, "RoughnessMin", 0.3, -550, 200), "", rough, "Min")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # Emissive = base * selfillum * strength + hit flash + hack glow (fresnel rim).
    glow = binop(mat, unreal.MaterialExpressionMultiply, tinted, "", selfillum, "RGB", -550, 450)
    strength = scalar(mat, "EmissiveStrength", 0.0, -550, 600)
    glow = binop(mat, unreal.MaterialExpressionMultiply, glow, "", strength, "", -350, 450)
    flash = scalar(mat, "HitFlash", 0.0, -550, 700)
    flash_color = vector(mat, "HitFlashColor", (2.0, 0.8, 0.3, 1.0), -550, 780)
    flash = binop(mat, unreal.MaterialExpressionMultiply, flash, "", flash_color, "", -350, 700)
    hack = scalar(mat, "HackGlow", 0.0, -550, 900)
    hack_color = vector(mat, "HackColor", (0.2, 1.2, 1.6, 1.0), -550, 980)
    fresnel = expr(mat, unreal.MaterialExpressionFresnel, -550, 1080, exponent=2.5)
    hack = binop(mat, unreal.MaterialExpressionMultiply, hack, "", hack_color, "", -350, 900)
    hack = binop(mat, unreal.MaterialExpressionMultiply, hack, "", fresnel, "", -200, 950)
    emissive = binop(mat, unreal.MaterialExpressionAdd, glow, "", flash, "", -150, 600)
    emissive = binop(mat, unreal.MaterialExpressionAdd, emissive, "", hack, "", 0, 700)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if masked:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("two_sided", True)
        mel.connect_material_property(base, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(mat)
    return mat


def build_unlit(name, additive, param_name="Texture", intensity=2.0):
    mat = fresh_material(name)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    if additive:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    tex = tex_param(mat, param_name, unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), -600, 0)
    tint = vector(mat, "Color", (1.0, 1.0, 1.0, 1.0), -600, -200)
    strength = scalar(mat, "Intensity", intensity, -600, 250)
    out = binop(mat, unreal.MaterialExpressionMultiply, tex, "RGB", tint, "", -450, 0)
    out = binop(mat, unreal.MaterialExpressionMultiply, out, "", strength, "", -300, 100)
    mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    return mat


def build_fx_glow():
    mat = fresh_material("M_FX_Glow")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    color = vector(mat, "Color", (1.0, 0.5, 0.1, 1.0), -500, 0)
    intensity = scalar(mat, "Intensity", 5.0, -500, 200)
    out = binop(mat, unreal.MaterialExpressionMultiply, color, "", intensity, "", -250, 100)
    mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    return mat


def build_env_metal():
    """Level geometry: the template's prototype grid, world-projected so scaled cubes tile cleanly."""
    mat = fresh_material("M_Env_Metal")
    mat.set_editor_property("used_with_skeletal_mesh", False)
    world = expr(mat, unreal.MaterialExpressionWorldPosition, -1400, 0)
    tile = scalar(mat, "TileSize", 256.0, -1400, 150)
    uvw = binop(mat, unreal.MaterialExpressionDivide, world, "", tile, "", -1200, 50)
    top = expr(mat, unreal.MaterialExpressionComponentMask, -1000, -100, r=True, g=True, b=False, a=False)
    mel.connect_material_expressions(uvw, "", top, "")
    side_x = expr(mat, unreal.MaterialExpressionComponentMask, -1000, 50, r=True, g=False, b=False, a=False)
    side_y = expr(mat, unreal.MaterialExpressionComponentMask, -1000, 150, r=False, g=True, b=False, a=False)
    side_z = expr(mat, unreal.MaterialExpressionComponentMask, -1000, 250, r=False, g=False, b=True, a=False)
    for m in (side_x, side_y, side_z):
        mel.connect_material_expressions(uvw, "", m, "")
    horizontal = binop(mat, unreal.MaterialExpressionAdd, side_x, "", side_y, "", -800, 100)
    side = binop(mat, unreal.MaterialExpressionAppendVector, horizontal, "", side_z, "", -650, 150)
    normal_ws = expr(mat, unreal.MaterialExpressionVertexNormalWS, -1000, 400)
    nz = expr(mat, unreal.MaterialExpressionComponentMask, -800, 400, r=False, g=False, b=True, a=False)
    mel.connect_material_expressions(normal_ws, "", nz, "")
    nz_abs = expr(mat, unreal.MaterialExpressionAbs, -650, 400)
    mel.connect_material_expressions(nz, "", nz_abs, "")
    is_top = expr(mat, unreal.MaterialExpressionRound, -500, 400)
    mel.connect_material_expressions(nz_abs, "", is_top, "")
    uv = expr(mat, unreal.MaterialExpressionLinearInterpolate, -450, 100)
    mel.connect_material_expressions(side, "", uv, "A")
    mel.connect_material_expressions(top, "", uv, "B")
    mel.connect_material_expressions(is_top, "", uv, "Alpha")

    tex = tex_param(mat, "Plates", load("/Game/LevelPrototyping/Textures/T_GridChecker_A"), -250, 0)
    mel.connect_material_expressions(uv, "", tex, "UVs")
    # The grid texture packs its checker in G; use it as a subtle light/dark shade under the tint.
    shade = expr(mat, unreal.MaterialExpressionLinearInterpolate, -50, 0, const_a=0.3, const_b=0.42)   # ~ the old plate texture's brightness
    mel.connect_material_expressions(tex, "G", shade, "Alpha")
    tint = vector(mat, "Tint", (0.55, 0.5, 0.45, 1.0), -250, 250)
    color = binop(mat, unreal.MaterialExpressionMultiply, shade, "", tint, "", 100, 100)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(scalar(mat, "Metallic", 0.35, 0, 300), "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(scalar(mat, "Roughness", 0.7, 0, 400), "", unreal.MaterialProperty.MP_ROUGHNESS)
    glow = binop(mat, unreal.MaterialExpressionMultiply, tint, "", scalar(mat, "Glow", 0.0, -250, 500), "", 0, 500)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    return mat


def make_instance(path, name, parent, scalars=None, vectors=None, textures=None):
    mi = load(f"{path}/{name}")
    if mi is None:
        mi = asset_tools.create_asset(name, path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.clear_all_material_instance_parameters(mi)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v))
    for k, v in (textures or {}).items():
        if v is not None:
            mel.set_material_instance_texture_parameter_value(mi, k, v)
    return mi


def build_materials(manifest):
    masters = {
        "lit": build_base("M_MZ_Base", masked=False),
        "masked": build_base("M_MZ_Base_Masked", masked=True),
        "light": build_unlit("M_MZ_Light", additive=False),
        "decal": build_unlit("M_MZ_Decal", additive=True),
    }
    build_fx_glow()
    env = build_env_metal()
    env_variants = {
        "MI_Env_Floor": {"Tint": (0.5, 0.42, 0.34, 1)},
        "MI_Env_Wall": {"Tint": (0.45, 0.33, 0.24, 1)},
        "MI_Env_Dark": {"Tint": (0.22, 0.19, 0.17, 1)},
        "MI_Env_Hazard": {"Tint": (0.9, 0.55, 0.08, 1)},
        "MI_Env_Glow": {"Tint": (0.1, 0.6, 1.0, 1)},
    }
    env_scalars = {"MI_Env_Glow": {"Glow": 4.0}, "MI_Env_Floor": {"Metallic": 0.15, "Roughness": 0.85},
                   "MI_Env_Dark": {"Metallic": 0.2, "Roughness": 0.8}}
    for name, vecs in env_variants.items():
        make_instance(ROOT + "/Materials", name, env, scalars=env_scalars.get(name, {}), vectors=vecs)

    def tex(key):
        return load(f"{ROOT}/Textures/T_{key}") if key else None

    instances = {}
    for key, m in manifest["materials"].items():
        kind = "masked" if m["kind"] == "lit" and m["masked"] else m["kind"]
        color = m.get("color")
        if kind in ("lit", "masked"):
            scalars = {"EmissiveStrength": m["emissive_strength"]}
            if color:
                scalars.update({"RoughnessMin": 0.6})   # flat graybox colour reads better matte
            mi = make_instance(ROOT + "/Materials/Instances", "MI_" + key, masters[kind], scalars=scalars,
                               vectors={"Tint": color} if color else None,
                               textures={"BaseColor": tex(m["base"]), "Normal": tex(m["normal"]), "SelfIllum": tex(m["selfillum"])})
        else:
            mi = make_instance(ROOT + "/Materials/Instances", "MI_" + key, masters[kind],
                               vectors={"Color": color} if color else None, textures={"Texture": tex(m["base"])})
        instances[key] = mi
    log(f"materials: {len(instances)} instances")
    return instances


# --------------------------------------------------------------------------------------------
# Meshes
# --------------------------------------------------------------------------------------------
def fbx_options(skeletal, skeleton=None, physics=False):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    if skeletal:
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        ui.set_editor_property("create_physics_asset", physics)
        if skeleton:
            ui.set_editor_property("skeleton", skeleton)
        data = ui.get_editor_property("skeletal_mesh_import_data")
        data.set_editor_property("import_morph_targets", False)
        data.set_editor_property("update_skeleton_reference_pose", False)
        data.set_editor_property("use_t0_as_ref_pose", False)
    else:
        ui.set_editor_property("import_as_skeletal", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        data = ui.get_editor_property("static_mesh_import_data")
        data.set_editor_property("combine_meshes", True)
        data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("convert_scene", True)
    data.set_editor_property("convert_scene_unit", True)
    data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    return ui


def import_fbx(file, dest_path, dest_name, options):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(PIPELINE_DIR, file))
    task.set_editor_property("destination_path", dest_path)
    task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", options)
    asset_tools.import_asset_tasks([task])
    asset = load(f"{dest_path}/{dest_name}")
    if asset is None:
        unreal.log_error(f"[MZImport] FAILED to import {file}")
    return asset


def assign_skeletal_materials(mesh, instances):
    # Iterating an unreal.Array of structs yields copies, so build a new list and write it back.
    slots = []
    for slot in mesh.get_editor_property("materials"):
        name = str(slot.get_editor_property("material_slot_name"))
        mi = instances.get(name)
        if mi is None:
            log(f"WARNING {mesh.get_name()}: no material for slot {name}")
        else:
            slot.set_editor_property("material_interface", mi)
        slots.append(slot)
    mesh.set_editor_property("materials", slots)


def assign_static_materials(mesh, instances):
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        mi = instances.get(str(slot.get_editor_property("material_slot_name")))
        if mi:
            mesh.set_material(index, mi)


def report_bounds(mesh, expected_height_cm=None):
    box = mesh.get_bounds().box_extent if hasattr(mesh, "get_bounds") else None
    origin = mesh.get_bounds().origin
    height = box.z * 2 if box else 0
    note = "" if expected_height_cm is None else f" expected~{expected_height_cm:.0f}"
    log(f"  {mesh.get_name()}: extent=({box.x:.0f},{box.y:.0f},{box.z:.0f}) origin=({origin.x:.0f},{origin.y:.0f},{origin.z:.0f}) height={height:.0f}{note}")


def import_characters(manifest, instances):
    skeletons = {}
    for cid, ch in manifest["characters"].items():
        name = pascal(cid)
        folder = f"{ROOT}/Characters/{name}"
        existing_skeleton = load(f"{folder}/SK_{name}_Skeleton")
        body = import_fbx(ch["file"], folder, f"SK_{name}", fbx_options(True, existing_skeleton, physics=existing_skeleton is None))
        if body is None:
            continue
        assign_skeletal_materials(body, instances)
        skeleton = body.get_editor_property("skeleton")
        skeletons[cid] = skeleton
        report_bounds(body, (ch["bounds_max_m"][2] - ch["bounds_min_m"][2]) * 100)
        for pid, part in ch["parts"].items():
            part_mesh = import_fbx(part["file"], folder, f"SK_{name}_{pascal(pid)}", fbx_options(True, skeleton))
            if part_mesh:
                assign_skeletal_materials(part_mesh, instances)
    log(f"characters: {len(skeletons)}")
    return skeletons


def import_weapons(manifest, instances, skeletons):
    count = 0
    for group in ("weapons", "props"):
        for wid, w in manifest[group].items():
            name = pascal(wid)
            prop = import_fbx(w["prop"], f"{ROOT}/Props", f"SM_{name}", fbx_options(False))
            if prop:
                assign_static_materials(prop, instances)
            for holder, file in w["followers"].items():
                if holder not in skeletons:
                    continue
                mesh = import_fbx(file, f"{ROOT}/Weapons/{name}", f"SK_{name}_On{pascal(holder)}", fbx_options(True, skeletons[holder]))
                if mesh:
                    assign_skeletal_materials(mesh, instances)
                    count += 1
    log(f"weapon followers: {count}")


def main():
    with open(os.path.join(PIPELINE_DIR, "manifest.json")) as f:
        manifest = json.load(f)
    # Use the legacy FBX importer so the options above are honoured exactly (Interchange ignores FbxImportUI).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    import_textures(manifest)
    instances = build_materials(manifest)
    skeletons = import_characters(manifest, instances)
    import_weapons(manifest, instances, skeletons)
    eal.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log("DONE")


main()
