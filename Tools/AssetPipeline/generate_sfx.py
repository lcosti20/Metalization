"""
Stage 1b of the Metalization pipeline: synthesizes every sound effect as an original 16-bit mono WAV.

    python generate_sfx.py <output_dir>        (default: Saved/AssetPipeline/Audio)

Standard library only, so Unreal's bundled Python can run it too. Every sound is built from a few
primitives (oscillators with pitch sweeps, filtered noise, envelopes, soft clipping) with a fixed random
seed per sound, so the output is identical on every run. Names ending in _loop are written to loop
seamlessly; ue_import_assets.py marks them as looping. Gameplay code plays them by name through
DA_Sounds (UMetalSoundLibrary), built by ue_setup_gameplay.py.

It also copies the licensed music tracks in Art/Music (credited in CREDITS.txt) into the same folder under
their library names, so they are imported and listed alongside the effects. A missing track is skipped
with a warning and the game plays without that music.
"""
import math
import os
import random
import shutil
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
# Robot voices: wordless beeps, whistles and warbles that stand in for speech.
# --------------------------------------------------------------------------------------------
def chirp(freq, glides, vibrato=0.0, vibrato_rate=0.0, fm=0.0, fm_ratio=2.0, buzz=0.0, attack=0.004, release=0.012):
    """
    One voice "syllable": a tone that starts at freq and glides through (seconds, Hz) steps in turn.

    vibrato is a pitch wobble depth (0.05 = +-5%), or a (start, end) pair to ramp it over the syllable.
    fm adds a rough, growling edge (phase modulation at fm_ratio times the pitch); buzz adds odd
    harmonics, towards a square wave. The pitch glides in log-frequency with eased ends, like a whistle.
    """
    total = sum(seconds for seconds, _ in glides)
    n = max(1, int(total * RATE))
    vib0, vib1 = vibrato if isinstance(vibrato, tuple) else (vibrato, vibrato)
    out = [0.0] * n
    a = max(1, int(attack * RATE))
    r = max(1, int(release * RATE))
    seg, seg_start, f_from = 0, 0.0, freq
    phase = 0.0
    for i in range(n):
        ti = i / RATE
        while seg < len(glides) - 1 and ti >= seg_start + glides[seg][0]:
            seg_start += glides[seg][0]
            f_from = glides[seg][1]
            seg += 1
        seconds, f_to = glides[seg]
        u = min(1.0, (ti - seg_start) / max(1e-6, seconds))
        u = u * u * (3.0 - 2.0 * u)
        f = math.exp(math.log(f_from) + (math.log(f_to) - math.log(f_from)) * u)
        depth = vib0 + (vib1 - vib0) * i / n
        if depth:
            f *= 1.0 + depth * math.sin(TWO_PI * vibrato_rate * ti)
        phase += f / RATE
        w = TWO_PI * phase
        v = math.sin(w + fm * math.sin(w * fm_ratio)) if fm else math.sin(w)
        if buzz:
            v += buzz * (math.sin(3.0 * w) / 3.0 + math.sin(5.0 * w) / 5.0)
        out[i] = v * min(1.0, i / a, (n - 1 - i) / r)
    return out


def say(*parts):
    """Joins syllables end to end; a number is a pause in seconds."""
    out = []
    for p in parts:
        out.extend(silence(p) if isinstance(p, (int, float)) else p)
    return out


