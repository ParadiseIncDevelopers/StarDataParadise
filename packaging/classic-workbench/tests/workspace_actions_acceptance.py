"""Exercise actual maintenance handlers with a recording backend, without a server."""
from pathlib import Path
from types import SimpleNamespace
import ast
import unittest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "classic-workbench/plugins/wb.sqlide/sqlide_catalogman_ext.py"


def load_handlers():
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"))
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "do_table_maintenance"]
    panel = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "TableMaintenancePanel")
    functions += [node for node in panel.body if isinstance(node, ast.FunctionDef) and node.name in ("optimize_table", "show_tables")]
    module = ast.Module(body=functions, type_ignores=[])
    namespace = {}
    exec(compile(module, str(SOURCE), "exec"), namespace)
    return namespace


class Value:
    def __init__(self, value):
        self.value = value

    def get_active(self):
        return self.value

    def get_string_value(self):
        return self.value


class Backend:
    def __init__(self):
        self.calls = []
        self.fail_optimize = False

    def executeManagementQuery(self, sql, flags):
        self.calls.append(("query", sql, flags))
        if sql.startswith("OPTIMIZE") and self.fail_optimize:
            raise RuntimeError("synthetic server error")
        return SimpleNamespace(goToFirstRow=lambda: True, stringFieldValue=lambda column: "0")

    def executeManagementCommand(self, sql, flags):
        self.calls.append(("command", sql, flags))


class ResultForm:
    def __init__(self, *args):
        pass

    def set_title(self, title):
        pass

    def run(self):
        pass


class WorkspaceActionsAcceptance(unittest.TestCase):
    def setUp(self):
        self.handlers = load_handlers()
        self.previews = []
        self.accept = False
        self.handlers["review_maintenance_sql"] = self.review
        self.handlers["MaintenanceResultForm"] = ResultForm
        self.backend = Backend()

    def review(self, editor, sql):
        self.previews.append(sql)
        return self.accept

    def panel(self):
        return SimpleNamespace(editor=self.backend, optimize_ft_only=Value(True),
                               optimize_ft_numwords=Value("2000"), optimize_local=Value(False),
                               table_list=["`demo`.`measurements`"])

    def test_cancelled_maintenance_runs_no_query(self):
        self.handlers["do_table_maintenance"](self.backend, "ANALYZE TABLE %s", ["`demo`.`measurements`"])
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(self.previews, ["ANALYZE TABLE `demo`.`measurements`;"])

    def test_cancelled_optimize_never_changes_global_variables(self):
        self.handlers["optimize_table"](self.panel())
        self.assertEqual(len(self.previews), 1)
        self.assertIn("OPTIMIZE TABLE `demo`.`measurements`", self.previews[0])
        self.assertEqual(self.backend.calls, [("query", "SELECT @@innodb_optimize_fulltext_only", 0)])

    def test_approved_optimize_restores_actual_boolean_value(self):
        self.accept = True
        self.handlers["optimize_table"](self.panel())
        self.assertEqual(len(self.previews), 1)
        self.assertEqual(self.backend.calls[-1], ("command", "SET GLOBAL innodb_optimize_fulltext_only=0", 1))
        self.assertIn(("query", "OPTIMIZE TABLE `demo`.`measurements`", 1), self.backend.calls)

    def test_failed_optimize_still_restores_variable(self):
        self.accept = True
        self.backend.fail_optimize = True
        with self.assertRaisesRegex(RuntimeError, "synthetic server error"):
            self.handlers["optimize_table"](self.panel())
        self.assertEqual(self.backend.calls[-1], ("command", "SET GLOBAL innodb_optimize_fulltext_only=0", 1))

    def test_object_names_are_quoted(self):
        panel = SimpleNamespace(_buttons=[])
        self.handlers["show_tables"](panel, "my`schema", [("table`name", "InnoDB")])
        self.assertEqual(panel.table_list, ["`my``schema`.`table``name`"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
