"""Exercise real readout/rate/initial-selection code without a writable server."""
import ast
import json
import re
from pathlib import Path
import unittest
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
FRONTEND = ROOT / "classic-workbench/plugins/wb.admin/frontend"


def definitions(filename, names):
    tree = ast.parse((FRONTEND / filename).read_text(encoding="utf-8"))
    nodes = [node for node in tree.body if getattr(node, "name", None) in names]
    scope = {"tr": lambda text: text, "json": json}
    exec(compile(ast.Module(body=nodes, type_ignores=[]), filename, "exec"), scope)
    return scope


class Result:
    def __init__(self, values):
        self.values = values
        self.first = True

    def nextRow(self):
        value, self.first = self.first, False
        return value

    def stringByIndex(self, index):
        return self.values[index - 1]


class QueryFailure(Exception):
    def __init__(self, error):
        self.error = error


class ReadOnlyBackend:
    def __init__(self, failures=None, cipher="TLS_AES_256_GCM_SHA384", capabilities=None):
        self.queries = []
        self.failures = failures or {}
        self.cipher = cipher
        self.capabilities = capabilities or '{"commands":["create_model","train_model","predict_model"]}'

    def exec_query(self, sql):
        assert sql.startswith(("SELECT ", "SHOW ")), sql
        self.queries.append(sql)
        for marker, code in self.failures.items():
            if marker in sql:
                raise QueryFailure(code)
        if "CAPABILITIES" in sql:
            return Result([self.capabilities])
        if "Ssl_cipher" in sql:
            return Result(["Ssl_cipher", self.cipher])
        if "compatibility_mode" in sql:
            return Result(["legacy"])
        return Result(["0"])


