"""Arithmetic of tools/power_budget.py on a hand-checkable model, and docs/POWER.md up to date."""
import importlib.util
import os
import sys
import unittest

ROOT = sys.argv.pop(1) if len(sys.argv) > 1 else os.getcwd()
spec = importlib.util.spec_from_file_location("power_budget", os.path.join(ROOT, "tools", "power_budget.py"))
pb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pb)

MODEL = {
    "battery": {"capacity_mah": 1000, "usable_fraction": 1.0},
    "sleep_ua": {"mcu": {"ua": 2.0}, "acc": {"ua": 3.0}},
    "modem_sleep_ua": {"off": {"ua": 0.0}, "psm": {"ua": 5.0}},
    "phases": {"tx": {"s": 6.0, "ma": 100.0}, "mcu_awake": {"ma": 0.0}},
    "strategies": {"off": {"label": "off", "phases": ["tx"]}, "psm": {"label": "psm", "phases": ["tx"]}},
    "intervals_min": [1],
}


class PowerBudget(unittest.TestCase):
    def test_floor(self):
        self.assertAlmostEqual(pb.sleep_floor_ua(MODEL, "off"), 5.0)
        self.assertAlmostEqual(pb.sleep_floor_ua(MODEL, "psm"), 10.0)

    def test_average(self):
        # 600 mC every 60 s = 10 mA; the floor applies for 54 of the 60 s: 5 uA * 0.9 = 4.5 uA
        self.assertAlmostEqual(pb.average_ua(MODEL, "off", 1), 10000.0 + 4.5)

    def test_life(self):
        # 1000 mAh at 1 mA = 1000 h
        self.assertAlmostEqual(pb.life_years(MODEL, 1000.0), 1000.0 / (24 * 365.25))

    def test_doc_tables_current(self):
        self.assertEqual(0, pb.main(["power_budget.py", "--check", os.path.join(ROOT, "docs", "POWER.md")]))


if __name__ == "__main__":
    unittest.main()
