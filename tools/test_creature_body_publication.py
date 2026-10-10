#!/usr/bin/env python3
"""Independent retail body binding publication/Restore; never copy asset banks."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from audit_creature_body_models import body_slots, mesh_directory, file_hash
from test_fmp_links import read_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--publisher',type=Path,default=repo/'build/fableforge_creaturevisualclone_tests.exe')
    parser.add_argument('--tools',type=Path,help='Exercise the public forge-tools CLI instead of the test publisher')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();native=args.install.resolve()
    assert not args.work_root.resolve().is_relative_to(native)
    inputs=sorted((native/'data/CompiledDefs').glob('*.bin'))+[native/'data/graphics/graphics.big']
    def hashes():return {str(p):file_hash(p) for p in inputs}
    before=hashes();records=read_bank(native)
    directory=mesh_directory(inputs[-1])
    valid={e['id'] for e in directory if e['type']==5 and sum(x['id']==e['id'] for x in directory)==1}
    choices=[]
    for index,record in enumerate(records):
        if record['definition']!='CCreatureDef':continue
        try:_,slots=body_slots(record['data'])
        except ValueError:continue
        for slot in slots:
            replacement=next((s['id'] for s in slots if s['body_part']==slot['body_part'] and
                              s['id']!=slot['id'] and s['id'] in valid),None)
            if slot['id'] in valid and replacement is not None:
                choices.append(dict(entry=index,**slot,replacement=replacement));break
        if len(choices)==2:break
    assert len(choices)==2,'two native alternative fixtures required'
    work=Path(tempfile.mkdtemp(prefix='creature-body-publication-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    source_bank=(native/'data/CompiledDefs/game.bin').read_bytes()
    source_names=(native/'data/CompiledDefs/names.bin').read_bytes()
    for number,choice in enumerate(choices):
        output=work/f'output-{number}'
        command=([str(args.tools.resolve()),'defs','bind-body-model',str(native/'data/CompiledDefs/game.bin'),
                  str(choice['entry']),str(choice['body_part']),str(choice['alternative']),str(choice['replacement']),
                  '--schema',str(args.schema.resolve()),'--graphics',str(inputs[-1]),'--out',str(output/'data/CompiledDefs/game.bin'),'--json']
                 if args.tools else
                 [str(args.publisher.resolve()),'--body-publish',str(native),str(args.schema.resolve()),str(output),
                  str(choice['entry']),str(choice['body_part']),str(choice['alternative']),str(choice['replacement'])])
        result=subprocess.run(command,
                              cwd=repo,capture_output=True,text=True,timeout=120)
        (work/f'publisher-{number}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
        proof=json.loads(result.stdout);assert proof['offset']==choice['offset'] and proof['old_model_id' if args.tools else 'old_id']==choice['id']
        expected=[dict(record) for record in records]
        data=bytearray(expected[choice['entry']]['data']);struct.pack_into('<I',data,choice['offset'],choice['replacement'])
        expected[choice['entry']]['data']=bytes(data)
        assert read_bank(output)==expected,'published record delta differs'
        bank=output/'data/CompiledDefs/game.bin';names=bank.parent/'names.bin'
        assert bank.read_bytes()[:13+12*len(records)]==source_bank[:13+12*len(records)],'identity/header table changed'
        assert names.read_bytes()==source_names,'name pool changed'
        for path in (bank,names):assert Path(str(path)+'.forge-created').is_file()
        restored=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],cwd=repo,
                                capture_output=True,text=True,timeout=60)
        (work/f'restore-{number}.log').write_text(restored.stdout+restored.stderr,encoding='utf-8')
        assert restored.returncode==0,restored.stdout+restored.stderr
        assert not bank.exists() and not names.exists(),'Restore left created bank'
        assert not list(output.rglob('*.forge-created')),'Restore left creation records'
        assert hashes()==before,'native inputs changed'
        print('Native component',choice['entry'],'exact four-byte publication and Restore: PASS',flush=True)
    negative_cases=0
    if args.tools:
        choice=choices[0];refusal=work/'refused'/'game.bin'
        command=[str(args.tools.resolve()),'defs','bind-body-model',str(native/'data/CompiledDefs/game.bin'),
                 str(choice['entry']),str(choice['body_part']),str(choice['alternative']),str(choice['replacement']),
                 '--schema',str(args.schema.resolve()),'--graphics',str(inputs[-1]),'--out',str(refusal),'--json']
        variants=[]
        for position,value in [(4,'-1'),(4,'9116junk'),(4,'0x23'),(4,'18446744073709551616'),(4,'999999'),(4,'0'),
                               (5,'9999'),(6,'9999'),(7,'4294967296'),(7,'999999')]:
            changed=list(command);changed[position]=value;variants.append(changed)
        variants+=[command+['--unknown'],command+['--out',str(refusal)],command[:8]+command[10:],
                   command[:10]+command[12:],command[:-3]+['--out']]
        for index,variant in enumerate(variants):
            result=subprocess.run(variant,cwd=repo,capture_output=True,text=True,timeout=60)
            (work/f'refusal-{index}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode!=0,'invalid binding command accepted'
            assert not refusal.parent.exists(),'refused command created output'
            negative_cases+=1
        assert hashes()==before,'CLI refusal changed native input'
        print(negative_cases,'CLI selector/option/range refusals: PASS',flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True,choices=choices,source_hashes=before,negative_cases=negative_cases,
                                                  runtime_compatibility_verified=False),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
