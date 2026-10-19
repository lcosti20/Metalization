"""
Metalization pipeline: capital ship data and the space battle maps (Phase 3 of Docs/SpaceBattle_Plan.md).

Creates:
    /Game/Metalization/Data/DA_Ship_Capital              the capital ship class (UMetalCapitalShipData)
    /Game/Metalization/Maps/MZ_CapitalShip_Interior      the interior every ship streams in as its own instance:
                                                         hangar (AMetalHangar, 3 parked Stalkers), corridor, spawn room
    /Game/Metalization/Maps/MZ_SpaceBattle               two capital ships (Heroes, Villains) facing each other
                                                         across 1.6 km of asteroid field

Interior layout (cm, level origin = floor at the middle of the hangar mouth, which faces +Y):
    hangar      y 0 .. -10200  (from SM_Hangar_Shell; pads at y = -5500)
    corridor    y -10200 .. -12200, 8 x 6 m, through the shell's back door
    spawn room  y -12200 .. -14600, 24 x 8 m high, four player starts in line with the corridor

Called from ue_setup_gameplay.py after the archetypes exist (MZ_LEVELS=space rebuilds only these).
Maps are regenerated in place every run; make manual changes on a copy.
"""
import random

import unreal

ROOT = "/Game/Metalization"
DATA = ROOT + "/Data"
INTERIOR_PATH = ROOT + "/Maps/MZ_CapitalShip_Interior"
SPACE_PATH = ROOT + "/Maps/MZ_SpaceBattle"
SHIPS = ROOT + "/Ships"

eal = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
# Interior pieces stop drawing at 5 km (like AMetalHangar) so they never show up over the battle; space scenery at 15 km.
INTERIOR_DRAW = 500000.0
SPACE_DRAW = 1500000.0
# Same fixed exposure in both maps: there is no sky to adapt to, and the camera crosses between them.
EXPOSURE_EV100 = 3.5


def log(msg):
    print("[MZSpace] " + msg, flush=True)


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def setp(obj, **props):
    for k, v in props.items():
        obj.set_editor_property(k, v)
    return obj


def color(r, g, b):
    return unreal.LinearColor(r, g, b, 1.0)


def spawn(cls, loc, yaw=0.0, label=None, folder=None, pitch=0.0, roll=0.0):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def box(label, lo, hi, material, folder, draw=INTERIOR_DRAW, yaw=0.0, pitch=0.0, roll=0.0, center=None, size=None):
    """Cube spanning lo..hi (cm), or centre + size when rotated."""
    if center is None:
        center = [(a + b) / 2 for a, b in zip(lo, hi)]
        size = [b - a for a, b in zip(lo, hi)]
    actor = spawn(unreal.StaticMeshActor, center, yaw, label, folder, pitch, roll)
    smc = actor.static_mesh_component
    smc.set_static_mesh(CUBE)
    smc.set_material(0, load(f"{ROOT}/Materials/{material}"))
    smc.set_editor_property("ld_max_draw_distance", draw)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return actor


def mesh(label, static_mesh, loc, yaw, folder, draw=INTERIOR_DRAW):
    actor = spawn(unreal.StaticMeshActor, loc, yaw, label, folder)
    smc = actor.static_mesh_component
    smc.set_static_mesh(static_mesh)
    smc.set_editor_property("ld_max_draw_distance", draw)
    return actor


def point_light(label, loc, candela, radius, rgb=(1.0, 0.92, 0.82)):
    actor = spawn(unreal.PointLight, loc, label=label, folder="Lighting")
    setp(actor.point_light_component, intensity_units=unreal.LightUnits.CANDELAS, intensity=candela,
         attenuation_radius=radius, cast_shadows=False,
         light_color=unreal.Color(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255), 255))
    return actor


def fixed_exposure(folder="Lighting"):
    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), label="PostProcess", folder=folder)
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    setp(settings, override_auto_exposure_min_brightness=True, auto_exposure_min_brightness=EXPOSURE_EV100,
         override_auto_exposure_max_brightness=True, auto_exposure_max_brightness=EXPOSURE_EV100,
         override_auto_exposure_bias=True, auto_exposure_bias=0.0)
    ppv.set_editor_property("settings", settings)


def fresh_level(path, game_mode):
    if eal.does_asset_exist(path):
        les.load_level(path)
        for actor in actors.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
                actors.destroy_actor(actor)
    else:
        les.new_level(path, False)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode.static_class())


