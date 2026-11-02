# Graybox models

Every model in the game comes from these Blender 5.2 files, one file per model. The asset pipeline
(`Tools/AssetPipeline/run_pipeline.ps1`) reads the whole folder, so there is nothing to register: save a
file here and re-run the pipeline.

```text
Art/Graybox/
├── Shared/GB_Materials.blend        materials used by many models (GB_Eye, GB_WeaponDark, ship paints)
├── Characters/Heroes/<Name>.blend   robots of the Heroes (Player, Guide, Veteran, ...)
├── Characters/Villains/<Name>.blend robots of the Villains (Shooter, Soldier, Brute, Stalker, ...)
├── Weapons/<Name>.blend             held weapons (Laser Level 1, Repeater Level 1, Sniper Rifle, ...)
├── Grenades/<Name>.blend            thrown items (EMP Grenade, ...)
└── Ships/<Group>/<Part>.blend       capital ship pieces and the hangar kit (static meshes)
```

## Rules the pipeline relies on

- **One model per file**, in a collection with **exactly the file's name** at the top of the scene
  (`Weapons/Laser Level 1.blend` holds the collection `Laser Level 1`). Everything else in the file is
  ignored.
- **The folder decides what it is.** `Characters/Heroes`, `Characters/Villains`, `Weapons`, `Grenades` and
  `Ships/<Group>` are recreated as collections when the pipeline assembles the files, exactly like the old
  single-file layout.
- **Inside a model**, the structure is unchanged:
  - **Characters:** an armature named `hero_<id>_ARM` / `villain_<id>_ARM`. The first sub-collection is the
    body; any later sub-collections are optional parts (hand, backpack, visor...). Keep the bone names
    gameplay uses (`bip_head`, `bip_pelvis`, `bip_hip_L/R`, `bip_upperArm/lowerArm_L/R`, `weapon_bone`...).
  - **Weapons and grenades:** an armature whose first bone is `weapon_bone` (the grip), and the meshes in
    the first sub-collection. A new holdable weapon also needs an entry in `WEAPON_COLLECTIONS` (and
    `WEAPON_HOLDERS`) in `Tools/AssetPipeline/blender_export.py`.
  - **Ship parts:** render meshes plus `UCX_` collision meshes, a `PIVOT_*` empty for the origin and
    `SOCKET_*` empties for attachment points.
- **Preview files are skipped.** `Ships/Capital Ship/CapShip_Preview.blend` is only for looking at the
  assembled ship. It links the shield generator and turret parts from their own files, so it updates when
  you edit them.

## Shared materials

Materials that several models use live in `Shared/GB_Materials.blend`, and the model files **link** them.
They show a chain-link icon and can't be edited inside a model file. To change a shared colour, open
`Shared/GB_Materials.blend`, edit it there and save; every model picks it up. A model's own materials are
stored in its own file.

If you add a new material that more than one model should use, put it in `GB_Materials.blend` and link it:
**File > Link... > Shared/GB_Materials.blend > Material**, with **Relative Path** checked.

## Adding a model

1. Copy a similar model's file (for a character, another character's file), rename it, and rename its top
   collection and armature to match.
2. Model it, keeping the rules above, and save.
3. Re-run the pipeline with the editor closed:
   `powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\run_pipeline.ps1`.
   Only the models you changed (and any weapons held by a character you changed) are re-imported, so
   the commit stays small. Commit `Tools/AssetPipeline/import_state.json` with the Content.
