"""Regression checks for composed native definition-name lookup candidates."""
import unittest
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from audit_defnamespace import lookup_groups, lookup_changes
from audit_levlookupplans import compare_queries
from audit_startupdefs import compare_startup
from audit_thingtext import definition_symbols, compare_symbol_queries
from test_fmp_links import crc0


class LookupTests(unittest.TestCase):
    def test_conditional_thing_fields_keep_query_identity(self):
        references = [{'thing_type':'AICreature','definition':'SHARED','file':'test.tng','line':4},
                      {'thing_type':'AICreature','definition':'SHARED','field':'OverridingBrainName',
                       'conditional':True,'file':'test.tng','line':5},
                      {'thing_type':'AICreature','definition':'','field':'OverridingBrainName',
                       'conditional':True,'file':'test.tng','line':6}]
        bank = [{'definition':'BRAIN','name':'SHARED'}]
        symbols = definition_symbols(references,bank)
        compared = compare_symbol_queries({0:('BRAIN','SHARED')},[0],symbols)
        self.assertEqual(len(symbols),3)
        self.assertEqual(len(compared),2)
        self.assertEqual([r['domain'] for r in compared],['AICreature','AICreature.OverridingBrainName'])
        self.assertNotIn('conditional',compared[0])
        self.assertTrue(compared[1]['conditional'])
        self.assertEqual(compared[0]['planned_candidates'],compared[1]['planned_candidates'])

    def test_startup_selection_uses_class_ordinal_not_name(self):
        targets = {0: ('ENVIRONMENT', 'NULLDEF_ENVIRONMENT'),
                   1: ('GLOBAL', 'other'),
                   2: ('ENVIRONMENT', 'ENVIRONMENT'),
                   3: ('ENVIRONMENT', 'ENVIRONMENT')}
        # Identical names can hide a changed fixed-ordinal selected row.
        row = compare_startup(targets, [0, 1, 3, 2])[2]
        self.assertEqual(row['status'], 'changed')
        self.assertEqual(row['source_in_planned_slots']['slot'], 3)
        self.assertEqual(row['planned']['slot'], 2)
        self.assertEqual(compare_startup(targets, [0, 1, 2, 3])[2]['status'], 'unchanged')
        self.assertEqual(compare_startup(targets, [2])[2]['status'], 'missing_source')
        with self.assertRaises(ValueError):
            compare_startup(targets, [0, 0])
        with self.assertRaises(ValueError):
            compare_startup(targets, [5])

    def test_sound_ordinal_must_fit_positive_signed_byte(self):
        targets = {i: ('SOUND_THEME', f'SOUND_{i}') for i in range(129)}
        references = [{'domain': 'sound', 'name': name, 'files': ['map.lev']}
                      for name in ('SOUND_0', 'SOUND_127', 'SOUND_128', 'MISSING')]
        result = compare_queries(targets, [0, 128], references)
        self.assertEqual(result[2]['status'], 'unique')
        self.assertTrue(result[2]['sound_indices']['source'][0]['nonzero_signed_byte_ordinal'])
        self.assertFalse(result[2]['sound_indices']['planned'][0]['nonzero_signed_byte_ordinal'])
        self.assertTrue(result[1]['sound_indices']['planned'][0]['nonzero_signed_byte_ordinal'])
        self.assertFalse(result[0]['sound_indices']['planned'][0]['nonzero_signed_byte_ordinal'])
        self.assertEqual(result[3]['sound_indices']['planned'], [])
        self.assertEqual(result[3]['status'], 'missing')

    def test_unchanged_atmosphere_candidate_can_outgrow_cell_byte(self):
        targets = {i: ('ENVIRONMENT_THEME_DAY', f'DAY_{i}') for i in range(257)}
        targets[257] = ('ENGINE_THEME', 'WRONG_CLASS')
        references = [{'domain': 'atmosphere', 'name': name, 'files': ['map.lev']}
                      for name in ('DAY_0', 'DAY_255', 'DAY_256', 'WRONG_CLASS')]
        # Source DAY_256 follows only DAY_0; intervening themes come from another
        # contributor. Its CRC candidate is unchanged, but its class ordinal grows.
        result = compare_queries(targets, [0, 256, 257], references)
        shifted = result[2]
        self.assertEqual(shifted['status'], 'unique')
        self.assertEqual(shifted['atmosphere_indices']['source'][0]['class_ordinal'], 1)
        self.assertEqual(shifted['atmosphere_indices']['planned'][0]['class_ordinal'], 256)
        self.assertTrue(shifted['atmosphere_indices']['source'][0]['nonzero_byte_ordinal'])
        self.assertFalse(shifted['atmosphere_indices']['planned'][0]['nonzero_byte_ordinal'])
        self.assertFalse(result[0]['atmosphere_indices']['planned'][0]['nonzero_byte_ordinal'])
        self.assertTrue(result[1]['atmosphere_indices']['planned'][0]['nonzero_byte_ordinal'])
        self.assertFalse(result[3]['atmosphere_indices']['planned'][0]['expected_class'])

    def test_nonempty_zero_crc_query_includes_anonymous_rows(self):
        zero_name = 'LOOKUP_ZERO_SY2CT3I'
        self.assertEqual(crc0(zero_name), 0)
        targets = {0: ('CDef', ''), 3: ('OBJECT', zero_name), 7: ('CDef', '')}
        self.assertEqual([r['slot'] for r in lookup_groups(targets)[0]], [0, 3, 7])
        references = [{'domain': 'test', 'name': name, 'files': ['map.tng']}
                      for name in [zero_name, '']]
        result = compare_queries(targets, [0, 3], references)
        self.assertEqual(result[0]['status'], 'changed')
        self.assertEqual([r['slot'] for r in result[0]['source_candidates_in_planned_slots']], [0, 3])
        self.assertEqual([r['slot'] for r in result[0]['planned_candidates']], [0, 3, 7])
        self.assertEqual(result[1]['status'], 'empty')
        self.assertEqual(result[1]['planned_candidates'], [])
        change = lookup_changes(targets, [0, 3])['changed_existing_groups']
        self.assertEqual(len(change), 1)
        self.assertEqual(change[0]['crc'], 0)
        # The queried name need not itself exist: anonymous rows still match.
        result = compare_queries({0: ('CDef', '')}, [0], references[:1])
        self.assertEqual(result[0]['status'], 'unique')
        self.assertEqual(result[0]['planned_candidates'][0]['name'], '')

    def test_stale_lookup_plans_do_not_replace_reports(self):
        with tempfile.TemporaryDirectory(prefix="forge-lookup-test-") as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            bank = source / "input.bin"
            bank.write_bytes(b"original input")
            tool = root / "unused-tool"
            tool.write_bytes(b"must not execute")
            plan = root / "plan.json"
            plan.write_text(json.dumps({"baseline": str(source), "sources": [],
                                       "input_sha256": {str(bank): "0" * 64}}), encoding="utf-8")
            output = root / "report.json"
            output.write_bytes(b"previous report")
            for script,reason in [('audit_worldregions.py','stale input report'),
                                  ('audit_factionregistry.py','stale or inconsistent namespace input')]:
                with self.subTest(script=script):
                    run = subprocess.run([sys.executable, str(Path(__file__).with_name(script)),
                                          "--tool", str(tool), "--source", str(source), "--plan", str(plan),
                                          "--out", str(output)], capture_output=True, text=True, check=False)
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(reason, run.stderr)
                    self.assertEqual(output.read_bytes(), b"previous report")
                    self.assertEqual(bank.read_bytes(), b"original input")

    def test_missing_query_can_be_captured_by_new_definition(self):
        targets = {3: ("ENGINE_THEME", "ground"), 7: ("ENGINE_THEME", "new")}
        references = [{"domain": "ground", "name": name, "files": ["map.lev"]}
                      for name in ("ground", "new", "still_missing")]
        result = compare_queries(targets, [3], references)
        self.assertEqual([r["status"] for r in result], ["unique", "changed", "missing"])
        self.assertEqual(result[1]["source_candidates_in_planned_slots"], [])
        self.assertEqual(result[1]["planned_candidates"][0]["slot"], 7)

    def test_distinct_names_with_equal_crc_remain_ambiguous(self):
        groups = lookup_groups({4: ("OBJECT", "LOOKUP_IOAQYHT9HSJ4"),
                                7: ("OBJECT", "LOOKUP_MC32E53Z7H9W")})
        self.assertEqual(list(groups), [0x5B4D7946])
        self.assertEqual([r["slot"] for r in groups[0x5B4D7946]], [4, 7])

    def test_existing_duplicate_can_gain_another_candidate(self):
        targets = {0: ("OBJECT", ""), 3: ("OBJECT", "shared"),
                   9: ("OBJECT", "shared"), 10: ("OBJECT", "shared"),
                   11: ("OBJECT", "new")}
        result = lookup_changes(targets, [0, 3, 9])
        self.assertEqual(result["source_crc_groups"], 1)
        self.assertEqual(result["added_crc_groups"], 1)
        changes = result["changed_existing_groups"]
        self.assertEqual(len(changes), 1)
        self.assertEqual([r["slot"] for r in changes[0]["source_candidates_in_planned_slots"]], [3, 9])
        self.assertEqual([r["slot"] for r in changes[0]["planned_candidates"]], [3, 9, 10])
        self.assertEqual(lookup_changes(targets, targets)["changed_existing_groups"], [])


if __name__ == "__main__":
    unittest.main()
