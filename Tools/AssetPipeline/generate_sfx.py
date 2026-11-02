"""
Stage 1b of the Metalization pipeline: synthesizes every sound effect as an original 16-bit mono WAV.

    python generate_sfx.py <output_dir>        (default: Saved/AssetPipeline/Audio)

Standard library only, so Unreal's bundled Python can run it too. Every sound is built from a few
primitives (oscillators with pitch sweeps, filtered noise, envelopes, soft clipping) with a fixed random
seed per sound, so the output is identical on every run. Names ending in _loop are written to loop
seamlessly; ue_import_assets.py marks them as looping. Gameplay code plays them by name through
DA_Sounds (UMetalSoundLibrary), built by ue_setup_gameplay.py.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 44100
TWO_PI = 2.0 * math.pi


# --------------------------------------------------------------------------------------------
# Primitives. A signal is a plain list of floats in -1..1.
# --------------------------------------------------------------------------------------------
def silence(seconds):
    return [0.0] * int(seconds * RATE)


def osc(seconds, freq, shape="sine", freq_end=None, curve=1.0, vibrato=0.0, vibrato_rate=0.0):
    """Oscillator whose pitch slides from freq to freq_end (curve > 1 slides late, < 1 early)."""
    n = int(seconds * RATE)
    out = [0.0] * n
    phase = 0.0
    f1 = freq if freq_end is None else freq_end
    for i in range(n):
        t = i / max(1, n - 1)
        f = freq + (f1 - freq) * (t ** curve)
        if vibrato:
            f *= 1.0 + vibrato * math.sin(TWO_PI * vibrato_rate * i / RATE)
        phase += f / RATE
        p = phase % 1.0
        if shape == "sine":
            v = math.sin(TWO_PI * p)
        elif shape == "square":
            v = 1.0 if p < 0.5 else -1.0
        elif shape == "saw":
            v = 2.0 * p - 1.0
        else:  # triangle
            v = 4.0 * abs(p - 0.5) - 1.0
        out[i] = v
    return out


def noise(seconds, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(int(seconds * RATE))]


def lowpass(sig, cutoff, cutoff_end=None):
    """One-pole low-pass; the cutoff can sweep."""
    out = [0.0] * len(sig)
    y = 0.0
    c1 = cutoff if cutoff_end is None else cutoff_end
    n = max(1, len(sig) - 1)
    for i, x in enumerate(sig):
        c = cutoff + (c1 - cutoff) * i / n
        a = 1.0 - math.exp(-TWO_PI * c / RATE)
        y += a * (x - y)
        out[i] = y
    return out


def highpass(sig, cutoff):
    low = lowpass(sig, cutoff)
    return [x - l for x, l in zip(sig, low)]


def bandpass(sig, low, high):
    return lowpass(highpass(sig, low), high)


def env(sig, attack=0.002, decay=None, hold=0.0, curve=4.0):
    """Linear attack, optional hold, then exponential decay to the end of the signal."""
    n = len(sig)
    a = int(attack * RATE)
    h = int(hold * RATE)
    out = [0.0] * n
    rest = max(1, n - a - h)
    for i, x in enumerate(sig):
        if i < a:
            g = i / max(1, a)
        elif i < a + h:
            g = 1.0
        else:
            g = math.exp(-curve * (i - a - h) / rest)
        out[i] = x * g
    return out


def fade_out(sig, seconds):
    n = int(seconds * RATE)
    out = list(sig)
    for i in range(min(n, len(out))):
        out[len(out) - 1 - i] *= i / max(1, n)
    return out


def gain(sig, g):
    return [x * g for x in sig]


def mix(*parts):
    """Mixes (signal, start_seconds, gain) tuples, or bare signals at time 0."""
    items = [(p, 0.0, 1.0) if isinstance(p, list) else (p[0], p[1], p[2] if len(p) > 2 else 1.0) for p in parts]
    length = max(int(start * RATE) + len(s) for s, start, _ in items)
    out = [0.0] * length
    for s, start, g in items:
        o = int(start * RATE)
        for i, x in enumerate(s):
            out[o + i] += x * g
    return out


def drive(sig, amount=2.0):
    """Soft clipping for grit."""
    k = math.tanh(amount)
    return [math.tanh(x * amount) / k for x in sig]


def crush(sig, bits=6, hold=3):
    """Bit and sample-rate reduction: digital, robotic edge."""
    step = 2.0 ** (bits - 1)
    out = []
    last = 0.0
    for i, x in enumerate(sig):
        if i % hold == 0:
            last = round(x * step) / step
        out.append(last)
    return out


def echo(sig, delay=0.11, feedback=0.35, taps=4):
    d = int(delay * RATE)
    out = sig + [0.0] * (d * taps)
    for t in range(1, taps + 1):
        g = feedback ** t
        for i, x in enumerate(sig):
            out[i + d * t] += x * g
    return out


def ring(seconds, freqs, rng, decay=5.0, amps=None):
    """Struck metal: a few inharmonic partials, each decaying at its own rate."""
    parts = []
    for k, f in enumerate(freqs):
        a = amps[k] if amps else 1.0 / (k + 1)
        parts.append(gain(env(osc(seconds, f * rng.uniform(0.99, 1.01)), 0.001, curve=decay * (1 + 0.4 * k)), a))
    return mix(*parts)


def tones(notes, shape="square", length=0.09, gap=0.0, decay=6.0):
    """A short sequence of beeps (frequencies in Hz)."""
    parts = []
    for i, f in enumerate(notes):
        parts.append((env(osc(length, f, shape), 0.002, curve=decay), i * (length + gap)))
    return mix(*parts)


def loopable(sig, crossfade):
    """Crossfades the tail into the head so the sound loops without a click."""
    n = int(crossfade * RATE)
    body = sig[:len(sig) - n]
    tail = sig[len(sig) - n:]
    for i in range(n):
        g = i / n
        body[i] = body[i] * g + tail[i] * (1.0 - g)
    return body


def normalize(sig, peak=0.89):
    m = max(1e-9, max(abs(x) for x in sig))
    return [x * peak / m for x in sig]


# --------------------------------------------------------------------------------------------
# The sounds
# --------------------------------------------------------------------------------------------
def build(name, rng):
    if name == "laser_fire":
        zap = env(osc(0.2, 2200, "saw", 320, curve=0.5), 0.001, curve=5)
        return mix(lowpass(zap, 6000), (env(highpass(noise(0.05, rng), 3000), 0.0, curve=6), 0, 0.3))
    if name == "enemy_laser_fire":
        zap = env(osc(0.22, 1300, "square", 240, curve=0.6), 0.001, curve=5)
        return lowpass(mix(zap, (env(noise(0.04, rng), 0.0, curve=6), 0, 0.25)), 4500)
    if name == "repeater_fire":
        body = env(osc(0.12, 240, "square", 90, curve=0.4), 0.001, curve=6)
        crack = env(lowpass(noise(0.09, rng), 5000), 0.0, curve=7)
        return drive(mix((body, 0, 0.7), crack), 2.5)
    if name == "shotgun_fire":
        blast = env(lowpass(noise(0.55, rng), 3500, 600), 0.001, curve=5)
        thump = env(osc(0.3, 95, "sine", 38), 0.001, curve=4)
        return drive(mix(blast, (thump, 0, 0.9)), 3.0)
    if name == "sniper_fire":
        crack = env(highpass(noise(0.03, rng), 2500), 0.0, curve=5)
        boom = env(osc(0.7, 75, "sine", 32, curve=0.5), 0.001, curve=4)
        whine = env(osc(0.25, 1800, "saw", 7000, curve=0.3), 0.001, curve=6)
        tail = env(lowpass(noise(1.4, rng), 1400, 300), 0.01, curve=5)
        return drive(echo(mix((crack, 0, 1.0), (boom, 0, 1.0), (lowpass(whine, 8000), 0, 0.25), (tail, 0.02, 0.6)), 0.18, 0.3, 3), 2.0)
    if name == "rocket_fire":
        whoosh = env(bandpass(noise(0.7, rng), 200, 2500), 0.03, curve=3)
        pop = env(osc(0.15, 160, "sine", 60), 0.001, curve=6)
        return mix(whoosh, (pop, 0, 0.8))
    if name == "blaster_fire":
        return lowpass(env(osc(0.16, 700, "square", 180, curve=0.5), 0.001, curve=6), 5000)
    if name == "minigun_fire":
        return drive(mix(env(lowpass(noise(0.06, rng), 4000), 0.0, curve=8), (env(osc(0.05, 180, "square", 120), 0.0, curve=8), 0, 0.5)), 2)
    if name == "reload":
        click1 = env(highpass(noise(0.03, rng), 2000), 0.0, curve=9)
        servo = env(osc(0.32, 320, "square", 420, vibrato=0.03, vibrato_rate=30), 0.03, hold=0.2, curve=6)
        click2 = mix(env(highpass(noise(0.04, rng), 1500), 0.0, curve=8), (ring(0.15, [1900, 3100], rng, 8), 0, 0.4))
        return mix((click1, 0), (lowpass(servo, 1800), 0.05, 0.25), (click2, 0.42))
    if name == "impact":
        return mix(env(highpass(noise(0.08, rng), 1500), 0.0, curve=7), (ring(0.12, [2800, 4100], rng, 10), 0, 0.3))
    if name == "robot_hit":
        clang = ring(0.35, [520, 1330, 2170, 3420], rng, 6)
        return drive(mix(clang, (env(highpass(noise(0.03, rng), 1000), 0.0, curve=8), 0, 0.6)), 1.5)
    if name == "player_hit":
        clang = ring(0.4, [310, 790, 1460], rng, 5)
        buzz = env(osc(0.18, 90, "square"), 0.001, curve=6)
        return mix(clang, (lowpass(buzz, 900), 0, 0.4))
    if name in ("robot_explode", "explosion"):
        big = name == "explosion"
        length = 1.6 if big else 1.2
        rumble = env(lowpass(noise(length, rng), 1600 if big else 2200, 200), 0.004, curve=4)
        boom = env(osc(0.8, 70 if big else 90, "sine", 28), 0.002, curve=4)
        crackle = [x if rng.random() < 0.03 else 0.0 for x in noise(length * 0.6, rng)]
        debris = ring(0.6, [700, 1700, 2600], rng, 7) if not big else silence(0.01)
        return drive(mix(rumble, (boom, 0, 1.0), (highpass(crackle, 2000), 0.05, 0.5), (debris, 0.08, 0.3)), 2.5)
    if name == "emp_blast":
        buzz = env(osc(0.9, 140, "square", 40, vibrato=0.2, vibrato_rate=23), 0.003, curve=3)
        zap = env(osc(0.3, 3000, "saw", 300, curve=0.3), 0.001, curve=5)
        crackle = [x if rng.random() < 0.05 else 0.0 for x in noise(0.9, rng)]
        return crush(drive(mix(lowpass(buzz, 2500), (zap, 0, 0.5), (highpass(crackle, 3000), 0, 0.6)), 2.0), 7, 2)
    if name == "grenade_throw":
        return env(bandpass(noise(0.28, rng), 600, 3000), 0.06, curve=4)
    if name in ("footstep", "footstep_sprint", "footstep_heavy"):
        heavy = name == "footstep_heavy"
        thud = env(osc(0.09 if not heavy else 0.2, 140 if not heavy else 70, "sine", 70 if not heavy else 30), 0.001, curve=6)
        click = env(lowpass(noise(0.03, rng), 3000 if name == "footstep_sprint" else 2200), 0.0, curve=8)
        clank = ring(0.12, [1700, 2600] if not heavy else [600, 1100], rng, 9)
        return mix(thud, (click, 0, 0.6), (clank, 0, 0.25 if not heavy else 0.4))
    if name == "jump":
        hiss = env(bandpass(noise(0.18, rng), 800, 5000), 0.005, curve=5)
        rise = env(osc(0.14, 220, "triangle", 520), 0.005, curve=4)
        return mix(hiss, (rise, 0, 0.4))
    if name == "double_jump":
        thrust = env(bandpass(noise(0.4, rng), 400, 4000), 0.01, hold=0.08, curve=4)
        rise = env(osc(0.3, 300, "saw", 950, curve=0.6), 0.005, curve=4)
        return mix(thrust, (lowpass(rise, 3000), 0, 0.35))
    if name == "land":
        thud = env(osc(0.2, 95, "sine", 40), 0.001, curve=5)
        return mix(thud, (env(lowpass(noise(0.12, rng), 1500), 0.0, curve=6), 0, 0.7), (ring(0.2, [800, 1500], rng, 8), 0.01, 0.25))
    if name == "lift_hum_loop":
        # 2 s with whole numbers of cycles of every tone, so it loops cleanly.
        hum = mix(gain(osc(2.0, 110), 0.5), gain(osc(2.0, 165), 0.25), gain(osc(2.0, 220, "triangle"), 0.2), gain(osc(2.0, 440), 0.06))
        trem = [x * (0.8 + 0.2 * math.sin(TWO_PI * 2.0 * i / RATE)) for i, x in enumerate(hum)]
        air = lowpass(noise(2.4, rng), 900)
        air = loopable(air, 0.4)
        return mix(trem, (air, 0, 0.15))
    if name == "lift_enter":
        return mix(env(osc(0.5, 200, "sine", 700, curve=0.7), 0.02, curve=3), (env(bandpass(noise(0.5, rng), 500, 3000), 0.05, curve=3), 0, 0.4))
    if name == "lift_exit":
        return mix(env(osc(0.3, 600, "sine", 250), 0.005, curve=4), (env(bandpass(noise(0.25, rng), 800, 3000), 0.0, curve=5), 0, 0.5))
    if name == "swap_servo":
        whir = osc(0.65, 170, "square", 260, vibrato=0.05, vibrato_rate=18)
        whir = [x * math.sin(math.pi * i / len(whir)) for i, x in enumerate(whir)]
        return lowpass(whir, 1400)
    if name == "swap_detach":
        clunk = mix(env(osc(0.12, 160, "sine", 80), 0.001, curve=6), (env(highpass(noise(0.03, rng), 1200), 0.0, curve=8), 0, 0.6))
        hiss = env(bandpass(noise(0.3, rng), 2000, 7000), 0.005, curve=4)
        return mix(clunk, (ring(0.18, [1200, 2300], rng, 7), 0, 0.35), (hiss, 0.02, 0.35))
    if name == "swap_rack":
        return mix(env(highpass(noise(0.04, rng), 1500), 0.0, curve=8), (ring(0.2, [950, 1800, 2900], rng, 7), 0, 0.5))
    if name == "swap_attach":
        clunk = drive(mix(env(osc(0.18, 120, "sine", 55), 0.001, curve=5), (env(lowpass(noise(0.05, rng), 2500), 0.0, curve=8), 0, 0.8)), 2.0)
        lock = mix(env(highpass(noise(0.02, rng), 2500), 0.0, curve=9), (ring(0.15, [2400, 3600], rng, 9), 0, 0.4))
        charge = env(osc(0.25, 400, "saw", 1600, curve=0.5), 0.02, curve=4)
        return mix(clunk, (lock, 0.07, 0.8), (lowpass(charge, 3000), 0.1, 0.2))
    if name == "alert":
        return crush(tones([880, 1320], "square", 0.08, 0.02, 4), 6, 2)
    if name == "suspicion":
        return env(osc(0.22, 480, "sine", 720, curve=0.8), 0.02, curve=3)
    if name == "tether_link":
        zap = env(osc(0.25, 2500, "saw", 400, curve=0.4), 0.001, curve=5)
        arp = tones([523, 659, 784, 1047], "square", 0.06, 0.0, 5)
        return mix(lowpass(zap, 5000), (crush(arp, 6, 2), 0.05, 0.5))
    if name == "tether_release":
        return crush(tones([1047, 784, 659, 523], "square", 0.06, 0.0, 5), 6, 2)
    if name == "tether_fail":
        return lowpass(tones([180, 150], "square", 0.1, 0.03, 4), 1500)
    if name == "console_granted":
        return crush(tones([660, 880, 1320], "square", 0.08, 0.02, 4), 7, 2)
    if name == "console_denied":
        return lowpass(tones([220, 165], "saw", 0.16, 0.04, 3), 1800)
    if name == "gate_open":
        rumble = env(lowpass(noise(2.2, rng), 300), 0.2, hold=1.2, curve=3)
        motor = env(osc(2.2, 55, "saw", 70, vibrato=0.02, vibrato_rate=6), 0.2, hold=1.2, curve=3)
        hiss = env(bandpass(noise(0.6, rng), 2000, 8000), 0.01, curve=4)
        clank = ring(0.6, [300, 720, 1300], rng, 5)
        return mix(rumble, (lowpass(motor, 600), 0, 0.5), (hiss, 0, 0.4), (clank, 1.8, 0.6))
    if name == "pickup":
        return tones([988, 1319, 1976], "triangle", 0.07, 0.0, 5)
    if name == "checkpoint":
        return mix(tones([784, 1175], "sine", 0.18, 0.0, 3), (tones([1568, 2350], "sine", 0.18, 0.0, 3), 0, 0.3))
    if name == "objective_complete":
        return mix(tones([523, 659, 784], "triangle", 0.12, 0.0, 3), (env(osc(0.6, 1047, "sine"), 0.01, curve=3), 0.36, 0.5))
    if name == "mission_complete":
        notes = [(392, 0.0), (523, 0.15), (659, 0.3), (784, 0.45), (1047, 0.7)]
        parts = [(env(mix(osc(0.5 if i < 4 else 1.6, f, "square"), gain(osc(0.5 if i < 4 else 1.6, f * 1.5, "saw"), 0.3)), 0.005, curve=3), t, 0.5)
                 for i, (f, t) in enumerate(notes)]
        return lowpass(mix(*parts, (env(osc(1.6, 262, "triangle"), 0.01, curve=2), 0.7, 0.6)), 3500)
    if name == "player_down":
        return mix(env(osc(1.2, 500, "saw", 40, curve=0.6), 0.005, curve=2), (env(lowpass(noise(0.8, rng), 1200), 0.0, curve=4), 0, 0.4))
    if name == "radio_blip":
        squelch = env(bandpass(noise(0.08, rng), 1200, 4000), 0.0, curve=6)
        return mix(squelch, (env(osc(0.06, 1800, "sine"), 0.002, curve=4), 0.03, 0.5))
    if name == "ui_move":
        return env(osc(0.035, 1500, "triangle"), 0.001, curve=6)
    if name == "ui_select":
        return tones([1100, 1650], "triangle", 0.05, 0.0, 5)
    if name == "ambience_loop":
        wind = lowpass(noise(8.6, rng), 500)
        swell = [x * (0.6 + 0.4 * math.sin(TWO_PI * i / (RATE * 4.3))) for i, x in enumerate(wind)]
        return loopable(swell, 0.6)
    if name == "battle_loop":
        bed = lowpass(noise(10.6, rng), 180)
        parts = [bed]
        for _ in range(9):
            t = rng.uniform(0.0, 9.0)
            boom = env(lowpass(noise(1.0, rng), 500, 120), 0.01, curve=4)
            parts.append((boom, t, rng.uniform(0.4, 1.0)))
        for _ in range(14):
            t = rng.uniform(0.0, 9.6)
            pew = env(osc(0.18, rng.uniform(900, 1600), "saw", 250, curve=0.5), 0.001, curve=5)
            parts.append((lowpass(pew, 1500), t, rng.uniform(0.05, 0.15)))
        return loopable(mix(*parts)[:int(10.6 * RATE)], 0.6)
    raise KeyError(name)


SOUNDS = [
    "laser_fire", "enemy_laser_fire", "repeater_fire", "shotgun_fire", "sniper_fire", "rocket_fire", "blaster_fire",
    "minigun_fire", "reload", "impact", "robot_hit", "player_hit", "robot_explode", "explosion", "emp_blast",
    "grenade_throw", "footstep", "footstep_sprint", "footstep_heavy", "jump", "double_jump", "land",
    "lift_hum_loop", "lift_enter", "lift_exit", "swap_servo", "swap_detach", "swap_rack", "swap_attach",
    "alert", "suspicion", "tether_link", "tether_release", "tether_fail", "console_granted", "console_denied",
    "gate_open", "pickup", "checkpoint", "objective_complete", "mission_complete", "player_down", "radio_blip",
    "ui_move", "ui_select", "ambience_loop", "battle_loop",
]


def write_wav(path, sig):
    sig = normalize(sig)
    # 5 ms fade-in/out on one-shots kills clicks; loops are already continuous.
    if not os.path.basename(path).endswith("_loop.wav"):
        for i in range(min(220, len(sig))):
            sig[i] *= i / 220
        sig = fade_out(sig, 0.005)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, x)) * 32767)) for x in sig))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "Saved", "AssetPipeline", "Audio")
    os.makedirs(out_dir, exist_ok=True)
    for index, name in enumerate(SOUNDS):
        rng = random.Random(1000 + index)
        sig = build(name, rng)
        write_wav(os.path.join(out_dir, name + ".wav"), sig)
    print(f"[MZSfx] DONE {len(SOUNDS)} sounds -> {os.path.abspath(out_dir)}", flush=True)


if __name__ == "__main__":
    main()
