#!/usr/bin/env python3
"""Independent read-only audit of native CCreatureDef appearance serialization.

Uses the recovered FableWin loader/PDB layout, including body/global texture maps.
This is evidence for a future checked core reader, not a writer or renderer gate.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from audit_creature_body_models import body_slots
from test_fmp_links import crc0, read_bank


class Reader:
    def __init__(self, data):
        self.data, self.at, self.total = data, 3, 0

    def take(self, size):
        if size < 0 or self.at > len(self.data) or size > len(self.data)-self.at:
            raise ValueError('truncated appearance layout')
        start = self.at
        self.at += size
        return start

    def word(self):
        return struct.unpack_from('<I', self.data, self.take(4))[0]

    def flag(self):
        value = self.data[self.take(1)]
        if value not in (0, 1):
            raise ValueError('invalid serialized boolean')
        return bool(value)

    def tag(self, value):
        if self.word() != value:
            raise ValueError('unsupported appearance control marker')

    def count(self, minimum):
        value = self.word()
        self.total += value
        if value > 4096 or self.total > 65536 or value > (len(self.data)-self.at)//minimum:
            raise ValueError('invalid appearance count')
        return value

    def textures(self):
        count = self.count(12)
        if self.word() != count*12:
            raise ValueError('texture-map byte size does not match count')
        rows = []
        for _ in range(count):
            offset = self.at
            rows.append(dict(offset=offset, original=self.word(), replacement=self.word(), group=self.word()))
        return dict(rows=rows, dirty=self.flag())


def decode(data, names):
    reader = Reader(data)
    reader.tag(crc0('WoundMorphs'))
    wounds = reader.count(44)
    reader.take(wounds*44)
    reader.tag(0x01ab6cee)
    reader.tag(crc0('RandomAppearanceMorph'))
    groups = reader.count(4)
    if groups != 3:
        raise ValueError('native appearance requires exactly three body groups')
    bodies, slots = [], []
    for body in range(groups):
        alternatives = []
        for alternative in range(reader.count(17)):
            offset = reader.at
            mesh, priority = reader.word(), reader.word()
            textures = reader.textures()
            alternatives.append(dict(mesh=mesh, priority=priority, textures=textures))
            slots.append(dict(body_part=body, alternative=alternative, offset=offset, id=mesh))
        bodies.append(alternatives)
    group_textures = [reader.textures() for _ in range(groups)]
    skeleton = []
    for _ in range(reader.count(13)):
        key, group, name = reader.word(), reader.word(), reader.word()
        variation = reader.flag()
        if name not in names:
            raise ValueError('skeletal name is not a names-pool boundary')
        skeleton.append(dict(key=key, group=group, name_offset=name,
                             name=names[name], allow_variation=variation))
    global_textures = reader.textures()
    variants = reader.word()
    reader.tag(crc0('Expressions'))
    expressions = reader.count(12)
    reader.take(expressions*12)
    reader.tag(0x01ab6d32)
    reader.tag(crc0('Stats'))
    return dict(bodies=bodies, body_texture_maps=group_textures,
                skeletal_morphs=skeleton, global_texture_map=global_textures,
                texture_group_count=variants, slots=slots,
                stats_value_offset=reader.at)


def names_pool(raw):
    if len(raw) < 20:
        raise ValueError('truncated names header')
    count, = struct.unpack_from('<I', raw, 8)
    if count > (len(raw)-20)//5:
        raise ValueError('invalid names count')
    names, at = {}, 20
    for _ in range(count):
        end = raw.index(0, at+4)
        names[at+4-20] = raw[at+4:end].decode('ascii')
        at = end+1
    if at != len(raw):
        raise ValueError('unsupported names tail')
    return names


def boundary_checks():
    # Minimal native shape exercises a populated global map and a name boundary.
    word = lambda *values: struct.pack('<'+'I'*len(values), *values)
    data = b'\x01\0\x01'+word(crc0('WoundMorphs'), 0, 0x01ab6cee,
        crc0('RandomAppearanceMorph'), 3, 0, 0, 0)
    data += (word(0, 0)+b'\0')*3
    skeletal_count = len(data)
    data += word(1, 0, 0, 4)+b'\1'
    map_count = len(data)
    data += word(1, 12, 214, 215, 0)+b'\0'
    data += word(0, crc0('Expressions'), 0, 0x01ab6d32, crc0('Stats'))
    result = decode(data, {4:'MORPH'})
    if result['global_texture_map']['rows'][0]['replacement'] != 215:
        raise AssertionError('populated global map was not decoded')
    for end in range(len(data)):
        try:
            decode(data[:end], {4:'MORPH'})
        except (ValueError, struct.error, IndexError):
            pass
        else:
            raise AssertionError(f'truncated layout accepted at {end}')
    corruptions = [('body count', 19, 2), ('skeletal count', skeletal_count, 0xffffffff),
                   ('name boundary', skeletal_count+12, 5),
                   ('map count', map_count, 0xffffffff), ('map byte size', map_count+4, 11)]
    for label, offset, value in corruptions:
        changed = bytearray(data)
        struct.pack_into('<I', changed, offset, value)
        try:
            decode(changed, {4:'MORPH'})
        except (ValueError, struct.error, IndexError):
            pass
        else:
            raise AssertionError('accepted malformed '+label)
    return dict(truncated_refusals=len(data), malformed_refusals=len(corruptions),
                populated_global_map=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    native = args.install.resolve(strict=True)
    if args.output.resolve().is_relative_to(native):
        raise ValueError('evidence must be outside the input install')
    checks = boundary_checks()
    inputs = [native/'data/CompiledDefs'/name for name in ('game.bin','names.bin')]
    hashes = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    names = names_pool(inputs[1].read_bytes())
    counts, rows, refused = Counter(), [], []
    for index, record in enumerate(read_bank(native)):
        if record['definition'] != 'CCreatureDef':
            continue
        try:
            layout = decode(record['data'], names)
        except (ValueError, struct.error, IndexError) as error:
            refused.append(dict(index=index, name=record['name'], reason=str(error)))
            continue
        # Existing body audit is a separate boundary walk. Preserve its known
        # refusal when a formerly opaque global map contains actual entries.
        try:
            old_end, old_slots = body_slots(record['data'])
        except ValueError as error:
            counts['legacy_layout_refusals'] += 1
            layout['legacy_refusal'] = str(error)
        else:
            if old_end != layout['stats_value_offset'] or old_slots != layout['slots']:
                raise AssertionError(('independent body-layout mismatch', index))
            counts['legacy_layout_agreements'] += 1
        counts['components'] += 1
        counts['body_slots'] += len(layout['slots'])
        counts['skeletal_rows'] += len(layout['skeletal_morphs'])
        counts['nonempty_body_texture_maps'] += sum(bool(m['rows']) for m in layout['body_texture_maps'])
        counts['nonempty_global_texture_maps'] += bool(layout['global_texture_map']['rows'])
        counts['nonzero_texture_group_count'] += bool(layout['texture_group_count'])
        rows.append(dict(index=index, name=record['name'], **layout))
    if any(hashlib.sha256(p.read_bytes()).hexdigest() != hashes[str(p)] for p in inputs):
        raise AssertionError('input banks changed')
    report = dict(input_hashes=hashes, input_hashes_unchanged=True,
                  producer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  boundary_checks=checks,
                  counts=dict(counts), records=rows, refused=refused,
                  scope='Read-only serialized appearance layout; selection/rendering unverified.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(counts=dict(counts), refused=len(refused), boundary_checks=checks)))
    if not rows or refused:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
