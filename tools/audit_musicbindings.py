"""Review MAIN_SOUND_SETUP music filenames against source-local Ogg files.

CMusicManager 02461C70 performs an instance-name lookup; RegisterMusic 02461DD0
uses MusicEntries IDs as keys in its own map and resolves filenames under
Data/Sound, replacing .wma with .ogg. This reports all CRC candidates and does
not choose duplicate definitions/music IDs, inherit baseline files, certify
codec storage/playback or admit a source.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path, PureWindowsPath
import struct
import subprocess

from audit_defnamespace import lookup_groups, names
from audit_levsymbols import digest
from test_fmp_links import read_bank, crc0


def registration_key(stored):
    # CMusicEntry transfers its ID with Read/WriteULONG (019D7BA4/019D7D05),
    # then RegisterMusic 02461DD0 copies those bits into CVectorMap<long,...>.
    if not 0 <= stored <= 0xffffffff:
        raise ValueError('music ID outside unsigned storage range')
    return stored if stored < 0x80000000 else stored - 0x100000000


def music_entries(raw, pool):
    if len(raw) < 4:
        raise ValueError('truncated music array')
    count, = struct.unpack_from('<I',raw)
    if count > (len(raw)-4)//13 or len(raw) != 4+13*count:
        raise ValueError('invalid music array extent')
    result = []
    for i in range(count):
        identity, reference, volume, cache = struct.unpack_from('<IifB',raw,4+13*i)
        if cache > 1 or not math.isfinite(volume):
            raise ValueError('unsupported music volume/cache value')
        if reference == -1:
            filename = ''
        elif reference not in pool or pool[reference][0] != crc0(pool[reference][1]):
            raise ValueError('invalid music filename offset or CRC')
        else:
            filename = pool[reference][1]
        path = PureWindowsPath(filename.replace('.wma','.ogg'))
        if filename and (path.is_absolute() or path.drive or path.root or '..' in path.parts or
                         '\0' in filename or path.suffix != '.ogg'):
            raise ValueError('unsupported music filename/path')
        result.append({'array_index':i,'music_id':identity,'registered_key':registration_key(identity),'name_offset':reference,'filename':filename,
                       'relative_ogg':path.as_posix() if filename else '', 'volume':volume,'cache':bool(cache)})
    return result


def cache_music_sets(raw, entries):
    if len(raw) < 4:
        raise ValueError('truncated music-set array')
    count, = struct.unpack_from('<I',raw)
    if count > (len(raw)-4)//12 or len(raw) != 4+12*count:
        raise ValueError('invalid music-set array extent')
    keys = {}
    for entry in entries:
        keys.setdefault(registration_key(entry['music_id']),[]).append(entry['array_index'])
    rows = []
    for i in range(count):
        first,second,third = struct.unpack_from('<iii',raw,4+12*i)
        # CacheMusicSet 02462310 accepts set IDs 2..76 as ARRAY INDICES and
        # passes members +4 and +0 to CacheMusic. The +8 member is not covered.
        active = 2 <= i <= 76
        arguments = []
        if active:
            for offset,key in ((4,second),(0,first)):
                candidates = keys.get(key,[])
                arguments.append({'member_offset':offset,'music_id':key,'registered_array_indices':candidates,
                                  'status':'missing' if not candidates else 'ambiguous' if len(candidates)>1 else 'unique'})
        rows.append({'set_array_index':i,'cache_call_range':active,'cache_arguments':arguments,
                     'words':[first,second,third],'member_8_reviewed':False})
    return {'scope':'CacheMusicSet argument membership only. CacheMusic/PlayMusic are stubs in this debug build; '
                    'playback behavior, missing-key fallback and member +8 remain unchecked.',
            'has_all_cache_set_slots':count>=77,'rows':rows}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,action='append',required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--out',type=Path,required=True)
    args = parser.parse_args()
    roots = [p.resolve() for p in args.source]
    tool,schema,output = (p.resolve() for p in (args.tool,args.schema,args.out))
    dependencies = [tool,schema,Path(__file__).resolve(),*(Path(__file__).with_name(n).resolve() for n in
                    ('audit_defnamespace.py','audit_levsymbols.py','test_fmp_links.py'))]
    if output in dependencies or any(output.is_relative_to(root) for root in roots):
        parser.error('report must not replace input files or tooling')
    hashes = {str(p):digest(p) for p in dependencies}
    report = {'scope':__doc__,'source_admitted':False,'sources':[]}
    findings = 0
    for root in roots:
        for filename in ('game.bin','names.bin'):
            path = root/'data/CompiledDefs'/filename; hashes[str(path)] = digest(path)
        bank,pool = read_bank(root),names(root)
        candidates = lookup_groups({i:(e['definition'],e['name']) for i,e in enumerate(bank)}).get(crc0('MAIN_SOUND_SETUP'),[])
        source = {'root':str(root),'lookup_candidates':candidates,'definitions':[]}
        findings += len(candidates) != 1
        directory = (root/'data/Sound').resolve()
        loose = {}
        for path in sorted(directory.rglob('*')):
            if not path.is_file() or path.suffix.lower() != '.ogg':
                continue
            resolved = path.resolve()
            if not resolved.is_relative_to(directory):
                raise RuntimeError('source Ogg path leaves sound directory')
            hashes[str(resolved)] = digest(resolved)
            loose.setdefault(path.relative_to(directory).as_posix().casefold(),[]).append(str(resolved))
        for candidate in candidates:
            row = {'candidate':candidate,'entries':[]}
            if candidate['definition'] != 'SOUND_SETUP':
                row['status'] = 'wrong_definition_class'; findings += 1
                source['definitions'].append(row); continue
            run = subprocess.run([str(tool),'defs','decode',str(root),str(schema),str(candidate['slot']),'--json'],
                                 capture_output=True,text=True,encoding='utf-8',check=True,timeout=30)
            decoded = json.loads(run.stdout)
            if not decoded['all_tags_ok'] or decoded['leftover'] or decoded['definition'] != 'SOUND_SETUP':
                raise RuntimeError('unclean sound definition decode')
            raw = bytes.fromhex(decoded['prefix_hex'])
            for field in decoded['fields']:
                raw += struct.pack('<I',int(field['tag'],16))+bytes.fromhex(field['raw_hex'])
            if raw != bank[candidate['slot']]['data']:
                raise RuntimeError('raw field reconstruction disagrees with independent bank reader')
            fields = [f for f in decoded['fields'] if f['name']=='MusicEntries' and f['type']=='Vector_VCMusicEntry__']
            if len(fields) != 1:
                raise RuntimeError('missing/ambiguous music array')
            entries = music_entries(bytes.fromhex(fields[0]['raw_hex']),pool)
            ids = Counter(e['music_id'] for e in entries)
            registered = {e['relative_ogg'].casefold() for e in entries if e['relative_ogg']}
            for entry in entries:
                entry['local_paths'] = loose.get(entry['relative_ogg'].casefold(),[])
                entry['duplicate_music_id'] = ids[entry['music_id']] != 1
                entry['status'] = ('empty' if not entry['filename'] else 'missing_source_file' if not entry['local_paths']
                                   else 'ambiguous_source_path' if len(entry['local_paths']) != 1 else 'source_file')
                findings += entry['duplicate_music_id'] or entry['status'] in ('missing_source_file','ambiguous_source_path')
            row.update(status='reviewed',entries=entries,unregistered_local_files=[p for k,v in loose.items() if k not in registered for p in v])
            findings += bool(row['unregistered_local_files'])
            sets = [f for f in decoded['fields'] if f['name']=='MusicSetEntries' and f['type']=='Vector_VCMusicSetEntry__']
            if len(sets) != 1:
                raise RuntimeError('missing/ambiguous music-set array')
            row['cache_sets'] = cache_music_sets(bytes.fromhex(sets[0]['raw_hex']),entries)
            row['cache_set_findings'] = sum(a['status']!='unique' for s in row['cache_sets']['rows'] for a in s['cache_arguments'])
            findings += row['cache_set_findings'] + (not row['cache_sets']['has_all_cache_set_slots'])
            source['definitions'].append(row)
            print(root.name,candidate['slot'],dict(Counter(e['status'] for e in entries)),
                  'unregistered local',len(row['unregistered_local_files']),
                  'cache-set findings',row['cache_set_findings'],flush=True)
        report['sources'].append(source)
    for path,expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f'input changed during music review: {path}')
    report.update(input_sha256=hashes,findings=findings)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return int(findings != 0)


if __name__ == '__main__':
    raise SystemExit(main())