def robot_voice(kind, rng):
    """
    Builds one line of robot "speech" for a mood. Every choice comes from rng, so each numbered variant
    of a mood (voice_chatter1, voice_chatter2...) is a different phrase in the same tone of voice.
    """
    def pick(low, high):
        return math.exp(rng.uniform(math.log(low), math.log(high)))

    def beep(f, seconds, **kw):
        return chirp(f, [(seconds, f)], **kw)

    def slide(f0, f1, seconds, **kw):
        return chirp(f0, [(seconds, f1)], **kw)

    def trill(f_a, f_b, seconds, rate, **kw):
        # Flips between two pitches rate times a second.
        step = 1.0 / rate
        glides, high = [], True
        for _ in range(max(2, int(seconds / step))):
            glides += [(0.006, f_b if high else f_a), (step - 0.006, f_b if high else f_a)]
            high = not high
        return chirp(f_a, glides, **kw)

    if kind == "chatter":
        # Idle muttering: an unhurried run of mixed beeps, slides, scoops, warbles and the odd low "boop".
        parts = []
        for _ in range(rng.randint(4, 7)):
            f = pick(650, 2300)
            shape = rng.choice(["beep", "beep", "slide", "slide", "scoop", "trill", "warble", "boop"])
            if shape == "beep":
                parts.append(beep(f, rng.uniform(0.05, 0.11)))
            elif shape == "slide":
                parts.append(slide(f, pick(650, 2600), rng.uniform(0.08, 0.2)))
            elif shape == "scoop":
                d = rng.uniform(0.12, 0.22)
                parts.append(chirp(f, [(d * 0.45, f * rng.uniform(0.55, 0.75)), (d * 0.55, f * rng.uniform(1.15, 1.5))]))
            elif shape == "trill":
                parts.append(trill(f, f * rng.uniform(1.2, 1.5), rng.uniform(0.12, 0.22), rng.uniform(22, 34)))
            elif shape == "warble":
                parts.append(beep(f, rng.uniform(0.14, 0.24), vibrato=rng.uniform(0.05, 0.1), vibrato_rate=rng.uniform(18, 30)))
            else:
                parts.append(slide(pick(380, 520), pick(260, 340), rng.uniform(0.09, 0.14), buzz=0.6))
            parts.append(rng.uniform(0.02, 0.09))
        return lowpass(say(*parts[:-1]), 7000)

    if kind == "alert":
        # "Whoop! bip-bip-bip": a fast rising whistle, then sharp high staccato.
        low, top = pick(520, 800), pick(2300, 3300)
        whoop = chirp(low, [(rng.uniform(0.11, 0.18), top), (0.05, top)], buzz=0.2)
        parts = [whoop, 0.03]
        if rng.random() < 0.4:
            parts += [chirp(low * 1.1, [(0.1, top * 1.05), (0.04, top * 1.05)], buzz=0.2), 0.03]
        step = rng.choice([1.0, 1.0, 1.12, 0.89])
        f = top * rng.uniform(0.75, 0.95)
        for _ in range(rng.randint(2, 4)):
            parts += [beep(f, 0.045, buzz=0.3), 0.03]
            f *= step
        return lowpass(say(*parts[:-1]), 8000)

    if kind == "orders":
        # Barked commands: clipped, buzzy, evenly spaced beeps on two or three pitches, with a firm full stop.
        base = pick(520, 1000)
        pitches = [base, base * rng.choice([1.25, 1.33, 1.5]), base * rng.choice([0.75, 0.8, 2.0])]
        length, gap = rng.uniform(0.055, 0.08), rng.uniform(0.02, 0.035)
        parts = []
        for _ in range(rng.randint(4, 7)):
            f = rng.choice(pitches)
            if rng.random() < 0.2:
                parts.append(beep(f, length * 2.2, buzz=0.7, vibrato=0.04, vibrato_rate=35))  # a rolled "rrr"
            else:
                parts.append(beep(f, length, buzz=0.7))
            parts.append(gap)
        if rng.random() < 0.6:
            parts.append(slide(base * 1.5, base * 0.7, rng.uniform(0.1, 0.15), buzz=0.7))
        else:
            parts += [beep(base * 2.0, 0.05, buzz=0.7), gap, beep(base * 2.0, 0.09, buzz=0.7)]
        return lowpass(say(*parts), 6500)

    if kind == "pain":
        # A yelp: the pitch shoots up and collapses, with a rough edge. Some are a double "ow-ow".
        def yelp(f, seconds):
            return chirp(f, [(0.035, f * rng.uniform(2.0, 2.8)), (seconds, f * rng.uniform(0.45, 0.7))],
                         fm=rng.uniform(0.8, 1.8), fm_ratio=rng.choice([1.41, 2.0, 2.76]), vibrato=0.06, vibrato_rate=rng.uniform(30, 45))
        f = pick(850, 1400)
        parts = [yelp(f, rng.uniform(0.13, 0.26))]
        if rng.random() < 0.4:
            parts += [0.03, yelp(f * rng.uniform(0.8, 0.95), rng.uniform(0.1, 0.16))]
        zap = env(highpass(noise(0.05, rng), 2500), 0.0, curve=7)
        return lowpass(mix(say(*parts), (zap, 0, 0.25)), 7500)

    if kind == "death":
        # "Aaaah!": shrieks up to a high wail that wobbles more and more as it falls away, then the voice
        # box runs down in a few slowing, sinking blips.
        f = pick(900, 1300)
        top = pick(2500, 3300)
        fall = rng.uniform(0.9, 1.4)
        bottom = pick(170, 260)
        wail = chirp(f, [(0.07, top), (rng.uniform(0.15, 0.3), top * rng.uniform(0.9, 1.0)), (fall, bottom)],
                     vibrato=(0.01, rng.uniform(0.12, 0.2)), vibrato_rate=rng.uniform(9, 14), fm=rng.uniform(0.3, 0.8), fm_ratio=2.0,
                     buzz=0.25, release=0.03)
        # Fade the wail as it falls, so it dies away instead of stopping.
        n = len(wail)
        wail = [x * (1.0 - 0.6 * max(0.0, i / n - 0.35) / 0.65) for i, x in enumerate(wail)]
        parts = [wail]
        gap, f = 0.04, bottom * rng.uniform(1.4, 1.9)
        for i in range(rng.randint(2, 4)):
            parts += [gap, gain(slide(f, f * 0.8, 0.06, buzz=0.8), 0.5 - 0.1 * i)]
            gap *= 1.6
            f *= 0.8
        return lowpass(say(*parts), 7000)

    if kind == "laugh":
        # "Heh-heh-heh-heh": bouncy little up-down blips that seesaw between two pitches as they tumble down.
        f = pick(1100, 1900)
        low = rng.random() < 0.3  # now and then a deeper, buzzier chuckle
        if low:
            f *= 0.5
        bounce = rng.uniform(1.15, 1.3)
        fall = rng.uniform(0.9, 0.96)
        length, gap = rng.uniform(0.055, 0.075), rng.uniform(0.035, 0.055)
        parts = []
        for i in range(rng.randint(4, 7)):
            note = f * (bounce if i % 2 == 0 else 1.0)
            parts += [chirp(note, [(length * 0.35, note * 1.3), (length * 0.65, note * 0.78)], buzz=0.5 if low else 0.15), gap]
            f *= fall
        if rng.random() < 0.5:
            parts.append(slide(f, f * rng.uniform(1.6, 2.2), rng.uniform(0.1, 0.16), buzz=0.15))  # a gleeful upward flick
        else:
            parts.pop()
        return lowpass(say(*parts), 7500)

    if kind == "confused":
        # Dazed: a seasick, wobbling moan that wanders and ends on a rising "...huh?", with a stuck stutter.
        f = pick(520, 850)
        woozy = chirp(f, [(rng.uniform(0.2, 0.3), f * rng.uniform(1.25, 1.5)), (rng.uniform(0.18, 0.28), f * rng.uniform(0.7, 0.9)),
                          (rng.uniform(0.22, 0.32), f * rng.uniform(1.1, 1.4))],
                      vibrato=(0.05, rng.uniform(0.12, 0.2)), vibrato_rate=rng.uniform(5, 8), fm=0.3, fm_ratio=0.5)
        sf = pick(700, 1500)
        stutter = []
        for _ in range(rng.randint(3, 5)):
            stutter += [beep(sf * rng.choice([1.0, 1.0, 1.0, 1.06, 0.94]), 0.03, buzz=0.4), 0.025]
        q = pick(600, 900)
        huh = say(beep(q, 0.06), 0.03, chirp(q * 1.1, [(rng.uniform(0.16, 0.24), q * rng.uniform(1.7, 2.2))], vibrato=0.03, vibrato_rate=7))
        order = rng.choice([[woozy, 0.06, say(*stutter), 0.05, huh], [say(*stutter), 0.05, woozy, 0.07, huh], [woozy, 0.07, huh, 0.06, say(*stutter[:-1])]])
        return lowpass(say(*order), 6500)

    raise KeyError(kind)


