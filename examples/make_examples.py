#!/usr/bin/env python3
"""Writes the example patches. Module ports and knobs come from
`SIGNALPATCH_DUMP_NODES=1 signalpatch_tests`; knobs are addressed by id, cable
ports by index. Run from the repository root: python3 examples/make_examples.py"""
import json, os

HW_IN, HW_OUT = 1, 2
HERE = os.path.dirname(os.path.abspath(__file__))


class Patch:
    def __init__(self):
        self.nodes, self.cables, self.next_id, self.groups = [], [], 100, []
        self.nodes.append({"id": HW_IN, "kind": "hardware-input", "name": "Hardware Inputs", "x": 60, "y": 200, "parameters": []})
        self.nodes.append({"id": HW_OUT, "kind": "hardware-output", "name": "Hardware Outputs", "x": 0, "y": 200, "parameters": []})

    def add(self, kind, name, x, y, knobs=None, shapes=None, depths=None):
        """knobs: {id: value}; shapes: {id: (min, max, curve)}; depths: {id: mod depth 0..1}"""
        node_id = self.next_id
        self.next_id += 1
        parameters = []
        for key in dict.fromkeys(list(knobs or {}) + list(shapes or {}) + list(depths or {})):
            entry = {"id": key}
            if shapes and key in shapes:
                entry["min"], entry["max"], entry["curve"] = shapes[key]
            entry["value"] = (knobs or {}).get(key, (shapes or {}).get(key, (0, 0, 0))[0])
            entry["depth"] = (depths or {}).get(key, 0.0)
            parameters.append(entry)
        self.nodes.append({"id": node_id, "kind": kind, "name": name, "x": x, "y": y, "parameters": parameters})
        return node_id

    def cable(self, source, source_port, destination, destination_port):
        self.cables.append({"sourceNode": source, "sourcePort": source_port,
                            "destinationNode": destination, "destinationPort": destination_port})

    def to_outputs(self, source, x):
        """Every patch ends in a limiter into outputs 1 and 2."""
        limiter = self.add("limiter", "Limiter", x, 200, {"ceiling": -1.0, "release": 80})
        self.cable(source, 0, limiter, 0)
        self.cable(limiter, 0, HW_OUT, 0)
        self.cable(limiter, 0, HW_OUT, 1)
        self.nodes[1]["x"] = x + 300
        return limiter

    def pedal(self, name, members, knobs):
        """One pedal on the Board: a footswitch over the members and up to four knobs (node, knob index)."""
        self.groups.append({"id": len(self.groups) + 1, "name": name, "members": members,
                            "knobs": [{"node": node, "parameter": index} for node, index in knobs]})

    def write(self, filename):
        document = {"schema": 1, "format": "signalpatch", "nodes": self.nodes, "connections": self.cables, "groups": self.groups}
        with open(os.path.join(HERE, filename), "w") as handle:
            json.dump(document, handle, indent=2)
            handle.write("\n")


def clouds():
    """After Mutable Instruments Clouds: two grain clouds over the guitar, one an
    octave up, into a long reverb. Position wanders on its own and playing harder
    thickens the cloud."""
    p = Patch()
    grains = p.add("granular-cloud", "Grains", 380, 80,
                   {"position": 35, "size": 190, "density": 12, "pitch": 0, "spread": 65, "feedback": 30, "mix": 75},
                   depths={"position": 0.45, "density": 0.5})
    shimmer = p.add("granular-cloud", "Octave Grains", 380, 520,
                    {"position": 55, "size": 120, "density": 9, "pitch": 12, "spread": 80, "feedback": 20, "mix": 100},
                    depths={"position": 0.35})
    wander = p.add("random-lfo", "Wander", 60, 620, {"rate": 0.35, "smooth": 85, "depth": 100})
    touch = p.add("envelope-follower", "Touch", 60, 900, {"attack": 25, "release": 600, "gain": 18})
    mixer = p.add("mixer-4", "Blend", 700, 200, {"level-1": 0, "level-2": -9, "level-3": -60, "level-4": -60, "master": 5})
    hall = p.add("reverb", "Hall", 1000, 200, {"size": 88, "damp": 35, "mix": 45})
    p.cable(HW_IN, 0, grains, 0)
    p.cable(HW_IN, 0, shimmer, 0)
    p.cable(HW_IN, 0, touch, 0)
    p.cable(wander, 0, grains, 1)
    p.cable(wander, 0, shimmer, 1)
    p.cable(touch, 0, grains, 3)
    p.cable(grains, 0, mixer, 0)
    p.cable(shimmer, 0, mixer, 1)
    p.cable(mixer, 0, hall, 0)
    limiter = p.to_outputs(hall, 1300)
    p.pedal("Clouds", [grains, shimmer, wander, touch, mixer, hall, limiter],
            [(grains, 1), (grains, 2), (mixer, 1), (hall, 2)])  # Size, Density, octave level, reverb mix
    p.write("clouds.signalpatch")


def whammy():
    """After the DigiTech Whammy: one knob, the Treadle, bends the guitar up an
    octave. Learn it to an expression pedal. Mix at 50% gives the harmony mode."""
    p = Patch()
    treadle = p.add("macro", "Treadle", 60, 560, {"value": 0.0, "slew": 35}, shapes={"value": (0.0, 1.0, 0.0)})
    # The pitch knob's travel is 0..+12 semitones (set in the inspector); the treadle pushes it through all of it.
    bend = p.add("pitch-shifter", "Bend", 380, 200, {"pitch": 0.0, "window": 38, "mix": 100},
                 shapes={"pitch": (0.0, 12.0, 0.0)}, depths={"pitch": 1.0})
    p.cable(HW_IN, 0, bend, 0)
    p.cable(treadle, 0, bend, 1)
    limiter = p.to_outputs(bend, 700)
    p.pedal("Whammy", [treadle, bend, limiter], [(treadle, 0), (bend, 2), (bend, 1), (treadle, 1)])  # Treadle, Mix, Window, Glide
    p.write("whammy.signalpatch")


