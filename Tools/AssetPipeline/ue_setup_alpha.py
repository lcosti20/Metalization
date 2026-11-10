"""
Metalization pipeline: the Production Alpha mission (/Game/Metalization/Maps/MZ_ProductionAlpha) and the
title screen (/Game/Metalization/Maps/MZ_Title).

OPERATION REDLINE - one ground mission, Blue (Heroes) vs Red (Villains). The map is a single valley run
west to east, and every section teaches one thing, simplest first, each gated by an objective:

    1  Blue camp            move / look
    2  Supply trench        sprint (a barrage chases you)
    3  Barricades           jump
    4  Collapsed bridge     double jump
    5  Cliff                anti-gravity lift
    6  Forward post         shooting, first weapon-arm swap (Repeater)
    7  Bunker               EMP grenades
    8  Patrol yard          sneaking (suspicion meters, crawl pipe)
    9  Sniper perch         lift + sniper rifle vs Red marksmen
    10 Shield gate          Hack Tether a Red guard
    11 Gate console         consoles (only Reds may use it); Blue reinforcements pour out
    12 Command yard         everything at once: Blue vs Red battle, lifts, the Brute commander
    13 Extraction

Called from ue_setup_gameplay.py (MZ_LEVELS=alpha,title) after the data assets exist. The maps are rebuilt
from scratch every run - make manual layout changes on a copy.
"""
import math

import unreal

ROOT = "/Game/Metalization"
MAP_PATH = ROOT + "/Maps/MZ_ProductionAlpha"
TITLE_PATH = ROOT + "/Maps/MZ_Title"
MAT_DIR = ROOT + "/Materials/Alpha"

eal = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder")
HEROES = unreal.MetalTeam.HEROES
VILLAINS = unreal.MetalTeam.VILLAINS
MATS = {}


def log(msg):
    print("[MZAlpha] " + msg, flush=True)


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def setp(obj, **props):
    for k, v in props.items():
        obj.set_editor_property(k, v)
    return obj


# --------------------------------------------------------------------------------------------
# Materials: the shared world-grid material in faction colours
# --------------------------------------------------------------------------------------------
def make_materials():
    parent = load(ROOT + "/Materials/M_Env_Metal")
    mel = unreal.MaterialEditingLibrary
    specs = {
        "MI_Alpha_Rock": ((0.36, 0.31, 0.27), 0.0),
        "MI_Alpha_Ground": ((0.42, 0.37, 0.30), 0.0),
        "MI_Alpha_Concrete": ((0.5, 0.5, 0.52), 0.0),
        "MI_Alpha_Blue": ((0.12, 0.28, 0.7), 0.0),
        "MI_Alpha_Red": ((0.62, 0.1, 0.07), 0.0),
        "MI_Alpha_BlueGlow": ((0.15, 0.5, 1.0), 5.0),
        "MI_Alpha_RedGlow": ((1.0, 0.12, 0.05), 5.0),
        "MI_Alpha_Fence": ((1.0, 0.25, 0.1), 3.0),
    }
    for name, (rgb, glow) in specs.items():
        mi = load(f"{MAT_DIR}/{name}")
        if mi is None:
            mi = asset_tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", parent)
        mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        mel.set_material_instance_scalar_parameter_value(mi, "Glow", glow)
        eal.save_loaded_asset(mi)
        MATS[name] = mi


def mat(name):
    if name not in MATS:
        MATS[name] = load(f"{MAT_DIR}/{name}") or load(f"{ROOT}/Materials/{name}")
    return MATS[name]


# --------------------------------------------------------------------------------------------
# Placement helpers (all sizes and positions in cm)
# --------------------------------------------------------------------------------------------
def spawn(cls, loc, yaw=0.0, label=None, folder=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def box(label, center, size, material="MI_Alpha_Concrete", folder="Geometry", mesh=None, rot=None, collision=None):
    actor = spawn(unreal.StaticMeshActor, center, 0.0, label, folder)
    smc = actor.static_mesh_component
    smc.set_static_mesh(mesh or CUBE)
    smc.set_material(0, mat(material))
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    if rot:
        actor.set_actor_rotation(rot, False)
    if collision:
        smc.set_collision_profile_name(collision)
    return actor


def block(label, x0, x1, y0, y1, z0, z1, material="MI_Alpha_Concrete", folder="Geometry", collision=None):
    """Axis-aligned box from corner ranges."""
    return box(label, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), material, folder, collision=collision)


def ground(label, x0, x1, y0, y1, top, material="MI_Alpha_Ground", thickness=60):
    return block(label, x0, x1, y0, y1, top - thickness, top, material)


def ramp_x(label, x0, z0, x1, z1, y, width, material="MI_Alpha_Concrete"):
    """Walkable slab rising from (x0, z0) to (x1, z1) along X."""
    dx, dz = x1 - x0, z1 - z0
    length = math.hypot(dx, dz)
    pitch = math.degrees(math.atan2(dz, dx))
    return box(label, ((x0 + x1) / 2, y, (z0 + z1) / 2 - 20), (length + 40, width, 40), material,
               rot=unreal.Rotator(roll=0.0, pitch=pitch, yaw=0.0))


def ramp_y(label, y0, z0, y1, z1, x, width, material="MI_Alpha_Concrete"):
    """Walkable slab rising from (y0, z0) to (y1, z1) along +Y (a ramp along X turned 90 degrees)."""
    dy, dz = y1 - y0, z1 - z0
    length = math.hypot(dy, dz)
    pitch = math.degrees(math.atan2(dz, abs(dy)))
    return box(label, (x, (y0 + y1) / 2, (z0 + z1) / 2 - 20), (length + 40, width, 40), material,
               rot=unreal.Rotator(roll=0.0, pitch=pitch, yaw=90.0 if dy > 0 else -90.0))


def crate(label, x, y, base, size=(160, 160, 130), material="MI_Alpha_Concrete", folder="Cover"):
    return box(label, (x, y, base + size[2] / 2), size, material, folder)


def pillar(label, x, y, base, height, radius, material="MI_Alpha_Rock"):
    return box(label, (x, y, base + height / 2), (radius * 2, radius * 2, height), material, "Geometry", mesh=CYLINDER)


def point_light(label, loc, rgb, intensity=9000.0, radius=1800.0):
    actor = spawn(unreal.PointLight, loc, label=label, folder="Lighting")
    setp(actor.point_light_component, intensity=intensity, attenuation_radius=radius, cast_shadows=False,
         light_color=unreal.Color(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255), 255))
    return actor


