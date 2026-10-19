"""
Stage 3 of the Metalization pipeline: gameplay data assets + the playable demo level.

Run after ue_import_assets.py (see run_pipeline.ps1). Creates:
    /Game/Metalization/Data/WD_*        weapon definitions (UMetalWeaponData)
    /Game/Metalization/Data/DA_Bot_*    robot archetypes (UMetalBotArchetype), one per imported character
    /Game/Metalization/Maps/MZ_Outpost_Demo  the demo mission
    /Game/Metalization/Maps/MZ_Arena         free-roam prototype arena (ue_setup_arena.py)
    /Game/Metalization/Maps/MZ_FlightTest    Stalker flight sandbox (ue_setup_flight.py)
    /Game/Metalization/Maps/MZ_SpaceBattle   capital ships + MZ_CapitalShip_Interior, DA_Ship_Capital (ue_setup_space.py)

The maps are regenerated from scratch every run - make manual layout changes on a copy.
Set MZ_LEVELS to a comma-separated subset of outpost,arena,flight,space to rebuild only those.
"""
import json
import math
import os
import sys

import unreal

ROOT = "/Game/Metalization"
DATA = ROOT + "/Data"
MAP_PATH = ROOT + "/Maps/MZ_Outpost_Demo"
PIPELINE_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "AssetPipeline")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    print("[MZSetup] " + msg, flush=True)


def pascal(text):
    return "".join(p[:1].upper() + p[1:] for p in text.split("_") if p)


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def data_asset(name, cls):
    asset = load(f"{DATA}/{name}")
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        asset = asset_tools.create_asset(name, DATA, cls, factory)
    return asset


def setp(obj, **props):
    for k, v in props.items():
        obj.set_editor_property(k, v)
    return obj


def color(r, g, b):
    return unreal.LinearColor(r, g, b, 1.0)


# --------------------------------------------------------------------------------------------
# Weapons
# --------------------------------------------------------------------------------------------
def held(weapon_id, holder):
    return load(f"{ROOT}/Weapons/{pascal(weapon_id)}/SK_{pascal(weapon_id)}_On{pascal(holder)}")


def level(meshes, damage, interval, clip=0, reserve=0):
    lv = unreal.MetalWeaponLevel()
    return setp(lv, held_meshes={unreal.Name(k): v for k, v in meshes.items() if v}, damage=damage,
                fire_interval=interval, clip_size=clip, max_reserve_ammo=reserve)


