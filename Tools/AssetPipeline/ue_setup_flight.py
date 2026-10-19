"""
Metalization pipeline: the Stalker flight sandbox (/Game/Metalization/Maps/MZ_FlightTest).

Phase 1 of Docs/SpaceBattle_Plan.md. A big open floor under a clear sky: the player starts next to
three parked Hero Stalkers (walk up, aim, [F] to pilot one), with towers to weave around, a squadron
of hovering Villain Stalkers to shoot (and shoot the arms off), and a few Shooters on the ground that
fire back. The map is regenerated in place every run like the others; make manual changes on a copy.

Called from ue_setup_gameplay.py after the data assets exist (MZ_LEVELS=flight rebuilds only this map).
"""
import unreal

ROOT = "/Game/Metalization"
MAP_PATH = ROOT + "/Maps/MZ_FlightTest"

eal = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder")


def log(msg):
    print("[MZFlight] " + msg, flush=True)


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def spawn(cls, loc, yaw=0.0, label=None, folder=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def box(label, center, size, material, folder="Geometry", mesh=None):
    actor = spawn(unreal.StaticMeshActor, center, 0.0, label, folder)
    smc = actor.static_mesh_component
    smc.set_static_mesh(mesh or CUBE)
    smc.set_material(0, load(f"{ROOT}/Materials/{material}"))
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return actor


def stalker(label, archetype, loc, yaw, team, parked, folder):
    half = archetype.get_editor_property("capsule_half_height")
    actor = spawn(unreal.MetalStalker, (loc[0], loc[1], loc[2] + (half + 5.0 if parked else 0.0)), yaw, label, folder)
    actor.set_editor_property("archetype", archetype)
    actor.set_editor_property("start_parked", parked)
    actor.set_editor_property("override_team", True)
    actor.set_editor_property("team_override", team)
    return actor


def shooter(label, archetype, loc, yaw):
    half = archetype.get_editor_property("capsule_half_height")
    actor = spawn(unreal.MetalBotCharacter, (loc[0], loc[1], loc[2] + half + 5.0), yaw, label, "Robots/Ground")
    actor.set_editor_property("archetype", archetype)
    actor.set_editor_property("patrol_radius", 500.0)
    return actor


def build_flight_test(archetypes):
    if eal.does_asset_exist(MAP_PATH):
        les.load_level(MAP_PATH)
        for actor in actors.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
                actors.destroy_actor(actor)
    else:
        les.new_level(MAP_PATH, False)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.MetalGameMode.static_class())

    import ue_setup_arena
    ue_setup_arena.build_environment(None)

    # 400 m square floor.
    box("Floor", (0, 0, -25), (40000, 40000, 50), "MI_Env_Floor")

    # Launch apron: the player and three parked Hero Stalkers, 20 m apart, noses toward the range (+X).
    spawn(unreal.PlayerStart, (0, 0, 120), 0.0, "PlayerStart", "Gameplay")
    box("Apron", (1800, 0, 5), (2400, 6400, 10), "MI_Env_Wall", "Apron")
    for i, y in enumerate((-2000, 0, 2000)):
        box(f"Pad_{i}", (1800, y, 12), (1500, 1300, 6), "MI_Env_Hazard", "Apron")
        stalker(f"Stalker_Hero_{i}", archetypes["stalker"], (1800, y, 15), 0.0, unreal.MetalTeam.HEROES, True, "Robots/Hangar")

    # Towers to weave between on the way to the range.
    for i, (x, y, h) in enumerate([(6000, -5000, 7000), (7500, 3500, 9000), (11000, -1500, 6000),
                                   (14000, 5000, 8000), (16000, -6000, 10000)]):
        box(f"Tower_{i}", (x, y, h / 2), (1400, 1400, h), "MI_Env_Dark", "Towers", mesh=CYLINDER)
    box("Arch_Top", (9500, 0, 4200), (800, 6000, 600), "MI_Env_Wall", "Towers")
    box("Arch_LegN", (9500, 2700, 2000), (800, 600, 4000), "MI_Env_Wall", "Towers")
    box("Arch_LegS", (9500, -2700, 2000), (800, 600, 4000), "MI_Env_Wall", "Towers")

    # Target squadron: hovering Villain Stalkers spread across the range (no pilots yet).
    for i, (x, y, z, yaw) in enumerate([(12000, 0, 2500, 180), (13500, -3000, 3500, 160), (13500, 3000, 1800, 200),
                                        (16000, -1000, 4500, 180), (17000, 2500, 2800, 210)]):
        stalker(f"Stalker_Villain_{i}", archetypes["stalker"], (x, y, z), yaw, unreal.MetalTeam.VILLAINS, False, "Robots/Targets")

    # Ground fire: a Shooter squad by a bunker under the range.
    box("Bunker", (12000, -6500, 150), (1200, 800, 300), "MI_Env_Wall", "Ground")
    for i, (dx, dy) in enumerate([(-700, 0), (0, 700), (700, 100)]):
        shooter(f"Shooter_{i}", archetypes["shooter"], (12000 + dx, -6500 + 600 + dy, 0), 180.0)
    nav = spawn(unreal.NavMeshBoundsVolume, (12000, -6000, 300), label="NavBounds", folder="Navigation")
    nav.set_actor_scale3d(unreal.Vector(4000 / 200.0, 4000 / 200.0, 900 / 200.0))

    les.save_current_level()
    log(f"level saved: {MAP_PATH}")
