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


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--work-root',type=Path,required=True)
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
    for label,address in FUNCTIONS.items():
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
    work=Path(tempfile.mkdtemp(prefix='creature-cdefs-native-',dir=args.work_root.resolve()))
    report=dict(exe=str(args.exe.resolve()),exe_sha256=identity,symbols=str(args.symbols.resolve()),
                symbols_sha256=hashlib.sha256(args.symbols.read_bytes()).hexdigest(),functions=packets)
    if args.pdb:
        result=subprocess.run(['powershell','-NoProfile','-File',str(args.pdb_oracle.resolve()),
            '-Name','CDefClassBase::CSubDefInfo','-Mode','Type','-PdbPath',str(args.pdb.resolve())],
            capture_output=True,text=True,timeout=120)
        assert result.returncode==0,result.stdout+result.stderr
        assert 'long DefIndex' in result.stdout and 'long OriginalParentDefIndex' in result.stdout
        report.update(pdb=str(args.pdb.resolve()),pdb_sha256=hashlib.sha256(args.pdb.read_bytes()).hexdigest())
        (work/'pdb-layout.txt').write_text(result.stdout+result.stderr,encoding='utf-8')
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (work/'disassembly.txt').write_text('\n'.join(text)+'\n',encoding='utf-8')
    print('Bounded native packet retained at',work)


if __name__=='__main__':main()
