#!/usr/bin/env python3
"""Unsupported sibling/FMP banks refuse before writes; unchanged carries survive."""
import argparse,hashlib,json,shutil,struct,subprocess,tempfile,zlib
from pathlib import Path
from test_mod_field_details import write_bank,read_values,RECORD,TYPE
from test_fmp_links import package_records

def snapshot(root):
    if root.is_file(): return {'file':hashlib.sha256(root.read_bytes()).hexdigest()}
    return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}

def fmp(path,banks):
    raw=bytearray(b'B\0\0\0'+struct.pack('<III',0,0,510));rows=[]
    for name,entries in banks:
        payloads=[]
        for payload in entries:
            payloads.append((len(raw),len(payload)));raw+=payload
        start=len(raw);raw+=struct.pack('<I',0)
        for offset,length in payloads:
            symbol=RECORD.encode();definition=TYPE.encode()+b'\0'
            raw+=struct.pack('<7I',0,0,0,length,offset,0,len(symbol))+symbol
            raw+=struct.pack('<III',0,0,len(definition))+definition
        rows.append((name,len(entries),start,len(raw)-start))
    directory=len(raw);raw+=struct.pack('<I',len(rows))
    for index,(name,count,start,length) in enumerate(rows):
        raw+=name.encode()+b'\0'+struct.pack('<5I',index,count,start,length,1)
    struct.pack_into('<I',raw,8,directory);path.write_bytes(raw)
    assert set(package_records(path))=={name for name,_ in banks}