def make_weapons():
    w = {}
    w["Laser"] = setp(data_asset("WD_Laser", unreal.MetalWeaponData),
        display_name="Laser", fire_mode=unreal.MetalFireMode.HITSCAN, automatic=True, infinite_ammo=True,
        spread_degrees=0.6, range=6000.0, muzzle_offset=40.0, tracer_color=color(1.0, 0.2, 0.08),
        prop_mesh=load(f"{ROOT}/Props/SM_LaserL1"),
        levels=[level({"player": held("laser_l1", "player")}, 8.0, 0.15),
                level({"player": held("laser_l2", "player")}, 11.0, 0.14),
                level({"player": held("laser_l3", "player")}, 15.0, 0.12)])
    w["Repeater"] = setp(data_asset("WD_Repeater", unreal.MetalWeaponData),
        display_name="Repeater", fire_mode=unreal.MetalFireMode.PROJECTILE, automatic=True, reload_time=1.3,
        spread_degrees=1.2, projectile_speed=6500.0, muzzle_offset=45.0, tracer_color=color(1.0, 0.85, 0.3),
        prop_mesh=load(f"{ROOT}/Props/SM_RepeaterL1"),
        levels=[level({"player": held("repeater_l1", "player"), "shooter": held("repeater_l1", "shooter")}, 13.0, 0.1, 30, 240),
                level({"player": held("repeater_l3", "player")}, 19.0, 0.085, 40, 320)])
    w["Shotgun"] = setp(data_asset("WD_Shotgun", unreal.MetalWeaponData),
        display_name="Shotgun", fire_mode=unreal.MetalFireMode.HITSCAN, automatic=False, reload_time=1.6,
        pellets=8, spread_degrees=6.0, range=2500.0, muzzle_offset=50.0, tracer_color=color(1.0, 0.55, 0.15),
        prop_mesh=load(f"{ROOT}/Props/SM_ShotgunL1"),
        levels=[level({"player": held("shotgun_l1", "player"), "shooter": held("shotgun_l1", "shooter")}, 9.0, 0.7, 6, 36),
                level({"player": held("shotgun_l3", "player")}, 12.0, 0.55, 8, 48)])
    w["RocketLauncher"] = setp(data_asset("WD_RocketLauncher", unreal.MetalWeaponData),
        display_name="Rocket Launcher", fire_mode=unreal.MetalFireMode.PROJECTILE, automatic=False, reload_time=2.0,
        spread_degrees=0.3, projectile_speed=2800.0, explosion_radius=380.0, muzzle_offset=60.0,
        tracer_color=color(1.0, 0.35, 0.05), prop_mesh=load(f"{ROOT}/Props/SM_RocketlauncherL1"),
        levels=[level({"player": held("rocketlauncher_l1", "player")}, 120.0, 0.9, 3, 15),
                level({"player": held("rocketlauncher_l3", "player")}, 160.0, 0.75, 4, 20)])
    # Enemy-only weapons.
    w["EnemyLaser"] = setp(data_asset("WD_EnemyLaser", unreal.MetalWeaponData),
        display_name="Enemy Laser", fire_mode=unreal.MetalFireMode.HITSCAN, automatic=True, infinite_ammo=True,
        spread_degrees=1.0, range=5000.0, muzzle_offset=40.0, tracer_color=color(1.0, 0.1, 0.25),
        levels=[level({"shooter": held("laser_enemy", "shooter")}, 3.0, 0.22)])
    w["ArmBlasters"] = setp(data_asset("WD_ArmBlasters", unreal.MetalWeaponData),
        display_name="Arm Blasters", fire_mode=unreal.MetalFireMode.PROJECTILE, automatic=True, infinite_ammo=True,
        spread_degrees=1.5, projectile_speed=3200.0, muzzle_offset=0.0, tracer_color=color(0.8, 0.2, 1.0),
        levels=[level({}, 7.0, 0.2)])
    w["TwinMiniguns"] = setp(data_asset("WD_TwinMiniguns", unreal.MetalWeaponData),
        display_name="Twin Miniguns", fire_mode=unreal.MetalFireMode.HITSCAN, automatic=True, infinite_ammo=True,
        spread_degrees=3.5, range=6000.0, muzzle_offset=0.0, tracer_color=color(1.0, 0.9, 0.2),
        levels=[level({}, 3.5, 0.08)])
    # The Stalker's four arm guns fire in turn (MuzzleBones); slugs leave from the gun tips.
    w["StalkerCannons"] = setp(data_asset("WD_StalkerCannons", unreal.MetalWeaponData),
        display_name="Arm Cannons", fire_mode=unreal.MetalFireMode.PROJECTILE, automatic=True, infinite_ammo=True,
        spread_degrees=0.4, projectile_speed=30000.0, muzzle_offset=290.0, projectile_scale=4.0,
        tracer_color=color(0.35, 1.0, 0.45), levels=[level({}, 12.0, 0.08)])
    w["Claw"] = setp(data_asset("WD_Claw", unreal.MetalWeaponData),
        display_name="Bare Servos", fire_mode=unreal.MetalFireMode.MELEE, automatic=True, infinite_ammo=True,
        range=130.0, tracer_color=color(1.0, 0.6, 0.2), levels=[level({}, 14.0, 0.8)])
    w["EMPGrenade"] = setp(data_asset("WD_EMPGrenade", unreal.MetalWeaponData),
        display_name="EMP Grenade", fire_mode=unreal.MetalFireMode.THROWN, projectile_speed=1500.0,
        projectile_gravity_scale=1.0, explosion_radius=420.0, stun_duration=4.0, tracer_color=color(0.3, 0.6, 1.0),
        prop_mesh=load(f"{ROOT}/Props/SM_GrenadeEmp"), levels=[level({}, 25.0, 1.0)])
    for a in w.values():
        eal.save_loaded_asset(a)
    log(f"weapons: {len(w)}")
    return w


# --------------------------------------------------------------------------------------------
# Robot archetypes
# --------------------------------------------------------------------------------------------
def rig(**overrides):
    r = unreal.MetalBotRig()
    for k, v in overrides.items():
        r.set_editor_property(k, v)
    return r


def zone(name, bone, radius, mult, durability=0.0, disables=False, muzzle="None"):
    return setp(unreal.MetalHitZone(), zone_name=name, bone=bone, radius=radius, damage_multiplier=mult,
                durability=durability, disables_weapon_when_broken=disables, disables_muzzle_bone=muzzle)


def team_colors(body, accent):
    return setp(unreal.MetalTeamColors(), body=color(*body), accent=color(*accent))


