"""
Metalization pipeline: the free-roam prototype arena (/Game/Metalization/Maps/MZ_Arena).

Recreates the first progress update video (Docs/example_video): the player running and double-jumping
around the UE Third Person template arena (grey grid walls, raised blocks, ramps, the disc
platform, blue physics cubes) while red enemy Shooters and a few neutral heroes wander the open floor.

The template's geometry (and sun angle) is copied actor by actor from /Game/ThirdPerson/Lvl_ThirdPerson
into a plain (non World Partition) level and re-skinned grey, so this script can regenerate the map in
place like ue_setup_gameplay.py does for MZ_Outpost_Demo. Make manual layout changes on a copy.

Called from ue_setup_gameplay.py after the data assets exist, or on its own:
    UnrealEditor-Cmd.exe Metalization.uproject -run=pythonscript -script=Tools/AssetPipeline/ue_setup_arena.py
"""
import unreal

ROOT = "/Game/Metalization"
TEMPLATE = "/Game/ThirdPerson/Lvl_ThirdPerson"
ARENA_PATH = ROOT + "/Maps/MZ_Arena"
BLUE_MAT = ROOT + "/Materials/MI_Arena_PhysicsBlue"

eal = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

GRID = "/Game/LevelPrototyping/Materials/"
# The video uses the older all-grey template: light floor, grey walls, dark-topped raised blocks.
# UE 5.8's template paints its blocks green, so every mesh gets one of these instead.
WALLS = {"SM_Cube2", "SM_Cube3", "SM_Cube4", "SM_Cube5", "SM_Cube17", "SM_Cube18", "SM_Cube19", "SM_Cube20"}
SKIP = {"SM_SkySphere"}  # the template's sky dome renders black without its own setup; we use a real sky


def log(msg):
    print("[MZArena] " + msg, flush=True)


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def setp(obj, **props):
    for k, v in props.items():
        obj.set_editor_property(k, v)
    return obj


def spawn(cls, loc, yaw=0.0, label=None, folder=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


# --------------------------------------------------------------------------------------------
# Template capture
# --------------------------------------------------------------------------------------------
def capture_template():
    """Snapshot of the template's static meshes, sun angle and player start (it is a World Partition map)."""
    les.load_level(TEMPLATE)
    meshes, markers = [], {}
    for a in actors.get_all_level_actors():
        label = a.get_actor_label()
        if isinstance(a, unreal.StaticMeshActor) and label not in SKIP:
            meshes.append({"label": label, "folder": str(a.get_folder_path()), "transform": a.get_actor_transform(),
                           "mesh": a.static_mesh_component.static_mesh})
        elif isinstance(a, (unreal.DirectionalLight, unreal.PlayerStart)):
            markers[a.get_class().get_name()] = a.get_actor_transform()
    log(f"template: {len(meshes)} meshes")
    return meshes, markers


def grid_material(label, mesh):
    if label == "Floor" or label in WALLS or mesh.get_name() == "SM_QuarterCylinder" and label.startswith("SM_Cylinder"):
        return load(GRID + "MI_PrototypeGrid_Gray")
    return load(GRID + "MI_PrototypeGrid_TopDark")


def rebuild_template(meshes):
    for r in meshes:
        actor = spawn(unreal.StaticMeshActor, (0, 0, 0), label=r["label"], folder="Template/" + r["folder"])
        actor.set_actor_transform(r["transform"], False, False)
        smc = actor.static_mesh_component
        smc.set_static_mesh(r["mesh"])
        smc.set_material(0, grid_material(r["label"], r["mesh"]))


def build_environment(sun_transform):
    """Clear midday sky with drifting clouds, as in the video."""
    sun = spawn(unreal.DirectionalLight, (0, 0, 1000), label="Sun", folder="Lighting")
    sun.set_actor_rotation(sun_transform.rotation.rotator() if sun_transform else unreal.Rotator(roll=0.0, pitch=-55.0, yaw=-20.0), False)
    setp(sun.get_component_by_class(unreal.DirectionalLightComponent), intensity=7.0, atmosphere_sun_light=True,
         cast_cloud_shadows=False, dynamic_shadow_distance_movable_light=12000.0)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere", folder="Lighting")
    spawn(unreal.VolumetricCloud, (0, 0, 0), label="VolumetricCloud", folder="Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 600), label="SkyLight", folder="Lighting")
    setp(sky.get_component_by_class(unreal.SkyLightComponent), mobility=unreal.ComponentMobility.MOVABLE,
         real_time_capture=True, intensity=1.0)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -200), label="HeightFog", folder="Lighting")
    setp(fog.get_component_by_class(unreal.ExponentialHeightFogComponent), fog_density=0.004, fog_height_falloff=0.2)
    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), label="PostProcess", folder="Lighting")
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    setp(settings, override_auto_exposure_bias=True, auto_exposure_bias=0.6,
         override_motion_blur_amount=True, motion_blur_amount=0.35)
    ppv.set_editor_property("settings", settings)


# --------------------------------------------------------------------------------------------
# Arena content
# --------------------------------------------------------------------------------------------
def blue_material():
    """The template's flat colorway material, tinted the video's saturated blue."""
    mi = load(BLUE_MAT)
    if mi is None:
        factory = unreal.MaterialInstanceConstantFactoryNew()
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset("MI_Arena_PhysicsBlue", ROOT + "/Materials",
                                                                    unreal.MaterialInstanceConstant, factory)
    mi.set_editor_property("parent", load("/Game/LevelPrototyping/Materials/MI_DefaultColorway"))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Base Color", unreal.LinearColor(0.0, 0.11, 0.85, 1.0))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Roughness", 0.45)
    eal.save_loaded_asset(mi)
    return mi


