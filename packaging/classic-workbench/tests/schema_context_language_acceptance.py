"""Run real schema context handlers with UI/SQL sinks; no GUI or DB writes."""
import ast
import json
import re
from types import SimpleNamespace
import unittest

from language_pack_acceptance import ROOT, LOCALES, language_module

SOURCE = ROOT / "classic-workbench/plugins/wb.sqlide/sqlide_schematree_ext.py"


class Item:
    def __init__(self, text, *_args):
        self.text, self.children, self.callback = text, [], None

    def set_name(self, value):
        self.name = value

    def setInternalName(self, value):
        self.internal_name = value

    def set_enabled(self, value):
        self.enabled = value

    def add_clicked_callback(self, callback):
        self.callback = callback

    def add_item_with_title(self, title, callback, *_args):
        item = Item(title)
        item.callback = callback
        self.children.append(item)
        return item

    def add_separator(self):
        self.children.append(Item(""))


class Menu:
    def __init__(self):
        self.items = []

    def find_item(self, _name):
        return None

    def insert_item(self, position, item):
        self.items.insert(position, item)


class Editor:
    def __init__(self):
        self.alters, self.writes, self.sql = [], [], []
        self.activeQueryEditor = SimpleNamespace(replaceSelection=self.sql.append)

    def alterLiveObject(self, *args):
        self.alters.append(args)

    def executeCommand(self, *args):
        self.writes.append(args)


def handlers(language="tr"):
    translations = language_module(language)
    messages = []
    utilities = SimpleNamespace(show_message=lambda *args: messages.append(args) or -1)
    ui = SimpleNamespace(newMenuItem=Item, fromgrt=lambda menu: menu,
                         SeparatorMenuItem=0, Utilities=utilities, ResultCancel=-1,
                         ResultOk=1, App=SimpleNamespace(get=lambda: SimpleNamespace(set_status_text=lambda _text: None)))
    namespace = {"_": translations.tr, "re": re,
                 "escape_sql_identifier": lambda value: value.replace("`", "``"),
                 "mforms": ui, "template_manager": SimpleNamespace(templates=[SimpleNamespace(name="Table")], edit_templates=lambda: None),
                 "insert_item_to_plugin_context_menu": lambda menu, item: menu.items.append(item),
                 "show_schema_manager": lambda *_args: None,
                 "open_search": lambda *_args: None,
                 "do_create_index": lambda *_args: None,
                 "do_truncate_table": lambda *_args: None}
    names = {"esc_ident", "handleLiveTreeContextMenu", "do_create_object", "do_alter_object", "do_drop_object", "CodeGenerator"}
    parsed = ast.parse(SOURCE.read_text(encoding="utf-8-sig"))
    module = ast.Module(body=[node for node in parsed.body if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names], type_ignores=[])
    exec(compile(module, str(SOURCE), "exec"), namespace)
    return namespace, messages


def selection(name="Table", kind="db.Table"):
    return SimpleNamespace(type=kind, name=name, schemaName="Schema", owner=None)