# --------------------------------------------------------------------------------------------
# Interior
# --------------------------------------------------------------------------------------------
def build_interior(archetypes):
    # Opened on its own it plays like any level (the hangar parks Hero Stalkers by itself).
    fresh_level(INTERIOR_PATH, unreal.MetalGameMode)

    hangar = spawn(unreal.MetalHangar, (0, 0, 0), 0.0, "Hangar", "Hangar")
    setp(hangar, shell_mesh=load(f"{SHIPS}/SM_Hangar_Shell"), pad_mesh=load(f"{SHIPS}/SM_Hangar_Pad"),
         robot_archetype=archetypes["stalker"], team=unreal.MetalTeam.HEROES, respawn_delay=20.0,
         interior_draw_distance=INTERIOR_DRAW)
    mesh("BlastDoor", load(f"{SHIPS}/SM_Hangar_BlastDoor"), (0, -10200, 0), 90.0, "Hangar")
    for i, (x, y) in enumerate([(x, y) for y in (-2500, -7500) for x in (-3000, 0, 3000)]):
        point_light(f"HangarLight_{i}", (x, y, 3700), 15000.0, 9000.0)

    # Corridor: 8 m wide, 6 m high, 20 m long, from the shell's back door to the spawn room.
    y0, y1 = -12200, -10200
    box("Corridor_Floor", (-500, y0, -50), (500, y1, 0), "MI_Env_Floor", "Corridor")
    box("Corridor_Ceiling", (-500, y0, 600), (500, y1, 700), "MI_Env_Wall", "Corridor")
    box("Corridor_Wall_W", (-500, y0, 0), (-400, y1, 600), "MI_Env_Wall", "Corridor")
    box("Corridor_Wall_E", (400, y0, 0), (500, y1, 600), "MI_Env_Wall", "Corridor")
    for i, y in enumerate((-10700, -11700)):
        point_light(f"CorridorLight_{i}", (0, y, 560), 600.0, 1500.0, (0.75, 0.9, 1.0))

    # Spawn room: 24 x 24 m, 8 m high; its north wall has the corridor opening.
    r0, r1, w, h = -14600, -12200, 1200, 800
    box("Spawn_Floor", (-w - 100, r0 - 100, -50), (w + 100, r1, 0), "MI_Env_Floor", "SpawnRoom")
    box("Spawn_Ceiling", (-w - 100, r0 - 100, h), (w + 100, r1, h + 100), "MI_Env_Wall", "SpawnRoom")
    box("Spawn_Wall_S", (-w - 100, r0 - 100, 0), (w + 100, r0, h), "MI_Env_Wall", "SpawnRoom")
    box("Spawn_Wall_W", (-w - 100, r0, 0), (-w, r1, h), "MI_Env_Wall", "SpawnRoom")
    box("Spawn_Wall_E", (w, r0, 0), (w + 100, r1, h), "MI_Env_Wall", "SpawnRoom")
    box("Spawn_Wall_N_W", (-w, r1 - 100, 0), (-400, r1, h), "MI_Env_Wall", "SpawnRoom")
    box("Spawn_Wall_N_E", (400, r1 - 100, 0), (w, r1, h), "MI_Env_Wall", "SpawnRoom")
    box("Spawn_Wall_N_Top", (-400, r1 - 100, 600), (400, r1, h), "MI_Env_Wall", "SpawnRoom")
    box("Spawn_Stripe", (-400, r1 - 102, 600), (400, r1 - 98, 630), "MI_Env_Glow", "SpawnRoom")
    # Two by two in line with the corridor, so walking straight ahead leads to the hangar.
    for i, (x, y) in enumerate([(x, y) for y in (-13300, -13900) for x in (-250, 250)]):
        spawn(unreal.PlayerStart, (x, y, 100), 90.0, f"PlayerStart_{i}", "SpawnRoom")
    for i, x in enumerate((-600, 600)):
        point_light(f"SpawnLight_{i}", (x, -13400, 760), 3500.0, 2500.0)

    fixed_exposure()
    nav = spawn(unreal.NavMeshBoundsVolume, (0, -7300, 400), label="NavBounds", folder="Navigation")
    nav.set_actor_scale3d(unreal.Vector(12800 / 200.0, 15200 / 200.0, 1200 / 200.0))
    les.save_current_level()
    log(f"level saved: {INTERIOR_PATH}")


