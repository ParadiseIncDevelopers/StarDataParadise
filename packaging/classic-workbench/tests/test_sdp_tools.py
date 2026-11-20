"""Pure contract checks for the Classic adapters; no app, network or credentials."""
from decimal import Decimal
import importlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace

MODULES = Path(__file__).resolve().parents[3] / 'classic-workbench/modules/wb.utils'
sys.path.insert(0, str(MODULES))
backend = importlib.import_module('sdp_tools_backend')
decision = importlib.import_module('sdp_compat_decision')
logical = importlib.import_module('sdp_compat_logical')


class BindingTests(unittest.TestCase):
    def test_parameter_values_cannot_become_sql(self):
        attack = "x'\\; DROP TABLE orders; -- Türkçe\0"
        encoded = backend.bind('SELECT ?', [attack])
        self.assertNotIn('DROP TABLE', encoded)
        self.assertIn(attack.encode('utf-8').hex(), encoded)

    def test_quoted_question_marks_and_comments_are_not_parameters(self):
        self.assertEqual(backend.bind("SELECT '?', `?`, ? /* ? */ -- ?\n", [7]),
                         "SELECT '?', `?`, 7 /* ? */ -- ?\n")

    def test_exact_numeric_literals(self):
        maximum = (1 << 64) - 1
        self.assertEqual(backend.literal(maximum), str(maximum))
        value = Decimal('99999999999999999999.123456789012345678')
        self.assertEqual(backend.literal(value), '99999999999999999999.123456789012345678')
        with self.assertRaises(ValueError):
            backend.literal(float('nan'))

    def test_parameter_count_checked(self):
        for sql, values in [('SELECT ?', []), ('SELECT 1', [2])]:
            with self.assertRaises(ValueError):
                backend.bind(sql, values)

    def test_identifier_is_quoted(self):
        self.assertEqual(backend.identifier('şema`adı'), '`şema``adı`')
        with self.assertRaises(ValueError):
            backend.identifier('x\0y')


class HistoryTests(unittest.TestCase):
    def test_bigint_revision_and_optional_filters(self):
        statement = backend.history_sql('app_history', 'read', end=str((1 << 64) - 1))
        self.assertIn(str((1 << 64) - 1), statement)
        self.assertIn('NULL, 0, 32', statement)

    def test_invalid_ranges_and_pages_rejected(self):
        for kwargs in [dict(mode='read'), dict(mode='diff', start='4', end='3'),
                       dict(mode='log', limit='257'), dict(mode='log', row='0'),
                       dict(mode='log', start='1; DROP TABLE t')]:
            with self.assertRaises(ValueError):
                backend.history_sql('history', **kwargs)


class NativeMetadataTests(unittest.TestCase):
    def test_native_numeric_tags_preserve_bigints_and_nulls(self):
        class Result:
            columns = [SimpleNamespace(name=name, columnType=kind) for name,kind in
                       [('id','numeric'), ('ratio','numeric'), ('optional','numeric'), ('text','string')]]
            def goToFirstRow(self):
                return True
            def nextRow(self):
                return False
            def stringFieldValue(self, index):
                return ['18446744073709551615', '1.25', None, '00123'][index]
        editor = SimpleNamespace(executeManagementQuery=lambda sql, log: Result())
        row = backend.ManagementSession(editor).run_sql('SELECT metadata_fixture').fetch_one()
        self.assertEqual(row, (18446744073709551615, Decimal('1.25'), None, '00123'))
        self.assertIs(type(row[0]), int)
        self.assertIs(type(row[3]), str)

    def test_decimal_string_conversion_requires_numeric_source_metadata(self):
        session = backend.ManagementSession(None)
        metadata = backend.Rows([('amount','decimal'), ('code','varchar')])
        session.run_sql = lambda sql, values: metadata
        with self.assertRaises(ValueError):
            session.configure_training_types('app', 'rows', ['code'])
        session.run_sql = lambda sql, values: backend.Rows([('amount','decimal')])
        session.configure_training_types('app', 'rows', ['amount'])
        self.assertEqual(session.training_numeric, {'amount'})
        self.assertEqual(session.training_source, 'FROM `app`.`rows` ')
        session.clear_training_types()
        self.assertIsNone(session.training_source)


class DecisionTests(unittest.TestCase):
    def test_entire_rule_set_compiles_as_one_trigger(self):
        source = ('CREATE DECISION RULE deny_blocked ON sales.orders FOR INSERT PRIORITY 100 '
                  'WHEN (NEW.blocked = 1) THEN DENY;\n'
                  'CREATE DECISION RULE minimum_score ON sales.orders FOR INSERT PRIORITY 10 '
                  'WHEN (NEW.score >= 10) THEN ALLOW;')
        result = decision.provision(decision.parse_rules(source))
        self.assertEqual(result.count('CREATE TRIGGER'), 1)
        self.assertIn('NEW.`blocked`', result)
        self.assertIn('NEW.`score`', result)
        self.assertIn('decision_dependency_unavailable', result)

    def test_rules_cannot_span_tables(self):
        rules = decision.parse_rules('CREATE DECISION RULE a ON sales.orders FOR INSERT WHEN (NEW.id = 1) THEN ALLOW;'
                                     'CREATE DECISION RULE b ON sales.other FOR INSERT WHEN (NEW.id = 1) THEN ALLOW;')
        with self.assertRaises(ValueError):
            decision.provision(rules)


class ManifestTests(unittest.TestCase):
    def test_real_provider_checks_every_file_and_detects_tampering(self):
        with tempfile.TemporaryDirectory() as root:
            archive = Path(root) / ('a' * 32)
            dump = archive / 'dump'
            dump.mkdir(parents=True)
            for name in logical.REQUIRED_FILES:
                (dump / name).write_text('{}', encoding='utf-8')
            manifest = dict(format=logical.FORMAT, id=archive.name, source_uuid='fixture-uuid',
                            schema='fixture', files=logical.index_dump(dump))
            (archive / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
            self.assertEqual(backend.verify_archive(archive), manifest)
            (dump / '@.json').write_text('{"tampered":true}', encoding='utf-8')
            with self.assertRaises(ValueError):
                backend.verify_archive(archive)


if __name__ == '__main__':
    unittest.main()
