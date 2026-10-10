"""TNG definition inventory coverage and ambiguity regression checks."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from audit_thingtext import definition_symbols


class ThingSymbolTests(unittest.TestCase):
    def test_component_consumers_and_unrelated_names(self):
        repo = Path(__file__).resolve().parents[1]
        text = ('Version 2;\nXXXSectionStart NULL;\nNewThing Object;\nPlayer -1;\nUID 1;\n'
                'DefinitionType "OBJECT_GOOD";\n'
                'StartCTCAtmosPlayer;\nAtmosName "ATMOS_TEST";\nEndCTCAtmosPlayer;\n'
                'StartCTCCreatureGenerator;\nCreatureFamilies[0] "FAMILY_TEST";\n'
                'ScriptNameOfAllGeneratedCreatures "NOT_A_DEF";\nEndCTCCreatureGenerator;\n'
                'StartCTCHero;\nhero_title_object_def_name "";\nEndCTCHero;\n'
                'StartCTCChest;\nContainerContents[0] "ITEM_TEST";\n'
                'ContainerContents[1] "ITEM_TEST";\nEndCTCChest;\n'
                'StartCTCObjectAugmentations;\nSavedInGame FALSE;\n'
                'AugmentationDefNames[0] "CONDITIONAL";\nEndCTCObjectAugmentations;\n'
                'EndThing;\nXXXSectionEnd;\n')
        with tempfile.TemporaryDirectory(prefix='forge-component-symbols-') as directory:
            path = Path(directory) / 'components.tng'
            path.write_bytes(text.replace('\n', '\r\n').encode('ascii'))
            before = path.read_bytes()
            def run(tool, args):
                result = subprocess.run([str(repo / 'build' / tool), *map(str, args)],
                                        capture_output=True, text=True, encoding='utf-8')
                self.assertEqual(result.returncode, 0, result.stderr)
                return json.loads(result.stdout)
            inventory = run('fableforge_defexternal_tests.exe', ['--tng-inventory', directory, '--symbols'])
            public = run('forge-tools.exe', ['world', 'review-storage', path, '--json'])['units'][0]
            references = inventory['component_definition_references']
            self.assertEqual([{k: v for k, v in r.items() if k != 'file'} for r in references],
                             public['unresolved_component_definitions'])
            self.assertEqual([(r['line'], r['definition']) for r in references],
                             [(8, 'ATMOS_TEST'), (11, 'FAMILY_TEST'), (15, ''),
                              (18, 'ITEM_TEST'), (19, 'ITEM_TEST')])
            symbols = definition_symbols(references, [])
            self.assertEqual(len(symbols), 4)
            self.assertEqual(next(r for r in symbols if r['name'] == '')['status'], 'empty')
            self.assertEqual([r['field'] for r in next(s for s in symbols if s['name'] == 'ITEM_TEST')['locations']],
                             ['ContainerContents[0]', 'ContainerContents[1]'])
            self.assertEqual(path.read_bytes(), before)

    def test_missing_and_duplicate_definitions_remain_findings(self):
        bank = [{'definition': 'OBJECT', 'name': 'shared'}, {'definition': 'THING', 'name': 'shared'}]
        references = [{'thing_type': 'Object', 'definition': name, 'file': 'map.tng', 'line': line}
                      for line, name in enumerate(['shared', 'missing', 'shared'], 1)]
        result = {r['name']: r for r in definition_symbols(references, bank)}
        self.assertEqual(result['shared']['status'], 'ambiguous')
        self.assertEqual([r['slot'] for r in result['shared']['source_candidates']], [0, 1])
        self.assertEqual([r['line'] for r in result['shared']['locations']], [1, 3])
        self.assertEqual(result['missing']['status'], 'missing')

    def test_refused_files_do_not_enter_reference_inventory(self):
        tool = Path(__file__).resolve().parents[1] / 'build/fableforge_defexternal_tests.exe'
        text = ('Version 2;\nXXXSectionStart NULL;\nNewThing Object;\nPlayer -1;\nUID 1;\n'
                'DefinitionType "OBJECT_GOOD";\nEndThing;\nXXXSectionEnd;\n')
        with tempfile.TemporaryDirectory(prefix='forge-thing-symbols-') as directory:
            root = Path(directory)
            (root / 'good.tng').write_text(text, encoding='ascii')
            (root / 'refused.tng').write_text(text.replace('EndThing;', 'Unknown 1;\nEndThing;'), encoding='ascii')
            (root / 'global.gtg').write_text('NEWMAP 1\n' + text + 'ENDMAP\n', encoding='ascii')
            before = {p: p.read_bytes() for p in root.iterdir()}
            run = subprocess.run([str(tool), '--tng-inventory', str(root), '--symbols'],
                                 capture_output=True, text=True, encoding='utf-8', check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            report = json.loads(run.stdout)
            self.assertEqual((report['accepted'], report['refused']), (2, 1))
            self.assertFalse(report['complete'])
            self.assertEqual(len(report['definition_references']), 2)
            reference = next(r for r in report['definition_references'] if Path(r['file']).name == 'good.tng')
            self.assertEqual(Path(reference['file']).name, 'good.tng')
            self.assertEqual((reference['line'], reference['thing_type'], reference['definition']),
                             (6, 'Object', 'OBJECT_GOOD'))
            global_reference = next(r for r in report['definition_references'] if Path(r['file']).name == 'global.gtg')
            self.assertEqual((global_reference['line'], global_reference['world_map_slot']), (7, 1))
            self.assertTrue(all(p.read_bytes() == data for p, data in before.items()))

    def test_gtg_map_and_file_locations_and_whole_file_refusal(self):
        repo = Path(__file__).resolve().parents[1]
        body = ('Version 2;\nXXXSectionStart NULL;\nNewThing Object;\nPlayer -1;\nUID 1;\n'
                'DefinitionType "SHARED";\nStartCTCAtmosPlayer;\nAtmosName "ATMOS_TEST";\n'
                'EndCTCAtmosPlayer;\nEndThing;\nXXXSectionEnd;\n')
        text = '\nNEWMAP 3\n\n' + body + 'ENDMAP\n\nNEWMAP 7\n' + body + 'ENDMAP\n'
        with tempfile.TemporaryDirectory(prefix='forge-gtg-symbols-') as directory:
            path = Path(directory) / 'global.gtg'
            path.write_bytes(text.replace('\n', '\r\n').encode('ascii'))
            def run():
                result = subprocess.run([str(repo / 'build/forge-tools.exe'), 'world', 'review-storage',
                                         str(path), '--json'], capture_output=True, text=True, encoding='utf-8')
                return result.returncode, json.loads(result.stdout)['units'][0]
            code, row = run()
            self.assertEqual(code, 0)
            references = row['unresolved_thing_definitions']
            self.assertEqual([(r['line'], r['world_map_slot']) for r in references], [(9, 3), (23, 7)])
            components = row['unresolved_component_definitions']
            self.assertEqual([(r['line'], r['world_map_slot']) for r in components], [(11, 3), (25, 7)])
            symbols = definition_symbols([dict(r, file=str(path)) for r in references], [])
            self.assertEqual([r['world_map_slot'] for r in symbols[0]['locations']], [3, 7])
            # A later bad body suppresses references from earlier valid bodies.
            for invalid in [text.replace('NEWMAP 7', 'NEWMAP 3'),
                            text.rsplit('ENDMAP', 1)[0],
                            text.replace('NEWMAP 7\nVersion 2;', 'NEWMAP 7\nVersion 2;\nUnknown 1;')]:
                path.write_text(invalid, encoding='ascii')
                before = path.read_bytes()
                code, row = run()
                self.assertEqual(code, 1)
                self.assertNotIn('unresolved_thing_definitions', row)
                self.assertNotIn('unresolved_component_definitions', row)
                self.assertEqual(path.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
