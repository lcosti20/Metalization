# Unreal → Unity port

The game was first built in Unreal Engine 5.8 (C++). This page records how it was moved to Unity 6 (URP, C#),
so the two versions can be compared and the remaining differences are known.

## Conventions

| Unreal | Unity |
|---|---|
| Centimetres, X forward / Y right / Z up | Metres, Z forward / X right / Y up. Values ported from Unreal go through `UE.Vec`, `UE.Dir`, `UE.Rot`, `UE.MeshVec` (`MetalTypes.cs`) |
| `AActor` + components | `GameObject` + `MonoBehaviour` |
| `APawn` / `AController` possession | `MetalBot` / `MetalController.Possess` (`MetalController.cs`) |
| `UCharacterMovementComponent` | `MetalCharacterMovement` (on a `CharacterController`), same friction/braking/air-control maths |
| `UMetalBotAnimInstance` proxy | `MetalBotAnimator` (`LateUpdate`, same character-space deltas) |
| Leader-pose / bonemerge skeletal meshes | `SkinnedMeshRenderer.bones` remapped onto the body's bones by name (`MetalBot.AttachFollower`) |
| Data assets (`DA_Bot_*`, `WD_*`) | ScriptableObjects (`MetalBotArchetype`, `MetalWeaponData`), same names |
| Niagara systems | Particle systems and line renderers built in code (`MetalFX`) |
| `TakeDamage` / `ApplyPointDamage` / radial damage | `IMetalDamageable`, `MetalDamageEvent`, `MetalPhysics.ApplyRadialDamage` |
| Visibility trace channel | `MetalPhysics.ShotMask` (everything but the `Projectile`, `MetalTrigger` and `MetalBlocker` layers) |
| `InvisibleWall` collision profile | `MetalBlocker` layer: blocks robots and the navmesh, not shots or sight |
| Maps (`.umap`) built by `ue_setup_*.py` | Scenes built by C# level builders (`Editor/MetalLevels*.cs`) through `MetalLevelWriter`, which takes the Unreal numbers |
| Static meshes with sockets | Prefabs (`Prefabs/Ships/SM_*`) with a `Model` child and `SOCKET_*` children |
| FBX import axes | Blender's FBX comes into Unity facing -Z; `MetalImportRules` turns every model's top-level nodes 180° about Y so characters face +Z (left at -X) and static models' Blender +X lands on +Z after `StaticModelRotation` |
| Level streaming of the ship interior | `SceneManager.LoadSceneAsync(..., Additive)`, roots moved to the interior location |
| Packaged build + `-game` command line | Player build (`MetalBuild`) + `-MetalLevel=<scene>` to skip the title screen |
| Static multicast delegates | Static C# events (`MetalBot.AnyBotDied`, ...) |
| Timers | Coroutines / `Invoke` |
| `GetWorld()->GetTimeSeconds()` | `Time.time` |

## Status

| System | Unreal source | Unity | Notes |
|---|---|---|---|
| Shared types, data assets | `MetalTypes.h`, `MetalBotArchetype.h`, `MetalWeaponData.h` | `Core/` | done |
| Audio | `MetalAudio` | `Core/MetalAudio.cs` | done; groups replace sound classes |
| Effects | `MetalFX` | `Core/MetalFX.cs` | done; code-built particles |
| Robots | `MetalBotCharacter`, `MetalProjectile` | `Bots/` | done |
| Animation | `MetalBotAnimInstance` | `Bots/MetalBotAnimator.cs` | done |
| Flyers | `MetalStalker` | `Bots/MetalStalker.cs` | done |
| AI | `MetalBotAIController`, `MetalFlyerAIController` | `AI/` | done; `MetalPathFollower` replaces PathFollowingComponent, jump links are detected from path corners |
| Player, Hack Tether, camera | `MetalPlayerController`, `MetalHackPuzzle` | `Player/` | done; Input System actions built in code; `MetalCameraRig` = spring arm |
| Missions | `MetalGameMode`, `MetalMission`, `MetalDialogue`, `MetalCheckpoint` | `Missions/` | done; checkpoints track `MetalPlaced` ids |
| Level actors | trigger zone, console, gate, pickup, gravlift, barrage, energy field, dispenser | `Level/` | done |
| UI drawing | Canvas / Slate | `UI/MetalDraw.cs` (IMGUI, 1080p reference units) | done |
| HUD, menus, title | `MetalHUD`, `MetalPauseMenu`, `MetalTitle` | `UI/` | done |
| Space battle | capital ship, hangar, portal, battle mode | `Space/` | done |
| Dev autopilot | `MetalDevAutopilot` | `Dev/MetalDevAutopilot.cs` | done; same options and scenarios, Unreal coordinates |
| Asset pipeline | Blender export + `ue_import_assets.py` + `ue_setup_gameplay.py` data | `Tools/AssetPipeline/blender_export.py` + `Editor/MetalAssetPipeline.cs`, `MetalImportRules.cs` | done |
| Levels | `ue_setup_*.py` | `Editor/MetalLevels.cs` (Outpost), `MetalLevelsSandbox.cs` (Arena, Flight Test), `MetalLevelsAlpha.cs` (Production Alpha, Title, Dispenser Test), `MetalLevelsSpace.cs` (interior, ship data, Space Battle) | done |
| Scripts | `run_pipeline.ps1`, `smoke_test.ps1` | same names, drive Unity in batch mode / the player build | done |

## Verified

Every scene was played in the editor with no console errors: Title (Start Mission loads Operation Redline), Operation
Redline, Outpost, Arena, Flight Test, Dispenser Test and Space Battle (both ship interiors load, AI pilots launch, turrets
fire). The `combat` smoke test passes on a Development player build (hijacks a courtyard Shooter).

## Known differences

- No ragdolls: destroyed robots slump with a code-driven collapse instead of a physics ragdoll.
- Navmesh jump links are not generated: the path follower treats a large height step between path corners as a jump.
  Grav lifts add explicit `NavMeshLink`s.
- UI is IMGUI drawn in code (as the Unreal HUD/Slate code was); effects are built from particle systems and line renderers
  rather than Niagara assets, so they look close to, not identical to, the Unreal version.
- Lighting is a URP approximation of the Unreal sky atmosphere / height fog / auto exposure: sun, trilight ambient, fog
  and a post-processing volume per scene, tuned by eye.
- The asteroid field uses a different random sequence than the Python version (same counts, sizes and exclusion zones).
- Draw-distance culling of the ship interiors is done by the camera far plane (6 km) instead of per-mesh draw distances.
