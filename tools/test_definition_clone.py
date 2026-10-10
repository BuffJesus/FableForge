#!/usr/bin/env python3
"""Clone exact donor records and validate paired publication/recovery independently."""
import argparse,contextlib,ctypes,hashlib,json,os,re,shutil,struct,subprocess,tempfile,zlib
from pathlib import Path
from ctypes import wintypes
from test_mod_field_details import tag
from test_fmp_links import read_bank

def fixture(root):
 folder=root/'data/CompiledDefs';folder.mkdir(parents=True);pool=bytearray();refs={}
 for name in ('OPAQUE','OTHER','DONOR','1','DUP','POOL_ONLY','POOL_ONLY'):
  refs.setdefault(name,len(pool)+4);pool+=struct.pack('<I',tag(name))+name.encode()+b'\0'
 (folder/'names.bin').write_bytes(struct.pack('<5I',0,0xa8e36c34,7,len(pool)+4,0)+pool)
 payloads=[bytes([i,255,0,41+i]) for i in range(5)];labels=['DONOR','1',None,'DUP','DUP'];types=['OPAQUE','OTHER','OPAQUE','OPAQUE','OPAQUE'];ordinals=[0,0,1,2,3]
 block=struct.pack('<5H',*[10+4*i for i in range(5)])+b''.join(payloads);chunk=zlib.compress(block)
 table=b''.join(struct.pack('<iii',refs[t],-1 if n is None else refs[n],ordinal) for t,n,ordinal in zip(types,labels,ordinals))
 (folder/'game.bin').write_bytes(bytes(9)+struct.pack('<I',5)+table+struct.pack('<I5I',2,0,0,5,len(chunk),len(chunk))+chunk)
 return folder

