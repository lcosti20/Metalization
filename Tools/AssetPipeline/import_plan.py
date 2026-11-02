"""
Stage 1c of the Metalization pipeline: decides what actually needs re-importing.

    python import_plan.py plan [--full]     before ue_import_assets.py
    python import_plan.py commit            after it succeeded
    python import_plan.py adopt             record the current export as already imported (no import)

The pipeline used to wipe every generated folder and re-import everything, so every .uasset changed on
every run. Instead, each generated asset is a "unit" with a hash of what it is made from:

    character:<id>          FBX fingerprints of the body and parts (from blender_export.py)
    follower:<weapon>@<who> the weapon-on-holder FBX fingerprint + the holder's character hash
                            (a re-imported character gets a new skeleton, so its held weapons follow)
    prop:<id>               the static prop FBX fingerprint
    ship:<part>             the ship part FBX fingerprint + its sockets
    material:<key>          the material description in manifest.json
    texture:<key>, sound:<name>   the file's bytes
    masters                 ue_import_assets.py (master materials; any importer change re-imports all)

`plan` compares them with import_state.json (committed next to this script, so it travels with the
Content it describes). For every changed, new or missing unit it deletes that unit's .uasset files on disk
(the commandlet can't delete files, and re-importing over a mesh keeps stale material slots), and units
that no longer exist are deleted too. It then writes Saved/AssetPipeline/import_plan.json, which tells
ue_import_assets.py what to import. `commit` records the new hashes once the import has succeeded.
--full re-imports everything, like the old pipeline (wipes the generated folders first).
"""
import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.abspath(os.path.join(HERE, "..", ".."))
PIPELINE = os.path.join(PROJECT, "Saved", "AssetPipeline")
CONTENT = os.path.join(PROJECT, "Content", "Metalization")
STATE_FILE = os.path.join(HERE, "import_state.json")
PLAN_FILE = os.path.join(PIPELINE, "import_plan.json")
GENERATED = ("Characters", "Weapons", "Props", "Ships", "Textures", os.path.join("Materials", "Instances"), "Audio")
MASTERS = ["Materials/M_MZ_Base", "Materials/M_MZ_Base_Masked", "Materials/M_MZ_Light", "Materials/M_MZ_Decal",
           "Materials/M_FX_Glow", "Materials/M_Env_Metal", "Materials/MI_Env_Floor", "Materials/MI_Env_Wall",
           "Materials/MI_Env_Dark", "Materials/MI_Env_Hazard", "Materials/MI_Env_Glow"]


def log(msg):
    print("[MZPlan] " + msg, flush=True)


def pascal(text):
    # Same as ue_import_assets.pascal: asset names derive from manifest ids.
    return "".join(p[:1].upper() + p[1:] for p in text.replace("-", "_").split("_") if p)


def sha(*parts):
    h = hashlib.sha256()
    for part in parts:
        h.update(part if isinstance(part, bytes) else str(part).encode())
        h.update(b"\0")
    return h.hexdigest()


def file_sha(path):
    with open(path, "rb") as f:
        return sha(f.read())


def load_state():
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE) as f:
            state = json.load(f)
        return state.get("imports", {}), state.get("levels", {})
    return {}, {}


def save_state(imports, levels):
    with open(STATE_FILE, "w", newline="\r\n") as f:
        json.dump({"imports": imports, "levels": levels}, f, indent=1, sort_keys=True)
        f.write("\n")


def units(manifest):
    """Every generated unit: key -> (hash, [asset paths under Content/Metalization, no extension or a folder/])."""
    fp = manifest.get("fingerprints", {})
    with open(os.path.join(HERE, "ue_import_assets.py"), "rb") as f:
        importer = sha(f.read())
    out = {"masters": (importer, MASTERS)}

    def fingerprint(rel):
        if rel not in fp:
            raise SystemExit(f"manifest.json has no fingerprint for {rel}: re-run blender_export.py")
        return fp[rel]

    for key, desc in manifest["materials"].items():
        out[f"material:{key}"] = (sha(importer, json.dumps(desc, sort_keys=True)), [f"Materials/Instances/MI_{key}"])
    for key, tex in manifest["textures"].items():
        out[f"texture:{key}"] = (sha(importer, file_sha(os.path.join(PIPELINE, tex["file"]))), [f"Textures/T_{key}"])
    characters = {}
    for cid, ch in manifest["characters"].items():
        files = [ch["file"]] + [p["file"] for p in ch["parts"].values()]
        characters[cid] = sha(importer, *[fingerprint(f) for f in files], *sorted(ch["parts"]))
        out[f"character:{cid}"] = (characters[cid], [f"Characters/{pascal(cid)}/"])
    for group in ("weapons", "props"):
        for wid, w in manifest[group].items():
            name = pascal(wid)
            out[f"prop:{wid}"] = (sha(importer, fingerprint(w["prop"])), [f"Props/SM_{name}"])
            for holder, rel in w["followers"].items():
                if holder in characters:
                    out[f"follower:{wid}@{holder}"] = (sha(importer, fingerprint(rel), characters[holder]),
                                                       [f"Weapons/{name}/SK_{name}_On{pascal(holder)}"])
    for part, s in manifest.get("ships", {}).items():
        out[f"ship:{part}"] = (sha(importer, fingerprint(s["file"]), json.dumps(s["sockets"], sort_keys=True)), [f"Ships/SM_{part}"])
    audio = os.path.join(PIPELINE, "Audio")
    for file in sorted(os.listdir(audio)) if os.path.isdir(audio) else []:
        if file.endswith(".wav"):
            out[f"sound:{file[:-4]}"] = (sha(importer, file_sha(os.path.join(audio, file))), [f"Audio/SW_{pascal(file[:-4])}"])
    return out


