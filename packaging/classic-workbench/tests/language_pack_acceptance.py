"""Behavioral acceptance for the shared Classic UI language catalog."""
import importlib.util
import json
import os
import re
import sys
from types import SimpleNamespace
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
LOCALES = ROOT / "classic-workbench/res/locales"
HELPER = ROOT / "classic-workbench/library/python/workbench/sdp_i18n.py"


def language_module(language="tr", directory=LOCALES):
    spec = importlib.util.spec_from_file_location("sdp_language_acceptance", HELPER)
    module = importlib.util.module_from_spec(spec)
    with patch.dict(os.environ, {"SDP_UI_LANGUAGE": language, "SDP_UI_LOCALES": str(directory)}):
        spec.loader.exec_module(module)
        module.tr("")
        module._load()
    return module


class LanguagePackAcceptance(unittest.TestCase):
    def test_english_source_wins_over_reverse_catalog_alias(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            (directory / "tr.json").write_text(json.dumps({"Upstream": "Host", "Host": "Sunucu"}), encoding="utf-8")
            language = language_module("en", directory)
            self.assertEqual(language.tr("Host"), "Host")
            self.assertEqual(language.tr("Sunucu"), "Host")

    def test_bootstrap_language_precedes_unmerged_grt_defaults(self):
        # Plugins are imported before saved GRT options are merged. Native
        # bootstrap's session language must win over those temporary defaults.
        grt = SimpleNamespace(root=SimpleNamespace(wb=SimpleNamespace(options=SimpleNamespace(options={"workbench:UILanguage": "tr"}))))
        with patch.dict(sys.modules, {"grt": grt}):
            language = language_module("en")
            self.assertEqual(language.current_language(), "en")
            self.assertEqual(language.tr("Dosya"), "File")

    def test_native_printf_placeholders_preserved(self):
        # A translated native format must retain argument types/order. A bad
        # catalog otherwise risks a varargs crash rather than a cosmetic error.
        placeholder = re.compile(r"%(?:\d+\$)?[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn]")
        for path in LOCALES.glob("tr*.json"):
            catalog = json.loads(path.read_text(encoding="utf-8-sig"))
            for source, target in catalog.items():
                with self.subTest(catalog=path.name, source=source):
                    self.assertEqual(placeholder.findall(source.replace("%%", "")), placeholder.findall(target.replace("%%", "")))

    def test_turkish_menu_and_tab_navigation(self):
        language = language_module()
        self.assertEqual(language.tr("Select Next Main Tab"), "Sonraki ana sekmeye geç")
        self.assertEqual(language.tr("Select Previous Main Tab"), "Önceki ana sekmeye geç")
        self.assertEqual(language.tr("&File"), "Dosya")

    def test_english_language_and_existing_turkish_labels(self):
        language = language_module("en")
        self.assertEqual(language.tr("Select Next Main Tab"), "Select Next Main Tab")
        self.assertEqual(language.tr("Dosya"), "File")
        self.assertEqual(language.tr("Arayüz dili"), "Interface language")

    def test_sql_and_server_identifiers_remain_exact(self):
        language = language_module()
        for value in ("SELECT id, exam_score FROM new_db_for_ai.ai_exam_demo;", "max_allowed_packet", "utf8mb4", "MySQL", "9007199254740993", "123.4500", "", None):
            self.assertEqual(language.tr(value), value)

    def test_unknown_language_falls_back_to_turkish(self):
        self.assertEqual(language_module("not-a-locale").tr("Cancel"), "Vazgeç")

    def test_invalid_catalog_does_not_break_ui(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            (directory / "tr-broken.json").write_text("{broken", encoding="utf-8")
            (directory / "tr.json").write_text(json.dumps({"Cancel": "Vazgeç"}), encoding="utf-8")
            language = language_module(directory=directory)
            self.assertEqual(language.tr("Cancel"), "Vazgeç")
            self.assertEqual(language.tr("Unknown presentation label"), "Unknown presentation label")

    def test_split_catalogs_merge_and_preserve_values(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            (directory / "tr.json").write_text(json.dumps({"File": "Dosya"}), encoding="utf-8")
            (directory / "tr-admin.json").write_text(json.dumps({"Server overview": "Sunucu özeti"}), encoding="utf-8")
            language = language_module(directory=directory)
            self.assertEqual(language.tr("File"), "Dosya")
            self.assertEqual(language.tr("Server overview"), "Sunucu özeti")

    def test_main_menu_has_turkish_catalog_coverage(self):
        language = language_module()
        menu = ET.parse(ROOT / "classic-workbench/res/wbdata/main_menu.xml")
        unchanged = {"Classic", "IDEF1X", "UML", "Model"}
        missing = []
        for item in menu.findall(".//value[@key='caption']"):
            value = item.text or ""
            key = language._key(value)
            if value and value.isascii() and key not in language._messages and key not in language._sources and key not in unchanged:
                missing.append(value)
        self.assertEqual(missing, [], "Missing menu language keys")

    def test_result_form_toolbar_captions_have_catalog_coverage(self):
        source = (ROOT / "classic-workbench/backend/wbprivate/sqlide/result_form_view.cpp").read_text(encoding="utf-8")
        constructor = source.split("ResultFormView::ResultFormView(", 1)[1].split("ResultFormView::~ResultFormView", 1)[0]
        setters = "\n".join(re.findall(r"item->set_text\((.*?)\);", constructor, flags=re.S))
        captions = re.findall(r'_\("([^"\\]*)"\)', setters)
        self.assertIn("Previous", captions)
        self.assertIn("Next", captions)
        language = language_module()
        for caption in captions:
            # Some existing native captions are already Turkish (including
            # ASCII words such as Son/Sil); reverse aliases cover those too.
            with self.subTest(caption=caption):
                key = language._key(caption)
                self.assertTrue(key in language._messages or key in language._sources,
                                "Missing Form toolbar language key: " + caption)
        self.assertEqual(language.tr("Previous"), "Önceki")

    def test_native_sql_save_filter_is_localized_without_changing_patterns(self):
        source = (ROOT / "classic-workbench/backend/wbprivate/sqlide/wb_sql_editor_panel.cpp").read_text(encoding="utf-8")
        save_as = source.split("bool SqlEditorPanel::save_as(", 1)[1].split("//--------------------------------------------------------------------------------------------------", 1)[0]
        match = re.search(r'dlg\.set_extensions\(_\("([^"\\]+)"\),\s*"([^"\\]*)"\)', save_as)
        self.assertIsNotNone(match, "The actual native save filter must pass through localization")
        filter_source, default_extension = match.groups()
        self.assertEqual(default_extension, "sql")
        self.assertEqual(filter_source, "SQL Files (*.sql)|*.sql")
        language = language_module()
        translated = language.tr(filter_source)
        self.assertEqual(translated.split("|")[1::2], filter_source.split("|")[1::2])
        self.assertEqual(translated.split("|")[0], language.tr("SQL Files (*.sql)"))
        self.assertEqual(translated, "SQL dosyaları (*.sql)|*.sql")
        self.assertEqual(language_module("en").tr(filter_source), filter_source)


if __name__ == "__main__":
    unittest.main()
