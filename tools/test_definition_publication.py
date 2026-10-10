#!/usr/bin/env python3
"""Compiled field publication preserves source pools, recovery and locked outputs."""
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile
from pathlib import Path
from test_mod_field_details import write_bank, read_values
from test_text_write_publication import deny_reads_and_renames
import _winapi

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build');args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='definition-publication-',dir=args.work_root.resolve()));source=work/'source'
    write_bank(source,(1,2,3,4));folder=source/'data/CompiledDefs';bank=folder/'game.bin';names=folder/'names.bin'
    original=bank.read_bytes();original_names=names.read_bytes();checks=0
    print('Evidence retained at',work,flush=True)
    def run(output=None,accepted=True,value=99):
        nonlocal checks
        cmd=[str(args.tool.resolve()),'defs','set-field',str(bank),'0','RandomVillagerMax',str(value),
            '--schema',str(repo/'docs/re_reference/def_schema.json')]
        if output is not None:cmd+=['--out',str(output)]
        r=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
        (work/f'case-{checks}.log').write_text(r.stdout+r.stderr,encoding='utf-8');checks+=1
        assert (r.returncode==0)==accepted,(r.stdout,r.stderr)
        assert names.read_bytes()==original_names,'source name pool changed'
        assert not list(work.rglob('forge-def-prepare-*')),'temporary publication leaked'
        return r
    run();assert bank.read_bytes()==original
    assert (folder/'game.set-field.bin').exists()
    run(names,False);assert bank.read_bytes()==original
    alias=work/'names-alias.bin';os.link(names,alias);run(alias,False)
    # New output copies the exact original pool and both creation markers.
    pack=work/'pack';target=pack/'data/CompiledDefs/game.bin';run(target)
    assert read_values(pack)==(99,2,3,4) and bank.read_bytes()==original
    assert (target.parent/'names.bin').read_bytes()==original_names
    assert Path(str(target)+'.forge-created').exists()
    assert Path(str(target.parent/'names.bin')+'.forge-created').exists()
    first=target.read_bytes()
    with deny_reads_and_renames(target):run(target,False,value=77)
    assert target.read_bytes()==first and bank.read_bytes()==original
    run(target,value=77);assert read_values(pack)==(77,2,3,4)
    assert not Path(str(target)+'.forge-orig').exists()
    run(target,value=66);assert not Path(str(target)+'.forge-orig').exists()
    restored=subprocess.run([str(args.forge.resolve()),'restore','--install',str(pack)],capture_output=True,text=True)
    assert restored.returncode==0 and not target.exists() and not (target.parent/'names.bin').exists(),(restored.stdout,restored.stderr)
    existing=work/'existing';write_bank(existing,(50,2,3,4));existing_target=existing/'data/CompiledDefs/game.bin'
    existing_bytes=existing_target.read_bytes();run(existing_target)
    assert Path(str(existing_target)+'.forge-orig').read_bytes()==existing_bytes
    run(existing_target,value=77);assert Path(str(existing_target)+'.forge-orig').read_bytes()==existing_bytes
    # Incompatible shared pool must be refused before any bank replacement.
    bad=work/'incompatible';write_bank(bad,(5,6,7,8),record='DIFFERENT_NAME')
    bad_target=bad/'data/CompiledDefs/game.bin';old_bad=bad_target.read_bytes()
    bad_names=(bad_target.parent/'names.bin').read_bytes();run(bad_target,False)
    assert bad_target.read_bytes()==old_bad and (bad_target.parent/'names.bin').read_bytes()==bad_names
    # A locked pre-existing bank cannot publish a newly copied name pool.
    lonely=work/'lonely/data/CompiledDefs/game.bin';lonely.parent.mkdir(parents=True);lonely.write_bytes(original)
    with deny_reads_and_renames(lonely):run(lonely,False)
    assert lonely.read_bytes()==original and not (lonely.parent/'names.bin').exists()
    # A missing bank with orphaned recovery cannot be treated as a fresh output.
    orphan=work/'orphan/data/CompiledDefs/game.bin';orphan.parent.mkdir(parents=True)
    orphan_marker=Path(str(orphan)+'.forge-created');orphan_marker.write_bytes(b'keep existing recovery')
    run(orphan,False);assert not orphan.exists() and orphan_marker.read_bytes()==b'keep existing recovery'
    # Names path errors must leave an existing bank and the sentinel unchanged.
    directory=work/'directory/data/CompiledDefs/game.bin';directory.parent.mkdir(parents=True);directory.write_bytes(original)
    names_directory=directory.parent/'names.bin';names_directory.mkdir();(names_directory/'sentinel').write_bytes(b'keep')
    run(directory,False);assert directory.read_bytes()==original and (names_directory/'sentinel').read_bytes()==b'keep'
    # Existing source can be edited with a byte-exact original backup.
    with deny_reads_and_renames(bank):run(bank,False)
    assert bank.read_bytes()==original
    run(bank);assert read_values(source)==(99,2,3,4)
    assert Path(str(bank)+'.forge-orig').read_bytes()==original
    run(bank,value=88);assert Path(str(bank)+'.forge-orig').read_bytes()==original
    r=subprocess.run([str(args.forge.resolve()),'restore','--install',str(source)],capture_output=True,text=True)
    assert r.returncode==0 and bank.read_bytes()==original and names.read_bytes()==original_names,(r.stdout,r.stderr)
    # The game's physical destination and a CompiledDefs junction are guarded.
    helper_source=work/'hold.cpp';helper=work/'hold.exe'
    helper_source.write_text('#include <windows.h>\n#include <cstdio>\nint main(){puts("ready");fflush(stdout);Sleep(60000);}\n',encoding='utf-8')
    compiler=re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$',(repo/'build/CMakeCache.txt').read_text(encoding='utf-8'),re.MULTILINE).group(1)
    subprocess.run([compiler,str(helper_source),'-o',str(helper)],capture_output=True,check=True,timeout=60)
    game=source/'Fable.exe';shutil.copyfile(helper,game)
    junction_root=work/'alias';(junction_root/'data').mkdir(parents=True)
    _winapi.CreateJunction(str(folder),str(junction_root/'data/CompiledDefs'))
    process=subprocess.Popen([str(game)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip()=='ready'
        run(bank,False);run(junction_root/'data/CompiledDefs/game.bin',False)
        assert bank.read_bytes()==original
        # A standalone output from this source is permitted while the source game runs.
        other=work/'other/game.bin';run(other)
    finally:process.terminate();process.communicate(timeout=10)
    assert bank.read_bytes()==original and names.read_bytes()==original_names
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks})+'\n',encoding='utf-8')
    print('PASS:',checks,'publication, exact source-pool preservation, alias guard and recovery checks')

if __name__=='__main__':main()
