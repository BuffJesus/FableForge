#!/usr/bin/env python3
"""Audit Controller FMP named camera targets through standalone and composed banks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


def u32(data, offset): return struct.unpack_from('<I', data, offset)[0]


def crc0(name):
    crc = 0
    for byte in name.encode('ascii'):
        crc ^= byte
        for _ in range(8): crc = (crc >> 1) ^ (0xedb88320 if crc & 1 else 0)
    return crc


def package_records(path):
    """Read BIG entry tables independently, retaining each raw payload and offset."""
    data = path.read_bytes(); pos = u32(data, 8); count = u32(data, pos); pos += 4
    banks = {}
    for _ in range(count):
        end = next(i for i in range(pos, len(data)) if data[i] in (0, 255))
        name = data[pos:end].decode('ascii'); pos = end+1
        _, entries, start, _, _ = struct.unpack_from('<5I', data, pos); pos += 20
        cursor = start+4+u32(data, start)*8; records = {}
        for _ in range(entries):
            _, _, _, length, offset, _, size = struct.unpack_from('<7I', data, cursor); cursor += 28
            record = data[cursor:cursor+size].decode('ascii'); cursor += size+4
            sources = u32(data, cursor); cursor += 4
            for _ in range(sources):
                size = u32(data, cursor); cursor += 4+size
            size = u32(data, cursor); cursor += 4+size
            records[record] = (data[offset:offset+length], offset)
        banks[name] = records
    return banks


def rewrite_bank(original, output, entries):
    """Independent fixture writer: preserve the identity table, replace chunks."""
    defs = output/'data/CompiledDefs'; defs.mkdir(parents=True)
    source = original/'data/CompiledDefs'
    raw = (source/'game.bin').read_bytes(); chunks = []; offsets = []; total = 0
    old_count = u32(raw, 9); assert old_count <= len(entries)
    names = bytearray((source/'names.bin').read_bytes())
    main_table = bytearray(raw[13:13+old_count*12])
    for index,entry in enumerate(entries[old_count:], old_count):
        matches = [i for i,e in enumerate(entries[:old_count]) if e['definition'] == entry['definition']]
        assert matches, 'new fixture row must use an existing definition type'
        name_offset = len(names)-20+4
        names += struct.pack('<I', crc0(entry['name']))+entry['name'].encode('ascii')+b'\0'
        main_table += struct.pack('<III', u32(raw, 13+matches[0]*12), name_offset,
                                  sum(e['definition'] == entry['definition'] for e in entries[:index]))
    struct.pack_into('<I', names, 8, u32(names, 8)+len(entries)-old_count)
    struct.pack_into('<I', names, 12, len(names)-24)
    (defs/'names.bin').write_bytes(names)
    for index, entry in enumerate(entries):
        block = zlib.compress(struct.pack('<H', 2)+entry['data'])
        offsets.append((index, total)); chunks.append(block); total += len(block)
    offsets.append((len(entries), total))
    table = b''.join(struct.pack('<II', *item) for item in offsets)
    (defs/'game.bin').write_bytes(raw[:9]+struct.pack('<I', len(entries))+main_table+struct.pack('<I', len(offsets))+
        table+struct.pack('<I', total)+b''.join(chunks))


def read_bank(root):
    folder = root/'data/CompiledDefs'
    names_data = (folder/'names.bin').read_bytes()
    names, pos = {}, 20
    for _ in range(u32(names_data,8)):
        end = names_data.index(0,pos+4)
        names[pos+4-20] = names_data[pos+4:end].decode('ascii')
        pos = end+1
    data = (folder/'game.bin').read_bytes()
    count = u32(data,9)
    chunk_pos = 13+count*12
    chunks = [struct.unpack_from('<II',data,chunk_pos+4+i*8) for i in range(u32(data,chunk_pos))]
    start = chunk_pos+4+len(chunks)*8+4
    entries = []
    for (first,offset),(last,end) in zip(chunks,chunks[1:]):
        block = zlib.decompress(data[start+offset:start+end])
        offsets = list(struct.unpack_from('<'+'H'*(last-first),block))+[len(block)]
        for local,index in enumerate(range(first,last)):
            definition,name,_ = struct.unpack_from('<III',data,13+index*12)
            entries.append({'name':names.get(name,''),'definition':names[definition],
                'data':block[offsets[local]:offsets[local+1]]})
    assert len(entries)==count
    return entries


def audit(root):
    entries = read_bank(root)
    by_name = {e['name']:i for i,e in enumerate(entries) if e['name']}
    errors=[]
    checked=[]
    def scalar(record,field,target):
        data=entries[by_name[record]]['data']; tag=struct.pack('<I',crc0(field))
        assert data.count(tag)==1,(record,field)
        offset=data.index(tag)+4
        actual=u32(data,offset); expected=by_name[target]
        row={'record':record,'field':field,'target':target,'expected_id':expected,
             'actual_id':actual,'actual_target':entries[actual]['name'] if actual<len(entries) else '<out of range>'}
        checked.append(row)
        if actual!=expected: errors.append(row)
    for field,target in [('CameraManagerMain','CAMERA_MANAGER_MAIN'),
        ('CameraManagerCombat','CAMERA_MANAGER_COMBAT_MAIN'),
        ('CameraManagerPCMain','CAMERA_MANAGER_PC_MAIN'),
        ('CameraManagerPCCombat','CAMERA_MANAGER_PC_MAIN_COMBAT')]:
        scalar('CAMERA_MANAGER_SET_TEMPLATE',field,target)
    owner='CAMERA_MANAGER_PC_MAIN_COMBAT'; data=entries[by_name[owner]]['data']
    for field,targets in [('CameraList',['CAMERA_PC_NORMAL','CAMERA_PC_FAR']),
        ('SpecialityCameraList',['CAMERA_FIRST_PERSON','CAMERA_FACE','CAMERA_TORSO'])]:
        tag=struct.pack('<I',crc0(field)); assert data.count(tag)==1
        offset=data.index(tag)+4; assert u32(data,offset)==len(targets)
        for index,target in enumerate(targets):
            actual=u32(data,offset+4+index*4); expected=by_name[target]
            row={'record':owner,'field':field+'['+str(index)+']','target':target,
                'expected_id':expected,'actual_id':actual,
                'actual_target':entries[actual]['name'] if actual<len(entries) else '<out of range>'}
            checked.append(row)
            if actual!=expected: errors.append(row)
    for field,target in [('InitialMode','CAMERA_PC_NORMAL'),('ZTargetMode','CAMERA_PC_NORMAL'),
        ('ProjectileWeaponMode','CAMERA_PROJECTILE_WEAPON_CAM')]: scalar(owner,field,target)
    assert struct.unpack_from('<H',data,3)[0]==1
    assert u32(data,5)==crc0('CEntitySoundDef'),'component class CRC changed'
    actual=u32(data,13); expected=by_name['CAMERA_MANAGER_TEMPLATE']
    row={'record':owner,'field':'CDefs[0].InheritedEntryID','target':'CAMERA_MANAGER_TEMPLATE',
         'expected_id':expected,'actual_id':actual,'actual_target':entries[actual]['name'] if actual<len(entries) else '<out of range>'}
    checked.append(row)
    if actual!=expected: errors.append(row)
    return {'checked':len(checked)+1,'references':checked,'errors':errors}


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--fmp',type=Path,required=True)
    parser.add_argument('--aeon',type=Path,required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='fmp-links-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    tool=args.tool.resolve()
    args.install, args.fmp, args.aeon = args.install.resolve(), args.fmp.resolve(), args.aeon.resolve()
    schema=tool.parent/'docs/re_reference/def_schema.json'
    if not schema.exists(): schema=repo/'docs/re_reference/def_schema.json'
    os.chdir(args.cwd.resolve())
    sources=[args.fmp]+[root/'data/CompiledDefs'/name for root in (args.install,args.aeon) for name in ('game.bin','names.bin')]
    def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
    before={p:digest(p) for p in sources}
    reports={}
    for label,base in [('retail',args.install),('aeon',args.aeon)]:
        output=work/label
        result=subprocess.run([str(tool),'fmp','apply',str(base),str(args.fmp),str(output)],capture_output=True,timeout=60)
        (work/(label+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode==0,(result.stdout+result.stderr).decode('utf-8',errors='replace')
        reports[label]=audit(output)
        # Every unmentioned record and every non-reference FMP byte survives.
        before_entries = read_bank(base); after_entries = read_bank(output)
        raw_records = package_records(args.fmp)['GameBINEntries']
        after_by_name = {e['name']:e for e in after_entries if e['name']}
        for index, entry in enumerate(before_entries):
            if entry['name'] not in raw_records:
                assert after_entries[index] == entry, (label, index, entry['name'])
        refs = reports[label]['references']
        for name, (payload, _) in raw_records.items():
            expected = bytearray(payload)
            for ref in refs:
                if ref['record'] != name: continue
                field = ref['field']
                if field.startswith('CDefs'): offset = 13
                else:
                    stem = field.split('[')[0]; offset = payload.index(struct.pack('<I', crc0(stem)))+4
                    if '[' in field: offset += 4+int(field.split('[')[1][:-1])*4
                struct.pack_into('<I', expected, offset, ref['expected_id'])
            assert after_by_name[name]['data'] == expected, (label, name, 'unexpected payload byte change')
        reports[label]['preserved_existing_records'] = len(before_entries)-sum(e['name'] in raw_records for e in before_entries)
        reports[label]['preserved_fmp_payloads'] = len(raw_records)
    base,aeon=work/'base',work/'aeon-source'
    for original,copy in [(args.install,base),(args.aeon,aeon)]:
        for name in ('game.bin','names.bin'):
            target=copy/'data/CompiledDefs'/name; target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(original/'data/CompiledDefs'/name,target)
    merged=work/'merged'
    result=subprocess.run([str(tool),'mods','merge',str(base),str(merged),'--with',str(aeon),'--with',str(args.fmp),'--fields',str(schema)],capture_output=True,timeout=120)
    (work/'merged.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode != 0, 'partial unnamed overhaul composition must refuse'
    assert b'changed or new unnamed definition entries' in result.stderr, result.stderr
    assert not (merged/'data/CompiledDefs/game.bin').exists()
    reports['unnamed_overhaul_refusal'] = True
    # Use the intact overhaul bank as the baseline, never a named-only merge.
    result=subprocess.run([str(tool),'mods','merge',str(aeon),str(merged),
                           '--with',str(args.fmp),'--fields',str(schema)],capture_output=True,timeout=120)
    (work/'aeon-baseline-merged.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,(result.stdout+result.stderr).decode('utf-8',errors='replace')
    reports['merged']=audit(merged)
    imported = package_records(args.fmp)['GameBINEntries']
    intact = read_bank(aeon); composed = read_bank(merged)
    for index, entry in enumerate(intact):
        if entry['name'] not in imported:
            assert composed[index] == entry, ('Aeon baseline preservation', index, entry['name'])
    reports['merged']['preserved_unnamed_records'] = sum(not e['name'] for e in intact)

    # Direct merge and transactional build must preserve prior output on refusal.
    before_merged = {p.relative_to(merged): digest(p) for p in merged.rglob('*') if p.is_file()}
    for mode in ('merge', 'build'):
        refusal_base = work/('refusal-base-'+mode)
        shutil.copytree(base, refusal_base)
        options = ['--with', str(aeon)]
        if mode == 'build':
            added = subprocess.run([str(tool),'mods','add',str(refusal_base),str(aeon)],capture_output=True,timeout=60)
            assert added.returncode == 0, added.stderr
            options = []
        failed = subprocess.run([str(tool),'mods',mode,str(refusal_base),str(merged),*options],capture_output=True,timeout=120)
        (work/('unnamed-refusal-'+mode+'.log')).write_bytes(failed.stdout+failed.stderr)
        assert failed.returncode != 0 and b'changed or new unnamed definition entries' in failed.stderr, failed.stderr
        assert {p.relative_to(merged): digest(p) for p in merged.rglob('*') if p.is_file()} == before_merged

    # Isolate changed, appended and moved unnamed components. None has a safe
    # identity merely because a numeric row exists in another edited bank.
    vanilla_entries = read_bank(base)
    unnamed_index = next(i for i, entry in enumerate(vanilla_entries) if not entry['name'])
    for label in ('changed', 'added', 'moved'):
        variant = list(vanilla_entries)
        if label == 'changed':
            variant[unnamed_index] = dict(variant[unnamed_index], data=variant[unnamed_index]['data']+b'guard-test')
        elif label == 'added':
            variant.append(dict(variant[unnamed_index]))
        else:
            other = next(i for i, entry in enumerate(variant) if not entry['name'] and
                         entry['definition'] == variant[unnamed_index]['definition'] and
                         entry['data'] != variant[unnamed_index]['data'])
            variant[unnamed_index], variant[other] = variant[other], variant[unnamed_index]
        source = work/('unnamed-'+label)
        rewrite_bank(base, source, variant)
        failed = subprocess.run([str(tool),'defs','merge',str(base),str(merged),'game.bin',str(source)],
                                capture_output=True,timeout=120)
        (work/('unnamed-'+label+'.log')).write_bytes(failed.stdout+failed.stderr)
        assert failed.returncode != 0 and b'changed or new unnamed definition entries' in failed.stderr, failed.stderr
        assert {p.relative_to(merged): digest(p) for p in merged.rglob('*') if p.is_file()} == before_merged
    reports['isolated_unnamed_refusals'] = ['changed', 'added', 'moved']

    # A competing camera field plus an independent edit: picks must apply only
    # to the conflicting field, and the FMP symbols must follow that choice.
    entries = read_bank(base); ids = {e['name']:i for i,e in enumerate(entries) if e['name']}
    owner = 'CAMERA_MANAGER_SET_TEMPLATE'; field = 'CameraManagerPCCombat'
    rival = work/'Rival'; payload = bytearray(entries[ids[owner]]['data'])
    base_reference = u32(payload, payload.index(struct.pack('<I', crc0(field)))+4)
    for changed, target in [(field, 'CAMERA_PC_NORMAL'), ('CameraManagerMain', 'CAMERA_PC_FAR')]:
        offset = payload.index(struct.pack('<I', crc0(changed)))+4
        struct.pack_into('<I', payload, offset, ids[target])
    entries[ids[owner]] = dict(entries[ids[owner]], data=bytes(payload))
    rewrite_bank(base, rival, entries)
    choices = {}
    for label, pick, reverse in [('last', None, False), ('controller', args.fmp.stem, False),
                                  ('vanilla', 'vanilla', False), ('reversed', None, True)]:
        output = work/('choice-'+label); options = []
        if pick:
            picks = work/(label+'.txt'); picks.write_text(owner+'\t'+pick+'\n', encoding='utf-8')
            options += ['--picks', str(picks)]
        sources_order = [rival, args.fmp] if reverse else [args.fmp, rival]
        choice_base = work/('choice-base-'+label)
        shutil.copytree(base, choice_base)
        for source in sources_order:
            added = subprocess.run([str(tool), 'mods', 'add', str(choice_base), str(source)],
                                   capture_output=True, timeout=60)
            assert added.returncode == 0, added.stderr
        argv = [str(tool), 'mods', 'build', str(choice_base), str(output), '--fields', str(schema)]
        result = subprocess.run(argv+options, capture_output=True, timeout=120)
        (work/('choice-'+label+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode == 0, (result.stdout+result.stderr).decode('utf-8', errors='replace')
        actual_entries = read_bank(output); by_name = {e['name']:i for i,e in enumerate(actual_entries) if e['name']}
        result_payload = actual_entries[by_name[owner]]['data']
        def value(name): return u32(result_payload, result_payload.index(struct.pack('<I', crc0(name)))+4)
        expected = (base_reference if label == 'vanilla' else
                    by_name['CAMERA_MANAGER_PC_MAIN_COMBAT'] if label in ('controller', 'reversed') else ids['CAMERA_PC_NORMAL'])
        assert value(field) == expected, (label, value(field), expected)
        assert value('CameraManagerMain') == ids['CAMERA_PC_FAR'], (label, 'independent field lost')
        choices[label] = {'selected_reference': value(field), 'independent_field_retained': True}
    reports['choices'] = choices

    # An explicit scalar-field choice overrides the rival record choice, and
    # must carry only that FMP part's named-link metadata into the final bank.
    field_base=work/'choice-base-last'
    result=subprocess.run([str(tool),'mods','build',str(field_base),str(work/'field-report'),
                           '--fields',str(schema),'--json'],capture_output=True,timeout=120)
    (work/'field-report.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,result.stderr
    rows=json.loads(result.stdout)['defs']['conflicts']
    row=next(row for row in rows if row['record']==owner)
    key=next(part['choice_key'] for part in row['field_conflicts'] if part['field']==field)
    assert key.startswith('field:'),key
    field_choices={}
    for label,winner in [('controller',args.fmp.stem),('vanilla','vanilla')]:
        picks=work/('field-'+label+'.txt')
        picks.write_text(owner+'\t'+rival.name+'\n'+key+'\t'+winner+'\n',encoding='utf-8')
        saved=picks.read_bytes();output=work/('field-'+label)
        result=subprocess.run([str(tool),'mods','build',str(field_base),str(output),
                               '--fields',str(schema),'--picks',str(picks)],capture_output=True,timeout=120)
        (work/('field-'+label+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode==0,result.stderr
        actual_entries=read_bank(output);actual_ids={e['name']:i for i,e in enumerate(actual_entries) if e['name']}
        payload=actual_entries[actual_ids[owner]]['data']
        actual=u32(payload,payload.index(struct.pack('<I',crc0(field)))+4)
        expected=base_reference if label=='vanilla' else actual_ids['CAMERA_MANAGER_PC_MAIN_COMBAT']
        assert actual==expected and picks.read_bytes()==saved
        assert u32(payload,payload.index(struct.pack('<I',crc0('CameraManagerMain')))+4)==ids['CAMERA_PC_FAR']
        field_choices[label]={'selected_reference':actual,'record_choice_overridden':True,'independent_field_retained':True}
    reports['field_choices']=field_choices

    # Corrupt an owned metadata copy and ensure failed application keeps prior
    # output bytes intact, rather than emitting a successfully imported bank.
    bad_fmp = work/'malformed.fmp'; bad_data = bytearray(args.fmp.read_bytes())
    _, meta_offset = package_records(args.fmp)['GameBINLinkMetaData'][owner]
    bad_data[meta_offset] = 21; bad_fmp.write_bytes(bad_data)
    protected = work/'protected/data/CompiledDefs'; protected.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'): (protected/name).write_bytes(b'prior output '+name.encode())
    old_output = {p:p.read_bytes() for p in protected.iterdir()}
    result = subprocess.run([str(tool), 'fmp', 'apply', str(base), str(bad_fmp), str(protected.parents[1])],
                            capture_output=True, timeout=60)
    (work/'malformed.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode != 0 and b'FMP links:' in result.stderr, result.stderr
    assert all(p.read_bytes() == data for p,data in old_output.items())
    reports['malformed'] = {'refused': True, 'prior_output_preserved': True}
    missing_fmp = work/'missing-target.fmp'; missing_data = bytearray(args.fmp.read_bytes())
    metadata, meta_offset = package_records(args.fmp)['GameBINLinkMetaData'][owner]
    target = b'CAMERA_MANAGER_PC_MAIN_COMBAT'
    assert metadata.count(target) == 1
    offset = meta_offset+metadata.index(target)
    missing_data[offset:offset+len(target)] = b'Z'*len(target)
    missing_fmp.write_bytes(missing_data)
    result = subprocess.run([str(tool), 'fmp', 'apply', str(base), str(missing_fmp), str(protected.parents[1])],
                            capture_output=True, timeout=60)
    (work/'missing-target.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode != 0 and b'missing target' in result.stderr, result.stderr
    assert all(p.read_bytes() == data for p,data in old_output.items())
    reports['missing_target'] = {'refused': True, 'prior_output_preserved': True}
    unused_base = work/'unused-missing-base'; shutil.copytree(base, unused_base)
    for source in (missing_fmp, rival):
        added = subprocess.run([str(tool), 'mods', 'add', str(unused_base), str(source)],
                               capture_output=True, timeout=60)
        assert added.returncode == 0, added.stderr
    unused_out = work/'unused-missing-output'
    result = subprocess.run([str(tool), 'mods', 'build', str(unused_base), str(unused_out),
                             '--fields', str(schema)], capture_output=True, timeout=120)
    (work/'unused-missing.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode == 0, result.stderr
    unused_entry = next(e for e in read_bank(unused_out) if e['name'] == owner)['data']
    assert u32(unused_entry, unused_entry.index(struct.pack('<I', crc0(field)))+4) == ids['CAMERA_PC_NORMAL']
    selected_picks = work/'selected-missing.txt'
    selected_picks.write_text(owner+'\t'+missing_fmp.stem+'\n', encoding='utf-8')
    preserved_out = {p:p.read_bytes() for p in (unused_out/'data/CompiledDefs').iterdir()}
    result = subprocess.run([str(tool), 'mods', 'build', str(unused_base), str(unused_out),
                             '--fields', str(schema), '--picks', str(selected_picks)], capture_output=True, timeout=120)
    (work/'selected-missing.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode != 0 and b'missing target' in result.stderr, result.stderr
    assert all(p.read_bytes() == data for p,data in preserved_out.items())
    reports['composed_missing'] = {'unused_version_allowed': True, 'selected_version_refused': True,
                                   'prior_output_preserved': True}
    field_missing_picks=work/'field-selected-missing.txt'
    field_missing_picks.write_text(owner+'\t'+rival.name+'\n'+key+'\t'+missing_fmp.stem+'\n',encoding='utf-8')
    result=subprocess.run([str(tool),'mods','build',str(unused_base),str(unused_out),
                           '--fields',str(schema),'--picks',str(field_missing_picks)],capture_output=True,timeout=120)
    (work/'field-selected-missing.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode!=0 and b'missing target' in result.stderr,result.stderr
    assert all(p.read_bytes()==data for p,data in preserved_out.items())
    reports['field_selected_missing']={'refused':True,'prior_output_preserved':True}
    # Another source may provide a named target absent during FMP normalization.
    # Both packages add an entry, so its final ID also differs from the provider's.
    provider = work/'Provider'
    supplied_name = 'Z'*len(target)
    supplied_entry = next(e for e in entries if e['name'] == 'CAMERA_MANAGER_PC_MAIN')
    provider_entries = entries+[dict(supplied_entry, name=supplied_name)]
    rewrite_bank(base, provider, provider_entries)
    provider_base = work/'provider-base'; shutil.copytree(base, provider_base)
    for source in (missing_fmp, provider):
        added = subprocess.run([str(tool), 'mods', 'add', str(provider_base), str(source)],
                               capture_output=True, timeout=60)
        assert added.returncode == 0, added.stderr
    provider_out = work/'provider-output'
    result = subprocess.run([str(tool), 'mods', 'build', str(provider_base), str(provider_out),
                             '--fields', str(schema), '--picks', str(selected_picks)], capture_output=True, timeout=120)
    (work/'provided-target.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode == 0, result.stderr
    final_entries = read_bank(provider_out)
    final_ids = {e['name']:i for i,e in enumerate(final_entries) if e['name']}
    provided_payload = final_entries[final_ids[owner]]['data']
    actual = u32(provided_payload, provided_payload.index(struct.pack('<I', crc0(field)))+4)
    assert actual == final_ids[supplied_name]
    assert actual != len(entries), 'fixture must exercise a relocated provider ID'
    assert final_entries[actual]['data'] == supplied_entry['data']
    reports['external_target'] = {'source_id': len(entries), 'final_id': actual,
                                   'resolved_name': supplied_name, 'provider_payload_preserved': True}
    field_provider_out=work/'field-provider-output'
    result=subprocess.run([str(tool),'mods','build',str(provider_base),str(field_provider_out),
                           '--fields',str(schema),'--picks',str(field_missing_picks)],capture_output=True,timeout=120)
    (work/'field-provided-target.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,result.stderr
    field_entries=read_bank(field_provider_out)
    assert [(e['name'],e['definition'],e['data']) for e in field_entries]==[(e['name'],e['definition'],e['data']) for e in final_entries]
    reports['field_external_target']={'final_id':actual,'all_record_payloads_equal_record_pick':True}
    assert {p:digest(p) for p in sources}==before
    (work/'result.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
    for label in ('retail', 'aeon', 'merged'):
        report = reports[label]; print(label,report['checked'],'references checked;',len(report['errors']),'mismatches',flush=True)
    assert all(not reports[label]['errors'] for label in ('retail', 'aeon', 'merged')),'FMP named references do not resolve to their intended targets; see result.json'
    print('Four conflict choices, payload preservation and malformed-output refusal: PASS', flush=True)
    print('External target relocation and selected/unused missing-target checks: PASS', flush=True)
    print('Scalar field precedence, FMP symbols, missing-target refusal and external relocation: PASS',flush=True)


if __name__=='__main__': main()