# How many variants of each mood to make. Gameplay picks one at random (MetalAudio::PickVariant), so more
# can be added here without touching the code.
VOICES = {"chatter": 8, "alert": 5, "orders": 6, "pain": 6, "death": 5, "laugh": 5, "confused": 5}
VOICE_SOUNDS = [f"voice_{kind}{i}" for kind, count in VOICES.items() for i in range(1, count + 1)]


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
        # ~1.1 s, to fit the default 1.2 s ReloadTime: release latch, the spent cell slides out (servo whir),
        # a fresh cell clunks home, then the charging handle racks and the arm powers back up.
        def tick(length, cutoff, partials, ring_gain):
            return mix(env(highpass(noise(length, rng), cutoff), 0.0, curve=9), (ring(0.12, partials, rng, 9), 0, ring_gain))
        release = tick(0.03, 2500, [2600, 3900], 0.35)
        slide_out = env(bandpass(noise(0.18, rng), 900, 4500), 0.02, curve=3)
        whir = osc(0.3, 260, "square", 380, vibrato=0.04, vibrato_rate=28)
        whir = lowpass([x * math.sin(math.pi * i / len(whir)) for i, x in enumerate(whir)], 1600)
        seat = drive(mix(env(osc(0.14, 150, "sine", 70), 0.001, curve=6), (env(lowpass(noise(0.05, rng), 2500), 0.0, curve=8), 0, 0.8),
                         (ring(0.2, [820, 1750, 2900], rng, 7), 0, 0.45)), 2.0)
        rack_back = tick(0.04, 1500, [1400, 2300], 0.5)
        rack_fwd = mix(tick(0.05, 1200, [1100, 2100, 3300], 0.6), (env(osc(0.08, 120, "sine", 60), 0.001, curve=6), 0, 0.6))
        charge = env(osc(0.22, 500, "saw", 1900, curve=0.5), 0.02, curve=4)
        return mix((release, 0.0), (slide_out, 0.03, 0.45), (whir, 0.08, 0.3), (seat, 0.48),
                   (rack_back, 0.72), (rack_fwd, 0.86), (lowpass(charge, 3200), 0.9, 0.18))
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
        # A bright rising chime over a soft mechanical clack: the item is grabbed and stowed.
        clack = mix(env(highpass(noise(0.03, rng), 1800), 0.0, curve=9), (ring(0.1, [2200, 3400], rng, 9), 0, 0.3))
        chime = tones([784, 1175, 1568], "triangle", 0.065, 0.0, 4)
        shimmer = env(mix(osc(0.45, 2349), gain(osc(0.45, 3136), 0.5)), 0.005, curve=5)
        swoosh = env(bandpass(noise(0.18, rng), 1500, 6000), 0.06, curve=4)
        return mix((clack, 0, 0.6), (swoosh, 0, 0.25), (chime, 0.02, 0.8), (shimmer, 0.2, 0.3))
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
    if name == "low_health":
        # Two quick high beeps, repeated by the player's robot every second while health is low.
        return crush(tones([1568, 1568], "square", 0.07, 0.06, 3), 7, 2)
    if name == "radio_whistle_loop":
        # Shortwave "radio waves" under dialogue: a pure whistle that never settles, zig-zagging in straight
        # sweeps between a low valley (~480-850 Hz) and a high peak (~1.1-3 kHz) about three times a second -
        # a quick rise, a slower fall, sometimes a kink or a small bump - over a few steady, flickering
        # carrier tones and a band-limited hiss. 12 s of random sweeps so the loop doesn't sound repetitive;
        # the game also starts it at a random point and pitch each time.
        length = 12.0
        n = int(length * RATE)

        def valley():
            return math.exp(rng.uniform(math.log(480.0), math.log(850.0)))

        start_f = valley()
        points = [(0.0, start_f)]
        t, f = 0.0, start_f
        while t < length - 0.6:
            if rng.random() < 0.2:  # small bump up and back down
                t += rng.uniform(0.06, 0.14)
                points.append((t, rng.uniform(750.0, 1000.0)))
                t += rng.uniform(0.06, 0.14)
                f = valley()
                points.append((t, f))
                continue
            t += rng.uniform(0.03, 0.1)  # fast rise
            peak = math.exp(rng.uniform(math.log(1100.0), math.log(3200.0)))
            points.append((t, peak))
            if rng.random() < 0.5:  # the fall bends partway down
                t += rng.uniform(0.04, 0.12)
                points.append((t, peak * rng.uniform(0.5, 0.8)))
            t += rng.uniform(0.06, 0.22)  # slower fall
            f = valley()
            points.append((t, f))
            if rng.random() < 0.3:  # brief rest in the valley
                t += rng.uniform(0.02, 0.08)
                points.append((t, f * rng.uniform(0.9, 1.1)))
        # Squeeze the sweeps to fit, and end back at the starting pitch, so the loop point is continuous.
        points = [(pt * (length - 0.25) / t, pf) for pt, pf in points]
        points.append((length, start_f))
        freqs = [0.0] * n
        seg = 0
        for i in range(n):
            ti = i / RATE
            while seg < len(points) - 2 and ti >= points[seg + 1][0]:
                seg += 1
            (t0, f0), (t1, f1) = points[seg], points[seg + 1]
            u = 0.0 if t1 <= t0 else min(1.0, max(0.0, (ti - t0) / (t1 - t0)))
            freqs[i] = f0 + (f1 - f0) * u  # straight sweeps in Hz, sharp corners
        # Nudge every frequency so the whistle completes a whole number of cycles: the loop then has no click.
        cycles = sum(freqs) / RATE
        scale = round(cycles) / cycles
        tone = [0.0] * n
        phase = 0.0
        for i, fi in enumerate(freqs):
            phase += fi * scale / RATE
            tone[i] = math.sin(TWO_PI * phase)
        # Steady carriers that flicker. Whole-Hz pitches and whole-cycles-per-loop flicker rates loop cleanly.
        carriers = [(528, 0.05), (786, 0.12), (1044, 0.12), (1120, 0.04), (1190, 0.03), (2875, 0.03)]
        for cf, amp in carriers:
            wobble = [(k / length, rng.uniform(0.0, TWO_PI)) for k in rng.sample(range(10, 60), 3)]
            for i in range(n):
                ti = i / RATE
                flick = sum(math.sin(TWO_PI * wf * ti + wp) for wf, wp in wobble) / 3.0
                tone[i] += amp * (0.5 + 0.5 * flick) * math.sin(TWO_PI * cf * ti)
        # Hiss: hard-cut below ~450 Hz, a hotter band around 3.3-4.8 kHz, little above 6 kHz (made a little
        # long and crossfaded to loop).
        raw = noise(length + 0.5, rng)
        body = lowpass(lowpass(highpass(highpass(highpass(highpass(raw, 500), 500), 500), 500), 3000), 3000)
        upper = lowpass(lowpass(lowpass(highpass(highpass(raw, 3300), 3300), 4800), 4800), 4800)
        hiss = loopable(lowpass(lowpass(mix((body, 0, 0.7), (upper, 0, 5.0)), 5500), 5500), 0.5)[:n]
        return mix(tone, (hiss, 0, 1.0))
    if name == "console_beep":
        # Terminal chatter, "beep-boop": a quick run of square-wave blips that hop between pitches, a short
        # data trill, and a final low "boop". The console's granted/denied tone follows at 0.7 s.
        notes = [(1320, 0.06), (990, 0.06), (1760, 0.05), (1175, 0.07), (0, 0.03), (1568, 0.045), (1397, 0.045)]
        parts, t = [], 0.0
        for f, length in notes:
            if f:
                parts.append((env(osc(length, f, "square"), 0.002, hold=length * 0.5, curve=5), t, 0.6))
            t += length + 0.012
        trill = osc(0.1, 2100, "square", vibrato=0.25, vibrato_rate=60)
        parts.append((env(trill, 0.002, hold=0.05, curve=4), t, 0.35))
        t += 0.11
        parts.append((env(osc(0.13, 620, "square", 440, curve=0.7), 0.003, hold=0.05, curve=4), t, 0.7))
        return crush(lowpass(mix(*parts), 5000), 7, 2)
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
    if name == "dispenser_drill":
        # ~0.4 s (one part of the robot dispenser's 2 s build): a power drill spinning up, biting with a
        # chattering whine, and spinning down.
        n = int(0.42 * RATE)
        motor = osc(0.42, 520, "saw", 880, curve=0.4, vibrato=0.03, vibrato_rate=37)
        bite = [0.55 + 0.45 * abs(math.sin(TWO_PI * 19 * i / RATE)) for i in range(n)]
        whine = [x * b for x, b in zip(lowpass(motor, 3800), bite)]
        grit = [x * b for x, b in zip(bandpass(noise(0.42, rng), 1800, 6000), bite)]
        shape = [min(1.0, i / (0.05 * RATE)) * min(1.0, (n - i) / (0.08 * RATE)) for i in range(n)]
        return drive([v * g for v, g in zip(mix((whine, 0, 0.8), (grit, 0, 0.45)), shape)], 1.8)
    if name == "dispenser_hammer":
        # A part hammered home: a dull thump, a bright metal clang and a short ring-out.
        thump = env(osc(0.25, 120, "sine", 48, curve=0.5), 0.001, curve=7)
        clang = ring(0.45, [610, 1490, 2370, 3650, 5100], rng, 6)
        click = env(highpass(noise(0.03, rng), 2000), 0.0, curve=8)
        return drive(mix((thump, 0, 1.0), (clang, 0, 0.6), (click, 0, 0.5)), 2.2)
    if name == "dispenser_shutdown":
        # The dispenser powering down for good: the motor winds down, sputters, and a relay clunks off.
        wind = env(osc(1.3, 420, "saw", 35, curve=0.6, vibrato=0.05, vibrato_rate=9), 0.01, curve=2)
        sputter = [x if (i // 1500) % 3 else 0.0 for i, x in enumerate(env(bandpass(noise(1.0, rng), 300, 2500), 0.0, curve=3))]
        clunk = mix(env(osc(0.3, 90, "sine", 40), 0.001, curve=6), (ring(0.4, [430, 1170, 1990], rng, 6), 0, 0.5))
        return drive(mix((lowpass(wind, 2200), 0, 0.8), (sputter, 0.1, 0.3), (clunk, 1.15, 1.0)), 1.8)
    if name.startswith("voice_"):
        return robot_voice(name[len("voice_"):].rstrip("0123456789"), rng)
    raise KeyError(name)


SOUNDS = [
    "laser_fire", "enemy_laser_fire", "repeater_fire", "shotgun_fire", "sniper_fire", "rocket_fire", "blaster_fire",
    "minigun_fire", "reload", "impact", "robot_hit", "player_hit", "robot_explode", "explosion", "emp_blast",
    "grenade_throw", "footstep", "footstep_sprint", "footstep_heavy", "jump", "double_jump", "land",
    "lift_hum_loop", "lift_enter", "lift_exit", "swap_servo", "swap_detach", "swap_rack", "swap_attach",
    "alert", "suspicion", "tether_link", "tether_release", "tether_fail", "console_granted", "console_denied",
    "gate_open", "pickup", "checkpoint", "objective_complete", "mission_complete", "player_down", "radio_blip",
    "ui_move", "ui_select", "ambience_loop", "battle_loop",
    # New sounds go last: each sound's random seed is its index, so inserting would change the others.
    "low_health", "radio_whistle_loop", "console_beep",
] + VOICE_SOUNDS + [
    # Added after the voices so their seeds (= list index) stay the same.
    "dispenser_drill", "dispenser_hammer", "dispenser_shutdown",
]


# Library name -> file in Art/Music. Names ending _loop loop. Credit every track in CREDITS.txt.
MUSIC = {
    "music_background_loop": "MaksymMalko_GameBackgroundMusic.mp3",
}


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
    music_dir = os.path.join(here, "..", "..", "Art", "Music")
    copied = 0
    for name, file in MUSIC.items():
        source = os.path.join(music_dir, file)
        if not os.path.exists(source):
            print(f"[MZSfx] WARNING music {file} missing from Art/Music - no {name}", flush=True)
            continue
        shutil.copyfile(source, os.path.join(out_dir, name + os.path.splitext(file)[1]))
        copied += 1
    print(f"[MZSfx] DONE {len(SOUNDS)} sounds, {copied} of {len(MUSIC)} music -> {os.path.abspath(out_dir)}", flush=True)


if __name__ == "__main__":
    main()