def physics_cube(label, x, y, base, size, yaw, material):
    mesh = load("/Game/LevelPrototyping/Meshes/SM_ChamferCube")
    actor = spawn(unreal.StaticMeshActor, (0, 0, 0), yaw, label, "Props/PhysicsCubes")
    smc = actor.static_mesh_component
    smc.set_mobility(unreal.ComponentMobility.MOVABLE)
    smc.set_static_mesh(mesh)
    smc.set_material(0, material)
    bounds = mesh.get_bounding_box()
    extent = bounds.max - bounds.min
    scale = size / max(extent.x, 1.0)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    # Place by the bounds centre, whatever the mesh pivot is.
    center = (bounds.min + bounds.max) * 0.5 * scale
    rotated = unreal.MathLibrary.greater_greater_vector_rotator(center, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    actor.set_actor_location(unreal.Vector(x - rotated.x, y - rotated.y, base + size * 0.5 - rotated.z + 2.0), False, False)
    smc.set_collision_profile_name("PhysicsActor")
    smc.set_simulate_physics(True)
    # Knocking cubes around shouldn't rebuild the navmesh every frame.
    smc.set_editor_property("can_ever_affect_navigation", False)
    return actor


def bot(label, archetype, loc, yaw, patrol, speed, team=None, folder="Robots"):
    half = archetype.get_editor_property("capsule_half_height")
    actor = spawn(unreal.MetalBotCharacter, (loc[0], loc[1], loc[2] + half + 5.0), yaw, label, folder)
    setp(actor, archetype=archetype, patrol_radius=patrol, patrol_speed_fraction=speed, docile_until_attacked=True)
    if team is not None:
        setp(actor, override_team=True, team_override=team, passive=True)
    return actor


def build_arena(archetypes):
    meshes, markers = capture_template()

    if eal.does_asset_exist(ARENA_PATH):
        les.load_level(ARENA_PATH)
        for actor in actors.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
                actors.destroy_actor(actor)
    else:
        les.new_level(ARENA_PATH, False)
    log("level ready")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.MetalArenaGameMode.static_class())

    rebuild_template(meshes)
    build_environment(markers.get("DirectionalLight"))

    # The player starts on the central platform looking out over the east field, like the video's opening.
    spawn(unreal.PlayerStart, (-150, 0, 330), 0.0, "PlayerStart", "Gameplay")

    # Blue physics cubes, scattered on the platform and the floor as in the video.
    blue = blue_material()
    for i, (x, y, base, size, yaw) in enumerate([
            (330, 240, 200, 90, 15), (-320, -260, 200, 90, -20), (-430, 180, 200, 90, 40),
            (1050, 420, 0, 100, 10), (-1150, -500, 0, 100, -30), (500, 1250, 0, 100, 25),
            (-450, -1200, 0, 100, 5), (1300, -650, 0, 90, 35)]):
        physics_cube(f"PhysicsCube_{i}", x, y, base, size, yaw, blue)

    # Enemy patrols on the open floor between the central platform and the corner blocks. They mind
    # their own business until the player opens fire, then the whole group fights back.
    shooter, soldier = archetypes["shooter"], archetypes["soldier"]
    heroes = [archetypes["miner"], archetypes["worker"]]
    neutral = unreal.MetalTeam.NEUTRAL
    groups = {
        "East":  ((1300, 0), [(shooter, 0, -250), (shooter, 150, 120), (shooter, -120, 300), (heroes[0], 60, 30)]),
        "North": ((0, 1300), [(shooter, -250, 0), (soldier, 150, 80), (shooter, 300, -60)]),
        "West":  ((-1300, 0), [(shooter, 0, 250), (shooter, -150, -150), (heroes[1], 120, -40), (heroes[0], -60, 60)]),
        "South": ((0, -1300), [(shooter, 220, 0), (shooter, -200, 60), (heroes[1], 0, -150)]),
    }
    for name, ((cx, cy), members) in groups.items():
        for i, (arch, dx, dy) in enumerate(members):
            is_hero = arch in heroes
            bot(f"{'Hero' if is_hero else 'Enemy'}_{name}_{i}", arch, (cx + dx, cy + dy, 0), (i * 97) % 360,
                patrol=450.0 if is_hero else 650.0, speed=0.45 if is_hero else 0.6,
                team=neutral if is_hero else None, folder=f"Robots/{name}")

    nav = spawn(unreal.NavMeshBoundsVolume, (0, 0, 300), label="NavBounds", folder="Navigation")
    nav.set_actor_scale3d(unreal.Vector(4000 / 200.0, 4000 / 200.0, 900 / 200.0))

    les.save_current_level()
    log(f"level saved: {ARENA_PATH}")


def main():
    archetypes = {cid: load(f"{ROOT}/Data/DA_Bot_{name}") for cid, name in
                  [("shooter", "Shooter"), ("soldier", "Soldier"), ("miner", "Miner"), ("worker", "Worker")]}
    missing = [cid for cid, a in archetypes.items() if a is None]
    if missing:
        raise RuntimeError(f"missing archetypes {missing}: run ue_setup_gameplay.py first")
    build_arena(archetypes)
    log("DONE")


if __name__ == "__main__":
    main()
