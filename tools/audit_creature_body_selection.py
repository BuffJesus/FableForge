#!/usr/bin/env python3
"""Bounded original-x86 body-selection control-flow oracle, with explicit bridges.

Executes FableWin GetRandomBodyParts and GFRandom. By default texture selection is
a Python bridge; --native-textures executes that original routine too, retaining
only container access/copy bridges for clean ordered maps. Neither mode validates
skeletal selection, native allocation/ABI, animation or rendering.
Requires pefile/unicorn. Native executable and optional install are read-only.
"""
import argparse
import copy
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

from audit_creature_appearance_layout import decode, names_pool
from audit_creature_appearance_rng import EXPECTED_SHA, recovered
from test_fmp_links import read_bank


BODY = 0x0289b110
RANGES = [(BODY, 361), (0x018cf370, 87), (0x0183fdab, 5), (0x018bad60, 18), (0x01797c14, 5)]
STUBS = {0x017c1979:'construct', 0x0182e4f7:'destroy', 0x017d2602:'body',
         0x017c11a4:'empty', 0x01799b95:'size', 0x017c5b2d:'model',
         0x017984fc:'body_textures', 0x01801ebb:'textures', 0x01856f1a:'append'}
TEXTURE_RANGES = [(0x01801ebb,5),(0x0289b300,991)]
ITERATORS = {0x01859ecc:'iter_construct', 0x0182f5f5:'begin', 0x018592ba:'iter_copy',
            0x017b80c2:'iter_destroy', 0x017c4570:'end', 0x0184b039:'iter_ne',
            0x01842605:'iter_read', 0x017ff28d:'upper_bound', 0x018282dc:'iter_copy',
            0x0180884c:'iter_next', 0x01831995:'texture_pair', 0x018446d0:'texture_append'}
MORPH_RANGES = [(0x0289ac90,78),(0x017a87ee,5),(0x017fe923,5),(0x0289ad40,567),
                (0x017a061b,5),(0x0289b010,169),(0x0181d55d,5),(0x018bac90,161),
                (0x017a9801,5)]
MORPH_STUBS = {0x0179bf26:'keys_construct',0x017bf188:'keys',0x01857a0a:'keys_begin',
    0x01820582:'iter_next',0x01849e50:'keys_end',0x017c7eaa:'iter_ne',0x017cdc9c:'iter_destroy',
    0x017999bf:'iter_read',0x0183a284:'upper_bound',0x017c105f:'lower_bound',0x0180f65b:'iter_ne',
    0x0183fc98:'iter_read',0x01850f1b:'iter_destroy',0x017db7e3:'key_count',0x017b403a:'iter_offset',
    0x0179ad65:'keys_construct',0x017efa18:'skeletal_construct',0x017c8fad:'def_string',
    0x017a3122:'string_copy',0x01822233:'iter_destroy',0x017db45a:'skeletal_append',
    0x017c5c63:'iter_destroy',0x0179588d:'iter_next'}
# Neutral CRT remainder bridge: fld divisor; fld dividend; fprem; fstp st(1); ret.
# Skeletal draws always use range 1 => divisor 65536 and uint32 dividend. This
# exact range needs one fprem (exponent difference <=16), not a general CRT stub.
REMAINDER = (0x035835b8, bytes.fromhex('dd44240c dd442404 d9f8 ddd9 c3'))


def textures(rows, color, seed):
    """Recovered helper bridge; shared by the oracle and comparison model."""
    if any(a['original'] > b['original'] for a,b in zip(rows, rows[1:])):
        raise ValueError('bridge requires clean, ordered texture maps')
    result, missing = [], 0
    for key in dict.fromkeys(row['original'] for row in rows):
        eligible = [row for row in rows if row['original']==key and row['group'] in (0,color)]
        if not eligible:
            # Native takes its diagnostic path and continues without an RNG draw.
            missing += 1
            continue
        seed, choice = recovered(seed, len(eligible))
        result.append([key, eligible[choice]['replacement']])
    return seed, result, missing


def expected(layout, seed, decapitated):
    color = 0
    if layout['texture_group_count']:
        seed, color = recovered(seed, layout['texture_group_count'])
        color += 1
    parts, calls = [], []
    for body, alternatives in enumerate(layout['bodies']):
        if not alternatives:
            continue
        seed, alternative = recovered(seed, len(alternatives))
        model = alternatives[alternative]
        combined = []
        for kind, rows in [('model',model['textures']['rows']),
                           ('body',layout['body_texture_maps'][body]['rows'])]:
            seed, selected, missing = textures(rows, color, seed)
            combined += selected
            calls.append([body,kind,color,selected,missing])
        if not (decapitated and body==0):
            parts.append(dict(mesh=model['mesh'], textures=combined))
    return dict(seed=seed, parts=parts, calls=calls)


