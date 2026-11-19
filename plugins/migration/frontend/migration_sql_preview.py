# Copyright (c) 2026, StarDataParadise contributors.
# Licensed under GPLv2, as part of the Classic migration frontend.

from pathlib import Path

import mforms
from workbench.ui import WizardPage


class MainView(WizardPage):
    """Inspect generated SQL before entering the existing execution options."""

    def __init__(self, main):
        super().__init__(main, "SQL Preview", wide=True)
        self.main.add_wizard_page(self, "Preview", "SQL Preview")
        self.objects = []

    def create_ui(self):
        self.write_status = mforms.newLabel("")
        self.content.add(self.write_status, False, True)
        self.connection_summary = mforms.newLabel("")
        self.connection_summary.set_font("Consolas 9.75")
        self.content.add(self.connection_summary, False, True)

        controls = mforms.newBox(True)
        controls.set_spacing(8)
        controls.add(mforms.newLabel("Tablo / nesne"), False, True)
        self.object_selector = mforms.newSelector()
        self.object_selector.set_name("SQL önizlemesi için nesne")
        self.object_selector.add_changed_callback(self.show_sql)
        controls.add(self.object_selector, True, True)
        save = mforms.newButton()
        save.set_text("Betiği kaydet…")
        save.add_clicked_callback(self.save_script)
        controls.add_end(save, False, True)
        self.content.add(controls, False, True)

        self.editor = mforms.newCodeEditor()
        self.editor.set_name("SQL önizlemesi")
        self.editor.set_language(mforms.LanguageMySQL)
        self.editor.set_font("Consolas 9.75")
        self.editor.set_read_only(True)
        self.content.add(self.editor, True, True)
        self.selection_count = mforms.newLabel("")
        self.button_box.add(self.selection_count, False, True)

    def page_activated(self, advancing):
        super().page_activated(advancing)
        plan = self.main.plan
        self.write_status.set_text("Hedef oluşturma günlüğü mevcut. Sonuçları gözden geçirin."
                                   if plan.state.creationLog else "Hedefe henüz yazılmadı.")
        source = plan.migrationSource.connection
        target = plan.migrationTarget.connection
        self.connection_summary.set_text("Kaynak  %s   →   Hedef  %s" %
                                         (source.name or source.hostIdentifier,
                                          target.name or target.hostIdentifier))
        self.objects = []
        table_count = 0
        self.object_selector.clear()
        for schema in plan.migrationTarget.catalog.schemata:
            self.objects.append(schema)
            self.object_selector.add_item(schema.name)
            for group in (schema.tables, schema.views, schema.routines):
                for obj in group:
                    self.objects.append(obj)
                    self.object_selector.add_item(schema.name + "." + obj.name)
            table_count += len(schema.tables)
        self.selection_count.set_text("%d tablo seçili" % table_count)
        self.object_selector.set_selected(1 if len(self.objects) > 1 else 0)
        self.show_sql()

    def show_sql(self):
        selected = self.object_selector.get_selected_index()
        self.editor.set_read_only(False)
        self.editor.set_value(self.objects[selected].temp_sql
                              if 0 <= selected < len(self.objects) else "")
        self.editor.set_read_only(True)

    def save_script(self):
        selected = self.object_selector.get_selected_index()
        if not 0 <= selected < len(self.objects):
            return
        chooser = mforms.newFileChooser(mforms.Form.main_form(), mforms.SaveFile)
        chooser.set_title("SQL betiğini kaydet")
        chooser.set_extensions("SQL dosyası|*.sql", "sql")
        if chooser.run_modal():
            destination = Path(chooser.get_path())
            if destination.exists() and mforms.Utilities.show_warning(
                    "Dosyayı değiştir", "Bu dosya zaten var. Değiştirilsin mi?",
                    "Değiştir", "Vazgeç", "") != mforms.ResultOk:
                return
            try:
                destination.write_text(self.objects[selected].temp_sql, encoding="utf-8")
            except OSError as error:
                mforms.Utilities.show_error("SQL betiği kaydedilemedi", str(error), "Tamam", "", "")