# --------------------------------------------------------------------------------------------
# Ship class
# --------------------------------------------------------------------------------------------
def make_ship_data(archetypes):
    path = f"{DATA}/DA_Ship_Capital"
    data = load(path)
    if data is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.MetalCapitalShipData)
        data = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_Ship_Capital", DATA, unreal.MetalCapitalShipData, factory)

    def colors(body, accent):
        return setp(unreal.MetalTeamColors(), body=color(*body), accent=color(*accent))

    setp(data, display_name="Capital Ship",
         hull_mesh=load(f"{SHIPS}/SM_CapShip_Hull"), engines_mesh=load(f"{SHIPS}/SM_CapShip_Engines"),
         shield_generator_mesh=load(f"{SHIPS}/SM_CapShip_ShieldGen"), comms_mesh=load(f"{SHIPS}/SM_CapShip_Comms"),
         turret_base_mesh=load(f"{SHIPS}/SM_CapShip_TurretBase"), turret_gun_mesh=load(f"{SHIPS}/SM_CapShip_TurretGun"),
         hull_health=20000.0, shield_generator_health=2500.0, engines_health=4000.0, comms_health=2000.0,
         shielded_hull_damage_scale=0.25,
         team_colors={unreal.MetalTeam.HEROES: colors((0.16, 0.2, 0.36), (0.45, 0.52, 0.72)),
                      unreal.MetalTeam.VILLAINS: colors((0.36, 0.17, 0.13), (0.72, 0.5, 0.42))},
         interior=load(INTERIOR_PATH), hangar_robot=archetypes["stalker"], hangar_respawn_delay=20.0)
    eal.save_loaded_asset(data)
    log("ship data: " + path)
    return data


# --------------------------------------------------------------------------------------------
# Battle space
# --------------------------------------------------------------------------------------------
def build_space(ship_data):
    fresh_level(SPACE_PATH, unreal.MetalSpaceBattleGameMode)

    # Broadside to broadside, hangar mouths (starboard) facing each other across 1.6 km.
    # Interiors float 20 km up and 10 km apart: beyond each other's and the ships' draw distances.
    for label, team, y, yaw, interior in (("Ship_Heroes", unreal.MetalTeam.HEROES, -80000, 0.0, (0, 0, 2000000)),
                                          ("Ship_Villains", unreal.MetalTeam.VILLAINS, 80000, 180.0, (1000000, 0, 2000000))):
        ship = spawn(unreal.MetalCapitalShip, (0, y, 0), yaw, label, "Ships")
        setp(ship, ship_data=ship_data, team=team, interior_location=unreal.Vector(*interior), draw_distance=SPACE_DRAW)

    # High sun along the ships' length: decks and both broadsides catch light; a blue fill lifts the shadows.
    sun = spawn(unreal.DirectionalLight, (0, 0, 50000), label="Sun", folder="Lighting", pitch=-50.0, yaw=20.0)
    # The sun is the one directional light used for forward shading (the fill just lifts the shadows).
    setp(sun.get_component_by_class(unreal.DirectionalLightComponent), intensity=10.0, forward_shading_priority=1,
         dynamic_shadow_distance_movable_light=60000.0, light_color=unreal.Color(255, 244, 228, 255))
    fill = spawn(unreal.DirectionalLight, (0, 0, 50000), label="Fill", folder="Lighting", pitch=30.0, yaw=-160.0)
    setp(fill.get_component_by_class(unreal.DirectionalLightComponent), intensity=2.5, cast_shadows=False,
         light_color=unreal.Color(150, 175, 255, 255))
    fixed_exposure()

    # Asteroid field around (not between) the ships, so the lanes between the hangars stay open.
    rng = random.Random(7)
    for i in range(36):
        while True:
            x, y, z = rng.uniform(-200000, 200000), rng.uniform(-160000, 160000), rng.uniform(-60000, 60000)
            if abs(x) > 50000 or abs(y) > 120000:
                break
        s = rng.uniform(1500, 9000)
        box(f"Asteroid_{i:02d}", None, None, "MI_Env_Dark", "Asteroids", SPACE_DRAW,
            yaw=rng.uniform(0, 360), pitch=rng.uniform(-60, 60), roll=rng.uniform(0, 360),
            center=(x, y, z), size=(s, s * rng.uniform(0.6, 1.0), s * rng.uniform(0.5, 0.9)))

    les.save_current_level()
    log(f"level saved: {SPACE_PATH}")


def build_space_maps(archetypes):
    build_interior(archetypes)
    data = make_ship_data(archetypes)
    build_space(data)