def on_disk(asset):
    path = os.path.join(CONTENT, asset)
    if asset.endswith("/"):
        return os.path.isdir(path) and any(f.endswith(".uasset") for f in os.listdir(path))
    return os.path.exists(path + ".uasset")


def delete(asset):
    path = os.path.join(CONTENT, asset)
    if asset.endswith("/"):
        shutil.rmtree(path, ignore_errors=True)
    elif os.path.exists(path + ".uasset"):
        os.remove(path + ".uasset")


def plan(full):
    with open(os.path.join(PIPELINE, "manifest.json")) as f:
        manifest = json.load(f)
    old, _ = load_state()
    new = units(manifest)
    if full:
        for folder in GENERATED:
            shutil.rmtree(os.path.join(CONTENT, folder), ignore_errors=True)
        old = {}
    changed = []
    for key, (digest, assets) in sorted(new.items()):
        if old.get(key) != digest or not all(on_disk(a) for a in assets):
            changed.append(key)
            # Masters and material instances are rebuilt in place; meshes, textures and sounds are re-imported fresh.
            if key != "masters" and not key.startswith("material:"):
                for asset in assets:
                    delete(asset)
    removed = sorted(k for k in old if k not in new)
    for key in removed:
        kind, _, name = key.partition(":")
        # The assets of a unit that no longer exists (a deleted model): rebuild the paths from the key.
        stale = {"character": [f"Characters/{pascal(name)}/"], "prop": [f"Props/SM_{pascal(name)}"],
                 "ship": [f"Ships/SM_{name}"], "sound": [f"Audio/SW_{pascal(name)}"], "texture": [f"Textures/T_{name}"],
                 "material": [f"Materials/Instances/MI_{name}"]}.get(kind, [])
        if kind == "follower":
            wid, _, holder = name.partition("@")
            stale = [f"Weapons/{pascal(wid)}/SK_{pascal(wid)}_On{pascal(holder)}"]
        for asset in stale:
            delete(asset)

    def names(kind):
        return sorted(k.split(":", 1)[1] for k in changed if k.startswith(kind + ":"))

    result = {
        "full": full,
        "rebuild_masters": "masters" in changed,
        "materials": names("material"),
        "textures": names("texture"),
        "characters": names("character"),
        "followers": [n.split("@") for n in names("follower")],
        "props": names("prop"),
        "ships": names("ship"),
        "sounds": names("sound"),
        "state": {k: v[0] for k, v in new.items()},
    }
    with open(PLAN_FILE, "w") as f:
        json.dump(result, f, indent=1)
    summary = ", ".join(f"{k} {len(result[k])}" for k in ("characters", "followers", "props", "ships", "materials", "textures", "sounds"))
    log(f"{len(changed)} of {len(new)} units to import ({summary}; masters {'yes' if result['rebuild_masters'] else 'no'})"
        + (f", {len(removed)} removed" if removed else "") + (" [full]" if full else ""))


def adopt():
    """Declares the Content on disk up to date with the current export (e.g. right after a full import, or
    when introducing import_state.json), so the next run doesn't re-import everything."""
    with open(os.path.join(PIPELINE, "manifest.json")) as f:
        manifest = json.load(f)
    new = units(manifest)
    missing = [k for k, (_, assets) in new.items() if not all(on_disk(a) for a in assets)]
    if missing:
        raise SystemExit(f"[MZPlan] can't adopt: {len(missing)} units have no assets on disk, e.g. {missing[:5]}")
    _, levels = load_state()
    save_state({k: v[0] for k, v in new.items()}, levels)
    log(f"adopted {len(new)} units as up to date")


def commit():
    with open(PLAN_FILE) as f:
        result = json.load(f)
    _, levels = load_state()
    save_state(result["state"], levels)
    log(f"recorded {len(result['state'])} units in {os.path.relpath(STATE_FILE, PROJECT)}")


if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else "plan"
    if command == "plan":
        plan("--full" in sys.argv)
    elif command == "commit":
        commit()
    elif command == "adopt":
        adopt()
    else:
        raise SystemExit("usage: import_plan.py plan [--full] | commit | adopt")
