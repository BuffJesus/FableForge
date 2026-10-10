"""Regression checks for exact-name LEV inventory findings."""
import unittest
from audit_levsymbols import inventory


class SymbolInventoryTests(unittest.TestCase):
    def test_atmosphere_requires_day_theme_class(self):
        bank = [{"name": "sky", "definition": "ENVIRONMENT_THEME_DAY"},
                {"name": "ground", "definition": "ENGINE_THEME"}]
        rows = [{"file": "map.lev", "ground": [], "atmosphere": ["sky", "ground"], "sound": []}]
        result = {r["name"]: r for r in inventory(bank, rows)}
        self.assertTrue(result["sky"]["class_matches"])
        self.assertFalse(result["ground"]["class_matches"])
        self.assertEqual(result["ground"]["expected_definition"], "ENVIRONMENT_THEME_DAY")

    def test_ambiguous_names_are_not_resolved_by_class_or_first_match(self):
        bank = [{"name": "shared", "definition": "ENGINE_THEME"},
                {"name": "shared", "definition": "SOUND_THEME"}]
        rows = [{"file": "map.lev", "ground": ["shared"], "atmosphere": [], "sound": []}]
        result = inventory(bank, rows)[0]
        self.assertEqual(result["status"], "ambiguous")
        self.assertEqual([m["slot"] for m in result["matches"]], [0, 1])
        self.assertIsNone(result["class_matches"])

    def test_missing_wrong_class_and_repeated_uses(self):
        bank = [{"name": "ground", "definition": "ENGINE_THEME"},
                {"name": "wrong", "definition": "CHighlightItemDef"}]
        rows = [{"file": "a.lev", "ground": ["ground", "ground", "wrong"],
                 "atmosphere": ["absent"], "sound": ["GROUND"]},
                {"file": "b.lev", "ground": ["ground"], "atmosphere": [], "sound": []}]
        result = {(r["domain"], r["name"]): r for r in inventory(bank, rows)}
        ground = result["ground", "ground"]
        self.assertTrue(ground["class_matches"])
        self.assertEqual(ground["occurrences"], 3)
        self.assertEqual(ground["files"], ["a.lev", "b.lev"])
        self.assertFalse(result["ground", "wrong"]["class_matches"])
        self.assertEqual(result["ground", "wrong"]["status"], "unique")
        self.assertEqual(result["atmosphere", "absent"]["status"], "missing")
        self.assertEqual(result["sound", "GROUND"]["status"], "missing")


if __name__ == "__main__":
    unittest.main()
