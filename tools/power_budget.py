#!/usr/bin/env python3
"""Average current and battery life of the tracker from a phase model.

  average = sleep floor + (charge of one report cycle) / (report interval)

Usage:
  power_budget.py [params.json]             # print the markdown tables
  power_budget.py --check docs/POWER.md     # fail if the tables in the doc are out of date
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BEGIN = "<!-- power_budget.py: begin -->"
END = "<!-- power_budget.py: end -->"


def sleep_floor_ua(p, strategy):
    return sum(v["ua"] for v in p["sleep_ua"].values()) + p["modem_sleep_ua"][strategy]["ua"]


def cycle(p, strategy):
    """(seconds, millicoulombs) of one report cycle; the MCU is awake for all of it."""
    ph = p["phases"]
    names = p["strategies"][strategy]["phases"]
    seconds = sum(ph[n]["s"] for n in names)
    mc = sum(ph[n]["s"] * ph[n]["ma"] for n in names) + seconds * ph["mcu_awake"]["ma"]
    return seconds, mc


def average_ua(p, strategy, interval_min):
    seconds, mc = cycle(p, strategy)
    period = interval_min * 60.0
    floor = sleep_floor_ua(p, strategy) * (period - seconds) / period
    return floor + mc * 1000.0 / period


def life_years(p, avg_ua):
    b = p["battery"]
    hours = b["capacity_mah"] * b["usable_fraction"] * 1000.0 / avg_ua
    return hours / (24 * 365.25)


def render(p):
    out = []
    out.append("| Sleep floor | µA |")
    out.append("|---|---:|")
    for name, v in p["sleep_ua"].items():
        out.append(f"| {name.replace('_', ' ')} | {v['ua']:.1f} |")
    for s in ("off", "psm"):
        out.append(f"| modem ({s}) | {p['modem_sleep_ua'][s]['ua']:.1f} |")
    for s in ("off", "psm"):
        out.append(f"| **total, {p['strategies'][s]['label']}** | **{sleep_floor_ua(p, s):.1f}** |")
    out.append("")
    out.append("| Report cycle | awake (s) | charge (mC) |")
    out.append("|---|---:|---:|")
    for s in ("off", "psm"):
        sec, mc = cycle(p, s)
        out.append(f"| {p['strategies'][s]['label']} | {sec:.0f} | {mc:.0f} |")
    out.append("")
    out.append("| Report every | power off: avg µA | years | PSM: avg µA | years |")
    out.append("|---|---:|---:|---:|---:|")
    for m in p["intervals_min"]:
        a_off = average_ua(p, "off", m)
        a_psm = average_ua(p, "psm", m)
        label = f"{m} min" if m < 60 else f"{m // 60} h"
        out.append(f"| {label} | {a_off:.0f} | {life_years(p, a_off):.1f} | {a_psm:.0f} | {life_years(p, a_psm):.1f} |")
    return "\n".join(out) + "\n"


def main(argv):
    args = [a for a in argv[1:] if a != "--check"]
    params_path = os.path.join(HERE, "power_params.json")
    doc = None
    if "--check" in argv:
        doc = args.pop(0)
    if args:
        params_path = args[0]
    with open(params_path, encoding="utf-8") as f:
        p = json.load(f)
    tables = render(p)
    if doc is None:
        sys.stdout.write(tables)
        return 0
    with open(doc, encoding="utf-8") as f:
        text = f.read()
    try:
        current = text.split(BEGIN, 1)[1].split(END, 1)[0].strip("\n") + "\n"
    except IndexError:
        print(f"{doc}: markers not found", file=sys.stderr)
        return 1
    if current != tables:
        print(f"{doc} is out of date: run tools/power_budget.py and paste the output", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