def make_archetypes(manifest, w):
    def mesh(cid, part=None):
        name = pascal(cid)
        return load(f"{ROOT}/Characters/{name}/SK_{name}" + (f"_{pascal(part)}" if part else ""))

    archetypes = {}
    # Every imported character gets a generic archetype (used by the hangar showcase)...
    for cid, ch in manifest["characters"].items():
        height = (ch["bounds_max_m"][2] - ch["bounds_min_m"][2]) * 100.0
        parts = [mesh(cid, p) for p in ch["parts"] if p not in ("hand", "hand_1", "arm_2")]
        a = setp(data_asset("DA_Bot_" + pascal(cid), unreal.MetalBotArchetype),
                 archetype_id=cid, display_name=ch["display_name"], mesh=mesh(cid), parts=[p for p in parts if p],
                 team=unreal.MetalTeam.HEROES if ch["faction"] == "Heroes" else unreal.MetalTeam.VILLAINS,
                 capsule_radius=34.0, capsule_half_height=max(30.0, height * 0.5), hackable=False,
                 max_health=100.0, walk_speed=350.0)
        archetypes[cid] = a

    # ...and the gameplay robots are tuned by hand.
    # A long stride keeps the run at ~3 cycles/s; short strides made the legs flicker at full speed.
    setp(archetypes["player"], display_name="Player", unarmed_part=mesh("player", "hand"),
         capsule_radius=30.0, capsule_half_height=66.0, max_health=150.0, walk_speed=600.0, jump_z_velocity=700.0, max_jumps=2,
         rig=rig(stride_length=200.0, leg_swing_degrees=40.0, knee_bend_degrees=50.0, bob_height=4.0, arm_swing_degrees=20.0),
         hit_zones=[zone("Head", "bip_head", 18.0, 1.5)],
         starting_weapons=[w["Laser"], w["Repeater"]], melee_weapon=w["Claw"],
         grenade_weapon=w["EMPGrenade"], starting_grenades=3,
         camera_distance=260.0, camera_offset=unreal.Vector(0.0, 50.0, 60.0))

    setp(archetypes["shooter"], display_name="Shooter", unarmed_part=mesh("shooter", "arm_2"), hackable=True,
         capsule_radius=34.0, capsule_half_height=65.0, max_health=60.0, walk_speed=400.0, possession_duration=25.0,
         rig=rig(thigh_l="bip_hip_L", thigh_r="bip_hip_R", knee_l="ankle_1_L", knee_r="ankle_1_R",
                 upper_arm_r_ready=unreal.Rotator(roll=0.0, pitch=-10.0, yaw=-80.0),
                 upper_arm_l_ready=unreal.Rotator(roll=-75.0, pitch=0.0, yaw=0.0),
                 stride_length=120.0, leg_swing_degrees=26.0, knee_bend_degrees=-30.0, bob_height=3.0,
                 arm_swing_degrees=0.0, aim_from_shoulder=True),
         hit_zones=[zone("Head", "bip_head", 20.0, 2.0),
                    zone("GunArm", "bip_lowerArm_R", 24.0, 1.0, durability=30.0, disables=True)],
         starting_weapons=[w["EnemyLaser"]], melee_weapon=w["Claw"],
         sight_range=3200.0, preferred_range=850.0, aim_error_degrees=4.5, burst_duration=1.1, burst_pause=1.1,
         retreat_health_fraction=0.3, camera_distance=320.0, camera_offset=unreal.Vector(0.0, 60.0, 70.0))

    setp(archetypes["soldier"], display_name="Soldier", hackable=True,
         capsule_radius=40.0, capsule_half_height=68.0, max_health=130.0, walk_speed=320.0, possession_duration=20.0,
         rig=rig(upper_arm_r_ready=unreal.Rotator(roll=50.0, pitch=0.0, yaw=0.0),
                 upper_arm_l_ready=unreal.Rotator(roll=-50.0, pitch=0.0, yaw=0.0),
                 stride_length=140.0, leg_swing_degrees=22.0, knee_bend_degrees=30.0, bob_height=4.0,
                 aim_with_both_arms=True),
         hit_zones=[zone("Head", "bip_head", 22.0, 2.0)],
         starting_weapons=[w["ArmBlasters"]], melee_weapon=w["Claw"],
         muzzle_bones=["bip_lowerArm_R", "bip_lowerArm_L"],
         sight_range=3500.0, preferred_range=1100.0, aim_error_degrees=3.5, burst_duration=1.5, burst_pause=1.4,
         camera_distance=380.0, camera_offset=unreal.Vector(0.0, 70.0, 80.0))

    setp(archetypes["brute"], display_name="Brute", hackable=True,
         capsule_radius=75.0, capsule_half_height=127.0, max_health=700.0, walk_speed=260.0, possession_duration=30.0,
         rig=rig(thigh_l="hip_L", thigh_r="hip_R", stride_length=230.0, leg_swing_degrees=20.0,
                 knee_bend_degrees=28.0, bob_height=6.0, aim_with_both_arms=True),
         hit_zones=[zone("Head", "bip_head", 35.0, 1.6)],
         starting_weapons=[w["TwinMiniguns"]], melee_weapon=w["Claw"], muzzle_bones=["minigun_R", "minigun_L"],
         sight_range=4500.0, preferred_range=1400.0, aim_error_degrees=5.5, burst_duration=1.6, burst_pause=2.0,
         squad_alert_radius=3000.0, camera_distance=620.0, camera_offset=unreal.Vector(0.0, 120.0, 160.0))

    # Flying gunship. The capsule wraps only the 5 m body (centred on it with mesh_offset: the body box sits
    # 0.86 m behind and 1.13 m above the model origin); the arms take hits through the physics asset and
    # each one carries a gun that can be shot off. Friendly Stalkers can be piloted without a time limit.
    sides = [("UpperL", "arm_upper_0_L", "gun_upper_L"), ("UpperR", "arm_upper_0_R", "gun_upper_R"),
             ("LowerL", "arm_lower_0_L", "gun_lower_L"), ("LowerR", "arm_lower_0_R", "gun_lower_R")]
    flight = setp(unreal.MetalFlightTuning(), max_speed=4500.0, acceleration=6000.0, braking=4000.0, grip=3.0, turn_rate=140.0,
                  max_bank=35.0, boost_speed=10000.0, boost_acceleration=14000.0, boost_duration=3.0,
                  boost_recharge_time=5.0, dodge_speed=4500.0, dodge_cost=0.25)
    setp(archetypes["stalker"], display_name="Stalker", hackable=True, ally_hackable=True, possession_duration=20.0,
         capsule_radius=140.0, capsule_half_height=170.0, mesh_offset=unreal.Vector(86.0, 0.0, 57.0),
         max_health=400.0, walk_speed=600.0, flight=flight,
         team_colors={unreal.MetalTeam.HEROES: team_colors((0.1575, 0.1883, 0.45), (0.4875, 0.5152, 0.75)),
                      unreal.MetalTeam.VILLAINS: team_colors((0.45, 0.2223, 0.1575), (0.75, 0.5456, 0.4875))},
         rig=rig(pelvis="body", head="None", aim_pitch_weight=0.0, arm_swing_degrees=0.0,
                 flyer_arm_upper_l="arm_upper_0_L", flyer_arm_upper_r="arm_upper_0_R",
                 flyer_arm_lower_l="arm_lower_0_L", flyer_arm_lower_r="arm_lower_0_R",
                 x_spread_degrees=28.0, boost_sweep_degrees=40.0, parked_fold_degrees=55.0),
         hit_zones=[zone("Gun" + name, bone, 60.0, 1.0, durability=90.0, muzzle=gun) for name, bone, gun in sides],
         starting_weapons=[w["StalkerCannons"]], muzzle_bones=["gun_upper_L", "gun_lower_R", "gun_upper_R", "gun_lower_L"],
         sight_range=8000.0, preferred_range=2500.0, aim_error_degrees=3.0,
         camera_distance=1500.0, camera_offset=unreal.Vector(0.0, 0.0, 330.0))

    setp(archetypes["guide"], display_name="Guide", capsule_half_height=48.0)

    for a in archetypes.values():
        eal.save_loaded_asset(a)
    log(f"archetypes: {len(archetypes)}")
    return archetypes


