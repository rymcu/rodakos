"""An explicit SIGABRT and branch markers are required; timeout is never a red pass."""
import argparse
import json
import signal
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True)
    parser.add_argument('--scenario', required=True)
    parser.add_argument('--expected', action='append', default=[])
    parser.add_argument('--forbidden', action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = subprocess.run([args.executable, args.scenario], capture_output=True, text=True, timeout=8)
    combined = result.stdout + result.stderr
    report = {'command': [args.executable, args.scenario], 'returncode': result.returncode,
              'stdout': result.stdout, 'stderr': result.stderr,
              'expected': args.expected, 'forbidden': args.forbidden}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    if result.returncode != -signal.SIGABRT:
        raise AssertionError(f'Expected SIGABRT, got {result.returncode}: {combined}')
    for marker in args.expected:
        if marker not in combined:
            raise AssertionError('Missing exact failure marker: ' + marker)
    for marker in args.forbidden:
        if marker in combined:
            raise AssertionError('Unexpected marker: ' + marker)


if __name__ == '__main__':
    main()