def spot_light(label, loc, rgb, intensity=60000.0, cone=32.0, radius=2400.0):
    actor = spawn(unreal.SpotLight, loc, label=label, folder="Lighting")
    actor.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0), False)
    setp(actor.spot_light_component, intensity=intensity, attenuation_radius=radius, outer_cone_angle=cone,
         inner_cone_angle=cone * 0.7, cast_shadows=True,
         light_color=unreal.Color(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255), 255))
    return actor


def bot(label, archetype, loc, yaw, tags=(), patrol=500.0, passive=False, team=None, docile=False, folder="Robots"):
    half = archetype.get_editor_property("capsule_half_height")
    actor = spawn(unreal.MetalBotCharacter, (loc[0], loc[1], loc[2] + half + 5.0), yaw, label, folder)
    setp(actor, archetype=archetype, patrol_radius=patrol, passive=passive, docile_until_attacked=docile)
    if team is not None:
        setp(actor, override_team=True, team_override=team)
    actor.set_editor_property("tags", [unreal.Name(t) for t in tags])
    return actor


def pickup(label, ptype, loc, amount, weapon=None):
    actor = spawn(unreal.MetalPickup, (loc[0], loc[1], loc[2] + 70.0), label=label, folder="Pickups")
    setp(actor, type=ptype, amount=amount)
    if weapon:
        actor.set_editor_property("weapon", weapon)
    return actor


def zone(label, center, extent, checkpoint=False, rgb=(0.1, 0.8, 1.0), dialogue=(), marker=True):
    actor = spawn(unreal.MetalTriggerZone, center, label=label, folder="Mission")
    actor.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector(*extent))
    setp(actor, is_checkpoint=checkpoint, marker_color=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0),
         show_marker=marker, dialogue=list(dialogue))
    return actor


def lift(label, pad, height, exit_dir=(1.0, 0.0, 0.0), radius=140.0, rgb=(0.25, 0.85, 1.0)):
    actor = spawn(unreal.MetalGravLift, pad, label=label, folder="Lifts")
    setp(actor, lift_height=height, radius=radius, exit_direction=unreal.Vector(*exit_dir),
         color=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return actor


def gate(label, loc, yaw=0.0, material="MI_Alpha_Red"):
    actor = spawn(unreal.MetalGate, loc, yaw, label, "Gates")
    actor.get_component_by_class(unreal.StaticMeshComponent).set_material(0, mat(material))
    return actor


def line(text, duration=5.0, speaker="Command"):
    return setp(unreal.MetalDialogueLine(), speaker=speaker, line=text, duration=duration)


def objective(text, otype, tag=None, target=None, dialogue=(), hint="", marker=True):
    o = setp(unreal.MetalObjective(), description=text, type=otype, start_dialogue=list(dialogue), hint=hint, show_marker=marker)
    if tag:
        o.set_editor_property("target_tag", unreal.Name(tag))
    if target:
        o.set_editor_property("target_actor", target)
    return o


def ambience(label, sound, volume):
    actor = spawn(unreal.AmbientSound, (0, 0, 500), label=label, folder="Audio")
    comp = actor.get_component_by_class(unreal.AudioComponent)
    comp.set_editor_property("sound", load(sound))
    comp.set_editor_property("volume_multiplier", volume)
    return actor


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


def environment(sun_pitch=-40.0, sun_yaw=-145.0, sun_rgb=(255, 205, 160), fog_rgb=(0.45, 0.3, 0.16), fog=0.008):
    """Late-afternoon sun over a dusty valley. Same recipe as the Outpost (a strong warm sun, a weak warm
    skylight, dusty sky and warm haze), which keeps the graybox colours readable; a cooler, brighter sky
    washed everything out blue-white."""
    sun = spawn(unreal.DirectionalLight, (0, 0, 3000), label="Sun", folder="Lighting")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=sun_pitch, yaw=sun_yaw), False)
    setp(sun.get_component_by_class(unreal.DirectionalLightComponent), intensity=26.0, atmosphere_sun_light=True,
         light_color=unreal.Color(*sun_rgb, 255), dynamic_shadow_distance_movable_light=15000.0)
    atmosphere = spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere", folder="Lighting")
    atmosphere.get_component_by_class(unreal.SkyAtmosphereComponent).set_editor_property(
        "rayleigh_scattering", unreal.LinearColor(0.0095, 0.0125, 0.0215, 1.0))
    sky = spawn(unreal.SkyLight, (0, 0, 600), label="SkyLight", folder="Lighting")
    setp(sky.get_component_by_class(unreal.SkyLightComponent), mobility=unreal.ComponentMobility.MOVABLE,
         real_time_capture=True, intensity=0.3, light_color=unreal.Color(255, 210, 170, 255))
    haze = spawn(unreal.ExponentialHeightFog, (0, 0, 0), label="HeightFog", folder="Lighting")
    setp(haze.get_component_by_class(unreal.ExponentialHeightFogComponent), fog_density=fog,
         fog_inscattering_luminance=unreal.LinearColor(fog_rgb[0], fog_rgb[1], fog_rgb[2], 1.0))
    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), label="PostProcess", folder="Lighting")
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    setp(settings, override_auto_exposure_bias=True, auto_exposure_bias=0.0, override_vignette_intensity=True,
         vignette_intensity=0.5, override_motion_blur_amount=True, motion_blur_amount=0.25)
    ppv.set_editor_property("settings", settings)


# --------------------------------------------------------------------------------------------
# Faction set dressing
# --------------------------------------------------------------------------------------------
def banner(label, x, y, base, faction="Blue", height=600):
    """Flag pole with a faction banner and a glowing cap."""
    side = "MI_Alpha_Blue" if faction == "Blue" else "MI_Alpha_Red"
    glow = "MI_Alpha_BlueGlow" if faction == "Blue" else "MI_Alpha_RedGlow"
    pillar(f"{label}_Pole", x, y, base, height, 12, "MI_Env_Dark")
    box(f"{label}_Flag", (x, y + 70, base + height - 130), (10, 130, 220), side, "Decor")
    box(f"{label}_Cap", (x, y, base + height + 10), (40, 40, 20), glow, "Decor")


def sandbags(label, x, y0, y1, base, height=95, material="MI_Alpha_Blue"):
    block(label, x - 40, x + 40, y0, y1, base, base + height, "MI_Alpha_Ground", "Cover")
    block(label + "_Trim", x - 42, x + 42, y0, y1, base + height, base + height + 8, material, "Decor")


