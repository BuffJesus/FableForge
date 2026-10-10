#!/usr/bin/env python3
"""Bounded read-only FableWin CDefs serialization/lookup evidence packet.

Optional local evidence dependencies: pefile and capstone. Not a shipping test.
No Ghidra project, game file or native source is modified.
"""
import argparse
from bisect import bisect_right
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import capstone
import pefile


FUNCTIONS={
    'compile_subdef':0x03060c70,
    'add_subdef':0x030630f0,
    'subdef_info_ctor':0x03063370,
    'save_parent':0x030633f0,
    'load_parent':0x030634d0,
    'load_base':0x03063850,
    'get_creature':0x01ac010e,
    'get_appearance':0x01b207b4,
}
EXPECTED_EXE='a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845'
ANIMATION_FUNCTIONS={
    'persist_animation':0x01998a2d,
    'animation_traits_in':0x019d4208,
    'animation_traits_out':0x019d4223,
    'animation_set_in':0x01fa26b0,
    'animation_set_out':0x01fa27a0,
    'animation_entry_in':0x01f9d270,
    'animation_entry_out':0x01f9d590,
    'allocate_animation_component':0x01fa0770,
    'animation_rotation':0x01fa0b90,
    'animation_postinit':0x01fa0f50,
    'animation_add':0x01fa0f80,
    'component_flags_in':0x01f9b300,
    'component_flags_out':0x01f9b330,
    'component_transition_in':0x01f9b4c0,
    'component_transition_out':0x01f9b4f0,
    'component_delay_in':0x01f9b680,
    'component_delay_out':0x01f9b6b0,
    'component_handedness_in':0x01f9bae0,
    'component_handedness_out':0x01f9bb20,
    'component_combo_in':0x01f9be90,
    'component_combo_out':0x01f9bed0,
    'component_recoil_in':0x01f9c070,
    'component_recoil_out':0x01f9c0a0,
    'component_misc_in':0x01f9c490,
    'component_misc_out':0x01f9c4d0,
    'component_offset_in':0x01f9c8e0,
    'component_offset_out':0x01f9c930,
    'component_following_in':0x01f9cab0,
    'component_following_out':0x01f9cae0,
    'component_creature_mode_in':0x01f9ccb0,
    'component_creature_mode_out':0x01f9cce0,
    'component_following_filter_in':0x01f9ce40,
    'component_following_filter_out':0x01f9ce70,
    'component_speed_in':0x01f9cfe0,
    'component_speed_out':0x01f9d010,
    'component_response_in':0x01f9d160,
    'component_response_out':0x01f9d200,
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--work-root',type=Path,required=True)
    parser.add_argument('--feature',choices=('cdefs','animation'),default='cdefs',
                        help='Bounded ownership or appearance Animation serialization evidence')
    parser.add_argument('--pdb',type=Path,help='Matching FableWin PDB for member-name corroboration')
    parser.add_argument('--pdb-oracle',type=Path,help='Existing read-only query_pdb_oracle.ps1')
    args=parser.parse_args();raw=args.exe.read_bytes();identity=hashlib.sha256(raw).hexdigest()
    assert bool(args.pdb)==bool(args.pdb_oracle),'provide both PDB and oracle'
    assert identity==EXPECTED_EXE,'unsupported native executable identity'
    symbols={}
    for line in args.symbols.read_text(encoding='utf-8').splitlines():
        address,sep,name=line.partition('\t')
        if sep:
            try:symbols[int(address,16)]=name
            except ValueError:pass
    addresses=sorted(symbols);pe=pefile.PE(data=raw);base=pe.OPTIONAL_HEADER.ImageBase
    dis=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    packets=[];text=[]
    for label,address in (FUNCTIONS if args.feature=='cdefs' else ANIMATION_FUNCTIONS).items():
        assert address in symbols,(label,'missing exact symbol')
        end=addresses[bisect_right(addresses,address)]
        assert 0<end-address<=0x800,(label,'unexpected function extent')
        body=pe.get_data(address-base,end-address)
        packet=dict(label=label,address=f'{address:08x}',end=f'{end:08x}',symbol=symbols[address],
                    bytes_sha256=hashlib.sha256(body).hexdigest(),instructions=[])
        text.append(f'=== {label} {address:08x}..{end:08x} {symbols[address]} ===')
        for instruction in dis.disasm(body,address):
            row=dict(address=f'{instruction.address:08x}',bytes=instruction.bytes.hex(),
                     mnemonic=instruction.mnemonic,operands=instruction.op_str)
            annotation=''
            if instruction.mnemonic in ('call','jmp') and instruction.op_str.startswith('0x'):
                target=int(instruction.op_str,16);resolved=target
                thunk=pe.get_data(target-base,5)
                if len(thunk)==5 and thunk[0]==0xe9:
                    resolved=target+5+struct.unpack_from('<i',thunk,1)[0]
                row.update(target=f'{target:08x}',resolved_target=f'{resolved:08x}',target_symbol=symbols.get(resolved))
                annotation=' ; '+f'{resolved:08x} '+str(symbols.get(resolved,''))
            packet['instructions'].append(row)
            text.append(f'{instruction.address:08x} {instruction.bytes.hex():24s} {instruction.mnemonic} {instruction.op_str}{annotation}')
        packets.append(packet)
    assert args.exe.read_bytes()==raw,'native executable changed'
    args.work_root.resolve().mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix=f'creature-{args.feature}-native-',dir=args.work_root.resolve()))
    report=dict(feature=args.feature,exe=str(args.exe.resolve()),exe_sha256=identity,symbols=str(args.symbols.resolve()),
                symbols_sha256=hashlib.sha256(args.symbols.read_bytes()).hexdigest(),functions=packets)
    if args.pdb:
        layouts=[]
        types=(('CDefClassBase::CSubDefInfo',('long DefIndex','long OriginalParentDefIndex')),
               ) if args.feature=='cdefs' else (
                   ('CAnimationEntry',('long BankIndex','CDefString AnimName','CDefString GroupName')),
                   ('CAnimationSet',('long DefaultFlags','long DefaultTransitionTime','long DefaultDelay','CDefString DefaultGroup')),
               )
        for name,members in types:
            result=subprocess.run(['powershell','-NoProfile','-File',str(args.pdb_oracle.resolve()),
                '-Name',name,'-Mode','Type','-PdbPath',str(args.pdb.resolve())],
                capture_output=True,text=True,timeout=120)
            assert result.returncode==0,result.stdout+result.stderr
            assert all(member in result.stdout for member in members),(name,result.stdout)
            layouts.append(result.stdout+result.stderr)
        report.update(pdb=str(args.pdb.resolve()),pdb_sha256=hashlib.sha256(args.pdb.read_bytes()).hexdigest())
        (work/'pdb-layout.txt').write_text('\n'.join(layouts),encoding='utf-8')
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (work/'disassembly.txt').write_text('\n'.join(text)+'\n',encoding='utf-8')
    print('Bounded native packet retained at',work)


if __name__=='__main__':main()
