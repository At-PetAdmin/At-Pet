import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('codeql_gate', Path(__file__).resolve().parents[1] / 'scripts/check-codeql-results.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class SecurityGateTest(unittest.TestCase):
    def scan(self, run):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'report.sarif').write_text(json.dumps({'version': '2.1.0', 'runs': [run]}), encoding='utf-8')
            return gate.main(directory)

    def test_clean_analysis_passes(self):
        self.assertEqual(self.scan({'results': []}), 0)

    def test_security_finding_fails(self):
        self.assertEqual(self.scan({'results': [{'ruleId': 'cpp/path-injection'}]}), 1)

    def test_missing_report_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(gate.main(directory), 2)

    def test_unsuccessful_analysis_fails(self):
        self.assertEqual(self.scan({'invocations': [{'executionSuccessful': False}], 'results': []}), 2)

    def test_empty_sarif_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'report.sarif').write_text('{}', encoding='utf-8')
            self.assertEqual(gate.main(directory), 2)


if __name__ == '__main__':
    unittest.main()