@contextlib.contextmanager
def deny_rename(path):
 kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE];kernel.CreateFileW.restype=wintypes.HANDLE;kernel.CloseHandle.argtypes=[wintypes.HANDLE]
 handle=kernel.CreateFileW(str(path),0x80000000,3,None,3,0x80,None);assert handle!=wintypes.HANDLE(-1).value
 try:yield
 finally:kernel.CloseHandle(handle)

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe');p.add_argument('--forge',type=Path,default=repo/'build/forge.exe');p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args();work=Path(tempfile.mkdtemp(prefix='definition-clone-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True);source=work/'source';folder=fixture(source);bank=folder/'game.bin';names=folder/'names.bin';original=bank.read_bytes();oldNames=names.read_bytes();rows=read_bank(source);checks=0
 def run(out,donor='DONOR',new='CLONED',accepted=True,extra=()):
  nonlocal checks
  r=subprocess.run([str(a.tool.resolve()),'defs','clone',str(bank),donor,new,'--out',str(out),'--json',*extra],cwd=repo,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45);(work/f'case-{checks}.log').write_text(r.stdout+r.stderr,encoding='utf-8');checks+=1
  assert (r.returncode==0)==accepted,(r.returncode,r.stdout,r.stderr);assert bank.read_bytes()==original and names.read_bytes()==oldNames,'source changed';assert not list(work.rglob('forge-def-clone-*')),'staging leaked';return r
 for number,(donor,index,new) in enumerate([('DONOR',0,'CLONED'),('1',1,'NUMERIC'),('2',2,'UNNAMED'),('3',3,'INDEXED_DUP'),('DONOR',0,'POOL_ONLY')]):
  outroot=work/(f'valid-{number}'+' \u03a9' if number==0 else f'valid-{number}');out=outroot/'data/CompiledDefs/game.bin';r=run(out,donor,new);result=json.loads(r.stdout);assert result['entry_index']==5 and result['donor_index']==index
  after=read_bank(outroot);assert after[:5]==rows and after[5]==dict(rows[index],name=new);raw=out.read_bytes();assert raw[13:73]==original[13:73]
  pool=(out.parent/'names.bin').read_bytes();assert pool[20:len(oldNames)]==oldNames[20:];assert pool[:8]==oldNames[:8] and pool[16:20]==oldNames[16:20]
  assert Path(str(out)+'.forge-created').exists() and Path(str(out.parent/'names.bin')+'.forge-created').exists()
  rr=subprocess.run([str(a.forge.resolve()),'restore','--install',str(outroot)],cwd=repo,capture_output=True,text=True,encoding='utf-8');assert rr.returncode==0 and not out.exists() and not (out.parent/'names.bin').exists(),rr.stdout+rr.stderr
 for donor in ('DUP','MISSING','0junk',' 0','+0','-1','0.5','184467440737095516160','999',''):
  outroot=work/f'refused-{checks}';run(outroot/'data/CompiledDefs/game.bin',donor,accepted=False);assert not outroot.exists()
 for new in ('','DONOR','DUP','X'*128,'BAD\nNAME','BAD\u00e9'):
  outroot=work/f'name-{checks}';run(outroot/'data/CompiledDefs/game.bin',new=new,accepted=False);assert not outroot.exists()
 existing=work/'existing';shutil.copytree(source,existing);target=existing/'data/CompiledDefs/game.bin';run(target);assert read_bank(existing)[:5]==rows;assert Path(str(target)+'.forge-orig').read_bytes()==original and Path(str(target.parent/'names.bin')+'.forge-orig').read_bytes()==oldNames
 rr=subprocess.run([str(a.forge.resolve()),'restore','--install',str(existing)],cwd=repo,capture_output=True,text=True,encoding='utf-8');assert rr.returncode==0 and target.read_bytes()==original and (target.parent/'names.bin').read_bytes()==oldNames
 for mode in ('different-pool','different-bank','missing-pool','orphan','rollback','bank-alias','names-alias','same-path','unknown-option'):
  case=work/mode;out=case/'data/CompiledDefs/game.bin'
  if mode in ('different-pool','different-bank','rollback'):shutil.copytree(source,case)
  else:out.parent.mkdir(parents=True)
  pool=out.parent/'names.bin'
  if mode=='different-pool':pool.write_bytes(oldNames[:-1]+b'X')
  if mode=='different-bank':
   other=fixture(case/'other');out.write_bytes((other/'game.bin').read_bytes().replace(b'\0'*9,b'\x01'+b'\0'*8,1));raw=bytearray(out.read_bytes());raw[21]=7;out.write_bytes(raw)
  if mode=='missing-pool':out.write_bytes(original)
  if mode=='orphan':Path(str(out)+'.forge-created').write_bytes(b'preserve')
  if mode=='bank-alias':os.link(bank,out);pool.write_bytes(oldNames)
  if mode=='names-alias':os.link(names,pool)
  if mode=='same-path':out=names
  before={q:q.read_bytes() for q in case.rglob('*') if q.is_file()}
  if mode=='rollback':
   with deny_rename(pool):run(out,accepted=False)
   assert out.read_bytes()==original and pool.read_bytes()==oldNames
  else:run(out,accepted=False,extra=['--bogus'] if mode=='unknown-option' else [])
  assert all(q.read_bytes()==data for q,data in before.items()),mode
 # In-place cloning updates both files and Restore returns the exact pair.
 inplace=work/'inplace';shutil.copytree(source,inplace);ib=inplace/'data/CompiledDefs/game.bin';ip=ib.parent/'names.bin';r=subprocess.run([str(a.tool.resolve()),'defs','clone',str(ib),'DONOR','INPLACE','--out',str(ib),'--json'],cwd=repo,capture_output=True,text=True,encoding='utf-8');assert r.returncode==0,r.stdout+r.stderr;assert read_bank(inplace)[:5]==rows
 rr=subprocess.run([str(a.forge.resolve()),'restore','--install',str(inplace)],cwd=repo,capture_output=True,text=True,encoding='utf-8');assert rr.returncode==0 and ib.read_bytes()==original and ip.read_bytes()==oldNames
 # Running-owner checks use an owned sleeping helper, never the real game.
 import _winapi
 helper_source=work/'hold.cpp';helper=work/'hold.exe';helper_source.write_text('#include <windows.h>\n#include <cstdio>\nint main(){puts("ready");fflush(stdout);Sleep(60000);}\n',encoding='utf-8')
 compiler=re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$',(repo/'build/CMakeCache.txt').read_text(encoding='utf-8'),re.MULTILINE).group(1)
 subprocess.run([compiler,str(helper_source),'-o',str(helper)],capture_output=True,check=True,timeout=60);game=source/'Fable.exe';shutil.copyfile(helper,game)
 junction=work/'junction';(junction/'data').mkdir(parents=True);_winapi.CreateJunction(str(folder),str(junction/'data/CompiledDefs'))
 process=subprocess.Popen([str(game)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
 try:
  assert process.stdout.readline().strip()=='ready';run(bank,accepted=False);run(junction/'data/CompiledDefs/game.bin',accepted=False)
  run(work/'running-source-output/data/CompiledDefs/game.bin')
 finally:process.terminate();process.communicate(timeout=10)
 checks+=1;(work/'result.json').write_text(json.dumps({'passed':True,'cases':checks})+'\n');print('PASS:',checks,'clone/selector/publication/rollback/Restore cases',flush=True)
if __name__=='__main__':main()
