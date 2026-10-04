#!/usr/bin/env python3
"""Scratch-audit selected named historic FMPs and an overlapping load order.

The source ZIP and install are read only. Success means offline composition,
never a claim that the mod works in-game or targets the PC release.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
import zipfile
from pathlib import Path


CASES = [
    '0 Silver Keys Needed.fmp',
    '100Multiarrow.fmp',
    'Ghost Sword - Black Wasps.fmp',
    'Ghost Sword - Blue Wasps.fmp',
    'Young Hero Retexture.fmp',
    '1Staff Set.fmp',
    'Oak Vale bed.fmp',
    'Working Stick 1000000.fmp',
    'Berserk Scale 1.5.fmp',
    'Small Berserk.fmp',
]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(root):
    return sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file())


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='named-fmp-stack-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    archive_hash = digest(args.archive)
    baseline = args.install/'data/CompiledDefs'
    baseline_hashes = {name: digest(baseline/name) for name in ('game.bin', 'names.bin')}
    selected = {}
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {Path(row['file']).name: row for row in json.loads(archive.read('catalogue.json'))}
        for name in CASES:
            record = catalogue[name]
            payload = archive.read(record['file'])
            assert hashlib.sha256(payload).hexdigest() == record['sha256'], name
            path = work/'sources'/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            selected[name] = (path, record)

    rows = []
    stacks = [[name] for name in CASES] + [
        ['Ghost Sword - Black Wasps.fmp', 'Ghost Sword - Blue Wasps.fmp'],
        ['Ghost Sword - Blue Wasps.fmp', 'Ghost Sword - Black Wasps.fmp'],
        ['Berserk Scale 1.5.fmp', 'Small Berserk.fmp'],
        ['Small Berserk.fmp', 'Berserk Scale 1.5.fmp'],
    ]
    for index, names in enumerate(stacks):
        output = work/'outputs'/f'{index:02d}'
        output.mkdir(parents=True)
        (output/'prior.txt').write_bytes(b'keep prior output')
        command = [str(args.tool.resolve()), 'mods', 'merge', str(args.install.resolve()),
                   str(output), '--with', *(str(selected[name][0]) for name in names), '--json']
        result = subprocess.run(command, capture_output=True, timeout=240, cwd=repo)
        (work/f'{index:02d}.stdout').write_bytes(result.stdout)
        (work/f'{index:02d}.stderr').write_bytes(result.stderr)
        row = {'mods': names, 'returncode': result.returncode,
               'banks': {name: selected[name][1]['banks'] for name in names}}
        if result.returncode:
            assert inventory(output) == ['prior.txt'], (names, inventory(output))
            row['status'] = 'refused'
            row['reason'] = result.stderr.decode('utf-8', errors='replace').strip()[-1000:]
        else:
            report = json.loads(result.stdout)
            row['status'] = 'composed_offline'
            row['summary'] = report.get('summary', {})
            row['definition_bank_written'] = (output/'data/CompiledDefs/game.bin').is_file()
            row['level_wad_written'] = (output/'data/Levels/FinalAlbion.wad').is_file()
        rows.append(row)
        (work/'progress.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
        print(index + 1, '/', len(stacks), ', '.join(names), row['status'], flush=True)

    # These two real packages both change the same named definition. Confirm
    # the reported field winner follows order and the built banks differ.
    berserk_outputs = []
    for index in (len(stacks) - 2, len(stacks) - 1):
        row = rows[index]
        assert row['status'] == 'composed_offline', row
        report = json.loads((work/f'{index:02d}.stdout').read_text(encoding='utf-8'))
        conflicts = report['defs']['conflicts']
        matches = [field for conflict in conflicts
                   if conflict['record'] == 'SPECIAL_ABILITIES_BERSERK_DEF_INSTANCE'
                   for field in conflict['field_conflicts']
                   if field['field'] == 'BerserkScale']
        assert len(matches) == 1 and matches[0]['winner'] == row['mods'][-1][:-4], matches
        bank = work/'outputs'/f'{index:02d}'/'data/CompiledDefs/game.bin'
        berserk_outputs.append(digest(bank))
        row['berserk_scale_winner'] = matches[0]['winner']
        row['game_bin_sha256'] = berserk_outputs[-1]
    assert berserk_outputs[0] != berserk_outputs[1]

    assert digest(args.archive) == archive_hash
    assert {name: digest(baseline/name) for name in baseline_hashes} == baseline_hashes
    report = {'source_archive_sha256': archive_hash,
              'baseline_game_bin_sha256': baseline_hashes['game.bin'],
              'rows': rows, 'gameplay_tested': False}
    (work/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Named FMP scratch audit complete:', work/'result.json', flush=True)


if __name__ == '__main__':
    main()
