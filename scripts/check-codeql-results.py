"""Turn CodeQL SARIF security findings into a failing CI check."""
import json
from pathlib import Path
import sys


def main(directory):
    reports = list(Path(directory).glob('*.sarif'))
    if not reports:
        print('CodeQL produced no SARIF report.', file=sys.stderr)
        return 2
    count = 0
    for report in reports:
        document = json.loads(report.read_text(encoding='utf-8'))
        runs = document.get('runs')
        if document.get('version') != '2.1.0' or not isinstance(runs, list) or not runs:
            print('CodeQL produced an invalid or empty report.', file=sys.stderr)
            return 2
        for run in runs:
            for notification in run.get('invocations', []):
                if notification.get('executionSuccessful') is False:
                    print('CodeQL reported an unsuccessful analysis.', file=sys.stderr)
                    return 2
            for result in run.get('results', []):
                if result.get('kind', 'fail') != 'fail':
                    continue
                count += 1
                # Source snippets are intentionally omitted from console output.
                print(f"Security finding: {result.get('ruleId', 'unknown rule')}")
    print(f'CodeQL security findings: {count}')
    return 1 if count else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
