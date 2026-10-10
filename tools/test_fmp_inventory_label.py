#!/usr/bin/env python3
"""Check the original Blue Ass Beta inventory label against the actual text bank."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile
from test_fmp_links import package_records, read_bank
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run, field_start


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--work-root', required=True, type=Path)
    args = parser.parse_args()
    tool = Path(__file__).resolve().parents[1]/'build/forge-tools.exe'
    text = args.install/'data/lang/English/text.big'
    inputs = [args.archive, tool, text, args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(p): digest(p) for p in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-inventory-label-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    member = 'mods/Blue Ass Beta.fmp'
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        payload = archive.read(member)
        assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
    package = work/'original.fmp'; package.write_bytes(payload)
    source = package_records(package)['GameBINEntries']
    report = json.loads(run(tool, 'fmp', 'links', package, '--groups', '--json').stdout)
    labels = [(r['record'], g['targets'][0]) for r in report['records']
              for g in r['groups'] if g['path'] == '3517586038.unknown'
              and len(g['targets']) == 1]
    assert labels
    identities = entries(text, 'TEXT_ENGLISH_MAIN', {n for _, n in labels}, include_payload=False)
    direct, merged = work/'direct', work/'merged'
    run(tool, 'fmp', 'apply', args.install, package, direct)
    run(tool, 'mods', 'merge', args.install, merged, '--with', package)
    for bank in ('game.bin', 'names.bin'):
        assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
    built = {e['name']: e for e in read_bank(direct) if e['name']}
    for record, target in labels:
        at = field_start(source[record][0], 'd1aa1a76')
        assert built[record]['definition'] == 'INVENTORY_TYPE'
        assert struct.unpack_from('<I', built[record]['data'], at)[0] == identities[target]['id']
        # This owner also has suit/object references that legitimately relink.
        # Its label and surrounding scalar bytes are checked independently.
        assert built[record]['data'][at-4:at] == source[record][0][at-4:at]
        assert built[record]['data'][at+4:at+8] == source[record][0][at+4:at+8]
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file': member, 'labels': labels,
        'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS: original inventory label; actual text ID and direct/composed parity')


if __name__ == '__main__':
    main()
