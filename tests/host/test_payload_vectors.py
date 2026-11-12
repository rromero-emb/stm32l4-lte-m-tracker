#!/usr/bin/env python3
"""Cross-checks the C codec (payload_cli) and the Python reference (tools/payload.py)
against tests/vectors/payload.json. Usage: test_payload_vectors.py <payload_cli> <repo root>"""
import json
import os
import subprocess
import sys

cli, root = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(root, "tools"))
import payload  # noqa: E402

ORDER = ["type", "fix", "moving", "seq", "unix_time", "lat_e7", "lon_e7", "alt_m", "speed_kmh", "heading_deg",
         "hacc_m", "nsat", "ttff_s", "vbat_mv", "temp_c", "rsrp_dbm"]

vectors = json.load(open(os.path.join(root, "tests", "vectors", "payload.json")))
failures = 0
for v in vectors:
    args = [str(int(v[k])) for k in ORDER]
    c_hex = subprocess.run([cli, "encode", *args], capture_output=True, text=True, check=True).stdout.strip()
    py_hex = payload.encode(v).hex()
    c_dec = [int(x) for x in subprocess.run([cli, "decode", v["hex"]], capture_output=True, text=True,
                                             check=True).stdout.split()]
    py_dec = [int(payload.decode(bytes.fromhex(v["hex"]))[k]) for k in ORDER]
    exp_dec = [int(v["decoded"][k]) for k in ORDER]
    ok = c_hex == v["hex"] == py_hex and c_dec == py_dec == exp_dec
    print(f"{'PASS' if ok else 'FAIL'} {v['name']}: C {c_hex} / Py {py_hex} / vector {v['hex']}")
    if not ok:
        print(f"     decoded C {c_dec}\n     decoded Py {py_dec}\n     expected   {exp_dec}")
        failures += 1
sys.exit(1 if failures else 0)
