# Copyright (c) 2026, StarDataParadise contributors.
# Licensed under GPLv2, as part of the Classic migration frontend.

import mforms


class StepSidebar(mforms.Box):
    """Keep the native wizard trail while presenting its four user stages."""

    def __init__(self, main):
        super().__init__(False)
        self.main = main
        self.set_padding(12)
        self.set_spacing(4)
        self.entries = {}
        self.sections = {}
        self.buttons = []
        self.markers = []
        for index, title in enumerate(("Kaynak ve hedef", "Tablo eşleştirme", "Önizleme", "Aktarım")):
            row = mforms.newBox(True)
            row.set_spacing(4)
            marker = mforms.newPanel(mforms.TransparentPanel)
            marker.set_size(3, 32)
            row.add(marker, False, True)
            button = mforms.newButton(mforms.ToolButton)
            button.set_text(title)
            button.set_size(-1, 32)
            button.add_clicked_callback(lambda stage=index: self.go_to_stage(stage))
            row.add(button, True, True)
            self.add(row, False, True)
            self.buttons.append(button)
            self.markers.append(marker)
        self.detail = mforms.newLabel("")
        self.detail.set_wrap_text(True)
        self.detail.set_font("Segoe UI 9")
        self.detail.set_front_color("#4F5759")
        self.add(self.detail, False, True)

    def add_section(self, identifier, *args):
        self.sections[identifier] = {
            "Overview": 0, "SourceTarget": 0, "ObjectMigration": 1,
            "Preview": 2, "DataMigration": 3, "Report": 3,
        }[identifier]

    def add_on_section_command_callback(self, callback):
        self.callback = callback

    def add_section_entry(self, section, identifier, title, *args):
        self.entries[identifier] = (self.sections[section], title)

    def set_section_entry_icon(self, identifier, icon):
        # Progress belongs to the existing trail, not to decorative entry icons.
        pass

    def select_entry(self, identifier):
        stage, title = self.entries[identifier]
        visited = {self.entries[page.identifier()][0] for page in self.main._page_trail}
        for index, button in enumerate(self.buttons):
            button.set_font("Segoe UI bold 9.75" if index == stage else "Segoe UI 9.75")
            button.set_back_color("#DDEBE4" if index == stage else "#EDF2F4")
            button.set_front_color("#102629")
            button.set_enabled(index == stage or index in visited)
            self.markers[index].set_back_color("#098874" if index == stage else "#EDF2F4")
        self.detail.set_text(title)

    def go_to_stage(self, stage):
        trail = self.main._page_trail
        candidates = [page for page in trail if self.entries[page.identifier()][0] == stage]
        if not candidates or not trail:
            return
        target = candidates[-1]
        while trail[-1] is not target:
            self.main.go_previous_page()