# --------------------------------------------------------------------------------------------
# Level
# --------------------------------------------------------------------------------------------
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder")
MATS = {}


def mat(name):
    if name not in MATS:
        MATS[name] = load(f"{ROOT}/Materials/{name}")
    return MATS[name]


def spawn(cls, loc, yaw=0.0, label=None, folder=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def box(label, center, size, material="MI_Env_Floor", folder="Geometry", mesh=None, yaw=0.0, collision=None):
    """Static mesh cube of the given size (cm), centred at center."""
    actor = spawn(unreal.StaticMeshActor, center, yaw, label, folder)
    smc = actor.static_mesh_component
    smc.set_static_mesh(mesh or CUBE)
    smc.set_material(0, mat(material))
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    if collision:
        smc.set_collision_profile_name(collision)
    return actor


def floor(label, x0, x1, y0, y1, top=0.0, material="MI_Env_Floor", folder="Geometry"):
    return box(label, ((x0 + x1) / 2, (y0 + y1) / 2, top - 25), (x1 - x0, y1 - y0, 50), material, folder)


def wall(label, x0, x1, y0, y1, height, material="MI_Env_Wall", base=0.0, folder="Geometry"):
    return box(label, ((x0 + x1) / 2, (y0 + y1) / 2, base + height / 2), (x1 - x0, y1 - y0, height), material, folder)


def crate(label, x, y, size=(150, 150, 120), base=0.0, material="MI_Env_Wall", folder="Cover"):
    return box(label, (x, y, base + size[2] / 2), size, material, folder)


def pillar(label, x, y, height=500, radius=60, material="MI_Env_Dark"):
    return box(label, (x, y, height / 2), (radius * 2, radius * 2, height), material, "Geometry", mesh=CYLINDER)


def light(label, loc, rgb, intensity=8000.0, radius=1600.0):
    actor = spawn(unreal.PointLight, loc, label=label, folder="Lighting")
    comp = actor.point_light_component
    comp.set_editor_property("intensity", intensity)
    comp.set_editor_property("light_color", unreal.Color(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255), 255))
    comp.set_editor_property("attenuation_radius", radius)
    comp.set_editor_property("cast_shadows", False)
    return actor


def bot(label, archetype, loc, yaw, tags=(), patrol=600.0, passive=False, team=None, folder="Robots"):
    half = archetype.get_editor_property("capsule_half_height")
    actor = spawn(unreal.MetalBotCharacter, (loc[0], loc[1], loc[2] + half + 5.0), yaw, label, folder)
    actor.set_editor_property("archetype", archetype)
    actor.set_editor_property("patrol_radius", patrol)
    actor.set_editor_property("passive", passive)
    if team is not None:
        actor.set_editor_property("override_team", True)
        actor.set_editor_property("team_override", team)
    actor.set_editor_property("tags", [unreal.Name(t) for t in tags])
    return actor


def pickup(label, ptype, loc, amount, weapon=None):
    actor = spawn(unreal.MetalPickup, (loc[0], loc[1], loc[2] + 70.0), label=label, folder="Pickups")
    actor.set_editor_property("type", ptype)
    actor.set_editor_property("amount", amount)
    if weapon:
        actor.set_editor_property("weapon", weapon)
    return actor