class AdminMetricsAcceptance(unittest.TestCase):
    def setUp(self):
        self.read = definitions("wb_admin_utils.py", {"read_product_metrics"})["read_product_metrics"]

    def test_real_declared_commands_and_zero_counts(self):
        backend = ReadOnlyBackend()
        rows = {name: value for name, value, hint in self.read(backend)}
        self.assertEqual(rows["SDP SQL commands"], "CREATE MODEL, TRAIN MODEL, PREDICT MODEL")
        self.assertEqual(rows["Trained model versions"], "0")
        self.assertEqual(rows["Administration session TLS"], backend.cipher)
        self.assertTrue(all(query.startswith(("SELECT ", "SHOW ")) for query in backend.queries))

    def test_missing_and_denied_are_distinct_from_zero(self):
        backend = ReadOnlyBackend({"sdp_model_definitions": 1142, "compute_jobs": 1146})
        rows = {name: value for name, value, hint in self.read(backend)}
        self.assertEqual(rows["Model definitions"], "Access denied")
        self.assertEqual(rows["Compute queue"], "Not installed")
        self.assertEqual(rows["Trained model versions"], "0")

    def test_invalid_capability_payload_is_unavailable(self):
        backend = ReadOnlyBackend(capabilities='{"commands":"create_model"}')
        rows = {name: value for name, value, hint in self.read(backend)}
        self.assertEqual(rows["SDP SQL commands"], "Unavailable")

    def test_empty_command_list_is_not_false_enabled(self):
        rows = self.read(ReadOnlyBackend(capabilities='{"commands":[]}'))
        self.assertEqual(rows[0][1], "No optional commands enabled")

    def test_plaintext_tls_is_not_server_capability(self):
        rows = {name: value for name, value, hint in self.read(ReadOnlyBackend(cipher=""))}
        self.assertEqual(rows["Administration session TLS"], "Unencrypted")

    def test_missing_runtime_counter_is_not_zero(self):
        function = definitions("wb_admin_monitor.py", {"runtime_metric_values"})["runtime_metric_values"]
        rows = dict(function({"Threads_running": 0, "Innodb_buffer_pool_pages_total": 100,
                              "Innodb_buffer_pool_pages_free": 25}))
        self.assertEqual(rows["Active sessions"], "0")
        self.assertEqual(rows["Connected sessions"], "Not reported")
        self.assertEqual(rows["InnoDB buffer usage"], "75.0%")

    def test_variable_description_distinguishes_status_and_setting_and_keeps_english(self):
        scope = definitions("wb_admin_variables.py", {"variable_description"})
        scope["current_language"] = lambda: "tr"
        describe = scope["variable_description"]
        self.assertEqual(describe("Untranslated explanation", "status"), "Status counter reported by the server.")
        self.assertEqual(describe("Untranslated explanation", "system"), "Server setting. The identifier and value are reported by the server.")
        scope["current_language"] = lambda: "en"
        self.assertEqual(describe("Original detailed explanation", "status"), "Original detailed explanation")

    def test_rate_from_zero_and_timestamp_zero(self):
        code = definitions("wb_admin_performance_dashboard.py", {"CDifferencePerSecond", "CSingleDifferencePerSecond"})
        rate = code["CSingleDifferencePerSecond"]("%(counter)s")
        self.assertIsNone(rate.handle({"counter": 0}, 0))
        self.assertEqual(rate.handle({"counter": 10}, 2), 5.0)

    def test_rate_counter_reset_or_absent_is_not_negative_or_zero(self):
        code = definitions("wb_admin_performance_dashboard.py", {"CDifferencePerSecond", "CSingleDifferencePerSecond", "CRawValue"})
        rate = code["CSingleDifferencePerSecond"]("%(counter)s")
        rate.handle({"counter": 10}, 1)
        self.assertIsNone(rate.handle({"counter": 1}, 2))
        self.assertIsNone(rate.handle({}, 3))
        self.assertIsNone(code["CRawValue"]("%(absent)s").handle({}, 1))

    def test_initial_variables_selection_matches_native_one_argument_api(self):
        tree = ast.parse((FRONTEND / "wb_admin_variables.py").read_text(encoding="utf-8"))
        viewer = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "VariablesViewer")
        method = next(node for node in viewer.body if isinstance(node, ast.FunctionDef) and node.name == "delayed_init")
        scope = {}
        exec(compile(ast.Module(body=[method], type_ignores=[]), "variables", "exec"), scope)

        class Tree:
            def __init__(self):
                self.selection = []

            def get_selection(self):
                return self.selection

            def node_at_row(self, row):
                return row

            def select_node(self, node):
                self.selection = [node]

        selection = Tree()
        subject = SimpleNamespace(_delayed_init_tm=1, ctrl_be=SimpleNamespace(exec_query=lambda sql: None),
                                  command="SHOW GLOBAL VARIABLES", support_variable_persistence=False,
                                  variable_info={}, variables_in_group={"Other": []}, tree=selection,
                                  values=SimpleNamespace(get_selection=lambda: []),
                                  copy_selected_to_clipboard_button=SimpleNamespace(set_enabled=lambda value: None),
                                  user_groups=SimpleNamespace(content={}), refresh=lambda: None)
        scope["delayed_init"](subject, [])
        self.assertEqual(selection.selection, [0])
        selection.selection = [1]
        scope["delayed_init"](subject, [])
        self.assertEqual(selection.selection, [1])

    def test_windows_native_disk_space_translation_preserves_values_and_unknown(self):
        source = ROOT / "classic-workbench/plugins/wb.admin/backend/wb_server_management.py"
        tree = ast.parse(source.read_text(encoding="utf-8"))
        owner = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "FileOpsLocalWindows")
        method = next(node for node in owner.body if isinstance(node, ast.FunctionDef) and node.name == "get_available_space")
        catalog = json.loads((ROOT / "classic-workbench/res/locales/tr-admin.json").read_text(encoding="utf-8"))
        state = {"raw": "49.21 GB of 475.69 GB available"}
        scope = {"Users": SimpleNamespace(CURRENT="current"), "FunctionType": SimpleNamespace(String="string"),
                 "FileUtils": SimpleNamespace(get_free_space=lambda path: state["raw"]), "re": re,
                 "tr": lambda text: catalog.get(text, text)}
        exec(compile(ast.Module(body=[method], type_ignores=[]), str(source), "exec"), scope)
        read = scope["get_available_space"]
        self.assertEqual(read(object(), "C:/"), "49.21 GB boş - 475.69 GB toplam")
        scope["tr"] = lambda text: text
        self.assertEqual(read(object(), "C:/"), state["raw"])
        scope["tr"] = lambda text: catalog.get(text, text)
        state["raw"] = "Could not determine"
        self.assertEqual(read(object(), "C:/"), "Belirlenemedi")
        for raw in ("raw unexpected result", "49.21 GB of 475.69 GB available (unknown)", None):
            state["raw"] = raw
            self.assertEqual(read(object(), "C:/"), raw)

    def test_remote_windows_disk_space_uses_translated_format_and_fallback(self):
        source = ROOT / "classic-workbench/plugins/wb.admin/backend/wb_server_management.py"
        tree = ast.parse(source.read_text(encoding="utf-8"))
        owner = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "FileOpsRemoteWindows")
        method = next(node for node in owner.body if isinstance(node, ast.FunctionDef) and node.name == "get_available_space")
        catalog = json.loads((ROOT / "classic-workbench/res/locales/tr-admin.json").read_text(encoding="utf-8"))
        scope = {"Users": SimpleNamespace(CURRENT="current"), "quote_path": lambda value: value,
                 "tr": lambda text: catalog.get(text, text)}
        exec(compile(ast.Module(body=[method], type_ignores=[]), str(source), "exec"), scope)
        def output(command, user, password, output_handler):
            output_handler("2 Dir(s) 16,777,216 bytes free")
            return 0
        subject = SimpleNamespace(process_ops=SimpleNamespace(exec_cmd=output))
        self.assertEqual(scope["get_available_space"](subject, "C:/"), "16.00 MB boş")
        subject.process_ops.exec_cmd = lambda *args, **kwargs: 1
        self.assertEqual(scope["get_available_space"](subject, "C:/"), "Belirlenemedi")


if __name__ == "__main__":
    unittest.main()
