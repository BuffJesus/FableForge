#!/usr/bin/env python3
"""Read-only audit of creature runtime Components and retail placement profiles."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
from audit_creature_visual_graph import listing
from test_fmp_links import crc0, read_bank


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def runtime_components(record, names):
    # CDefs prefix and runtime Components are distinct serialized structures.
    rows = listing(record)
    at = 5+12*len(rows)+4
    data = record['data']
    next_tag = struct.pack('<I', crc0('AvailableInEditor'))
    end = data.find(next_tag, at)
    if end<0 or at+4>end:
        raise ValueError('missing Components value/next field boundary')
    count, = struct.unpack_from('<I', data, at)
    if count>4096 or count>(end-at-4)//9:
        raise ValueError('invalid runtime component count')
    tail = end-at-4-count*9
    if tail != 4:
        raise ValueError(f'unsupported opaque runtime component tail: {tail}')
    if struct.unpack_from('<I', data, at+4+count*9)[0] != 0x01ab6cce:
        raise ValueError('runtime component verification word mismatch')
    result = []
    for index in range(count):
        offset, parameter, flag = struct.unpack_from('<IIB', data, at+4+index*9)
        if offset not in names:
            raise ValueError(f'component name offset is not a names-pool boundary: {offset}')
        result.append(dict(name=names[offset], parameter=parameter, flag=flag))
    return result


def self_test():
    components = struct.pack('<I',crc0('Components'))
    available = struct.pack('<I',crc0('AvailableInEditor'))
    data = b'\x01\0\x01\0\0'+components+struct.pack('<IIIBI',1,4,0,0,0x01ab6cce)+available+b'\1'
    record = dict(data=data)
    assert runtime_components(record,{4:'CTCVillageMember'}) == [dict(name='CTCVillageMember',parameter=0,flag=0)]
    for label,changed in (
        ('count', data[:9]+struct.pack('<I',2)+data[13:]),
        ('count overflow', data[:9]+struct.pack('<I',0xffffffff)+data[13:]),
        ('name boundary', data[:13]+struct.pack('<I',5)+data[17:]),
        ('verify word', data[:22]+bytes(4)+data[26:]),
        ('truncated', data[:24])):
        try:runtime_components(dict(data=changed),{4:'CTCVillageMember'})
        except ValueError:pass
        else:raise AssertionError('accepted malformed '+label)


def main():
    self_test()
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--maps', nargs='*', default=['StartOakValeWest','GuildExterior'])
    args = parser.parse_args()
    native = args.install.resolve(strict=True)
    output = args.work_root.resolve()
    assert not output.is_relative_to(native), 'evidence must stay outside the install'
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='creature-placement-profile-', dir=output))
    print('Evidence retained at', work, flush=True)
    files = [native/'data/CompiledDefs'/n for n in ('game.bin','names.bin')]
    hashes = {str(p):digest(p) for p in files}
    records = read_bank(native)
    pool = (native/'data/CompiledDefs/names.bin').read_bytes()
    names, at = {}, 20
    for _ in range(struct.unpack_from('<I',pool,8)[0]):
        end = pool.index(0, at+4)
        names[at+4-20] = pool[at+4:end].decode('ascii')
        at = end+1
    rows, refused = [], []
    for index,record in enumerate(records):
        if record['definition']!='CREATURE':
            continue
        try:
            components = runtime_components(record, names)
        except ValueError as error:
            refused.append(dict(index=index, name=record['name'], reason=str(error)))
            continue
        rows.append(dict(index=index, name=record['name'], component_count=len(components),
            village_member=any(c['name']=='CTCVillageMember' for c in components),
            name_heuristic='VILLAGER' in record['name'],
            village_rows=[c for c in components if c['name']=='CTCVillageMember']))
    by_name = {r['name']:r for r in rows}
    samples, missing_maps = [], []
    for name in args.maps:
        assert re.fullmatch(r'[A-Za-z0-9_]+',name), 'map must be a simple loose-map name'
        path = native/'data/Levels/FinalAlbion'/(name+'.tng')
        if not path.is_file():
            missing_maps.append(name)
            continue
        hashes[str(path)] = digest(path)
        text = path.read_text(encoding='cp1252')
        for body in re.findall(r'NewThing AICreature;(.*?)EndThing;',text,re.S):
            match = re.search(r'DefinitionType "([^"]+)";',body)
            if match and match[1] in by_name:
                r = by_name[match[1]]
                samples.append(dict(map=name, name=match[1],
                    runtime_component=r['village_member'], name_heuristic=r['name_heuristic'],
                    tng_village_member='StartCTCVillageMember;' in body))
    mismatches = [r for r in rows if r['village_member']!=r['name_heuristic']]
    tng_mismatches = [r for r in samples if r['runtime_component']!=r['tng_village_member']]
    assert all(digest(Path(p))==value for p,value in hashes.items()), 'native inputs changed'
    report = dict(input_hashes=hashes, records=len(records), creature_roots=len(rows)+len(refused),
        decoded=len(rows), refused=refused, runtime_component_counts=dict(Counter(r['village_member'] for r in rows)),
        name_heuristic_mismatches=mismatches, samples=samples, tng_mismatches=tng_mismatches,
        missing_maps=missing_maps, rows=rows, input_hashes_unchanged=True,
        scope='Read-only runtime-Components observation. Native creation/serialization evidence and boundary tests required before writer changes.')
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('decoded',len(rows),'refused',len(refused),'name mismatches',len(mismatches),
        'TNG samples',len(samples),'TNG mismatches',len(tng_mismatches),'missing maps',missing_maps)


if __name__=='__main__':
    main()