def expected_morphs(layout, seed, decapitated):
    rows, skeletal = layout['skeletal_morphs'], []
    if any(a['key']>b['key'] for a,b in zip(rows,rows[1:])):
        raise ValueError('skeletal bridge requires ordered keys')
    for key in dict.fromkeys(row['key'] for row in rows):
        candidates = [r for r in rows if r['key']==key]
        selected = []
        if key==0:
            for row in candidates:
                if row['allow_variation']:
                    selected.append(row)
                else:
                    seed, draw = recovered(seed,100)
                    if draw>=50:
                        selected.append(row)
        else:
            seed, draw = recovered(seed,100)
            if draw>50:
                seed, choice = recovered(seed,len(candidates))
                selected.append(candidates[choice])
        for row in selected:
            amount = 1.0
            if row['allow_variation']:
                seed, _ = recovered(seed,0)
                amount = (seed & 0xffff)/65536.0
            skeletal.append(dict(name_offset=row['name_offset'],amount=amount))
    result = expected(layout,seed,decapitated)
    seed, global_textures, missing = textures(layout['global_texture_map']['rows'],0,result['seed'])
    result.update(seed=seed,skeletal=skeletal,global_textures=global_textures)
    result['calls'].append([-1,'global',0,global_textures,missing])
    return result


def edge_layouts():
    empty = lambda: dict(rows=[],dirty=False)
    base = dict(bodies=[[],[],[]],body_texture_maps=[empty() for _ in range(3)],
        skeletal_morphs=[],global_texture_map=empty(),texture_group_count=0)
    yield 'empty', copy.deepcopy(base)
    filled = copy.deepcopy(base)
    for body in (0,1,2):
        for alternative in (0,1):
            filled['bodies'][body].append(dict(mesh=10+10*body+alternative,priority=1,
                textures=dict(dirty=False,rows=[dict(original=100,replacement=200+alternative,group=0),
                    dict(original=100,replacement=300+alternative,group=1),
                    dict(original=100,replacement=400+alternative,group=2)])))
        filled['body_texture_maps'][body] = dict(dirty=False,rows=[dict(original=100,replacement=500+body,group=0)])
    filled['global_texture_map'] = dict(dirty=False,rows=[dict(original=100,replacement=v,group=0) for v in (600,601)])
    filled['texture_group_count'] = 2
    zero = copy.deepcopy(filled)
    zero['skeletal_morphs'] = [dict(key=0,group=0,name_offset=4,allow_variation=False),
                              dict(key=0,group=0,name_offset=8,allow_variation=True)]
    yield 'zero-key', zero
    grouped = copy.deepcopy(filled)
    grouped['skeletal_morphs'] = [dict(key=1,group=1,name_offset=4,allow_variation=False),
        dict(key=1,group=1,name_offset=8,allow_variation=True),
        dict(key=2,group=2,name_offset=12,allow_variation=True)]
    yield 'nonzero-keys', grouped
    yield 'layered-global', filled


