import unittest
from v410_oracle_paths import normalize_fixture_paths


class OraclePathsTest(unittest.TestCase):
    def test_observed_msys_failure_and_exact_origin(self):
        aliases = ['/tmp/nift-sort-abc', 'D:/a/_temp/msys64/tmp/nift-sort-abc']
        observed = 'error: D:/a/_temp/msys64/tmp/nift-sort-abc/unused-timer-observable.f:3:6: async function capture contains a non-transferable timer\n'
        expected = 'error: <ORACLE_ROOT>/unused-timer-observable.f:3:6: async function capture contains a non-transferable timer\n'
        self.assertEqual(normalize_fixture_paths(observed, aliases), expected)

    def test_native_and_posix_prefixes(self):
        aliases = ['/tmp/oracle', 'C:\\tmp\\oracle']
        for path in ['/tmp/oracle/case.f', 'C:\\tmp\\oracle\\case.f']:
            self.assertEqual(normalize_fixture_paths('error: '+path+':2:7: message\n', aliases),
                             'error: <ORACLE_ROOT>/case.f:2:7: message\n')

    def test_unrelated_suffix_and_similar_name_are_preserved(self):
        text = 'error: D:/other/tmp/oracle/case.f:2:7\nerror: /tmp/oracle-extra/case.f:1:1\n'
        self.assertEqual(normalize_fixture_paths(text, ['/tmp/oracle']), text)


if __name__ == '__main__':
    unittest.main()
