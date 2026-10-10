#!/usr/bin/env python3
"""Verify real anonymous FMP records, load order, picks and guarded IDs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import read_bank
from test_fmp_texture_layers import entries


PACKAGES = {
    'beard': 'mods/Bandit Beard.fmp',
    'bright': 'mods/Brighterlamp.fmp',
    'super': 'mods/Lamp-Super Bright.fmp',
    'invalid': 'mods/Fireball to dreadwing magic.fmp',
    'silver': 'mods/0 Silver Keys Needed.fmp',
    'always': 'mods/Alwaysthere001.fmp',
}
LIGHT = 12869


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *args, json_output=False, ok=True):
    argv = [str(tool), *map(str, args)]
    if json_output: argv.append('--json')
    result = subprocess.run(argv, capture_output=True, text=True, timeout=180)
    assert (result.returncode == 0) == ok, (argv, result.stderr[-2000:])
    return json.loads(result.stdout) if json_output and ok else result


def radii(tool, root, schema):
    decoded = run(tool, 'defs', 'decode', root, schema, str(LIGHT), json_output=True)
    assert decoded['definition'] == 'CLightDef' and decoded['name'] == ''
    return {field['name']: float(field['value']) for field in decoded['fields']
            if field['name'] in ('InnerRadius', 'OuterRadius')}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    sources = (args.archive, args.install/'data/CompiledDefs/game.bin',
               args.install/'data/CompiledDefs/names.bin',
               args.install/'data/Misc/pc/effects.big')
    hashes = {str(path): digest(path) for path in sources}
    build = (repo/'build').resolve()
    with tempfile.TemporaryDirectory(prefix='fmp-anonymous-layers-', dir=build) as temp:
        work = Path(temp).resolve()
        assert work != build and work.is_relative_to(build)
        packages = {}
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            for label, member in PACKAGES.items():
                payload = archive.read(member)
                assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
                packages[label] = work/(label+'.fmp')
                packages[label].write_bytes(payload)
        beard = entries(packages['beard'], 'GameBINEntries')
        assert set(beard) == {''}
        beard_index = run(args.tool, 'fmp', 'list', packages['beard'], json_output=True)
        rows = next(bank['entries'] for bank in beard_index['banks'] if bank['bank'] == 'GameBINEntries')
        assert len(rows) == 1 and rows[0]['id'] == 11696 and rows[0]['name'] == ''
        direct = work/'direct'
        run(args.tool, 'fmp', 'apply', args.install, packages['beard'], direct)
        merged = work/'merged'
        report = run(args.tool, 'mods', 'merge', args.install, merged,
                     '--with', packages['beard'], json_output=True)
        assert report['defs']['summary']['applied'] == 1
        assert digest(direct/'data/CompiledDefs/game.bin') == digest(
            merged/'data/CompiledDefs/game.bin')
        assert read_bank(merged)[11696]['data'] == beard['']['payload']

        # The original Silver Keys package links five blank-name chest records
        # to CHEST_OPEN_01 in effects.big. Retail gives that effect ID 391.
        silver = packages['silver']
        listing = run(args.tool, 'fmp', 'list', silver, json_output=True)
        chest_ids = [entry['id'] for bank in listing['banks']
                     if bank['bank'] == 'GameBINEntries' for entry in bank['entries']]
        assert chest_ids == [12266, 12268, 12270, 12272, 12276]
        effects = run(args.tool, 'big', 'list', args.install/'data/Misc/pc/effects.big',
                      json_output=True)
        particle = next(bank for bank in effects['banks'] if bank['bank'] == 'PARTICLE_MAIN_PC')
        assert next(entry['id'] for entry in particle['entries']
                    if entry['name'] == 'CHEST_OPEN_01') == 391
        silver_direct, silver_merged = work/'silver-direct', work/'silver-merged'
        run(args.tool, 'fmp', 'apply', args.install, silver, silver_direct)
        report = run(args.tool, 'mods', 'merge', args.install, silver_merged,
                     '--with', silver, json_output=True)
        assert report['defs']['summary']['applied'] == 4
        assert digest(silver_direct/'data/CompiledDefs/game.bin') == digest(
            silver_merged/'data/CompiledDefs/game.bin')
        assert digest(silver_direct/'data/CompiledDefs/names.bin') == digest(
            silver_merged/'data/CompiledDefs/names.bin')
        silver_records = read_bank(silver_merged)
        for chest_id in chest_ids:
            payload = silver_records[chest_id]['data']
            marker = payload.find(bytes.fromhex('2a902fa0'))
            assert marker >= 0 and struct.unpack_from('<I', payload, marker + 4)[0] == 391

        # A selected effect link must refuse when its target bank is absent.
        no_effects = work/'no-effects'
        no_effects_defs = no_effects/'data/CompiledDefs'
        no_effects_defs.mkdir(parents=True)
        for name in ('game.bin', 'names.bin'):
            shutil.copyfile(args.install/'data/CompiledDefs'/name, no_effects_defs/name)
        absent = work/'no-effects-output'
        failure = run(args.tool, 'fmp', 'apply', no_effects, silver, absent, ok=False)
        assert 'missing target CHEST_OPEN_01' in failure.stderr and not absent.exists()

        # A native CREATURE control nests DeadBodyDecayEffect after a float.
        # ChocolateBox calls its link path "unknown"; the guarded nested tag
        # must resolve through the same effects.big identity table.
        always = packages['always']
        always_direct, always_merged = work/'always-direct', work/'always-merged'
        run(args.tool, 'fmp', 'apply', args.install, always, always_direct)
        run(args.tool, 'mods', 'merge', args.install, always_merged,
            '--with', always, json_output=True)
        for bank_name in ('game.bin', 'names.bin'):
            assert digest(always_direct/'data/CompiledDefs'/bank_name) == digest(
                always_merged/'data/CompiledDefs'/bank_name)
        decay = read_bank(always_merged)[1591]['data']
        decay_marker = decay.find(bytes.fromhex('2cf7f120'))
        assert decay_marker >= 0 and struct.unpack_from('<I', decay, decay_marker + 4)[0] == 909

        schema = repo/'docs/re_reference/def_schema.json'
        ab = work/'ab'
        report = run(args.tool, 'mods', 'merge', args.install, ab,
                     '--with', packages['bright'], packages['super'], json_output=True)
        conflicts = report['defs']['conflicts']
        assert len(conflicts) == 1 and conflicts[0]['record'] == '@unnamed:12869'
        assert {field['field'] for field in conflicts[0]['field_conflicts']} == (
            {'InnerRadius', 'OuterRadius'})
        assert radii(args.tool, ab, schema) == {'InnerRadius': 80, 'OuterRadius': 83}

        ba = work/'ba'
        run(args.tool, 'mods', 'merge', args.install, ba,
            '--with', packages['super'], packages['bright'], json_output=True)
        assert radii(args.tool, ba, schema) == {'InnerRadius': 12, 'OuterRadius': 15}

        picks = work/'picks.txt'
        picks.write_text('@unnamed:12869\tbright\n', encoding='utf-8')
        picked = work/'picked'
        report = run(args.tool, 'mods', 'merge', args.install, picked,
                     '--with', packages['bright'], packages['super'],
                     '--picks', picks, json_output=True)
        assert report['defs']['conflicts'][0]['overridden']
        assert radii(args.tool, picked, schema) == {'InnerRadius': 12, 'OuterRadius': 15}

        invalid = work/'invalid-output'
        failed = run(args.tool, 'fmp', 'apply', args.install, packages['invalid'],
                     invalid, ok=False)
        assert 'matching blank-name baseline identity' in failed.stderr
        assert not invalid.exists()
    assert hashes == {str(path): digest(path) for path in sources}
    print('Anonymous FMPs: direct/merged bytes agree, real lamp load order and '
          'saved pick win, chest/creature effect links resolve, missing effect '
          'and invalid baseline ID refuse')


if __name__ == '__main__':
    main()