def tent(label, x, y, base, faction="Blue"):
    side = "MI_Alpha_Blue" if faction == "Blue" else "MI_Alpha_Red"
    block(label, x - 220, x + 220, y - 160, y + 160, base, base + 260, "MI_Alpha_Concrete", "Decor")
    block(label + "_Roof", x - 240, x + 240, y - 180, y + 180, base + 260, base + 290, side, "Decor")
    block(label + "_Stripe", x - 222, x + 222, y - 162, y + 162, base + 120, base + 140,
          "MI_Alpha_BlueGlow" if faction == "Blue" else "MI_Alpha_RedGlow", "Decor")


def watchtower(label, x, y, base, top, faction="Red"):
    """Square tower with a parapet; a marksman stands on top."""
    block(label, x - 150, x + 150, y - 150, y + 150, base, top, "MI_Alpha_Concrete", "Geometry")
    trim = "MI_Alpha_Red" if faction == "Red" else "MI_Alpha_Blue"
    for i, (a, b, c, d) in enumerate([(-170, 170, -170, -150), (-170, 170, 150, 170), (-170, -150, -150, 150), (150, 170, -150, 150)]):
        block(f"{label}_Parapet{i}", x + a, x + b, y + c, y + d, top, top + 60, trim, "Geometry")
    box(f"{label}_Lamp", (x, y, top + 140), (30, 30, 30), "MI_Alpha_RedGlow" if faction == "Red" else "MI_Alpha_BlueGlow", "Decor")


# --------------------------------------------------------------------------------------------
# Robots for this mission
# --------------------------------------------------------------------------------------------
ARCHETYPE_PROPS = ("archetype_id", "display_name", "team", "mesh", "parts", "unarmed_part", "mesh_scale", "mesh_offset",
                   "team_colors", "rig", "capsule_radius", "capsule_half_height", "max_health", "walk_speed",
                   "jump_z_velocity", "max_jumps", "hit_zones", "starting_weapons", "melee_weapon", "grenade_weapon",
                   "starting_grenades", "muzzle_bones", "sight_range", "sight_half_angle", "preferred_range",
                   "aim_error_degrees", "burst_duration", "burst_pause", "retreat_health_fraction", "squad_alert_radius",
                   "hackable", "possession_duration", "ally_hackable", "camera_distance", "camera_offset")


def variant(name, base, **overrides):
    """A copy of an archetype with a few changes (DA_Bot_<name>)."""
    path = f"{ROOT}/Data/DA_Bot_{name}"
    asset = load(path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.MetalBotArchetype)
        asset = asset_tools.create_asset(f"DA_Bot_{name}", ROOT + "/Data", unreal.MetalBotArchetype, factory)
    for prop in ARCHETYPE_PROPS:
        asset.set_editor_property(prop, base.get_editor_property(prop))
    setp(asset, **overrides)
    eal.save_loaded_asset(asset)
    return asset


def mission_archetypes(archetypes):
    """Blue allies are the same robot types as the Reds on the Heroes side (team colours repaint them
    blue). The camp and forward-post squads use the stock robots, so they only notice Reds the player has
    drawn close; the hangar reinforcements see far, so they storm the yard once the gate opens."""
    a = dict(archetypes)
    a["ally_soldier"] = archetypes["soldier"]
    a["ally_shooter"] = archetypes["shooter"]
    a["blue_soldier"] = variant("BlueSoldier", archetypes["soldier"], display_name="Blue Soldier", team=HEROES,
                                max_health=260.0, sight_range=6500.0, sight_half_angle=80.0, hackable=False)
    a["blue_shooter"] = variant("BlueShooter", archetypes["shooter"], display_name="Blue Shooter", team=HEROES,
                                max_health=160.0, sight_range=6500.0, sight_half_angle=80.0, hackable=False,
                                retreat_health_fraction=0.0)
    return a


