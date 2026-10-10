"""Review source environment colour-table coordinates against its loose TGA.

Native BuildThemes appends ColourLookupTexture to data/LightingTable; day-theme
Build uses ColourLookupColumn + Time element index. LookupFloatColour consumes
those columns and the environment's LookupRow fields as pixel coordinates.
This inventories every ENVIRONMENT candidate; it does not select the active
environment, certify native image decoding, other assets or source admission.
Dimensions come from a successful strict core image-storage review.
Missing source-local images may be inherited from the baseline and are not
automatically treated as missing installed assets.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

from audit_levsymbols import digest
from test_fmp_links import read_bank


def decode(tool, schema, root, slot):
    run = subprocess.run([str(tool), 'defs', 'decode', str(root), str(schema), str(slot), '--json'],
                         capture_output=True, text=True, encoding='utf-8', timeout=30)
    if run.returncode:
        raise RuntimeError('definition decode refused: ' + run.stderr.strip())
    result = json.loads(run.stdout)
    if not result.get('all_tags_ok') or result.get('leftover') != 0:
        raise RuntimeError(f'unclean definition decode: {root} slot {slot}')
    return result


def day_coordinates(fields):
    columns = [f for f in fields if f['name'] == 'ColourLookupColumn' and f['type'] == 'int32']
    times = [f for f in fields if f['type'] == 'Vector_CEnvironmentThemeDef']
    if len(columns) != 1 or len(times) != 1:
        raise RuntimeError('missing or ambiguous day-theme coordinate fields')
    count = re.fullmatch(r'count=(\d+)', times[0]['value'])
    if not count:
        raise RuntimeError('unsupported day-theme array count')
    return int(columns[0]['value']), int(count[1])


def image_storage(tool, path):
    run = subprocess.run([str(tool), 'world', 'review-storage', str(path), '--json'],
                         capture_output=True, text=True, encoding='utf-8', timeout=30)
    if run.returncode not in (0, 1):
        raise RuntimeError('image storage command failed: ' + run.stderr.strip())
    result = json.loads(run.stdout)
    if result.get('source_admitted') is not False or len(result.get('units', [])) != 1:
        raise RuntimeError('unexpected image storage report')
    supported = result.get('storage_supported') is True
    if supported != (run.returncode == 0):
        raise RuntimeError('inconsistent image storage result')
    unit = result['units'][0]
    if not supported:
        return None, unit['reason']
    if unit.get('format') != '.tga' or unit.get('supported') is not True:
        raise RuntimeError('unexpected image format or unit status')
    return (unit['metrics']['width'], unit['metrics']['height']), None


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo / 'build/forge-tools.exe')
    parser.add_argument('--schema', type=Path, default=repo / 'docs/re_reference/def_schema.json')
    args = parser.parse_args()
    root, output, tool, schema = [p.resolve() for p in (args.source, args.out, args.tool, args.schema)]
    inputs = [tool, schema, Path(__file__).resolve(), Path(__file__).with_name('audit_levsymbols.py'),
              Path(__file__).with_name('test_fmp_links.py'),
              root / 'data/CompiledDefs/names.bin', root / 'data/CompiledDefs/game.bin']
    if output.is_relative_to(root) or output in inputs:
        parser.error('report must not replace source inputs or tooling')
    before = {str(p): digest(p) for p in inputs}
    bank = read_bank(root)
    days = []
    for slot, entry in enumerate(bank):
        if entry['definition'] != 'ENVIRONMENT_THEME_DAY':
            continue
        start, count = day_coordinates(decode(tool, schema, root, slot)['fields'])
        days.append({'slot': slot, 'name': entry['name'], 'first_column': start, 'time_count': count})
    environments = []
    for slot, entry in enumerate(bank):
        if entry['definition'] != 'ENVIRONMENT':
            continue
        fields = decode(tool, schema, root, slot)['fields']
        paths = [f for f in fields if f['name'] == 'ColourLookupTexture' and f['type'] == 'CCharString']
        if len(paths) != 1:
            raise RuntimeError('missing or ambiguous colour-table path')
        filename = json.loads(paths[0]['value'])
        row = {'slot': slot, 'name': entry['name'], 'texture': filename, 'status': 'empty', 'rows': [], 'days': []}
        if filename:
            directory = (root / 'data/LightingTable').resolve()
            path = (directory / filename).resolve()
            if not path.is_relative_to(directory):
                raise RuntimeError('colour-table path leaves source lighting directory')
            row.update(path=str(path), status='missing_source_file')
            if path.is_file():
                before[str(path)] = digest(path)
                dimensions, refusal = image_storage(tool, path)
                row['image_storage_checked'] = True
                if dimensions is None:
                    row.update(status='image_storage_refused', reason=refusal)
                    environments.append(row)
                    continue
                width, height = dimensions
                row.update(width=width, height=height, status='coordinates_in_bounds')
                row['rows'] = [{'field': f['name'], 'row': int(f['value']),
                                'in_bounds': 0 <= int(f['value']) < height}
                               for f in fields if f['name'].endswith('LookupRow') and f['type'] == 'int32']
                row['days'] = [dict(day, in_bounds=day['time_count'] == 0 or
                                   (day['first_column'] >= 0 and day['first_column'] + day['time_count'] <= width))
                               for day in days]
                if any(not v['in_bounds'] for v in row['rows'] + row['days']):
                    row['status'] = 'out_of_bounds'
        environments.append(row)
    if not environments or not days:
        raise RuntimeError('no environment/day-theme definitions found')
    for path, expected in before.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f'input changed during lighting review: {path}')
    report = {'scope': __doc__, 'source_admitted': False, 'root': str(root),
              'input_sha256': before, 'environments': environments, 'day_theme_count': len(days)}
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    for row in environments:
        print(row['name'], row['status'], row['texture'])
    return int(any(row['status'] in ('out_of_bounds', 'missing_source_file', 'image_storage_refused')
                   for row in environments))


if __name__ == '__main__':
    raise SystemExit(main())
