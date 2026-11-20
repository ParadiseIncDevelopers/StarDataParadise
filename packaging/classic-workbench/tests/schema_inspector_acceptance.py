"""Exercise the real schema-preview handlers without a server or UI writes."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest


SOURCE = Path(__file__).resolve().parents[3] / "classic-workbench/plugins/wb.sqlide/sqlide_catalogman_ext.py"
TABLE_SOURCE = SOURCE.with_name("sqlide_tableman_ext.py")


def load_handlers():
    source = ast.parse(SOURCE.read_text(encoding="utf-8-sig"))
    selected = [node for node in source.body if isinstance(node, (ast.ClassDef, ast.FunctionDef))
                and node.name in ("SelectedTablePreview", "quote_identifier")]
    namespace = {
        "mforms": SimpleNamespace(Box=object),
        "tr": lambda text: {"Yes": "Evet", "No": "Hayır"}.get(text, text),
    }
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(SOURCE), "exec"), namespace)
    return namespace["SelectedTablePreview"]


class View:
    def __init__(self):
        self.text = ""
        self.value = ""
        self.enabled = False
        self.read_only = True
        self.rows = []

    def set_text(self, value):
        self.text = value

    def set_value(self, value):
        self.value = value

    def set_enabled(self, value):
        self.enabled = value

    def set_read_only(self, value):
        self.read_only = value

    def clear(self):
        self.rows.clear()

    def add_node(self):
        values = {}
        node = SimpleNamespace(set_string=lambda index, value: values.__setitem__(index, value))
        self.rows.append(values)
        return node


class Result:
    def __init__(self, rows):
        self.rows = rows
        self.position = 0
        self.releases = 0

    def goToFirstRow(self):
        self.position = 0
        return bool(self.rows)

    def nextRow(self):
        self.position += 1
        return self.position < len(self.rows)

    def stringFieldValueByName(self, name):
        return self.rows[self.position][name]

    def stringFieldValue(self, index):
        return self.rows[self.position][index]

    def reset_references(self):
        self.releases += 1

    @property
    def columns(self):
        return [SimpleNamespace(name=name) for name in self.rows[0]] if self.rows else []


class Editor:
    def __init__(self):
        self.calls = []
        self.alter_calls = []
        self.results = []
        self.failure = None

    def executeManagementQuery(self, sql, flags):
        self.calls.append((sql, flags))
        if self.failure:
            raise self.failure
        if sql.startswith("SHOW FULL COLUMNS"):
            result = Result([
                {"Field": "id", "Type": "int", "Null": "NO", "Key": "PRI"},
                {"Field": "note", "Type": "text", "Null": "YES", "Key": ""},
            ])
        elif sql.startswith("SHOW INDEX"):
            result = Result([{"Key_name": "PRIMARY", "Column_name": "id", "Non_unique": "0", "Seq_in_index": "1"}])
        elif sql.startswith("SHOW CREATE TABLE"):
            result = Result([{1: "CREATE TABLE `sample` (`id` INT)"}])
        else:
            raise AssertionError("Unexpected SQL: " + sql)
        self.results.append(result)
        return result

    def alterLiveObject(self, kind, schema, name):
        self.alter_calls.append((kind, schema, name))


class PreviewAcceptance(unittest.TestCase):
    def setUp(self):
        preview_type = load_handlers()
        self.preview = preview_type.__new__(preview_type)
        self.preview.editor = Editor()
        self.preview.schema = self.preview.table = None
        for attribute in ("columns", "indexes", "definition", "heading", "note", "edit"):
            setattr(self.preview, attribute, View())

    def test_live_metadata_is_read_only_escaped_and_released(self):
        self.preview.show_tables("schema`name", [("table`name", "InnoDB")])
        self.assertEqual(len(self.preview.editor.calls), 3)
        for sql, flags in self.preview.editor.calls:
            self.assertTrue(sql.startswith("SHOW "))
            self.assertIn("`schema``name`.`table``name`", sql)
            self.assertEqual(flags, 0)
        self.assertEqual([result.releases for result in self.preview.editor.results], [1, 1, 1])
        self.assertEqual(self.preview.columns.rows[0], {0: "id", 1: "int", 2: "Hayır", 3: "PRI"})
        self.assertEqual(self.preview.columns.rows[1][2], "Evet")
        self.assertEqual(self.preview.indexes.rows[0][2], "Evet")
        self.assertIn("CREATE TABLE", self.preview.definition.value)
        self.assertTrue(self.preview.definition.read_only)

    def test_empty_selection_clears_stale_metadata_and_disables_edit(self):
        self.preview.show_tables("schema", [("sample", "InnoDB")])
        count = len(self.preview.editor.calls)
        self.preview.show_tables("schema", [])
        self.assertEqual(len(self.preview.editor.calls), count)
        self.assertEqual(self.preview.columns.rows, [])
        self.assertEqual(self.preview.indexes.rows, [])
        self.assertEqual(self.preview.definition.value, "")
        self.assertFalse(self.preview.edit.enabled)
        self.preview.edit_definition()
        self.assertEqual(self.preview.editor.alter_calls, [])

    def test_error_is_inline_and_next_selection_can_recover(self):
        self.preview.editor.failure = RuntimeError("permission denied")
        self.preview.show_tables("schema", [("sample", "InnoDB")])
        self.assertIn("permission denied", self.preview.note.text)
        self.preview.editor.failure = None
        self.preview.show_tables("schema", [("sample", "InnoDB")])
        self.assertNotIn("permission denied", self.preview.note.text)
        self.assertEqual(len(self.preview.columns.rows), 2)

    def test_result_released_when_reader_fails(self):
        def fail(result):
            raise RuntimeError("reader failed")
        with self.assertRaisesRegex(RuntimeError, "reader failed"):
            self.preview._rows("SHOW INDEX FROM `s`.`t`", fail)
        self.assertEqual(self.preview.editor.results[0].releases, 1)

    def test_edit_opens_the_native_definition_editor(self):
        self.preview.show_tables("exact.schema", [("exact.table", "InnoDB")])
        self.preview.edit_definition()
        self.assertEqual(self.preview.editor.alter_calls, [("db.Table", "exact.schema", "exact.table")])


class MetadataError(Exception):
    pass


class StrictLabel(View):
    def set_text(self, value):
        if not isinstance(value, str):
            raise TypeError("native Label.set_text requires a string")
        self.text = value


class MetadataEditor:
    def __init__(self, info, count="4", innodb=None):
        self.info = Result(info)
        self.count = Result([{"column_count": count}])
        self.innodb = Result(innodb or [{"PATH": None, "datadir": None}])
        self.calls = []

    def executeManagementQuery(self, query, flags):
        self.calls.append((query, flags))
        if "information_schema.tables" in query:
            return self.info
        if "information_schema.columns" in query:
            return self.count
        return self.innodb


def metadata_handlers():
    source = ast.parse(TABLE_SOURCE.read_text(encoding="utf-8-sig"))
    names = {"metadata_text", "read_metadata_row", "TableInfoPanel", "TableDDL"}
    nodes = [node for node in source.body if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names]
    namespace = {
        "mforms": SimpleNamespace(Box=object), "grt": SimpleNamespace(DBError=MetadataError),
        "tr": lambda text: text, "human_size": lambda value: str(value) + " B",
        "log_error": lambda message: None,
    }
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(TABLE_SOURCE), "exec"), namespace)
    return namespace


class MetadataAcceptance(unittest.TestCase):
    def panel(self, editor, innodb=False):
        panel_type = metadata_handlers()["TableInfoPanel"]
        panel = panel_type.__new__(panel_type)
        panel.editor = editor
        panel._schema, panel._table = "qa_schema", "qa_table"
        panel.i_s_innodb_available = lambda: innodb
        panel.target_version = SimpleNamespace(is_supported_mysql_version_at_least=lambda *args: True)
        for name in ("engine", "row_format", "column_count", "table_rows", "avg_row_length", "data_length",
                     "index_length", "max_data_length", "data_free", "table_size", "update_time", "create_time",
                     "auto_increment", "table_collation", "create_options", "table_comment", "file_format", "data_path"):
            label = StrictLabel()
            label.text = "stale"
            setattr(panel, name, label)
        return panel

    def test_null_metadata_never_reaches_native_labels_or_invents_zero(self):
        editor = MetadataEditor([{"ENGINE": None, "UPDATE_TIME": None, "DATA_LENGTH": None, "INDEX_LENGTH": "0"}], count=None)
        panel = self.panel(editor)
        panel.refresh()
        self.assertEqual(panel.engine.text, "")
        self.assertEqual(panel.update_time.text, "")
        self.assertEqual(panel.column_count.text, "")
        self.assertEqual(panel.data_length.text, "")
        self.assertEqual(panel.index_length.text, "0 B")
        self.assertEqual(panel.table_size.text, "")
        self.assertIsNone(panel.get_table_engine())
        self.assertEqual([editor.info.releases, editor.count.releases], [1, 1])

    def test_real_size_engine_and_column_count_are_preserved(self):
        editor = MetadataEditor([{"ENGINE": "InnoDB", "DATA_LENGTH": "16", "INDEX_LENGTH": "8", "TABLE_ROWS": 0}])
        panel = self.panel(editor)
        panel.refresh()
        self.assertEqual(panel.engine.text, "InnoDB")
        self.assertEqual(panel.get_table_engine(), "innodb")
        self.assertEqual(panel.table_rows.text, "0")
        self.assertEqual(panel.table_size.text, "24 B")
        self.assertEqual(panel.column_count.text, "4")
        self.assertTrue(all(flags == 0 for query, flags in editor.calls))

    def test_empty_metadata_and_null_path_clear_stale_labels(self):
        editor = MetadataEditor([], count=None)
        panel = self.panel(editor, innodb=True)
        panel.refresh()
        self.assertEqual(panel.engine.text, "")
        self.assertEqual(panel.data_path.text, "")
        self.assertEqual(panel.file_format.text, "")
        self.assertEqual([editor.info.releases, editor.count.releases, editor.innodb.releases], [1, 1, 1])

    def test_metadata_result_is_released_when_field_read_fails(self):
        editor = MetadataEditor([{"ENGINE": "InnoDB"}])
        def fail(name):
            raise MetadataError("metadata read denied")
        editor.info.stringFieldValueByName = fail
        panel = self.panel(editor)
        panel.refresh()
        self.assertEqual(editor.info.releases, 1)
        self.assertEqual(panel.engine.text, "")
        self.assertEqual(panel.column_count.text, "4")

    def test_ddl_null_is_safe_and_result_is_released(self):
        handlers = metadata_handlers()
        ddl_type = handlers["TableDDL"]
        panel = ddl_type.__new__(ddl_type)
        result = Result([{1: None}])
        panel.editor = SimpleNamespace(executeManagementQuery=lambda *args: result)
        panel._schema, panel._table = "qa_schema", "qa_table"
        panel.code_editor = View()
        panel.code_editor.value = "stale definition"
        panel.refresh()
        self.assertEqual(panel.code_editor.value, "")
        self.assertTrue(panel.code_editor.read_only)
        self.assertEqual(result.releases, 1)


if __name__ == "__main__":
    unittest.main()
