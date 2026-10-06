"""Read-only review of supported external BIG layouts, not source admission.

Records refusals as well as verified definition-independent entries. Input hashes
must remain unchanged; unsupported formats never become accepted by raw scanning.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess


FILES = ('data/graphics/pc/textures.big', 'data/graphics/pc/frontend.big',
         'data/graphics/graphics.big', 'data/lang/English/text.big')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo / 'build/fableforge_defexternal_tests.exe')
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    roots = [root.resolve() for root in args.source]
    tool, output = args.tool.resolve(), args.out.resolve()
    if output == tool or any(output.is_relative_to(root) for root in roots):
        parser.error('report must be outside input roots and must not replace the tool')
    hashes = {str(root / name): digest(root / name) for root in roots for name in FILES}
    report = {'scope': 'Supported BIG entry review only; remaining external files require separate admission',
              'tool_sha256': digest(tool), 'sources': []}
    for root in roots:
        run = subprocess.run([str(tool), str(root)], capture_output=True, text=True, encoding='utf-8')
        if run.returncode:
            raise RuntimeError(f'external review failed ({run.returncode}): {run.stderr.strip()}')
        source = json.loads(run.stdout)
        report['sources'].append(source)
        print(root)
        for row in source['files']:
            print(row['file'], 'verified', row['accepted'], 'refused', len(row['refusals']),
                  dict(Counter(item['reason'] for item in row['refusals'])))
    if any(digest(Path(path)) != before for path, before in hashes.items()):
        raise RuntimeError('review changed an input archive')
    report['input_sha256'] = hashes
    report['script_sha256'] = digest(Path(__file__))
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