class SchemaContextLanguageAcceptance(unittest.TestCase):
    def test_inspector_import_export_entry_labels_and_identifiers(self):
        translations = language_module()
        cases = (("sqlide_tableman_ext.py", "handleLiveTreeContextMenu", "Tablo inceleyicisi", "show_table_inspector"),
                 ("sqlide_power_export_wizard.py", "handleContextMenu", "Tablo verisini dışa aktar…", "showPowerExport"),
                 ("sqlide_power_import_wizard.py", "handleContextMenu", "Tabloya veri al…", "showPowerImport"))
        for filename, handler, caption, callback in cases:
            with self.subTest(plugin=filename):
                menu, editor, calls = Menu(), Editor(), []
                namespace = {"tr": translations.tr, "to_unicode": str,
                             "mforms": SimpleNamespace(newMenuItem=Item, fromgrt=lambda value: value, SeparatorMenuItem=0),
                             callback: lambda *args: calls.append(args)}
                path = SOURCE.parent / filename
                parsed = ast.parse(path.read_text(encoding="utf-8-sig"))
                module = ast.Module(body=[node for node in parsed.body if isinstance(node, ast.FunctionDef) and node.name == handler], type_ignores=[])
                exec(compile(module, str(path), "exec"), namespace)
                namespace[handler]("menu", editor, {"menu": menu, "selection": [selection()]})
                item = next(item for item in menu.items if item.text == caption)
                item.callback()
                payload = calls[0][1]
                self.assertEqual(payload, [("Schema", "Table")] if callback == "show_table_inspector" else {"schema": "Schema", "table": "Table"})
                self.assertEqual(editor.writes, [])

    def test_dynamic_table_actions_are_turkish(self):
        namespace, _messages = handlers()
        menu, editor = Menu(), Editor()
        namespace["handleLiveTreeContextMenu"]("menu", editor, {"menu": menu, "selection": [selection()]})
        captions = [item.text for item in menu.items]
        for caption in ("Tablo oluştur…", "Tablo düzenle…", "Tablo kaldır…", "Tabloyu boşalt…", "SQL editörüne gönder"):
            self.assertIn(caption, captions)
        template_menu = next(item for item in menu.items if item.text == "Şablondan tablo oluştur…")
        self.assertEqual(template_menu.children[0].text, "Table")

    def test_create_alter_callbacks_preserve_raw_identifiers(self):
        namespace, _messages = handlers()
        menu, editor = Menu(), Editor()
        namespace["handleLiveTreeContextMenu"]("menu", editor, {"menu": menu, "selection": [selection()]})
        next(item for item in menu.items if item.text == "Tablo oluştur…").callback()
        next(item for item in menu.items if item.text == "Tablo düzenle…").callback()
        self.assertEqual(editor.alters, [("db.Table", "Schema", ""), ("db.Table", "Schema", "Table")])
        self.assertEqual(editor.writes, [])

    def test_multiple_object_actions_keep_count_and_type(self):
        namespace, _messages = handlers()
        menu, editor = Menu(), Editor()
        namespace["handleLiveTreeContextMenu"]("menu", editor, {"menu": menu, "selection": [selection("Table"), selection("Other")]})
        captions = [item.text for item in menu.items]
        self.assertIn("2 nesneyi düzenle - Tablolar…", captions)
        self.assertIn("2 nesneyi kaldır - Tablolar…", captions)
        self.assertIn("2 tabloyu boşalt…", captions)

    def test_english_catalog_returns_english_dynamic_actions(self):
        namespace, _messages = handlers("en")
        menu, editor = Menu(), Editor()
        namespace["handleLiveTreeContextMenu"]("menu", editor, {"menu": menu, "selection": [selection()]})
        captions = [item.text for item in menu.items]
        self.assertIn("Alter Table...", captions)
        self.assertIn("Send to SQL Editor", captions)

    def test_generated_identifier_sql_is_not_translated(self):
        namespace, _messages = handlers()
        editor = Editor()
        generator = namespace["CodeGenerator"](editor, [selection()], True)
        generator.name_long()
        self.assertEqual(editor.sql, ["`Schema`.`Table`"])
        self.assertEqual(editor.writes, [])

    def test_drop_cancel_keeps_names_and_never_executes(self):
        namespace, messages = handlers()
        editor = Editor()
        namespace["do_drop_object"](editor, [selection()])
        self.assertEqual(messages[0][0], "Tabloyu kaldır")
        self.assertIn("`Table`", messages[0][1])
        self.assertEqual(editor.writes, [])

    def test_all_schema_context_presentation_keys_have_catalog_entries(self):
        catalog = {}
        for path in LOCALES.glob("tr*.json"):
            catalog.update(json.loads(path.read_text(encoding="utf-8-sig")))
        parsed = ast.parse(SOURCE.read_text(encoding="utf-8-sig"))
        keys = {node.args[0].value for node in ast.walk(parsed) if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name) and node.func.id == "_" and node.args
                and isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str)}
        self.assertEqual(sorted(keys - catalog.keys()), [])


if __name__ == "__main__":
    unittest.main()
