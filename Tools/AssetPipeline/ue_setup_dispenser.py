"""
Metalization pipeline: the robot dispenser test map (/Game/Metalization/Maps/MZ_DispenserTest).

A walled yard with two robot dispensers (AMetalRobotDispenser) facing the player start:

    south (left)   Respawn mode: a Shooter and a Soldier are assigned to it. Destroy one and, after a
                   moment, a copy is welded together on the pad (about 2 s) and walks back to its post.
    north (right)  Continuous mode: idle until the test gate opens, then builds a Shooter (2/3) or a
                   Soldier (1/3) every 5 s while fewer than 3 of its robots stand.

The gate console by the start opens the gate (and so starts the north dispenser); the blue console at the
back shuts both dispensers down. Anyone, the player included, can use either console.

Called from ue_setup_gameplay.py (MZ_LEVELS=dispenser rebuilds only this map). Uses the level helpers in
ue_setup_alpha.py, so it is rebuilt when either script changes.
"""
import unreal

import ue_setup_alpha as A

MAP_PATH = A.ROOT + "/Maps/MZ_DispenserTest"


def log(msg):
    print("[MZDispenser] " + msg, flush=True)


def build_dispenser_test(archetypes):
    A.make_materials()
    A.fresh_level(MAP_PATH, unreal.MetalAlphaGameMode)
    A.environment()
    X0, X1, Y = -2500, 3500, 2000
    A.ground("Ground", X0, X1, -Y, Y, 0)
    for label, (x0, x1, y0, y1) in {"Wall_N": (X0, X1, Y, Y + 100), "Wall_S": (X0, X1, -Y - 100, -Y),
                                     "Wall_W": (X0 - 100, X0, -Y, Y), "Wall_E": (X1, X1 + 100, -Y, Y)}.items():
        A.block(label, x0, x1, y0, y1, 0, 500, "MI_Alpha_Rock")
    A.spawn(unreal.PlayerStart, (-2000, 0, 120), 0.0, "PlayerStart", "Mission")
    for i, (x, y) in enumerate([(-300, -700), (-200, 800), (600, 0)]):
        A.crate(f"Cover_{i}", x, y, 0, (200, 200, 140))

    # South: Respawn mode with two assigned guards.
    guards = [A.bot("Red_Guard_Shooter", archetypes["shooter"], (900, -900, 0), 180.0, ("TestGuards",), 300.0),
              A.bot("Red_Guard_Soldier", archetypes["soldier"], (900, -400, 0), 180.0, ("TestGuards",), 300.0)]
    respawner = A.dispenser("Dispenser_Respawn", (2600, -1100, 0), 180.0, assigned=guards)

    # North: Continuous mode, started by the gate.
    gate = A.gate("TestGate", (-800, 1400, 0), 90.0)
    A.block("TestGate_Wall", -900, -700, 1700, Y, 0, 500, "MI_Alpha_Red")
    builder = A.dispenser("Dispenser_Continuous", (2600, 1100, 0), 180.0, gate=gate, max_alive=3,
                          choices=[(archetypes["shooter"], 2.0), (archetypes["soldier"], 1.0)], tags=("TestSpawn",))

    gate_console = A.spawn(unreal.MetalControlConsole, (-1700, 600, 0), 0.0, "GateConsole", "Mission")
    A.setp(gate_console, required_faction=unreal.MetalTeam.NEUTRAL, linked_gates=[gate])
    off_console = A.dispenser_console("DispenserConsole", (3200, 0, 0), 180.0, [respawner, builder])
    A.point_light("Light_S", (2200, -1100, 600), (1.0, 0.5, 0.3), 6000.0, 2000.0)
    A.point_light("Light_N", (2200, 1100, 600), (1.0, 0.5, 0.3), 6000.0, 2000.0)
    A.pickup("Pickup_Health", unreal.MetalPickupType.HEALTH, (-1800, -600, 0), 100.0)
    A.pickup("Pickup_Ammo", unreal.MetalPickupType.AMMO, (-1800, -900, 0), 1.0)

    nav = A.spawn(unreal.NavMeshBoundsVolume, ((X0 + X1) / 2, 0, 300), label="NavBounds", folder="Mission")
    nav.set_actor_scale3d(unreal.Vector((X1 - X0) / 200.0, Y / 100.0, 8.0))

    OT = unreal.MetalObjectiveType
    mission = A.spawn(unreal.MetalMission, (-2000, 0, 400), label="Mission_DispenserTest", folder="Mission")
    A.setp(mission, mission_name="DISPENSER TEST")
    mission.set_editor_property("objectives", [
        A.objective("Open the gate to start the second dispenser", OT.ACTIVATE_CONSOLE, target=gate_console, hint="[E] use the console", dialogue=[
            A.line("Dispenser test. The left machine rebuilds its two guards - scrap one and watch it weld a new one."),
            A.line("The right machine starts when the gate opens, then builds a robot every five seconds."),
        ]),
        A.objective("Shut down both robot dispensers", OT.ACTIVATE_CONSOLE, target=off_console, hint="[E] use the console", dialogue=[
            A.line("Now get to the blue console at the back and shut them both down."),
        ]),
    ])
    A.les.save_current_level()
    log(f"level saved: {MAP_PATH}")
