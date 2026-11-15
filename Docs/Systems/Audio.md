# Audio

Every sound in the game is original and synthesized, so the project has no third-party audio files.

## Where sounds come from

- **Generator.** `Tools/AssetPipeline/generate_sfx.py` builds 49 WAVs from a few primitives:
  - oscillators with pitch sweeps;
  - filtered noise;
  - envelopes, soft clipping, bit-crushing and echoes.

  Each sound has a fixed random seed, so the output is the same on every run. It needs only the standard
  library, and the pipeline runs it with the engine's bundled Python.
- **Loops.** Names ending in `_loop` (lift hum, wind, distant battle, radio whistle) are built to loop without a click.
- **Music.** Licensed tracks live in `Art/Music` and are listed in `MUSIC` in `generate_sfx.py`, which copies
  them (as MP3) next to the WAVs under their library name, e.g. `music_background_loop`. Credit every track in
  `CREDITS.txt` and on the menus' Credits panel. The current track is from Pixabay; its licence certificate
  can sit next to it as `<name>-license.txt` (git-ignored, since it names the downloading account).
- **Import.** `ue_import_assets.py` (`import_audio`) imports them as `/Game/Metalization/Audio/SW_<Name>` and
  marks the `_loop` ones as looping.
- **Library.** `ue_setup_gameplay.py` (`make_sound_library`) lists them in `DA_Sounds`, a
  `UMetalSoundLibrary` (name → `USoundBase`), with optional per-sound volume trims. It also puts every wave on
  one of two sound classes, `SC_Music` (`MUSIC_SOUNDS`: music and background loops) or `SC_Effects`
  (everything else).

Regenerate everything with the normal pipeline run. To audition a sound, run the generator on its own:

```powershell
python Tools\AssetPipeline\generate_sfx.py C:\temp\sfx
```

## Playing sounds

Gameplay code never references a sound asset. It plays a name through `MetalAudio` (`MetalAudio.h`):

| Function | Use |
|---|---|
| `MetalAudio::PlayAt(Context, Name, Location, Volume, PitchJitter)` | Positional one-shot, with the shared distance falloff (full volume within 4 m, silent past ~45 m) and a little random pitch |
| `MetalAudio::Play2D(Context, Name, Volume)` | UI, radio, stingers |
| `MetalAudio::PlayLoop(Component, Name, Volume)` | Attached looping sound (lift hum) |
| `MetalAudio::CreateLoop2D(Context, Name, Volume)` | Non-positional loop, created stopped; the caller fades it in and out from any point (radio whistle) |

- **Library loading.** The library is loaded by path on first use and rooted, and so is the shared
  attenuation object.
- **Missing sounds.** An unknown name, or no library at all, is silent, so the game runs before the pipeline
  has been run.
- **Weapons.** Each weapon names its own `FireSound`, `ReloadSound` and `ImpactSound` (`UMetalWeaponData`).

## Hooks

| Event | Sound |
|---|---|
| Weapon fire / reload / hitscan impact / projectile impact | weapon's names (`laser_fire`, `sniper_fire`...) |
| Robot hit, player hit, robot destroyed, explosion, EMP | `robot_hit`, `player_hit`, `robot_explode`, `explosion`, `emp_blast` |
| Footsteps (every half stride; sneaking is near silent) | `footstep`, `footstep_sprint`, `footstep_heavy` |
| Jump, double jump, landing | `jump`, `double_jump`, `land` |
| Anti-grav lift | `lift_hum_loop`, `lift_enter`, `lift_exit` |
| Weapon-arm swap beats | `swap_servo`, `swap_detach`, `swap_rack`, `swap_attach` |
| Stealth | `suspicion` (a robot starts to notice), `alert` (spotted) |
| Hack Tether | `tether_link`, `tether_release`, `tether_fail` |
| Console, gate, pickups | `console_granted`, `console_denied`, `gate_open`, `pickup` |
| Mission | `checkpoint`, `objective_complete`, `mission_complete`, `player_down`, `radio_blip` (each dialogue line), `radio_whistle_loop` (a heterodyne whistle sweeping between random pitches, quietly under dialogue while the box is open; random start point and pitch each time) |
| Menus | `ui_move`, `ui_select` |
| Music and ambience (AmbientSound actors in the maps) | `music_background_loop` (title screen and Production Alpha), `battle_loop` |

## Volume settings

The menus' **Sound** panel has two sliders, **Music / Background** and **Sound Effects**. They call
`MetalAudio::SetVolume`, which saves the value in `GameUserSettings.ini` (`[/Script/Metalization.MetalAudio]`)
and applies it straight away as sound-class overrides on a transient base sound mix, so sounds already playing
follow. Both player controllers call `MetalAudio::ApplyVolumes` in `BeginPlay`. A new sound only needs adding
to `MUSIC_SOUNDS` if it belongs on the music slider.
