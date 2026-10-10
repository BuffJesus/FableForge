"""Read-only TNG/GTG coverage inventory of loose files and WAD payloads.

Refusals remain in the report; successful inventory execution is not source admission.
WAD envelopes are validated; symbolic definition resolution remains separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from collections import Counter

from audit_defnamespace import lookup_groups, names
from audit_levlookupplans import compare_queries, planned_identities
from test_fmp_links import read_bank, crc0


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def definition_symbols(references, bank):
    groups = lookup_groups({i: (entry['definition'], entry['name']) for i, entry in enumerate(bank)})
    symbols = {}
    for reference in references:
        component = reference.get('component', '')
        field = reference.get('field', 'DefinitionType').split('[', 1)[0]
        key = (reference['thing_type'], component, field, reference['definition'])
        if key not in symbols:
            name = reference['definition']
            candidates = groups.get(crc0(name), []) if name else []
            symbols[key] = {'thing_type': key[0], 'component': component, 'field': field,
                'conditional': reference.get('conditional',False),
                'name': name, 'source_candidates': candidates,
                'status': 'empty' if not name else 'missing' if not candidates else
                          'unique' if len(candidates) == 1 else 'ambiguous', 'locations': []}
        symbols[key]['locations'].append({'file': reference['file'], 'line': reference['line'],
                                         'field': reference.get('field', 'DefinitionType')})
        if 'world_map_slot' in reference:
            symbols[key]['locations'][-1]['world_map_slot'] = reference['world_map_slot']
    return [symbols[key] for key in sorted(symbols)]


def compare_symbol_queries(targets, source_slots, symbols):
    queries = []
    for symbol in symbols:
        if not symbol['name']:
            continue
        domain = symbol['thing_type']
        if symbol['component']:
            domain += ':' + symbol['component'] + '.' + symbol['field']
        elif symbol['field'] != 'DefinitionType':
            domain += '.' + symbol['field']
        queries.append({'domain':domain,'name':symbol['name'],
                        'conditional':symbol.get('conditional',False),
                        'files':sorted({loc['file'] for loc in symbol['locations']})})
    compared = compare_queries(targets, source_slots, queries)
    for query,row in zip(queries,compared,strict=True):
        if query['conditional']:
            row['conditional'] = True
    return compared


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo / 'build/fableforge_defexternal_tests.exe')
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--definition-symbols', action='store_true',
                        help='inventory DefinitionType and explicitly reviewed component name fields in accepted TNG/GTG files')
    parser.add_argument('--plan', type=Path, action='append', default=[],
                        help='hash-pinned namespace audit to compare; requires --definition-symbols')
    args = parser.parse_args()
    roots = [path.resolve() for path in args.source]
    tool, output = args.tool.resolve(), args.out.resolve()
    if args.plan and not args.definition_symbols:
        parser.error('--plan requires --definition-symbols')
    plans = [(p.resolve(), json.loads(p.read_text(encoding='utf-8'))) for p in args.plan]
    protected_roots = [*roots, *(Path(p['baseline']).resolve() for _, p in plans),
                       *(Path(s['root']).resolve() for _, p in plans for s in p['sources'])]
    scripts = [Path(__file__).resolve(), *(Path(__file__).with_name(name).resolve() for name in
               ('audit_defnamespace.py', 'audit_levlookupplans.py', 'audit_levsymbols.py', 'test_fmp_links.py'))]
    if output in (tool, *scripts, *(p for p, _ in plans)) or any(output.is_relative_to(root) for root in protected_roots):
        parser.error('report must be outside input roots and must not replace audit tooling')
    plan_hashes = {str(p): digest(p) for p, _ in plans}
    for _, plan in plans:
        for path, expected in plan['input_sha256'].items():
            if plan_hashes.setdefault(path, expected) != expected:
                raise RuntimeError(f'namespace reports disagree: {path}')
    for path, expected in plan_hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f'stale namespace report: {path}')
    target_plans = [(p, plan, planned_identities(plan)) for p, plan in plans]
    tool_hash = digest(tool)
    report = {'scope': __doc__, 'source_admitted': False, 'tool_sha256': tool_hash,
              'plan_input_sha256': plan_hashes, 'sources': []}
    for root in roots:
        levels = root / 'data/Levels'
        files = sorted(path for path in levels.rglob('*') if path.is_file() and path.suffix.lower() in ('.tng', '.gtg', '.wad'))
        if not files:
            raise RuntimeError(f'no TNG/GTG/WAD inputs under {levels}')
        inputs = [*files]
        if args.definition_symbols:
            inputs += [root / 'data/CompiledDefs/game.bin', root / 'data/CompiledDefs/names.bin']
        before = {str(path): digest(path) for path in inputs}
        if args.definition_symbols:
            if any(stored != crc0(name) for stored, name in names(root).values()):
                raise RuntimeError(f'name CRC/text mismatch: {root}')
            bank = read_bank(root)
        source = {'root': str(root), 'input_sha256': before, 'reviews': []}
        for path in [levels, *(path for path in files if path.suffix.lower() == '.wad')]:
            command = [str(tool), '--tng-inventory', str(path)]
            if args.definition_symbols:
                command.append('--symbols')
            run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
            if run.returncode:
                raise RuntimeError(f'TNG inventory failed ({run.returncode}): {run.stderr.strip()}')
            row = json.loads(run.stdout)
            if args.definition_symbols:
                references = row.pop('definition_references')
                row['definition_reference_count'] = len(references)
                row['definition_symbols'] = definition_symbols(references, bank)
                row['symbol_counts'] = dict(Counter(s['status'] for s in row['definition_symbols']))
                components = row.pop('component_definition_references')
                row['component_definition_reference_count'] = len(components)
                row['component_definition_symbols'] = definition_symbols(components, bank)
                row['component_symbol_counts'] = dict(Counter(s['status'] for s in row['component_definition_symbols']))
                for artifact, plan, targets in target_plans:
                    owners = [s for s in plan['sources'] if Path(s['root']).resolve() == root]
                    if len(owners) != 1:
                        raise RuntimeError(f'plan must contain source exactly once: {root}')
                    source_slots = [e['target'] for e in owners[0]['prepared_game']]
                    compared = compare_symbol_queries(targets, source_slots, row['definition_symbols'])
                    row.setdefault('plans', []).append({'plan': str(artifact),
                        'counts': dict(Counter(s['status'] for s in compared)), 'references': compared})
                    compared = compare_symbol_queries(targets, source_slots, row['component_definition_symbols'])
                    row.setdefault('component_plans', []).append({'plan': str(artifact),
                        'counts': dict(Counter(s['status'] for s in compared)), 'references': compared})
            row['path'] = str(path)
            source['reviews'].append(row)
            print({k: v for k, v in row.items() if k not in ('refusals', 'definition_symbols', 'plans',
                  'component_definition_symbols', 'component_plans')}, flush=True)
        if any(digest(Path(path)) != value for path, value in before.items()):
            raise RuntimeError(f'input changed during review: {root}')
        report['sources'].append(source)
    if digest(tool) != tool_hash:
        raise RuntimeError('audit tool changed during review')
    for path, expected in plan_hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f'namespace input changed during review: {path}')
    report['script_sha256'] = digest(Path(__file__))
    report['dependency_sha256'] = {str(p): digest(p) for p in scripts[1:]}
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