def fuzz_factory():
    """After the Z.Vex Fuzz Factory: a gated fuzz with a feedback loop around it.
    Stab is how much of the fuzz is fed back into itself; past half way it
    squeals, and Squeal tunes the pitch of that. If the loop runs away the guard
    latches off: press RESET LOOP on it."""
    p = Patch()
    gate = p.add("noise-gate", "Gate", 380, 200, {"threshold": -38, "attack": 1, "release": 60, "range": 80})
    fuzz = p.add("distortion", "Fuzz", 680, 200, {"drive": 40, "tone": 4200, "mix": 100})
    squeal = p.add("filter-svf", "Squeal", 680, 620, {"cutoff": 900, "resonance": 70, "mode": 2})
    # The fuzz has 40 dB of gain, so the loop is padded down by about as much: with
    # Stab below about 70% the loop only colours the fuzz; above that it squeals when you stop playing.
    pad = p.add("gain", "Loop Pad", 980, 620, {"gain-db": -39})
    stab = p.add("feedback-guard", "Stab", 380, 620, {"amount": 55, "ceiling": -8})
    trim = p.add("gain", "Volume", 980, 200, {"gain-db": -10})
    p.cable(HW_IN, 0, gate, 0)
    p.cable(gate, 0, fuzz, 0)
    p.cable(fuzz, 0, squeal, 0)
    p.cable(squeal, 0, pad, 0)
    p.cable(pad, 0, stab, 0)
    p.cable(stab, 0, fuzz, 0)
    p.cable(fuzz, 0, trim, 0)
    limiter = p.to_outputs(trim, 1280)
    p.pedal("Fuzz Factory", [gate, fuzz, squeal, pad, stab, trim, limiter], [(gate, 0), (fuzz, 0), (stab, 0), (squeal, 0)])
    p.write("fuzz-factory.signalpatch")


def rainbow():
    """After the EarthQuaker Devices Rainbow Machine: a short delay into a pitch
    shifter, fed back into the delay, so every repeat is shifted again and the
    echoes climb. Magic is the amount fed back."""
    p = Patch()
    lag = p.add("delay", "Tracking", 380, 480, {"time-ms": 160, "feedback": 0, "mix": 100})
    shift = p.add("pitch-shifter", "Pitch", 680, 480, {"pitch": 5, "window": 60, "mix": 100})
    magic = p.add("feedback-guard", "Magic", 520, 900, {"amount": 72, "ceiling": -6})
    wobble = p.add("chorus", "Wobble", 980, 480, {"rate": 0.4, "depth": 35, "delay": 12, "mix": 40})
    mixer = p.add("mixer-4", "Blend", 1280, 200, {"level-1": 0, "level-2": 2, "level-3": -60, "level-4": -60, "master": -3})
    p.cable(HW_IN, 0, lag, 0)
    p.cable(lag, 0, shift, 0)
    p.cable(shift, 0, magic, 0)
    p.cable(magic, 0, lag, 0)
    p.cable(shift, 0, wobble, 0)
    p.cable(HW_IN, 0, mixer, 0)
    p.cable(wobble, 0, mixer, 1)
    limiter = p.to_outputs(mixer, 1580)
    p.pedal("Rainbow", [lag, shift, magic, wobble, mixer, limiter], [(shift, 0), (lag, 0), (magic, 0), (mixer, 1)])
    p.write("rainbow-machine.signalpatch")


def slicer():
    """After the Boss Slicer: a clock steps two eight-step patterns, one chopping
    the volume and one moving a filter. Draw the patterns on the sequencers."""
    p = Patch()
    clock = p.add("clock", "Tempo", 60, 560, {"bpm": 112, "beats": 4})
    chop = p.add("step-sequencer-8", "Chop Pattern", 380, 620,
                 {"rate": 4, "glide": 6, "step-1": 1, "step-2": -1, "step-3": 1, "step-4": 1,
                  "step-5": -1, "step-6": 1, "step-7": -1, "step-8": 1})
    sweep = p.add("step-sequencer-8", "Filter Pattern", 700, 620,
                  {"rate": 4, "glide": 25, "step-1": 0.9, "step-2": 0.1, "step-3": 0.5, "step-4": 0.2,
                   "step-5": 1.0, "step-6": 0.3, "step-7": 0.7, "step-8": 0.0})
    # The gain knob travels -60..0 dB and sits at the bottom; a step at 1 opens it fully.
    vca = p.add("gain", "Chop", 380, 200, shapes={"gain-db": (-60.0, 0.0, -0.6)}, knobs={"gain-db": -60.0}, depths={"gain-db": 1.0})
    voice = p.add("filter-svf", "Filter", 700, 200, {"cutoff": 450, "resonance": 45, "mode": 0}, depths={"cutoff": 0.55})
    p.cable(clock, 1, chop, 10)   # Eighth -> Clock
    p.cable(clock, 1, sweep, 10)
    p.cable(chop, 0, vca, 1)
    p.cable(sweep, 0, voice, 1)
    p.cable(HW_IN, 0, vca, 0)
    p.cable(vca, 0, voice, 0)
    limiter = p.to_outputs(voice, 1000)
    p.pedal("Slicer", [clock, chop, sweep, vca, voice, limiter], [(clock, 0), (voice, 0), (voice, 1), (chop, 1)])
    p.write("slicer.signalpatch")


if __name__ == "__main__":
    for make in (clouds, whammy, fuzz_factory, rainbow, slicer):
        make()