# --------------------------------------------------------------------------------------------
# The mission map
# --------------------------------------------------------------------------------------------
def build_alpha(archetypes, w):
    make_materials()
    fresh_level(MAP_PATH, unreal.MetalAlphaGameMode)
    environment()
    A = mission_archetypes(archetypes)
    X0, X1, Y = -2200, 38000, 1600
    UP = 1250       # cliff-top plateau height (sections 6-9)
    LEDGE = 250     # far side of the collapsed bridge

    # --- Valley shell: rock walls both sides, end caps -------------------------------------------
    block("Cliff_North", X0, X1, Y, Y + 400, -700, 3000, "MI_Alpha_Rock")
    block("Cliff_South", X0, X1, -Y - 400, -Y, -700, 3000, "MI_Alpha_Rock")
    block("Cliff_West", X0 - 400, X0, -Y, Y, -700, 3000, "MI_Alpha_Rock")
    block("Cliff_East", X1, X1 + 400, -Y, Y, -700, 3000, "MI_Alpha_Rock")
    for x in range(X0 + 1500, X1, 3000):
        for y in (Y - 60, -Y + 60):
            pillar(f"Buttress_{x}_{y}", x, y, -700, 3600, 180, "MI_Alpha_Rock")

    # --- 1. Blue camp ------------------------------------------------------------------------------
    ground("Ground_Camp", X0, 1600, -Y, Y, 0)
    spawn(unreal.PlayerStart, (-1500, 0, 120), 0.0, "PlayerStart", "Mission")
    for i, (x, y) in enumerate([(-1100, 900), (-300, 950), (400, 900), (-700, -950)]):
        tent(f"Camp_Tent_{i}", x, y, 0)
    for i, (x, y) in enumerate([(-1800, 600), (-1800, -600), (1300, 700), (1300, -700)]):
        banner(f"Camp_Banner_{i}", x, y, 0)
    for i, (x, y) in enumerate([(200, -900), (500, -600), (800, -1000)]):
        crate(f"Camp_Crate_{i}", x, y, 0, material="MI_Alpha_Blue")
    bot("Blue_Commander", A["veteran"] if "veteran" in A else A["guide"], (-900, -350, 0), 30.0, patrol=0.0, passive=True, team=HEROES)
    for i, (x, y, yaw) in enumerate([(-400, 400, 200.0), (-200, -500, 150.0), (700, 300, 180.0), (900, -350, 160.0)]):
        bot(f"Blue_CampTrooper_{i}", A["ally_soldier" if i % 2 else "ally_shooter"], (x, y, 0), yaw, patrol=0.0, passive=True, team=HEROES)
    point_light("Camp_Light_A", (-800, 0, 500), (0.6, 0.75, 1.0), 4800.0, 2400.0)
    point_light("Camp_Light_B", (800, 0, 500), (0.6, 0.75, 1.0), 4800.0, 2400.0)
    zone_trench_entry = zone("Zone_TrenchEntry", (1500, 0, 150), (150, 300, 150))

    # --- 2. Supply trench (sprint) - cut into the ground ---------------------------------------
    TX0, TX1 = 1600, 6500
    ground("Ground_Trench", TX0, TX1, -300, 300, 0, "MI_Env_Dark")
    block("Trench_Bank_N", TX0, TX1, 300, Y, -60, 330, "MI_Alpha_Ground")
    block("Trench_Bank_S", TX0, TX1, -Y, -300, -60, 330, "MI_Alpha_Ground")
    block("Camp_Wall_N", 1400, TX0, 300, Y, 0, 330, "MI_Alpha_Ground")
    block("Camp_Wall_S", 1400, TX0, -Y, -300, 0, 330, "MI_Alpha_Ground")
    for i, x in enumerate(range(TX0 + 300, TX1, 700)):
        block(f"Trench_Brace_{i}", x - 15, x + 15, -300, 300, 300, 330, "MI_Env_Dark", "Decor")
        sandbags(f"Trench_Sandbag_N{i}", x + 200, 300, 520, 330, 70, "MI_Alpha_Blue")
        point_light(f"Trench_Light_{i}", (x, 250, 260), (1.0, 0.7, 0.4), 1400.0, 900.0)
    barrage = spawn(unreal.MetalBarrage, ((TX0 + TX1) / 2, 0, 150), label="Trench_Barrage", folder="Mission")
    barrage.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector((TX1 - TX0) / 2 - 100, 280, 250))
    pickup("Pickup_Health_TrenchEnd", unreal.MetalPickupType.HEALTH, (6300, -200, 0), 50.0)
    zone_trench_end = zone("Zone_TrenchEnd", (6350, 0, 150), (150, 300, 150))

    # --- 3. Barricades (jump) ---------------------------------------------------------------------
    BX0, BX1 = TX1, 8900
    ground("Ground_Barricades_A", BX0, 7950, -Y, Y, 0)
    ground("Ground_Barricades_B", 8250, BX1, -Y, Y, 0)
    ground("Ground_Barricades_Ditch", 7950, 8250, -Y, Y, -150, "MI_Env_Dark")   # shallow: jump back out
    block("Barricade_Wall_N", BX0, BX1, 1100, Y, 0, 330, "MI_Alpha_Ground")
    block("Barricade_Wall_S", BX0, BX1, -Y, -1100, 0, 330, "MI_Alpha_Ground")
    block("Barricade_1", 6950, 7020, -1100, 1100, 0, 110, "MI_Alpha_Blue", "Traversal")
    block("Barricade_2a", 7400, 7470, -1100, 100, 0, 120, "MI_Alpha_Concrete", "Traversal")
    block("Barricade_2b", 7650, 7720, -100, 1100, 0, 120, "MI_Alpha_Concrete", "Traversal")
    block("Barricade_Ditch_Lip", 7940, 7950, -1100, 1100, 0, 8, "MI_Env_Hazard", "Decor")
    zone_barricades = zone("Zone_Barricades", (8650, 0, 150), (200, 1000, 150))

    # --- 4. Collapsed bridge (double jump) --------------------------------------------------------
    ground("Bridge_Deck_Near", BX1, 9400, -700, 700, 0, "MI_Alpha_Concrete")
    block("Bridge_Fill_N", BX1, 9400, 700, Y, -60, 330, "MI_Alpha_Rock")
    block("Bridge_Fill_S", BX1, 9400, -Y, -700, -60, 330, "MI_Alpha_Rock")
    ground("Chasm_Floor", 9400, 9800, -Y, Y, -500, "MI_Env_Dark")
    ramp_y("Chasm_EscapeRamp", -1300, -500, 0, 0, 9550, 260, "MI_Alpha_Concrete")
    block("Chasm_Wall_Far", 9800, 9900, -Y, Y, -500, LEDGE, "MI_Alpha_Rock")
    ground("Ledge_Plateau", 9800, 11400, -Y, Y, LEDGE, "MI_Alpha_Ground")
    block("Ledge_Under", 9800, 11400, -Y, Y, -500, LEDGE - 60, "MI_Alpha_Rock")
    # Broken deck pieces in the chasm and the stub of the far span.
    box("Bridge_Debris_A", (9600, 300, -420), (380, 500, 40), "MI_Alpha_Concrete", rot=unreal.Rotator(roll=10.0, pitch=-25.0, yaw=0.0))
    box("Bridge_Debris_B", (9620, -500, -440), (300, 400, 40), "MI_Alpha_Concrete", rot=unreal.Rotator(roll=-8.0, pitch=18.0, yaw=10.0))
    block("Bridge_Rail_N", BX1, 9400, 680, 700, 0, 100, "MI_Env_Hazard", "Decor")
    block("Bridge_Rail_S", BX1, 9400, -700, -680, 0, 100, "MI_Env_Hazard", "Decor")
    point_light("Bridge_Light", (9600, 0, 400), (1.0, 0.6, 0.3), 3600.0, 1500.0)
    zone_ledge = zone("Checkpoint_Ledge", (10100, 0, LEDGE + 150), (250, 1200, 150), checkpoint=True)

    # --- 5. Cliff + anti-gravity lift -------------------------------------------------------------
    block("Cliff_Face", 11400, 11700, -Y, Y, LEDGE - 60, UP, "MI_Alpha_Rock")
    lift("Lift_Cliff", (11200, 0, LEDGE), UP - LEDGE + 80, (1.0, 0.0, 0.0))
    box("Lift_Cliff_Sign", (11380, 0, LEDGE + 500), (20, 400, 60), "MI_Alpha_BlueGlow", "Decor")
    ground("Plateau", 11400, 23500, -Y, Y, UP, "MI_Alpha_Ground")
    zone_cliff_top = zone("Zone_CliffTop", (12000, 0, UP + 150), (250, 1200, 150))

    # --- 6. Forward post (shooting + first weapon swap) -------------------------------------------
    sandbags("Post_Sandbags_N", 12350, 150, 900, UP)
    sandbags("Post_Sandbags_S", 12350, -900, -150, UP)
    banner("Post_Banner", 11900, 1100, UP, "Blue", 500)
    # The Blue squad holds just behind the sandbags. The Reds are kept 38 m+ away (beyond either side's sight)
    # so the firefight only starts when the player pushes forward and drags the Reds into range.
    for i, (x, y) in enumerate([(11800, 500), (11850, -500), (11750, 0)]):
        bot(f"Blue_PostTrooper_{i}", A["ally_soldier" if i != 2 else "ally_shooter"], (x, y, UP), 0.0, patrol=80.0, team=HEROES)
    pickup("Pickup_Repeater", unreal.MetalPickupType.WEAPON, (12650, 350, UP), 1.0, w["Repeater"])
    pickup("Pickup_Health_Post", unreal.MetalPickupType.HEALTH, (12650, -400, UP), 50.0)
    for i, (x, y, s) in enumerate([(13800, 600, (200, 200, 140)), (14300, -500, (260, 140, 120)), (14900, 200, (200, 200, 150)),
                                   (15500, -800, (160, 300, 130)), (15700, 700, (200, 200, 140))]):
        crate(f"Post_Cover_{i}", x, y, UP, s, "MI_Alpha_Concrete")
    squad1 = [bot("Red_PostShooter_1", A["shooter"], (15950, -300, UP), 180.0, ("Squad1",), 250.0),
              bot("Red_PostShooter_2", A["shooter"], (16200, 500, UP), 180.0, ("Squad1",), 250.0),
              bot("Red_PostShooter_3", A["shooter"], (16300, -900, UP), 180.0, ("Squad1",), 250.0)]
    banner("Post_RedBanner", 16300, 1100, UP, "Red", 500)
    point_light("Post_Light", (13800, 0, UP + 600), (1.0, 0.75, 0.5), 5600.0, 3000.0)

    # --- 7. Bunker (grenades) -----------------------------------------------------------------------
    BK = 17600
    block("Bunker_Front", BK - 600, BK - 500, -800, 800, UP, UP + 95, "MI_Alpha_Red")
    block("Bunker_Side_N", BK - 600, BK + 500, 800, 900, UP, UP + 95, "MI_Alpha_Concrete")
    block("Bunker_Side_S", BK - 600, BK + 500, -900, -800, UP, UP + 95, "MI_Alpha_Concrete")
    block("Bunker_Back_N", BK + 400, BK + 500, 250, 800, UP, UP + 95, "MI_Alpha_Concrete")
    block("Bunker_Back_S", BK + 400, BK + 500, -800, -250, UP, UP + 95, "MI_Alpha_Concrete")
    block("Bunker_Stripe", BK - 605, BK - 600, -800, 800, UP + 70, UP + 82, "MI_Alpha_RedGlow", "Decor")
    # Hunkered down behind a chest-high wall, close together: only their heads show, which makes them a
    # poor target for bullets and a perfect one for a grenade that stuns the lot.
    bunker = [bot(f"Red_BunkerShooter_{i}", A["shooter"], (BK + dx, dy, UP), 180.0, ("Bunker",), 0.0, docile=True)
              for i, (dx, dy) in enumerate([(-300, -250), (-250, 150), (-100, -50)])]
    pickup("Pickup_Grenades_A", unreal.MetalPickupType.GRENADES, (16600, 500, UP), 3.0)
    pickup("Pickup_Grenades_B", unreal.MetalPickupType.GRENADES, (16600, -500, UP), 3.0)
    pickup("Pickup_Ammo_Bunker", unreal.MetalPickupType.AMMO, (18600, 1100, UP), 0.6)
    point_light("Bunker_Light", (BK, 0, UP + 600), (1.0, 0.3, 0.2), 4800.0, 2000.0)

    # --- 8. Patrol yard (sneak) ---------------------------------------------------------------------
    YX0, YX1 = 18900, 23000
    # A wall shuts the yard off from the bunker fight; its only gap lines up with the crawl pipe.
    block("Yard_Wall", 18750, 18850, -1000, Y, UP, UP + 450, "MI_Alpha_Red")
    block("Yard_Wall_Stripe", 18745, 18750, -1000, Y, UP + 300, UP + 320, "MI_Alpha_RedGlow", "Decor")
    zone_yard_cp = zone("Checkpoint_Yard", (19050, -1225, UP + 150), (150, 225, 150), checkpoint=True, marker=False)
    # Low walls: cover for a sneaking robot, not for a standing one.
    for i, (x, y0, y1) in enumerate([(19700, -400, 500), (20400, -900, -100), (20400, 400, 1100), (21200, -300, 700),
                                     (21900, -1000, -300), (22300, 300, 1000)]):
        block(f"Yard_LowWall_{i}", x - 50, x + 50, y0, y1, UP, UP + 105, "MI_Alpha_Concrete", "Cover")
    # Crawl pipe along the south wall: only fits a crouched robot (and blocks every sight line).
    PX0, PX1, PY0, PY1 = 19300, 22700, -1450, -1000
    block("Pipe_Wall_Out", PX0, PX1, PY0 - 60, PY0, UP, UP + 115, "MI_Env_Dark")
    block("Pipe_Wall_In", PX0, PX1, PY1, PY1 + 60, UP, UP + 115, "MI_Env_Dark")
    block("Pipe_Roof", PX0, PX1, PY0 - 60, PY1 + 60, UP + 115, UP + 175, "MI_Alpha_Blue")
    for i, x in enumerate(range(PX0 + 400, PX1, 800)):
        point_light(f"Pipe_Light_{i}", (x, (PY0 + PY1) / 2, UP + 80), (0.3, 0.6, 1.0), 1500.0, 500.0)
    yard = [bot(f"Red_YardPatrol_{i}", A["shooter" if i % 3 else "soldier"], (x, y, UP), yaw, ("Yard",), 800.0)
            for i, (x, y, yaw) in enumerate([(20000, 300, 180.0), (20800, -500, 90.0), (21600, 500, -90.0), (22200, -200, 180.0)])]
    for i, (x, y) in enumerate([(19900, -200), (20800, 700), (21500, -500), (22300, 400)]):
        pillar(f"Yard_LampPost_{i}", x, y, UP, 650, 15, "MI_Env_Dark")
        spot_light(f"Yard_Spot_{i}", (x, y, UP + 640), (1.0, 0.35, 0.25), 18000.0, 30.0, 1500.0)
    banner("Yard_Banner_A", 19300, 1300, UP, "Red")
    banner("Yard_Banner_B", 22700, 1300, UP, "Red")
    zone_yard_exit = zone("Zone_YardExit", (22850, 0, UP + 150), (150, 1500, 150), rgb=(0.3, 0.9, 1.0))

    # --- 9. Sniper perch (lift + sniper) ------------------------------------------------------------
    PERCH = UP + 900
    block("Perch_Tower", 22950, 23450, 600, 1400, UP, PERCH, "MI_Alpha_Concrete")
    block("Perch_Floor", 22950, 23450, 600, 1400, PERCH - 40, PERCH, "MI_Alpha_Blue")
    # Only a low lip on the valley side, so shots angled down at the towers clear the edge.
    block("Perch_Lip", 23440, 23470, 600, 1400, PERCH, PERCH + 20, "MI_Env_Hazard", "Decor")
    block("Perch_Rail", 22950, 23470, 1380, 1420, PERCH, PERCH + 90, "MI_Env_Hazard", "Decor")
    lift("Lift_Perch", (23200, 330, UP), PERCH - UP + 80, (0.0, 1.0, 0.0))
    pickup("Pickup_Sniper", unreal.MetalPickupType.WEAPON, (23380, 1000, PERCH), 1.0, w["Sniper"])
    pickup("Pickup_Health_Perch", unreal.MetalPickupType.HEALTH, (23050, 750, PERCH), 40.0)
    zone_perch = zone("Zone_Perch", (23200, 1000, PERCH + 120), (220, 380, 120), checkpoint=True, rgb=(0.3, 0.9, 1.0))

    # --- Valley floor (sections 10-13) and the way down -------------------------------------------
    VX0 = 23500
    block("Valley_CliffFace", VX0 - 100, VX0, -Y, Y, -60, UP, "MI_Alpha_Rock")
    ground("Valley_Floor", VX0, X1, -Y, Y, 0, "MI_Alpha_Ground")
    ramp_x("Valley_Ramp", VX0, UP, VX0 + 4400, 0, -1350, 420, "MI_Alpha_Concrete")
    # Marksman towers covering the valley.
    marksmen = []
    for i, (x, y) in enumerate([(26600, -400), (28200, 900), (29700, -200)]):
        watchtower(f"Valley_Tower_{i}", x, y, 0, 650)
        marksmen.append(bot(f"Red_Marksman_{i}", A["marksman"], (x, y, 650), 180.0, ("Marksmen",), 0.0))
    for i, (x, y) in enumerate([(25500, 300), (27300, -900), (28900, 200)]):
        crate(f"Valley_Cover_{i}", x, y, 0, (220, 200, 160), "MI_Alpha_Concrete")

    # --- 10/11. Shield gate, console and the Blue reinforcement hangar ---------------------------
    GX = 31000
    # Checkpoint across the valley just short of the fence (west of the hack test's start spot at GX - 350).
    zone("Checkpoint_Gate", (GX - 700, 0, 150), (200, Y, 150), checkpoint=True)
    main_gate = gate("ShieldGate", (GX, 0, 0), 0.0, "MI_Alpha_Red")
    block("ShieldGate_Lintel", GX - 60, GX + 60, -320, 320, 450, 520, "MI_Alpha_Red")
    for side, (y0, y1) in (("N", (300, 1000)), ("S", (-1000, -300))):
        block(f"Fence_Base_{side}", GX - 50, GX + 50, y0, y1, 0, 40, "MI_Env_Dark")
        # Energy fence: blocks robots but not sight lines, shots or the Hack Tether.
        block(f"Fence_Field_{side}", GX - 10, GX + 10, y0, y1, 40, 520, "MI_Alpha_Fence", "Gates", collision="InvisibleWall")
        block(f"Fence_Block_{side}", GX - 50, GX + 50, y0, y1, 40, 520, "MI_Alpha_Fence", "Gates", collision="InvisibleWall").set_actor_hidden_in_game(True)
        block(f"Fence_Post_{side}", GX - 60, GX + 60, y1 - 20 if y1 > 0 else y0, y1 if y1 > 0 else y0 + 20, 0, 560, "MI_Alpha_Red")
    block("GateWall_N", GX - 60, GX + 60, 1000, Y, 0, 600, "MI_Alpha_Concrete")
    block("GateWall_S", GX - 60, GX + 60, -Y, -1000, 0, 600, "MI_Alpha_Concrete")
    # The guard idles behind the see-through part of the fence (not the solid door), so the tether can reach.
    guard = bot("Red_GateGuard", A["soldier"], (GX + 650, 650, 0), 180.0, ("GateGuard",), 200.0, docile=True)
    console = spawn(unreal.MetalControlConsole, (GX + 500, -1250, 0), 90.0, "GateConsole", "Gates")
    point_light("Gate_Light", (GX - 300, 0, 600), (1.0, 0.3, 0.15), 6000.0, 2200.0)
    # Blue reinforcements wait in a closed hangar; the console opens its door together with the gate.
    HX0, HX1, HY0, HY1 = 29500, 30700, 1000, 1550
    block("Hangar_Back", HX0, HX1, HY1, HY1 + 50, 0, 480, "MI_Alpha_Concrete")
    block("Hangar_End_W", HX0 - 50, HX0, HY0, HY1 + 50, 0, 480, "MI_Alpha_Concrete")
    block("Hangar_End_E", HX1, HX1 + 50, HY0, HY1 + 50, 0, 480, "MI_Alpha_Concrete")
    block("Hangar_Front_W", HX0 - 50, 29800, HY0 - 50, HY0, 0, 480, "MI_Alpha_Blue")
    block("Hangar_Front_E", 30400, HX1 + 50, HY0 - 50, HY0, 0, 480, "MI_Alpha_Blue")
    block("Hangar_Lintel", 29800, 30400, HY0 - 50, HY0, 450, 480, "MI_Alpha_Blue")
    block("Hangar_Roof", HX0 - 50, HX1 + 50, HY0 - 50, HY1 + 50, 480, 530, "MI_Alpha_Blue")
    block("Hangar_Stripe", HX0, HX1, HY0 - 55, HY0 - 50, 380, 400, "MI_Alpha_BlueGlow", "Decor")
    hangar_door = gate("Hangar_Door", (30100, HY0 - 25, 0), 90.0, "MI_Alpha_Blue")
    console.set_editor_property("linked_gates", [main_gate, hangar_door])
    for comp in console.get_components_by_class(unreal.StaticMeshComponent):
        if comp.get_name() == "Base":
            comp.set_material(0, mat("MI_Alpha_Red"))
    for i, x in enumerate([29700, 29950, 30200, 30450]):
        bot(f"Blue_Reinforcement_{i}", A["blue_soldier" if i % 2 else "blue_shooter"], (x, 1280, 0), -90.0, patrol=300.0)
    point_light("Hangar_Light", (30100, 1280, 380), (0.5, 0.7, 1.0), 1600.0, 800.0)

    # --- 12. Command yard ---------------------------------------------------------------------------
    CX0, CX1 = GX + 100, 37000
    pickup("Pickup_RocketLauncher", unreal.MetalPickupType.WEAPON, (GX - 400, -700, 0), 1.0, w["RocketLauncher"])
    pickup("Pickup_Health_Gate", unreal.MetalPickupType.HEALTH, (GX - 400, 700, 0), 75.0)
    pickup("Pickup_Ammo_Gate", unreal.MetalPickupType.AMMO, (GX - 600, 0, 0), 0.7)
    pickup("Pickup_Grenades_Gate", unreal.MetalPickupType.GRENADES, (GX - 600, -300, 0), 3.0)
    for i, (x, y, s) in enumerate([(32300, -600, (240, 200, 150)), (32600, 700, (200, 240, 150)), (33400, 0, (300, 160, 140)),
                                   (34200, -900, (220, 220, 160)), (34400, 800, (220, 220, 160)), (35300, -200, (260, 180, 150))]):
        crate(f"Command_Cover_{i}", x, y, 0, s, "MI_Alpha_Red" if i % 2 else "MI_Alpha_Concrete")
    for i, (x, y) in enumerate([(33000, -1100), (35800, 1100)]):
        pillar(f"Command_Pylon_{i}", x, y, 0, 900, 110, "MI_Alpha_Red")
    # Flanking catwalk along the north wall, reached by lift.
    CW = 600
    block("Catwalk", 32800, 36200, 1050, 1550, CW - 40, CW, "MI_Alpha_Concrete")
    block("Catwalk_Rail", 32800, 36200, 1030, 1050, CW, CW + 90, "MI_Env_Hazard", "Decor")
    for i, x in enumerate(range(33200, 36200, 1000)):
        block(f"Catwalk_Leg_{i}", x - 40, x + 40, 1250, 1330, 0, CW - 40, "MI_Env_Dark")
    lift("Lift_Catwalk", (32550, 1250, 0), CW + 80, (1.0, 0.0, 0.0))
    # Kept well back from the gate (beyond their sight of the fence) so the hack puzzle plays out in peace.
    command = [bot("Red_CommandShooter_1", A["shooter"], (34400, -400, 0), 180.0, ("Command",), 400.0),
               bot("Red_CommandShooter_2", A["shooter"], (34700, 600, 0), 180.0, ("Command",), 400.0),
               bot("Red_CommandSoldier_1", A["soldier"], (35300, -600, 0), 180.0, ("Command",), 350.0),
               bot("Red_CommandSoldier_2", A["soldier"], (35500, 500, 0), 180.0, ("Command",), 350.0),
               bot("Red_CommandCatwalk", A["shooter"], (35800, 1300, CW), 180.0, ("Command",), 0.0)]
    boss = bot("Red_Commander_Brute", A["brute"], (36300, 0, 0), 180.0, ("Boss",), 300.0)
    for i, (x, y) in enumerate([(33500, 1300), (36500, -1300)]):
        banner(f"Command_Banner_{i}", x, y, 0, "Red", 800)
    point_light("Command_Light_A", (33500, 0, 900), (1.0, 0.35, 0.15), 10000.0, 3500.0)
    point_light("Command_Light_B", (36000, 0, 900), (1.0, 0.35, 0.15), 10000.0, 3500.0)

    # --- 13. Extraction -------------------------------------------------------------------------------
    extraction = zone("Zone_Extraction", (37400, 0, 150), (350, 350, 150), rgb=(0.2, 1.0, 0.3))
    box("Extraction_Pad", (37400, 0, 5), (750, 750, 10), "MI_Alpha_BlueGlow", "Mission")
    banner("Extraction_Banner", 37700, 500, 0, "Blue", 700)

    # --- Audio, navigation ----------------------------------------------------------------------------
    ambience("Ambience_Wind", ROOT + "/Audio/SW_AmbienceLoop", 0.5)
    ambience("Ambience_Battle", ROOT + "/Audio/SW_BattleLoop", 0.35)
    nav = spawn(unreal.NavMeshBoundsVolume, ((X0 + X1) / 2, 0, 900), label="NavBounds", folder="Mission")
    nav.set_actor_scale3d(unreal.Vector((X1 - X0 + 400) / 200.0, (2 * Y + 400) / 200.0, 3400 / 200.0))

    # --- Mission script ---------------------------------------------------------------------------------
    OT = unreal.MetalObjectiveType
    mission = spawn(unreal.MetalMission, (-1500, 0, 400), label="Mission_Redline", folder="Mission")
    setp(mission, mission_name="OPERATION REDLINE", player_starting_weapons=[w["Laser"]], player_starting_grenades=0)
    mission.set_editor_property("objectives", [
        objective("Head for the supply trench", OT.REACH_ZONE, target=zone_trench_entry, hint="[W][A][S][D] move     [Mouse] look", dialogue=[
            line("Morning, soldier. Blue Command here. The Reds dug into the valley overnight - today we take it back."),
            line("Shake the rust off first: WASD to move, the mouse to look around. Head east to the supply trench."),
        ]),
        objective("Sprint through the trench", OT.REACH_ZONE, target=zone_trench_end, hint="Hold [Shift] to sprint", dialogue=[
            line("Red mortars have that trench zeroed in. Hold Shift and SPRINT - don't stop for anything!", 5.5),
        ]),
        objective("Get over the barricades", OT.REACH_ZONE, target=zone_barricades, hint="[Space] jump", dialogue=[
            line("Made it! Barricades ahead - Space to jump. Mind the ditch."),
        ]),
        objective("Cross the collapsed bridge", OT.REACH_ZONE, target=zone_ledge, hint="[Space] jump, then [Space] again in mid-air", dialogue=[
            line("Bridge is out and the far side's higher. Jump, then hit Space again at the top - double jump!", 5.5),
        ]),
        objective("Ride the anti-grav lift up the cliff", OT.REACH_ZONE, target=zone_cliff_top, hint="Walk into the glowing lift beam", dialogue=[
            line("Our forward post is up on the cliff. Step into the anti-grav lift and it'll carry you up."),
        ]),
        objective("Help the forward post: scrap the Red squad", OT.ELIMINATE_TAGGED, tag="Squad1",
                  hint="[LMB] shoot   [RMB] aim   [1][2] / wheel swap arm   [R] reload", dialogue=[
            line("Our squad's dug in behind the sandbags. Push up and scrap those Reds. Left click shoots, right click aims."),
            line("And grab that Repeater by the sandbags. 1 and 2, or the mouse wheel, swap your gun arm."),
        ]),
        objective("Flush out the bunker", OT.ELIMINATE_TAGGED, tag="Bunker", hint="Aim and press [G] to lob an EMP grenade - it stuns robots", dialogue=[
            line("Bunker ahead. Those Reds are hunkered down behind the wall - only their heads show. Hard to hit."),
            line("Grab the grenades and lob one on them - aim and press G. EMP stuns every robot in the blast. Then pick them off."),
        ]),
        objective("Sneak through the patrol yard", OT.REACH_ZONE, target=zone_yard_exit,
                  hint="Hold [Ctrl] / tap [C] to sneak     ? = suspicious     ! = spotted", dialogue=[
            line("Patrol yard. Too many to fight - sneak through. Hold Ctrl, or tap C, to crouch down."),
            line("Crouched, you're quiet and hard to see. Stay out of the lights, or crawl through the pipe on your left."),
            line("A question mark means one's getting curious. If one spots you, the whole yard comes running."),
        ]),
        objective("Take the perch and grab the sniper rifle", OT.REACH_ZONE, target=zone_perch, hint="Walk into the lift by the tower", dialogue=[
            line("Good work. Ride the lift up that tower - there's a sniper rifle on the perch."),
        ]),
        objective("Snipe the Red marksmen in the valley", OT.ELIMINATE_TAGGED, tag="Marksmen",
                  hint="[RMB] scope in   [LMB] fire   (the sniper is only accurate scoped)", dialogue=[
            line("Three Red marksmen are covering the valley from their towers. Hold right click to scope in."),
            line("Watch for their red laser sights. Then take the ramp down to the valley floor."),
        ]),
        objective("Hack the gate guard", OT.HIJACK_TAGGED, tag="GateGuard", hint="Aim at the guard behind the fence and press [F]", dialogue=[
            line("Shield gate. The field only lets Red robots through... but your Hack Tether goes right through it."),
            line("Aim at that guard and press F. He's yours for half a minute - when you let go, he's theirs again."),
        ]),
        objective("Open the gate from the console", OT.ACTIVATE_CONSOLE, target=console, hint="[E] use the console     [F] let go", dialogue=[
            line("You're driving a Red now. Walk him to the gate console and press E. Act natural."),
            line("The other Reds won't look twice at one of their own - unless you start shooting."),
        ]),
        objective("Destroy the Red commander", OT.ELIMINATE_TAGGED, tag="Boss", hint="Rockets and EMP grenades work wonders on a Brute", dialogue=[
            line("Gate's open! Blue squad, go go go! Their commander's a Brute - bring it down and the valley's ours."),
            line("There's a rocket launcher by the gate. Use the lift to the catwalk if you need the high ground."),
        ]),
        objective("Reach the extraction pad", OT.REACH_ZONE, target=extraction, hint="", dialogue=[
            line("Commander's down! Get to the extraction pad, soldier."),
        ]),
    ])
    mission.set_editor_property("completion_dialogue", [line("Outstanding work. The valley's ours. Blue Command out.", 6.0)])

    les.save_current_level()
    log(f"level saved: {MAP_PATH}")