def zone_actor(label, center, extent, checkpoint=False, rgb=(0.1, 0.8, 1.0)):
    actor = spawn(unreal.MetalTriggerZone, center, label=label, folder="Mission")
    actor.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector(*extent))
    actor.set_editor_property("is_checkpoint", checkpoint)
    actor.set_editor_property("marker_color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return actor


def line(speaker, text, duration=4.5):
    return setp(unreal.MetalDialogueLine(), speaker=speaker, line=text, duration=duration)


def objective(text, otype, tag=None, target=None, dialogue=()):
    o = setp(unreal.MetalObjective(), description=text, type=otype, start_dialogue=list(dialogue))
    if tag:
        o.set_editor_property("target_tag", unreal.Name(tag))
    if target:
        o.set_editor_property("target_actor", target)
    return o


def build_level(archetypes, w, manifest):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if eal.does_asset_exist(MAP_PATH):
        # Rebuild in place so anything referencing the map keeps working.
        les.load_level(MAP_PATH)
        for actor in actors.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)):
                actors.destroy_actor(actor)
    else:
        les.new_level(MAP_PATH, False)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.MetalGameMode.static_class())

    X0, X1, Y = -9500, 15500, 1500
    HANGAR_X0 = -3000

    # --- Sky, sun and haze: late-afternoon industrial smog ---------------------------------
    sun = spawn(unreal.DirectionalLight, (0, 0, 2000), label="Sun", folder="Lighting")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-55.0, yaw=-135.0), False)
    sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_comp.set_editor_property("intensity", 28.0)
    sun_comp.set_editor_property("light_color", unreal.Color(255, 200, 150, 255))
    sun_comp.set_editor_property("atmosphere_sun_light", True)
    atmosphere = spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere", folder="Lighting")
    # Slightly desaturated, dusty sky for the outpost's industrial smog.
    atmosphere.get_component_by_class(unreal.SkyAtmosphereComponent).set_editor_property(
        "rayleigh_scattering", unreal.LinearColor(0.0095, 0.0125, 0.0215, 1.0))
    sky = spawn(unreal.SkyLight, (0, 0, 500), label="SkyLight", folder="Lighting")
    sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_comp.set_editor_property("real_time_capture", True)
    sky_comp.set_editor_property("intensity", 0.3)
    sky_comp.set_editor_property("light_color", unreal.Color(255, 205, 160, 255))
    sky_comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, 0), label="Smog", folder="Lighting")
    fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog_comp.set_editor_property("fog_density", 0.012)
    fog_comp.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.45, 0.28, 0.14, 1.0))
    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), label="PostProcess", folder="Lighting")
    ppv.set_editor_property("unbound", True)
    settings = ppv.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", 0.2)
    settings.set_editor_property("override_vignette_intensity", True)
    settings.set_editor_property("vignette_intensity", 0.5)
    ppv.set_editor_property("settings", settings)

    # --- Shell: floor slabs per area and perimeter walls -----------------------------------
    floor("Floor_Boneyard", X0, HANGAR_X0, -Y, Y, material="MI_Env_Dark")
    floor("Floor_Hangar", HANGAR_X0, 1500, -Y, Y)
    floor("Floor_Courtyard", 1500, 5500, -Y, Y, material="MI_Env_Dark")
    floor("Floor_ScrapHeap", 5500, 7500, -Y, Y)
    floor("Floor_GateYard", 7500, 10500, -Y, Y, material="MI_Env_Dark")
    floor("Floor_Arena", 10500, X1, -Y, Y)
    wall("Wall_North", X0, X1, Y, Y + 100, 700)
    wall("Wall_South", X0, X1, -Y - 100, -Y, 700)
    wall("Wall_West", X0 - 100, X0, -Y - 100, Y + 100, 1200)
    wall("Wall_East", X1, X1 + 100, -Y - 100, Y + 100, 700)
    for x in range(X0 + 500, X1, 1000):
        for y in (Y - 10, -Y + 10):
            box(f"GlowStrip_{x}_{y}", (x, y, 320), (600, 20, 20), "MI_Env_Glow", "Decor")
            pillar(f"Buttress_{x}_{y}", x + 500, y, 700, 70)

    # --- A: Docking hangar + robot showcase --------------------------------------------------
    # Every imported robot stands on display (neutral, passive). Giants get their own boneyard hall.
    start = spawn(unreal.PlayerStart, (-300, 0, 120), 0.0, "PlayerStart", "Mission")

    def size(cid):
        ch = manifest["characters"][cid]
        width = (ch["bounds_max_m"][0] - ch["bounds_min_m"][0]) * 100.0
        depth = (ch["bounds_max_m"][1] - ch["bounds_min_m"][1]) * 100.0
        height = (ch["bounds_max_m"][2] - ch["bounds_min_m"][2]) * 100.0
        return width, depth, height

    def showcase(cid, loc, yaw):
        bot(f"Showcase_{pascal(cid)}", archetypes[cid], loc, yaw, patrol=0.0, passive=True,
            team=unreal.MetalTeam.NEUTRAL, folder="Robots/Showcase")

    shown = [c for c in sorted(manifest["characters"]) if c not in ("player", "guide")]
    giants = [c for c in shown if size(c)[2] > 300 or size(c)[0] > 400]
    for side, (faction, y, yaw) in {"S": ("Heroes", -1150, 90.0), "N": ("Villains", 1150, -90.0)}.items():
        x = HANGAR_X0 + 200
        for cid in [c for c in shown if c not in giants and manifest["characters"][c]["faction"] == faction]:
            width = max(size(cid)[0], 80.0)
            showcase(cid, (x + width / 2, y, 0), yaw)
            x += width + 60
        box(f"Showcase_Plinth_{side}", ((HANGAR_X0 + x) / 2, y, 5), (x - HANGAR_X0, 300, 10), "MI_Env_Hazard", "Decor")
    x = HANGAR_X0 - 700
    for cid in giants:
        depth = size(cid)[1]
        x -= depth / 2
        showcase(cid, (x, 0, 0), 0.0)
        x -= depth / 2 + 500
    wall("Boneyard_Divider_N", HANGAR_X0 - 50, HANGAR_X0 + 50, 600, Y, 700)
    wall("Boneyard_Divider_S", HANGAR_X0 - 50, HANGAR_X0 + 50, -Y, -600, 700)
    light("BoneyardLight_A", (HANGAR_X0 - 2500, 0, 1400), (0.6, 0.7, 1.0), 30000.0, 4000.0)
    light("BoneyardLight_B", (X0 + 1500, 0, 1400), (1.0, 0.6, 0.3), 30000.0, 4000.0)
    guide = bot("Guide", archetypes["guide"], (300, -500, 0), 150.0, patrol=0.0, passive=True, team=unreal.MetalTeam.NEUTRAL)
    for i, (x, y) in enumerate([(700, 400), (900, 600), (-900, 300)]):
        crate(f"HangarCrate_{i}", x, y)
    pickup("Pickup_Health_Hangar", unreal.MetalPickupType.HEALTH, (1000, -300, 0), 50.0)
    light("HangarLight_A", (-1500, 0, 550), (1.0, 0.75, 0.45))
    light("HangarLight_B", (0, 0, 550), (1.0, 0.75, 0.45))

    # --- B: Courtyard firefight ---------------------------------------------------------------
    wall("Courtyard_Gateway_N", 1450, 1550, 700, Y, 500)
    wall("Courtyard_Gateway_S", 1450, 1550, -Y, -700, 500)
    for i, (x, y) in enumerate([(2400, -500), (2700, 450), (3200, -100), (3600, 800), (3900, -800), (4400, 200), (4900, -400), (2100, 1000)]):
        crate(f"CourtyardCover_{i}", x, y, (160, 160, 130) if i % 2 else (220, 120, 110))
    for i, (x, y) in enumerate([(3000, 1100), (4200, -1100)]):
        pillar(f"CourtyardPillar_{i}", x, y, 450, 80)
    floor("Catwalk", 3300, 5200, 900, 1450, top=250, material="MI_Env_Dark", folder="Geometry")
    box("Catwalk_Ramp", (2950, 1175, 125), (800, 550, 30), "MI_Env_Dark", "Geometry").set_actor_rotation(unreal.Rotator(roll=0.0, pitch=17.0, yaw=0.0), False)
    box("Catwalk_Edge", (4250, 900, 290), (1900, 20, 30), "MI_Env_Hazard", "Decor")
    squad1 = [bot("Shooter_Courtyard_1", archetypes["shooter"], (3500, -400, 0), 180.0, ("Squad1",), 500.0),
              bot("Shooter_Courtyard_2", archetypes["shooter"], (3900, 350, 0), 180.0, ("Squad1",), 500.0),
              bot("Shooter_Courtyard_3", archetypes["shooter"], (4600, -150, 0), 180.0, ("Squad1",), 500.0),
              bot("Soldier_Courtyard", archetypes["soldier"], (4600, 1150, 250), 180.0, ("Squad1",), 300.0)]
    pickup("Pickup_Ammo_Courtyard", unreal.MetalPickupType.AMMO, (2200, 1250, 0), 0.5)
    pickup("Pickup_Health_Courtyard", unreal.MetalPickupType.HEALTH, (5000, -1250, 0), 60.0)
    pickup("Pickup_Grenades_Catwalk", unreal.MetalPickupType.GRENADES, (5000, 1200, 250), 2.0)
    light("CourtyardLight", (3500, 0, 700), (1.0, 0.55, 0.25), 15000.0, 2500.0)
    zone_actor("Checkpoint_ScrapHeap", (5350, 0, 150), (150, 1400, 150), checkpoint=True)

    # --- C: Scrap heap traversal ---------------------------------------------------------------
    wall("ScrapWall", 6900, 7500, -Y, Y, 300, "MI_Env_Wall")
    box("ScrapWall_Lip", (6900, 0, 305), (20, 2 * Y, 10), "MI_Env_Hazard", "Decor")
    for i, (x, y, h) in enumerate([(6000, -700, 100), (6300, -350, 200), (6600, 0, 290)]):
        crate(f"ScrapStep_{i}", x, y, (220, 220, h), material="MI_Env_Hazard" if i == 2 else "MI_Env_Wall", folder="Traversal")
    crate("ScrapStep_Secret_0", 5900, 900, (200, 200, 120), folder="Traversal")
    box("ScrapPillar_Secret", (6250, 1150, 125), (240, 240, 250), "MI_Env_Wall", "Traversal")
    pickup("Pickup_Shotgun", unreal.MetalPickupType.WEAPON, (6250, 1150, 250), 1.0, w["Shotgun"])
    pickup("Pickup_LaserUpgrade", unreal.MetalPickupType.WEAPON, (7200, 0, 300), 1.0, w["Laser"])
    top_zone = zone_actor("Zone_ScrapWallTop", (7200, 0, 450), (300, 1400, 150), rgb=(1.0, 0.6, 0.1))
    light("ScrapLight", (6500, 0, 650), (0.5, 0.7, 1.0), 9000.0, 2000.0)

    # --- D: Gate yard - possession puzzle ---------------------------------------------------
    for i, (x, y) in enumerate([(8200, -600), (8500, 700), (8900, -100)]):
        crate(f"GateYardCover_{i}", x, y)
    squad2 = [bot("Shooter_GateYard_1", archetypes["shooter"], (8700, -500, 0), 180.0, ("Squad2",), 400.0),
              bot("Shooter_GateYard_2", archetypes["shooter"], (8800, 600, 0), 180.0, ("Squad2",), 400.0)]
    gx = 9500
    wall("GateWall_N", gx - 50, gx + 50, 1000, Y, 500)
    wall("GateWall_S", gx - 50, gx + 50, -Y, -1000, 500)
    for side, (y0, y1) in (("N", (300, 1000)), ("S", (-1000, -300))):
        # Kept low so the tether's sight line clears it to the short Shooter guards behind.
        wall(f"FenceBase_{side}", gx - 50, gx + 50, y0, y1, 40, "MI_Env_Dark")
        # Energy fence: blocks robots but not sight lines, lasers or the Hack Tether.
        box(f"EnergyFence_{side}", (gx, (y0 + y1) / 2, 40 + 230), (20, y1 - y0, 460), "MI_Env_Glow", "Gate", collision="InvisibleWall")
        box(f"EnergyFenceBlock_{side}", (gx, (y0 + y1) / 2, 40 + 230), (100, y1 - y0, 460), "MI_Env_Glow", "Gate", collision="InvisibleWall").set_actor_hidden_in_game(True)
    gate = spawn(unreal.MetalGate, (gx, 0, 0), 0.0, "MainGate", "Gate")
    gate.get_component_by_class(unreal.StaticMeshComponent).set_material(0, mat("MI_Env_Hazard"))
    box("GateLintel", (gx, 0, 525), (120, 640, 50), "MI_Env_Dark", "Gate")
    console = spawn(unreal.MetalControlConsole, (9850, -1150, 0), 90.0, "GateConsole", "Gate")
    console.set_editor_property("linked_gates", [gate])
    for comp in console.get_components_by_class(unreal.StaticMeshComponent):
        if comp.get_name() == "Base":
            comp.set_material(0, mat("MI_Env_Dark"))
    guards = [bot("Shooter_GateGuard_1", archetypes["shooter"], (10050, -700, 0), 180.0, ("GateGuard",), 150.0),
              bot("Shooter_GateGuard_2", archetypes["shooter"], (10150, 650, 0), 180.0, ("GateGuard",), 150.0)]
    light("GateLight", (9300, 0, 600), (1.0, 0.3, 0.1), 12000.0, 1800.0)

    # --- E: Boss arena ----------------------------------------------------------------------
    zone_actor("Checkpoint_Arena", (10750, 0, 150), (150, 1400, 150), checkpoint=True)
    pickup("Pickup_RocketLauncher", unreal.MetalPickupType.WEAPON, (11000, 1150, 0), 1.0, w["RocketLauncher"])
    pickup("Pickup_Health_Arena", unreal.MetalPickupType.HEALTH, (11000, -1150, 0), 75.0)
    pickup("Pickup_Ammo_Arena", unreal.MetalPickupType.AMMO, (12800, 1250, 0), 0.6)
    pickup("Pickup_Grenades_Arena", unreal.MetalPickupType.GRENADES, (12800, -1250, 0), 2.0)
    for i, (x, y) in enumerate([(11700, -500), (11700, 500), (12600, 0), (13300, -700), (13300, 700)]):
        pillar(f"ArenaPillar_{i}", x, y, 600, 90)
    for i, (x, y) in enumerate([(12100, -1000), (12100, 1000), (13700, 0)]):
        crate(f"ArenaCover_{i}", x, y, (200, 200, 140))
    brute = bot("Brute_Boss", archetypes["brute"], (13300, 0, 0), 180.0, ("Boss",), 400.0)
    arena = [bot("Soldier_Arena_1", archetypes["soldier"], (12500, -900, 0), 180.0, ("Arena",), 400.0),
             bot("Soldier_Arena_2", archetypes["soldier"], (12500, 900, 0), 180.0, ("Arena",), 400.0)]
    light("ArenaLight_A", (12000, 0, 800), (1.0, 0.4, 0.15), 20000.0, 3000.0)
    light("ArenaLight_B", (14000, 0, 800), (0.3, 0.6, 1.0), 15000.0, 3000.0)

    # --- F: Extraction ----------------------------------------------------------------------
    extraction = zone_actor("Zone_Extraction", (15000, 0, 150), (350, 350, 150), rgb=(0.2, 1.0, 0.3))
    box("ExtractionPad", (15000, 0, 5), (750, 750, 10), "MI_Env_Glow", "Mission")

    # --- Navigation ---------------------------------------------------------------------------
    nav = spawn(unreal.NavMeshBoundsVolume, ((X0 + X1) / 2, 0, 300), label="NavBounds", folder="Mission")
    nav.set_actor_scale3d(unreal.Vector((X1 - X0 + 400) / 200.0, (2 * Y + 400) / 200.0, 1200 / 200.0))

    # --- Mission script ------------------------------------------------------------------------
    K = "Guide"
    mission = spawn(unreal.MetalMission, (-300, 0, 300), label="Mission_OutpostDemo", folder="Mission")
    mission.set_editor_property("mission_name", "OUTPOST - THE GATE JOB")
    mission.set_editor_property("objectives", [
        objective("Scrap the enemy patrol in the courtyard", unreal.MetalObjectiveType.ELIMINATE_TAGGED, tag="Squad1", dialogue=[
            line(K, "Hey, you're awake! Enemy shooters are all over the courtyard. Go scrap 'em - I'll guard this extremely important crate."),
            line(K, "WASD to move, mouse to aim, left click to shoot. 1-2 or the wheel swaps arms. G throws an EMP grenade."),
            line(K, "And don't mind the hangar behind you. Those bots are on display. Like art. Expensive art."),
        ]),
        objective("Climb over the scrap wall", unreal.MetalObjectiveType.REACH_ZONE, target=top_zone, dialogue=[
            line(K, "Nice shooting! Now get over that scrap heap. Robots don't get vertigo. Mostly."),
            line(K, "Word is somebody stashed a Shotgun up on the pillar to the left. Finders keepers."),
        ]),
        objective("Hijack a gate guard with the Hack Tether [F]", unreal.MetalObjectiveType.HIJACK_TAGGED, tag="GateGuard", dialogue=[
            line(K, "Energy fence. You can't walk through it... but your Hack Tether can. Aim at a guard behind it and press F."),
        ]),
        objective("Use the guard to open the gate console [E]", unreal.MetalObjectiveType.ACTIVATE_CONSOLE, target=console, dialogue=[
            line(K, "Ha! You're wearing an enemy shooter! Walk him over to the console and press E. Try to look... evil-ish."),
            line(K, "When you let go with F, that poor guy blows up. So maybe let go near his friends."),
        ]),
        objective("Destroy the Brute (or borrow it)", unreal.MetalObjectiveType.ELIMINATE_TAGGED, tag="Boss", dialogue=[
            line(K, "Gate's open! Uh. Oh. That's a Brute. Big guns, bigger attitude. Grab the rocket launcher..."),
            line(K, "...or tether the big lug and give his buddies a taste of their own minigun."),
        ]),
        objective("Reach the extraction pad", unreal.MetalObjectiveType.REACH_ZONE, target=extraction, dialogue=[
            line(K, "That's how we do it at the outpost! Get to the extraction pad before the Warlord notices his gate is missing."),
        ]),
    ])
    mission.set_editor_property("completion_dialogue", [
        line(K, "You magnificent pile of bolts. Drinks are on me. Oil. I mean oil.", 6.0),
    ])

    les.save_current_level()
    log(f"level saved: {MAP_PATH}")


def restore_template_game_mode():
    """The previous attempt pointed the template's game mode at a deleted class; put the template pawn back."""
    bp = load("/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode")
    pawn = unreal.load_class(None, "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C")
    if bp and pawn:
        cdo = unreal.get_default_object(bp.generated_class())
        cdo.set_editor_property("default_pawn_class", pawn)
        eal.save_loaded_asset(bp)
        log("restored BP_ThirdPersonGameMode default pawn")


def main():
    with open(os.path.join(PIPELINE_DIR, "manifest.json")) as f:
        manifest = json.load(f)
    weapons = make_weapons()
    archetypes = make_archetypes(manifest, weapons)
    restore_template_game_mode()
    levels = os.environ.get("MZ_LEVELS", "outpost,arena,flight,space").split(",")
    sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Tools", "AssetPipeline"))
    if "outpost" in levels:
        build_level(archetypes, weapons, manifest)
    if "arena" in levels:
        import ue_setup_arena
        ue_setup_arena.build_arena(archetypes)
    if "flight" in levels:
        import ue_setup_flight
        ue_setup_flight.build_flight_test(archetypes)
    if "space" in levels:
        import ue_setup_space
        ue_setup_space.build_space_maps(archetypes)
    log("DONE")


main()
