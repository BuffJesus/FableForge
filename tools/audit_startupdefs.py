"""Compare fixed startup definition class ordinals across namespace plans.

FableWin CGameDefinitionManager::InitAndCompile 01913D9D requests class ordinal
1 of GLOBAL, ENGINE, ENVIRONMENT and HERO_STATS. These are class-index queries,
not instance-name CRC lookups. Checks planned row identity only; definition
payload behavior, later pointer changes and source admission remain unchecked.
"""
import argparse
import json
from pathlib import Path

from audit_levlookupplans import planned_identities
from audit_levsymbols import digest


STARTUP_CLASSES = ('GLOBAL', 'ENGINE', 'ENVIRONMENT', 'HERO_STATS')


def compare_startup(targets, source_slots):
    source_slots = list(source_slots)
    if len(set(source_slots)) != len(source_slots) or any(s not in targets for s in source_slots):
        raise ValueError('invalid source-to-plan slot map')

    def selected(slots, definition):
        candidates = [s for s in slots if targets[s][0] == definition]
        if len(candidates) < 2:
            return None
        slot = candidates[1]
        return {'slot': slot, 'name': targets[slot][1]}

    rows = []
    for definition in STARTUP_CLASSES:
        before = selected(source_slots, definition)
        after = selected(sorted(targets), definition)
        rows.append({'definition': definition, 'class_ordinal': 1,
                     'source_in_planned_slots': before, 'planned': after,
                     'status': 'missing_source' if before is None else
                               'missing_planned' if after is None else
                               'unchanged' if before == after else 'changed'})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    artifacts = [p.resolve() for p in args.plan]
    plans = [json.loads(p.read_text(encoding='utf-8')) for p in artifacts]
    dependencies = [Path(__file__).resolve(), *(Path(__file__).with_name(name).resolve() for name in
                    ('audit_levlookupplans.py', 'audit_defnamespace.py', 'audit_levsymbols.py', 'test_fmp_links.py'))]
    roots = [Path(p['baseline']).resolve() for p in plans]
    roots += [Path(s['root']).resolve() for p in plans for s in p['sources']]
    output = args.out.resolve()
    hashes = {str(p): digest(p) for p in artifacts + dependencies}
    for plan in plans:
        for path, expected in plan['input_sha256'].items():
            if hashes.setdefault(path, expected) != expected:
                raise RuntimeError(f'inconsistent input hash: {path}')
    if output in artifacts + dependencies or str(output) in hashes or any(output.is_relative_to(r) for r in roots):
        parser.error('report must not replace inputs or tooling')

    def verify():
        for path, expected in hashes.items():
            if digest(Path(path)) != expected:
                raise RuntimeError(f'stale or changed input: {path}')

    verify()
    report = {'scope': __doc__, 'source_admitted': False, 'input_sha256': hashes, 'plans': []}
    findings = 0
    for artifact, plan in zip(artifacts, plans, strict=True):
        targets = planned_identities(plan)
        reviews = []
        for source in plan['sources']:
            references = compare_startup(targets, (entry['target'] for entry in source['prepared_game']))
            findings += sum(r['status'] != 'unchanged' for r in references)
            reviews.append({'root': source['root'], 'references': references})
            print(artifact.name, source['root'], {r['definition']: r['status'] for r in references})
        report['plans'].append({'path': str(artifact), 'sources': reviews})
    verify()
    report['findings'] = findings
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return int(findings != 0)


if __name__ == '__main__':
    raise SystemExit(main())