class BodyOracle:
    stack, seed_at, stop = 0x10010000, 0x10020000, 0x10030000
    obj, output = 0x20000000, 0x20000100

    def __init__(self, pe, native_textures=False, full_morph_set=False):
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.pe = pe
        self.native_textures = native_textures
        self.full_morph_set = full_morph_set
        self.ranges = RANGES + (TEXTURE_RANGES if native_textures else []) + (MORPH_RANGES if full_morph_set else [])
        self.stubs = STUBS | (ITERATORS if native_textures else {}) | (MORPH_STUBS if full_morph_set else {})
        pages = {address & ~0xfff for address, _ in self.ranges} | {a & ~0xfff for a in self.stubs}
        if full_morph_set:
            pages |= {REMAINDER[0] & ~0xfff,0x04023000}
        for address in pages:
            self.machine.mem_map(address, 0x1000)
        for address, length in self.ranges:
            self.machine.mem_write(address, pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase, length))
        for address in self.stubs:
            if native_textures and address==0x01801ebb:
                continue
            self.machine.mem_write(address, b'\xc3')
        if full_morph_set:
            self.machine.mem_write(*REMAINDER)
            for address in (0x04023800,0x04023810):
                self.machine.mem_write(address,pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase,8))
        for address in (0, self.stack-0x1000, self.seed_at, self.stop):
            self.machine.mem_map(address, 0x1000)
        self.machine.mem_map(self.obj, 0x400000)
        self.machine.hook_add(UC_HOOK_CODE, self.bridge)

    def word(self, address):
        return struct.unpack('<I', self.machine.mem_read(address,4))[0]

    def store(self, address, *values):
        self.machine.mem_write(address, struct.pack('<'+'I'*len(values), *values))

    def bridge(self, machine, address, size, user):
        if self.native_textures and address in (0x0289b201,0x0289b225,0x0289acd8):
            body, scope, color, destination, start = self.pending
            self.calls.append([body,scope,color,self.local[destination][start:],0])
            self.pending = None
        if address not in self.stubs:
            return
        kind = self.stubs[address]
        stack, this = machine.reg_read(UC_X86_REG_ESP), machine.reg_read(UC_X86_REG_ECX)
        arg = lambda n: self.word(stack+4*n)
        pop, result = 0, this
        if kind=='construct':
            machine.mem_write(this, bytes(24))
            self.local[this+4] = []
        elif kind=='destroy':
            pass
        elif kind=='body':
            assert this==self.obj
            result, pop = self.arrays[arg(1)], 4
        elif kind=='body_textures':
            assert this==self.obj+0x14
            result, pop = self.maps[arg(1)], 4
        elif kind=='empty':
            result = int(not self.models[this])
        elif kind=='size':
            result = len(self.models[this])
        elif kind=='model':
            result, pop = self.models[this][arg(1)], 4
        elif kind=='textures':
            assert this==self.obj and arg(1)==self.seed_at
            color, table, destination = arg(2), arg(3), arg(4)
            body, scope, rows = self.tables[table]
            if self.native_textures:
                assert self.pending is None
                self.pending = (body,scope,color,destination,len(self.local[destination]))
                return
            seed, selected, missing = textures(rows, color, self.word(self.seed_at))
            self.store(self.seed_at, seed)
            self.local[destination] += selected
            self.calls.append([body,scope,color,selected,missing])
            pop = 16
        elif kind=='append':
            assert this==self.output
            source = arg(1)
            self.parts.append(dict(mesh=self.word(source), textures=list(self.local[source+4])))
            pop = 4
        elif kind=='iter_construct':
            self.iterators[this] = (None,0)
        elif kind=='iter_destroy':
            pass
        elif kind in ('begin','end'):
            self.iterators[arg(1)] = (this,0 if kind=='begin' else len(self.tables[this][2]))
            result, pop = arg(1), 4
        elif kind in ('keys_begin','keys_end'):
            self.iterators[arg(1)] = (this,0 if kind=='keys_begin' else len(self.raw_rows[this]))
            result, pop = arg(1),4
        elif kind=='iter_copy':
            self.iterators[this] = self.iterators[arg(1)]
            pop = 4
        elif kind=='iter_ne':
            result, pop = int(self.iterators[this]!=self.iterators[arg(1)]), 4
        elif kind=='iter_next':
            table, index = self.iterators[this]
            self.iterators[this] = (table,index+1)
        elif kind=='iter_read':
            table, index = self.iterators[this]
            result = self.raw_rows[table][index]
        elif kind in ('upper_bound','lower_bound','key_count'):
            is_skeletal = this==self.obj+0x28
            field = 'key' if is_skeletal else 'original'
            key = self.word(arg(1) if kind=='key_count' else arg(2))
            if is_skeletal and key>=0x80000000:
                key -= 0x100000000
            rows = self.tables[this][2]
            if kind=='key_count':
                result, pop = sum(r[field]==key for r in rows),4
            else:
                index = next((i for i,r in enumerate(rows) if
                    (r[field]>key if kind=='upper_bound' else r[field]>=key)),len(rows))
                self.iterators[arg(1)] = (this,index)
                result, pop = arg(1), 8
        elif kind=='texture_pair':
            self.store(this,arg(1),arg(2))
            pop = 8
        elif kind=='texture_append':
            source = arg(1)
            self.local[this].append([self.word(source),self.word(source+4)])
            pop = 4
        elif kind=='keys_construct':
            pass
        elif kind=='keys':
            assert this==self.obj+0x28
            destination = arg(1)
            keys = list(dict.fromkeys(row['key'] for row in self.tables[this][2]))
            self.raw_rows[destination] = []
            for i,key in enumerate(keys):
                pointer = self.obj+0x300000+4*i
                self.store(pointer,key & 0xffffffff)
                self.raw_rows[destination].append(pointer)
            pop = 4
        elif kind=='iter_offset':
            table, index = self.iterators[this]
            self.iterators[arg(1)] = (table,index+arg(2))
            result, pop = arg(1),8
        elif kind=='skeletal_construct':
            self.store(this,0,0,0)
        elif kind=='def_string':
            self.store(arg(1),self.word(this),0)
            result, pop = arg(1),4
        elif kind=='string_copy':
            machine.mem_write(this,bytes(machine.mem_read(arg(1),8)))
            pop = 4
        elif kind=='skeletal_append':
            assert this==self.output+0x14
            pointer = arg(1)
            amount, = struct.unpack('<f',machine.mem_read(pointer+8,4))
            self.skeletal.append(dict(name_offset=self.word(pointer),amount=amount))
            pop = 4
        else:
            raise AssertionError(kind)
        machine.reg_write(UC_X86_REG_EAX, result)
        machine.reg_write(UC_X86_REG_EIP, self.word(stack))
        machine.reg_write(UC_X86_REG_ESP, stack+4+pop)

    def run(self, layout, seed, decapitated):
        self.arrays = [self.obj+0x1000+b*0x100 for b in range(3)]
        self.maps = [self.obj+0x2000+b*0x100 for b in range(3)]
        self.models, self.tables, self.local = {}, {}, {}
        self.raw_rows, self.iterators, self.pending = {}, {}, None
        self.calls, self.parts = [], []
        self.skeletal = []
        for body, alternatives in enumerate(layout['bodies']):
            pointers = []
            for alternative, model in enumerate(alternatives):
                address = self.obj+0x10000+body*0x20000+alternative*32
                self.store(address,model['mesh'],model['priority'] & 0xffffffff)
                self.tables[address+8] = (body,'model',model['textures']['rows'])
                pointers.append(address)
            self.models[self.arrays[body]] = pointers
            self.tables[self.maps[body]] = (body,'body',layout['body_texture_maps'][body]['rows'])
        cursor = self.obj+0x80000
        for table, (_,_,rows) in self.tables.items():
            self.raw_rows[table] = []
            if any(a['original']>b['original'] for a,b in zip(rows,rows[1:])):
                raise ValueError('container bridge requires clean, ordered maps')
            for row in rows:
                self.store(cursor,row['original'],row['replacement'],row['group'])
                self.raw_rows[table].append(cursor)
                cursor += 12
        if self.full_morph_set:
            self.tables[self.obj+0x28] = (-1,'skeletal',layout['skeletal_morphs'])
            self.raw_rows[self.obj+0x28] = []
            for row in layout['skeletal_morphs']:
                self.store(cursor,row['key'] & 0xffffffff,row['group'] & 0xffffffff,
                           row['name_offset'],int(row['allow_variation']))
                self.raw_rows[self.obj+0x28].append(cursor)
                cursor += 16
            self.tables[self.obj+0x40] = (-1,'global',layout['global_texture_map']['rows'])
            self.raw_rows[self.obj+0x40] = []
            for row in layout['global_texture_map']['rows']:
                self.store(cursor,row['original'],row['replacement'],row['group'])
                self.raw_rows[self.obj+0x40].append(cursor)
                cursor += 12
            self.local[self.output+0x28] = []
        self.store(0,0)
        self.store(self.obj+0x58,layout['texture_group_count'])
        self.store(self.seed_at,seed)
        self.store(self.stack-16,self.stop,self.seed_at,self.output,int(decapitated))
        self.machine.reg_write(UC_X86_REG_ESP,self.stack-16)
        self.machine.reg_write(UC_X86_REG_ECX,self.obj)
        self.machine.reg_write(UC_X86_REG_EDX,0)
        self.machine.emu_start(0x0289ac90 if self.full_morph_set else BODY,self.stop,timeout=500000,count=100000)
        if (self.machine.reg_read(UC_X86_REG_ESP)!=self.stack or
                self.machine.reg_read(UC_X86_REG_EIP)!=self.stop):
            raise AssertionError('original body routine did not return within bound')
        result = dict(seed=self.word(self.seed_at),parts=self.parts,calls=self.calls)
        if self.full_morph_set:
            result.update(skeletal=self.skeletal,global_textures=self.local[self.output+0x28])
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--native-textures',action='store_true',help='Execute original texture selector with neutral container bridges')
    parser.add_argument('--full-morph-set',action='store_true',help='Also execute original skeletal/global selection and unit-range float draws')
    parser.add_argument('--reader',type=Path,help='Optional C++ selector audit executable; requires a coordinated test window')
    parser.add_argument('--schema',type=Path,default=Path(__file__).resolve().parents[1]/'docs/re_reference/def_schema.json')
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXPECTED_SHA:
        raise ValueError('unsupported executable identity')
    native = args.install.resolve(strict=True)
    if args.output.resolve().is_relative_to(native) or args.output.resolve()==args.exe.resolve():
        raise ValueError('evidence must be outside native inputs')
    files = [native/'data/CompiledDefs'/name for name in ('game.bin','names.bin')]
    hashes = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    names = names_pool(files[1].read_bytes())
    if args.full_morph_set:
        args.native_textures = True
    if args.reader and not args.full_morph_set:
        raise ValueError('C++ comparison requires --full-morph-set')
    machine = BodyOracle(pefile.PE(data=raw),args.native_textures,args.full_morph_set)
    counts, rows = Counter(), []
    seeds = [0,1,0x12345678,0xffffffff]
    for index, record in enumerate(read_bank(native)):
        if record['definition']!='CCreatureDef':
            continue
        layout = decode(record['data'], names)
        for seed in seeds:
            for decapitated in (False,True):
                actual = machine.run(layout,seed,decapitated)
                prediction = (expected_morphs if args.full_morph_set else expected)(layout,seed,decapitated)
                if actual!=prediction:
                    raise AssertionError((index,seed,decapitated,actual,prediction))
                counts['cases'] += 1
                counts['texture_bridge_calls'] += len(actual['calls'])
                counts['missing_texture_groups'] += sum(c[4] for c in actual['calls'])
                rows.append(dict(index=index,seed=seed,decapitated=decapitated,
                    seed_after=actual['seed'],meshes=[p['mesh'] for p in actual['parts']],
                    selection={k:v for k,v in actual.items() if k!='calls'}))
        counts['components'] += 1
    edge_cases = []
    if args.full_morph_set:
        edge_seeds = seeds + [next(seed for seed in range(10000) if recovered(seed,100)[1]==value)
                              for value in (49,50,51)]
        for name,layout in edge_layouts():
            for seed in dict.fromkeys(edge_seeds):
                for decapitated in (False,True):
                    actual = machine.run(layout,seed,decapitated)
                    prediction = expected_morphs(layout,seed,decapitated)
                    if actual!=prediction:
                        raise AssertionError(('edge',name,seed,decapitated,actual,prediction))
                    edge_cases.append(dict(profile=name,seed=seed,decapitated=decapitated,
                        selection={k:v for k,v in actual.items() if k!='calls'}))
        counts['edge_cases'] = len(edge_cases)
    core = None
    if args.reader:
        binary = args.reader.resolve(strict=True)
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        result = subprocess.run([str(binary),'--creature-appearance-selection-audit',str(native),
            str(args.schema.resolve(strict=True))],capture_output=True,text=True,timeout=120)
        if result.returncode:
            raise AssertionError(result.stderr)
        actual = json.loads(result.stdout)['records']
        wanted = [{k:v for k,v in row.items() if k not in ('seed_after','meshes')} for row in rows]
        if actual!=wanted:
            mismatch = next(((a,e) for a,e in zip(actual,wanted) if a!=e),None)
            raise AssertionError(('native/C++ selection mismatch',len(actual),len(wanted),mismatch))
        if hashlib.sha256(binary.read_bytes()).hexdigest()!=digest:
            raise AssertionError('C++ reader changed during audit')
        core = dict(binary=str(binary),sha256=digest,compared=len(actual),exact_match=True,
                    schema_sha256=hashlib.sha256(args.schema.read_bytes()).hexdigest())
    if args.exe.read_bytes()!=raw or any(hashlib.sha256(p.read_bytes()).hexdigest()!=hashes[str(p)] for p in files):
        raise AssertionError('native input changed')
    report = dict(passed=True,counts=dict(counts),input_hashes=hashes,exe_sha256=EXPECTED_SHA,
        producer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        original_ranges=[dict(address=f'{a:08x}',length=n,
            sha256=hashlib.sha256(machine.pe.get_data(a-machine.pe.OPTIONAL_HEADER.ImageBase,n)).hexdigest()) for a,n in machine.ranges],
        bridge_addresses={f'{a:08x}':name for a,name in machine.stubs.items()
                          if not(args.native_textures and name=='textures')},cases=rows,edge_cases=edge_cases,
        native_textures=args.native_textures,
        full_morph_set=args.full_morph_set,
        core_comparison=core,
        remainder_bridge=REMAINDER[1].hex() if args.full_morph_set else None,
        scope=('Original body/texture control flow and RNG; neutral clean-container bridges only.' if args.native_textures else
               'Original body control flow and RNG; container/texture helpers bridged.')+
              (' Original skeletal/global selection plus a unit-range x87 remainder bridge.' if args.full_morph_set else
               ' No skeletal selection acceptance.')+
              ' No dirty-map sort, native allocation/ABI or rendering acceptance.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=True,counts=dict(counts),output=str(args.output))))


if __name__=='__main__':
    main()
