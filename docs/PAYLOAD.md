# Report payload (version 1)

20 bytes, bit-packed MSB first: 156 bits of fields plus 4 zero padding bits. The format is small enough to fit any of the planned links:
- a UDP datagram over LTE-M;
- an NB-IoT NTN satellite message;
- an Iridium SBD message.

| # | Field | Bits | Encoding | Range / resolution |
|---|---|---|---|---|
| 1 | version | 2 | `1` | decoder rejects other values |
| 2 | type | 3 | 0 periodic, 1 motion start, 2 motion stop, 3 heartbeat, 4 alarm | |
| 3 | fix | 2 | 0 none, 1 2D, 2 3D, 3 last known | |
| 4 | moving | 1 | flag | |
| 5 | seq | 8 | counter | de-duplicates a report sent over two links |
| 6 | time | 32 | seconds since 2025-01-01T00:00:00Z | to 2161 |
| 7 | lat | 24 | ⌊(lat + 90°) · 2²⁴ / 180°⌋ | 10.7e-6° ≈ 1.2 m |
| 8 | lon | 24 | ⌊(lon + 180°) · 2²⁴ / 360°⌋ | 21.5e-6° ≈ 2.4 m at the equator |
| 9 | alt | 11 | (alt + 500 m) / 4 m | −500 … 7688 m, 4 m |
| 10 | speed | 8 | km/h | 0 … 255 |
| 11 | heading | 6 | deg · 64 / 360 | 5.6° steps |
| 12 | hacc | 5 | m / 2 | 0 … 62 m (31 = 62 m or worse) |
| 13 | nsat | 4 | satellites used | 0 … 15 |
| 14 | ttff | 5 | s / 2 | 0 … 62 s (31 = timeout) |
| 15 | vbat | 8 | (mV − 2000) / 10 | 2.00 … 4.55 V |
| 16 | temp | 7 | °C + 40 | −40 … 87 °C |
| 17 | rsrp | 6 | dBm + 140 | −140 … −77 dBm |

Encoding rules:
- Values outside a field's range are **clamped**, not wrapped.
- Lat/lon outside ±90°/±180° and times before the epoch are rejected.
- Decoders return the **centre** of each lat/lon step.
- All arithmetic is integer, so no FPU code is needed on the device.

## Implementations

| | Code | Checked by |
|---|---|---|
| Device (C) | `app/src/payload.c` | `tests/host/test_payload.c` (round trip, layout, clamping, poles/antimeridian, invalid input) |
| Reference (Python) | `tools/payload.py`, which also works as a CLI: `python3 tools/payload.py decode <hex>` | |
| Shared vectors | `tests/vectors/payload.json` | `tests/host/test_payload_vectors.py`: the C encoder, the C decoder and the Python decoder must all match every vector byte for byte |

Example: the console command `payload` prints the report below, and the Python tool decodes it.

```
$ python3 tools/payload.py decode 452a019fe7e0b97b4c7d5dbe2426180e4f76e6a0
{ "type": 0, "fix": 2, "moving": true, "seq": 42, "unix_time": 1762946400,
  "lat_e7": 404167753, "lon_e7": -37037981, "alt_m": 656, "speed_kmh": 48, "heading_deg": 270, ... }
```
