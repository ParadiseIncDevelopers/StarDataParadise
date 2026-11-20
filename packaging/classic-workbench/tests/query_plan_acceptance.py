"""EXPLAIN source binding, fallback and native result ownership contracts."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest

SOURCE = Path(__file__).resolve().parents[3] / 'classic-workbench/plugins/wb.query.analysis/wb_query_analysis_grt.py'
tree = ast.parse(SOURCE.read_text(encoding='utf-8'))
functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
             and node.name in ('result_statement', 'collect_explain')]
scope = {}
exec(compile(ast.Module(body=functions, type_ignores=[]), str(SOURCE), 'exec'), scope)


class NativeResult:
    def __init__(self, text='{"query_block": {"select_id": 1}}', fail=False):
        self.text, self.fail, self.released = text, fail, False

    def goToFirstRow(self):
        if self.fail:
            raise RuntimeError('read error')
        return True

    def stringFieldValue(self, index):
        return self.text

    def reset_references(self):
        self.released = True


class PlanAcceptance(unittest.TestCase):
    def editor(self, responses):
        self.calls = []

        def execute(sql, log):
            self.calls.append(sql)
            response = responses[len(self.calls) - 1]
            if isinstance(response, Exception):
                raise response
            return response
        return SimpleNamespace(owner=SimpleNamespace(executeQuery=execute), currentStatement='SELECT 99')

    def test_result_uses_origin_sql_after_buffer_edit(self):
        panel = SimpleNamespace(resultset=SimpleNamespace(sql='SELECT 1'))
        self.assertEqual(scope['result_statement'](self.editor([]), panel), 'SELECT 1')

    def test_unexecuted_buffer_plan_uses_current_statement(self):
        self.assertEqual(scope['result_statement'](self.editor([]), SimpleNamespace(resultset=None)), 'SELECT 99')

    def test_product_version_does_not_block_json_and_releases_result(self):
        result = NativeResult()
        editor = self.editor([object(), result])
        table, text = scope['collect_explain'](editor, 'SELECT 1')
        self.assertTrue(table)
        self.assertEqual(text, result.text)
        self.assertTrue(result.released)
        self.assertEqual(self.calls, ['EXPLAIN FORMAT=TRADITIONAL SELECT 1', 'EXPLAIN FORMAT=JSON SELECT 1'])

    def test_json_unavailable_preserves_real_tabular_plan(self):
        table = object()
        editor = self.editor([table, RuntimeError('JSON unsupported')])
        self.assertEqual(scope['collect_explain'](editor, 'SELECT 1'), (table, None))

    def test_traditional_fallback_works(self):
        table = object()
        editor = self.editor([RuntimeError('format unsupported'), table, None])
        self.assertEqual(scope['collect_explain'](editor, 'SELECT 1'), (table, None))
        self.assertEqual(self.calls[1], 'EXPLAIN SELECT 1')

    def test_denied_plan_reports_failure_for_retry(self):
        editor = self.editor([RuntimeError('denied')] * 3)
        with self.assertRaisesRegex(RuntimeError, 'EXPLAIN sonucu alınamadı'):
            scope['collect_explain'](editor, 'SELECT 1')

    def test_json_read_failure_still_releases_native_result(self):
        result = NativeResult(fail=True)
        table = object()
        self.assertEqual(scope['collect_explain'](self.editor([table, result]), 'SELECT 1'), (table, None))
        self.assertTrue(result.released)


if __name__ == '__main__':
    unittest.main()
