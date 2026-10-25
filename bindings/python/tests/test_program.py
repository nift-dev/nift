"""Public installed-package CP18 surface (also run by test-python-binding)."""
import unittest
from nift import Program, ProgramCompileError


class ProgramTests(unittest.TestCase):
    def test_compile_once_fresh_runs_and_owned_result(self):
        with Program('x := 0; x += 1; print(x); return 42;') as p:
            for _ in range(3):
                result = p.run()
                self.assertTrue(result.ok)
                self.assertEqual(result.output, b'1\n')
                self.assertEqual(result.return_text, '42')
        self.assertEqual(result.output, b'1\n')
        with self.assertRaises(RuntimeError):
            p.run()

    def test_error_provenance(self):
        with Program('print(1);\nthrow error("failure");', logical_name='api.f') as p:
            result = p.run()
        self.assertFalse(result.ok)
        self.assertEqual((result.output, result.message, result.code),
                         (b'1\n', 'failure', 'user.raised'))
        self.assertEqual((result.source, result.line), ('api.f', 2))
        self.assertTrue(result.catchable)

    def test_compile_refuses_without_executing(self):
        with self.assertRaises(ProgramCompileError) as context:
            Program('print(999); import("not-loaded.f");')
        self.assertEqual(context.exception.result.outcome, 2)
        self.assertEqual(context.exception.result.output, b'')