# --------------------------------------------------------------------------------------------
# Title screen
# --------------------------------------------------------------------------------------------
def build_title(archetypes):
    make_materials()
    fresh_level(TITLE_PATH, unreal.MetalTitleGameMode)
    environment(sun_pitch=-22.0, sun_yaw=-120.0, sun_rgb=(255, 190, 140), fog=0.01)
    ground("Title_Ground", -3000, 3000, -3000, 3000, 0, "MI_Alpha_Ground")
    # Blue line on the left, Red line on the right, facing each other across a no man's land.
    for i, y in enumerate((-500, 0, 500)):
        sandbags(f"Title_BlueBags_{i}", -700, y - 200, y + 200, 0, 90, "MI_Alpha_Blue")
        sandbags(f"Title_RedBags_{i}", 700, y - 200, y + 200, 0, 90, "MI_Alpha_Red")
    banner("Title_BlueBanner", -1300, -900, 0, "Blue", 700)
    banner("Title_RedBanner", 1400, 900, 0, "Red", 700)
    tent("Title_Tent", -1700, 600, 0, "Blue")
    watchtower("Title_Tower", 1700, -700, 0, 500)
    bot("Title_Player", archetypes["player"], (-900, 0, 0), 0.0, patrol=0.0, passive=True, team=HEROES)
    for i, (x, y) in enumerate([(-1000, -500), (-1000, 500)]):
        bot(f"Title_BlueTrooper_{i}", archetypes["soldier" if i else "shooter"], (x, y, 0), 0.0, patrol=0.0, passive=True, team=HEROES)
    for i, (x, y) in enumerate([(950, -450), (950, 450)]):
        bot(f"Title_RedShooter_{i}", archetypes["shooter"], (x, y, 0), 180.0, patrol=0.0, passive=True)
    bot("Title_RedBrute", archetypes["brute"], (1500, 100, 0), 180.0, patrol=0.0, passive=True)
    point_light("Title_BlueLight", (-900, 0, 450), (0.4, 0.6, 1.0), 4000.0, 1200.0)
    point_light("Title_RedLight", (1000, 0, 450), (1.0, 0.35, 0.2), 4000.0, 1200.0)
    camera = spawn(unreal.CameraActor, (0, -1600, 450), 90.0, "TitleCamera", "Camera")
    camera.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-12.0, yaw=90.0), False)
    camera.set_editor_property("tags", [unreal.Name("TitleCamera")])
    ambience("Title_Ambience", ROOT + "/Audio/SW_AmbienceLoop", 0.6)
    les.save_current_level()
    log(f"level saved: {TITLE_PATH}")
