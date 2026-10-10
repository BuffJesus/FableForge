#!/usr/bin/env python3
"""Browse type/group trees, search confirmed group names and reveal virtualized identities."""
import argparse,hashlib,json,os,re,subprocess,tempfile
from pathlib import Path
from test_definition_groups_ui import write_fixture
from test_definition_edit_ui import click

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--cwd',type=Path,default=repo);p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-tree-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode,extra in [('small',0),('virtualized',120)]:
   case=work/f'{size}-{mode}';root=case/'install';write_fixture(root,repo,extra);hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
   lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','frames 3',f'assert_state definitions_count {8+extra}',f'assert_state definitions_grouped {2+extra}']
   if mode=='small':
    lines+=['reveal combo_definition_type','frames 3','reveal definition_type_node_0','frames 3','assert_widget definition_type_node_0',
     'reveal definition_group_node_0_2','frames 3','assert_widget definition_group_node_0_2','reveal definition_row_0','frames 3','click definition_row_0','frames 3','assert_state definition_selected 0','assert_state definition_group 2',
     'definition_edit 1','reveal combo_definition_type','frames 3','reveal definition_row_1','frames 3','click definition_row_1','frames 3','assert_state definition_selected 0','assert_state definition_editing 1','definition_cancel',
     'definition_search GROUP_NORMAL','frames 3','assert_state definitions_filtered 2','reveal combo_definition_type','frames 3','reveal definition_row_1','frames 3','click definition_row_1','frames 3','assert_state definition_selected 1',
     'definition_search -','frames 3','reveal combo_definition_type','frames 3','reveal definition_group_node_0_3','frames 3','assert_widget definition_group_node_0_3',
     'definition_bank frontend.bin','wait_definitions','frames 3','assert_state definitions_grouped 0','definition_select OBJ_A','frames 3','assert_state definition_group -','definition_search GROUP_NORMAL','frames 3','assert_state definitions_filtered 1']
   else:
    lines+=['definition_select OBJ_EXTRA_99','frames 3','assert_state definition_selected 107','assert_state definition_group 2','reveal combo_definition_type','frames 3','reveal definition_row_107','frames 3','assert_widget definition_row_107','click definition_row_107','frames 3','assert_state definition_selected 107',
     f'screenshot {(case/"tree.png").as_posix()}','definition_search GROUP_NORMAL','frames 3','assert_state definitions_filtered 2','definition_search -','frames 3','definition_select OBJ_B','frames 3','assert_state definition_selected 1',
     'reveal combo_definition_type','frames 3','reveal definition_row_1','frames 3','assert_widget definition_row_1']
   lines+=['quit'];script=case/'tree.txt';script.write_text('\n'.join(lines)+'\n')
   r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text();assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6500:]
   assert 'ok   assert_widget definition_row_107' in trace if mode=='virtualized' else 'ok   assert_state definitions_filtered 1' in trace
   assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items()) and not list(root.rglob('*.forge-orig'))
   print(size,mode,'tree groups, search, indexed reveal and protected source: PASS',flush=True)
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode,names,first,second in [('alphabetical',('Z_GROUP','A_GROUP'),3,2),('duplicate',('SAME_GROUP','SAME_GROUP'),2,3)]:
   case=work/f'{size}-{mode}';root=case/'install';write_fixture(root,repo,group_names=names)
   hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
   script=case/'sort.txt';script.write_text('\n'.join(['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','frames 3','reveal combo_definition_type','frames 3',f'dump_widget definition_group_node_0_{first}',f'dump_widget definition_group_node_0_{second}','definition_select OBJ_A','frames 3','assert_state definition_selected 0','assert_state definition_group 2','quit'])+'\n')
   r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text();assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,(r.returncode,trace[-6500:])
   def y(index):
    match=re.search(r'widget_rect definition_group_node_0_'+str(index)+r' ([-0-9.]+) ([-0-9.]+)',trace);assert match,trace
    return float(match.group(2))
   assert y(first)<y(second),(mode,trace)
   assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
   print(size,mode,'group ordering and native identity: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':8,'source_hashes_unchanged':True})+'\n')
if __name__=='__main__':main()
