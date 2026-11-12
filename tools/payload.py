#!/usr/bin/env python3
"""Reference encoder/decoder for the tracker payload (docs/PAYLOAD.md).

    python3 tools/payload.py decode 4a2b...        # 20-byte report as hex -> JSON
    python3 tools/payload.py encode '{"lat_e7": ...}'

Integer arithmetic only, mirroring app/src/payload.c bit for bit.
"""
import json
import sys

VERSION = 1
EPOCH_UNIX = 1735689600  # 2025-01-01T00:00:00Z
LEN = 20

# (name, width) in transmission order, MSB first
FIELDS = [
    ("version", 2), ("type", 3), ("fix", 2), ("moving", 1), ("seq", 8), ("time", 32),
    ("lat", 24), ("lon", 24), ("alt", 11), ("speed", 8), ("heading", 6), ("hacc", 5),
    ("nsat", 4), ("ttff", 5), ("vbat", 8), ("temp", 7), ("rsrp", 6),
]
TYPES = ["periodic", "motion_start", "motion_stop", "heartbeat", "alarm"]
FIXES = ["none", "2d", "3d", "last_known"]


def _clamp(v, width):
    return max(0, min(v, (1 << width) - 1))


def _trunc_div(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def _scale(v, lo, span, width):
    return _clamp(((v - lo) << width) // span, width)


def _unscale(q, lo, span, width):
    return lo + (((2 * q + 1) * span) >> (width + 1))


def encode(r):
    if r["unix_time"] < EPOCH_UNIX or abs(r["lat_e7"]) > 900000000 or abs(r["lon_e7"]) > 1800000000:
        raise ValueError("invalid report")
    q = {
        "version": VERSION,
        "type": _clamp(r["type"], 3),
        "fix": _clamp(r["fix"], 2),
        "moving": 1 if r["moving"] else 0,
        "seq": r["seq"] & 0xFF,
        "time": r["unix_time"] - EPOCH_UNIX,
        "lat": _scale(r["lat_e7"], -900000000, 1800000000, 24),
        "lon": _scale(r["lon_e7"], -1800000000, 3600000000, 24),
        "alt": _clamp(_trunc_div(r["alt_m"] + 500, 4), 11),
        "speed": _clamp(r["speed_kmh"], 8),
        "heading": _clamp(((r["heading_deg"] % 360) << 6) // 360, 6),
        "hacc": _clamp(r["hacc_m"] // 2, 5),
        "nsat": _clamp(r["nsat"], 4),
        "ttff": _clamp(r["ttff_s"] // 2, 5),
        "vbat": _clamp(_trunc_div(r["vbat_mv"] - 2000, 10), 8),
        "temp": _clamp(r["temp_c"] + 40, 7),
        "rsrp": _clamp(r["rsrp_dbm"] + 140, 6),
    }
    acc, nbits = 0, 0
    for name, width in FIELDS:
        acc = (acc << width) | q[name]
        nbits += width
    acc <<= LEN * 8 - nbits  # padding
    return acc.to_bytes(LEN, "big")


def decode(data):
    if len(data) != LEN:
        raise ValueError(f"expected {LEN} bytes, got {len(data)}")
    acc = int.from_bytes(data, "big")
    shift = LEN * 8
    q = {}
    for name, width in FIELDS:
        shift -= width
        q[name] = (acc >> shift) & ((1 << width) - 1)
    if q["version"] != VERSION:
        raise ValueError(f"unknown payload version {q['version']}")
    return {
        "type": q["type"],
        "fix": q["fix"],
        "moving": bool(q["moving"]),
        "seq": q["seq"],
        "unix_time": q["time"] + EPOCH_UNIX,
        "lat_e7": _unscale(q["lat"], -900000000, 1800000000, 24),
        "lon_e7": _unscale(q["lon"], -1800000000, 3600000000, 24),
        "alt_m": q["alt"] * 4 - 500,
        "speed_kmh": q["speed"],
        "heading_deg": (q["heading"] * 360) >> 6,
        "hacc_m": q["hacc"] * 2,
        "nsat": q["nsat"],
        "ttff_s": q["ttff"] * 2,
        "vbat_mv": q["vbat"] * 10 + 2000,
        "temp_c": q["temp"] - 40,
        "rsrp_dbm": q["rsrp"] - 140,
    }


def main(argv):
    if len(argv) == 3 and argv[1] == "decode":
        r = decode(bytes.fromhex(argv[2]))
        r["type_name"] = TYPES[r["type"]] if r["type"] < len(TYPES) else "?"
        r["fix_name"] = FIXES[r["fix"]]
        r["lat"] = r["lat_e7"] / 1e7
        r["lon"] = r["lon_e7"] / 1e7
        print(json.dumps(r, indent=2))
        return 0
    if len(argv) == 3 and argv[1] == "encode":
        print(encode(json.loads(argv[2])).hex())
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
