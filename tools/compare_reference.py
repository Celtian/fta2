#!/usr/bin/env python3
"""Compare pixels, controlled simulation checkpoints and original save chunks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def chunks(path):
    data = path.read_bytes()
    assert data[:4] == b"FTA2", f"Invalid save: {path}"
    pos, result = 128, {}
    while pos < len(data):
        assert pos + 8 <= len(data), f"Truncated save: {path}"
        tag, length = struct.unpack_from("<4sI", data, pos)
        assert length <= len(data) - pos - 8, f"Invalid chunk: {path}"
        assert tag not in result, f"Duplicate chunk: {path}"
        result[tag] = data[pos + 8:pos + 8 + length]
        pos += 8 + length
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-output", type=Path, required=True)
    parser.add_argument("--reference-saves", type=Path, required=True)
    parser.add_argument("--native-output", type=Path, required=True)
    parser.add_argument("--native-saves", type=Path, required=True)
    args = parser.parse_args()
    report = {"screenshots": {}, "simulation": {}, "save_chunks": {}}
    for name in ["initial.ppm", "controlled.ppm"]:
        reference = (args.reference_output / name).read_bytes()
        native = (args.native_output / name).read_bytes()
        assert reference == native, f"Screenshot differs: {name}"
        report["screenshots"][name] = hashlib.sha256(native).hexdigest()
    log = (args.reference_output / "reference.log").read_text()
    states = {s["phase"]: s for s in (json.loads(line) for line in (args.native_output / "state.jsonl").read_text().splitlines())}
    pattern = r"REFERENCE_PROBE (\w+) frame=(\d+) actor=(\d+) u=(-?\d+) v=(-?\d+) z=(-?\d+) rng=(\d+)"
    for phase, *values in re.findall(pattern, log):
        expected = dict(zip(["frame", "actor", "u", "v", "z", "rng"], map(int, values)))
        assert all(states[phase][key] == val for key, val in expected.items()), f"Simulation differs: {phase}"
        report["simulation"][phase] = expected
    assert set(report["simulation"]) == {"initial", "moved", "switched", "saved"}, "Missing reference checkpoints"
    for name in ["fta2.999", "fta2.001"]:
        reference, native = chunks(args.reference_saves / name), chunks(args.native_saves / name)
        reference.pop(b"SCVM", None)
        native.pop(b"NRNG", None)
        assert reference == native, f"World save chunks differ: {name}"
        report["save_chunks"][name] = len(reference)
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
