"""Dependency-free guards for the APWorld/C++ location ID catalog."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCATIONS = (ROOT / "archipelago" / "sonic_unleashed" / "Locations.py").read_text(encoding="utf-8")
CLIENT = (ROOT / "UnleashedRecomp" / "archipelago_client.cpp").read_text(encoding="utf-8")

def catalog():
    return [(m.group(1), int(m.group(2)))
            for m in re.finditer(r"""['"]([^'"]+)['"]\s*:\s*(\d+)""", LOCATIONS)]

def rank_bases():
    part = CLIENT.split("kRankGroupBases[]", 1)[1].split("bool GetRankLocationGroup", 1)[0]
    return {int(m.group(1)) for m in re.finditer(r"(\d+)LL", part)}

def dlc_ranges():
    part = CLIENT.split("bool IsDlcLocation(", 1)[1].split("bool IsLocationAllowed(", 1)[0]
    return [(int(a), int(b)) for a, b in
            re.findall(r"\(id >= (\d+)LL && id <= (\d+)LL\)", part)]

class TestCatalog(unittest.TestCase):
    def test_every_rank_d_has_a_group(self):
        bases = rank_bases()
        rank_ids = {num for name, num in catalog() if name.endswith(" - Rank D")}
        self.assertEqual(rank_ids, bases)

    def test_dlc_filter_matches_apworld(self):
        ranges = dlc_ranges()
        for name, num in catalog():
            is_dlc = "(DLC)" in name
            filtered = any(lo <= num <= hi for lo, hi in ranges)
            self.assertEqual(is_dlc, filtered, f"{name} ({num})")

if __name__ == "__main__":
    unittest.main()