def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    args=parser.parse_args();tool=args.tool.resolve()
    work=Path(tempfile.mkdtemp(prefix='mod-bank-refusal-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    base,source,changed,out=[work/name for name in ('base','source','changed','output')]
    write_bank(base,(1,2,3,4));write_bank(source,(10,2,3,4));write_bank(changed,(11,2,3,4))
    for bank in ('script.bin','frontend.bin'):
        shutil.copyfile(base/'data/CompiledDefs/game.bin',base/'data/CompiledDefs'/bank)
        shutil.copyfile(base/'data/CompiledDefs/game.bin',source/'data/CompiledDefs'/bank)
    out.mkdir();(out/'prior.txt').write_bytes(b'preserve prior output')
    checks=[]
    def run(label,argv,refusal=None):
        inputs=[base,source,changed]
        if argv[0] in ('mods','defs'): inputs.append(Path(argv[2]))
        if '--with' in argv: inputs.append(Path(argv[argv.index('--with')+1]))
        if argv[:2] in (['fmp','apply'],['fmp','export']): inputs += [Path(argv[2]),Path(argv[3])]
        before={str(root):snapshot(root) for root in inputs}
        prior=snapshot(out)
        proc=subprocess.run([str(tool),*map(str,argv)],cwd=args.cwd.resolve(),capture_output=True,timeout=120)
        (work/(label+'.log')).write_bytes(proc.stdout+proc.stderr)
        if refusal:
            assert proc.returncode!=0 and refusal.encode() in proc.stderr,(label,proc.stderr)
            assert snapshot(out)==prior,label
        else: assert proc.returncode==0,(label,proc.stderr)
        assert {str(root):snapshot(root) for root in inputs}==before,label
        checks.append({'check':label,'passed':True})
    # A source may carry unchanged banks or a compression-only re-save.
    run('unchanged',['mods','merge',base,out,'--with',source])
    assert read_values(out)==(10,2,3,4)
    original=(source/'data/CompiledDefs/script.bin').read_bytes()
    chunk=zlib.compress(zlib.decompress(original[49:]),level=0)
    raw=bytearray(original[:49]);struct.pack_into('<II',raw,41,len(chunk),len(chunk));raw+=chunk
    assert raw!=original
    (source/'data/CompiledDefs/script.bin').write_bytes(raw)
    run('recompressed',['mods','merge',base,out,'--with',source])
    assert read_values(out)==(10,2,3,4)
    (source/'data/CompiledDefs/script.bin').write_bytes(original)
    for bank in ('script.bin','frontend.bin'):
        target=source/'data/CompiledDefs'/bank;original_bank=target.read_bytes()
        target.write_bytes((changed/'data/CompiledDefs/game.bin').read_bytes())
        run('changed-'+bank,['mods','merge',base,out,'--with',source],'changed sibling definition banks')
        run('export-'+bank,['fmp','export',base,source,out/'export.fmp'],'changed sibling definition banks')
        target.write_bytes(original_bank)
    # A partial tree with only script.bin must not bypass root normalization.
    partial=work/'partial/data/CompiledDefs';partial.mkdir(parents=True)
    shutil.copyfile(source/'data/CompiledDefs/names.bin',partial/'names.bin')
    shutil.copyfile(changed/'data/CompiledDefs/game.bin',partial/'script.bin')
    run('partial',['mods','merge',base,out,'--with',partial.parent.parent],'changed sibling definition banks')
    # Reusing raw bytes with a different existing name pool is also unsafe.
    names_path=source/'data/CompiledDefs/names.bin';old_names=names_path.read_bytes()
    altered=bytearray(old_names);altered[-2]^=1;names_path.write_bytes(altered)
    run('different-name-pool',['mods','merge',base,out,'--with',source],'changed sibling definition banks')
    names_path.write_bytes(old_names)
    extended=bytearray(old_names);extra=struct.pack('<I',zlib.crc32(b'UNUSED_NEW_NAME'))+b'UNUSED_NEW_NAME\0'
    extended+=extra;struct.pack_into('<I',extended,8,3)
    struct.pack_into('<I',extended,12,struct.unpack_from('<I',old_names,12)[0]+len(extra))
    names_path.write_bytes(extended)
    run('extended-name-pool',['mods','merge',base,out,'--with',source])
    names_path.write_bytes(old_names)
    # Registered builds, conflict checks and unstaged deploy also refuse before
    # changing baseline files or the prior build output.
    (source/'data/CompiledDefs/script.bin').write_bytes((changed/'data/CompiledDefs/game.bin').read_bytes())
    added=subprocess.run([str(tool),'mods','add',str(base),str(source)],capture_output=True,timeout=60)
    assert added.returncode==0,added.stderr
    run('registered-build',['mods','build',base,out],'changed sibling definition banks')
    run('registered-conflicts',['mods','conflicts',base,'--json'],'changed sibling definition banks')
    run('registered-deploy',['mods','deploy',base],'changed sibling definition banks')
    # A bank-scoped defs command still applies only its explicitly requested bank.
    run('explicit-game-bank',['defs','merge',base,out,'game.bin',source])
    assert read_values(out)==(10,2,3,4)
    (source/'data/CompiledDefs/script.bin').write_bytes(original)
    # FMP with game changes and unsupported banks refuses the entire package.
    payload=zlib.decompress((source/'data/CompiledDefs/game.bin').read_bytes()[49:])[2:]
    for bank in ('ScriptBINEntries','FrontEndBINEntries','graphics','FinalAlbionSTB','UnknownBank'):
        package=work/(bank+'.fmp');fmp(package,[('GameBINEntries',[payload]),(bank,[payload])])
        run('standalone-'+bank,['fmp','apply',base,package,out],'FMP banks:')
        run('composed-'+bank,['mods','merge',base,out,'--with',package],'FMP banks:')
    empty=work/'empty-sibling.fmp';fmp(empty,[('GameBINEntries',[payload]),('ScriptBINEntries',[])])
    run('empty-sibling',['fmp','apply',base,empty,out])
    assert read_values(out)==(10,2,3,4)
    run('named-export',['fmp','export',base,source,out/'export.fmp'])
    assert len(package_records(out/'export.fmp')['GameBINEntries'])==1
    unnamed_base,unnamed_source=work/'unnamed-base',work/'unnamed-source'
    write_bank(unnamed_base,(1,2,3,4),record='');write_bank(unnamed_source,(10,2,3,4),record='')
    run('unnamed-export',['fmp','export',unnamed_base,unnamed_source,out/'export.fmp'],
        'changed or new unnamed definition entries')
    ordinal=work/'unnamed-ordinal';write_bank(ordinal,(1,2,3,4),record='')
    raw=bytearray((ordinal/'data/CompiledDefs/game.bin').read_bytes());struct.pack_into('<i',raw,21,1)
    (ordinal/'data/CompiledDefs/game.bin').write_bytes(raw)
    run('unnamed-ordinal-export',['fmp','export',unnamed_base,ordinal,out/'export.fmp'],
        'changed or new unnamed definition entries')
    unicode_root=work/'base \u00e9 \u03a9 \u6f22\u5b57 %LITERAL_BANK_TOKEN% & files'
    unicode_source=work/'source \u00e9 \u03a9 \u6f22\u5b57 %LITERAL_BANK_TOKEN% & files'
    shutil.copytree(base,unicode_root);shutil.copytree(source,unicode_source)
    run('unicode-unchanged',['mods','merge',unicode_root,out,'--with',unicode_source])
    (unicode_source/'data/CompiledDefs/script.bin').write_bytes((changed/'data/CompiledDefs/game.bin').read_bytes())
    run('unicode-refusal',['mods','merge',unicode_root,out,'--with',unicode_source],'changed sibling definition banks')
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'gameplay_tested':False},indent=2),encoding='utf-8')
    print('PASS:',len(checks),'bank refusal/carry cases; prior outputs and inputs preserved',flush=True)

if __name__=='__main__':main()
