# Copyright (c) 2012, 2025, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is designed to work with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms, as
# designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have either included with
# the program or referenced in the documentation.
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

import mforms
import grt
import sys
from workbench.ui import WizardPage
from workbench.utils import server_version_str2tuple

class MainView(WizardPage):
    def __init__(self, main):
        WizardPage.__init__(self, main, "Overview", wide=True, no_buttons=True)

        self.main = main

        self.main.add_content_page(self, "Overview", "Overview", "migration")
        grt.modules.Workbench.initializeOtherRDBMS()

    def create_ui(self):
        self.content.set_spacing(8)
        self.content.add(mforms.newLabel("Verileri okuyacağınız kaynağı ve yazacağınız hedefi seçin."), False, True)
        pair = mforms.newBox(True)
        pair.set_spacing(12)
        self.source_selector = mforms.newSelector()
        self.target_selector = mforms.newSelector()
        self.source_driver = mforms.newLabel("")
        self.target_driver = mforms.newLabel("")
        for title, selector, driver in (("Kaynak", self.source_selector, self.source_driver),
                                        ("Hedef", self.target_selector, self.target_driver)):
            column = mforms.newBox(False)
            column.set_spacing(8)
            heading = mforms.newLabel(title)
            heading.set_font("Segoe UI bold 12")
            column.add(heading, False, True)
            selector.set_name(title + " bağlantısı")
            selector.add_changed_callback(self.connection_changed)
            column.add(mforms.newLabel("Bağlantı"), False, True)
            column.add(selector, False, True)
            column.add(mforms.newLabel("Sürücü"), False, True)
            driver.set_font("Consolas 9.75")
            column.add(driver, False, True)
            pair.add(column, True, True)
        self.content.add(pair, False, True)
        actions = mforms.newBox(True)
        actions.set_spacing(8)
        settings = mforms.newButton()
        settings.set_text("Bağlantı ayarları…")
        settings.add_clicked_callback(self.start)
        actions.add(settings, False, True)
        odbc = mforms.newButton()
        odbc.set_text("ODBC yöneticisini aç")
        odbc.add_clicked_callback(self.start_odbc)
        actions.add(odbc, False, True)
        self.content.add(actions, False, True)
        heading = mforms.newLabel("Gerekli izinler")
        heading.set_font("Segoe UI bold 10")
        self.content.add(heading, False, True)
        self.content.add(mforms.newLabel("Kaynak    Şemayı ve verileri okuma"), False, True)
        self.content.add(mforms.newLabel("Hedef       Tablo oluşturma ve veri yazma"), False, True)
        self.footer = mforms.newBox(True)
        self.footer.set_padding(8)
        self.footer.set_back_color("#E7EEEF")
        self.read_button = mforms.newButton()
        self.read_button.set_text("Şemaları oku")
        self.read_button.set_back_color("#096E56")
        self.read_button.set_front_color("#FFFFFF")
        self.read_button.set_font("Segoe UI 9.75")
        self.read_button.set_size(130, 26)
        self.read_button.add_clicked_callback(self.read_schemas)
        self.footer.add_end(self.read_button, False, True)
        self.add_end(self.footer, False, True)
        self.sources = []
        self.targets = []

    def page_activated(self, advancing):
        super().page_activated(advancing)
        previous_source = self.selected_connection(True)
        previous_target = self.selected_connection(False)
        supported = {rdbms.__id__ for rdbms in self.main.plan.supportedSources()}
        mgmt = grt.root.wb.rdbmsMgmt
        self.sources = [connection for connection in list(mgmt.storedConns) + list(mgmt.otherStoredConns)
                        if connection.driver and connection.driver.owner.__id__ in supported]
        self.targets = [connection for connection in mgmt.storedConns if connection.driver
                        and connection.driver.owner.name == "Mysql"]
        for selector, connections, previous in ((self.source_selector, self.sources, previous_source),
                                                 (self.target_selector, self.targets, previous_target)):
            selector.clear()
            selector.add_item("Bağlantı seçin…")
            selector.add_items([connection.name for connection in connections])
            selector.set_selected(connections.index(previous) + 1 if previous in connections else 0)
        self.connection_changed()

    def selected_connection(self, source):
        connections = self.sources if source else self.targets
        selector = self.source_selector if source else self.target_selector
        selected = selector.get_selected_index() - 1
        return connections[selected] if 0 <= selected < len(connections) else None

    def connection_changed(self):
        for source, label in ((True, self.source_driver), (False, self.target_driver)):
            connection = self.selected_connection(source)
            label.set_text(connection.driver.caption if connection else "—")
        self.read_button.set_enabled(bool(self.selected_connection(True) and self.selected_connection(False)))

    def read_schemas(self):
        source = self.selected_connection(True)
        target = self.selected_connection(False)
        if not source or not target:
            return
        self.main.plan.setSourceConnection(source)
        self.main.plan.setTargetConnection(target)
        self.main._page_trail = [self]
        self.main.go_next_page(3)

    def start_odbc(self):
        if not grt.modules.PyWbUtils.startODBCAdmin():
            mforms.Utilities.show_error("ODBC yöneticisi", "ODBC yöneticisi bulunamadı.", "Tamam", "", "")
            

    def create_uix(self):
        container = mforms.newBox(True)
        container.set_spacing(30)

        left_side_box = mforms.newBox(False)
        left_side_box.set_padding(8)
        logo_image = mforms.newImageBox()
        logo_image.set_image('migration_logo.png')
        left_side_box.add(logo_image, False, True)
        container.add(left_side_box, False, True)

        # Main layout structure
        content = mforms.newBox(False)
        content.set_padding(8)
        content.set_spacing(12)
        title_image = mforms.newImageBox()
        title_image.set_image('migration_title.png')
        content.add(title_image, False, True)

        help_label = mforms.newLabel('''To perform a new migration click the [Start New Migration] button below. To re-run
a previous migration or to perform a new migration based on a previous
migration please double click one of the migration projects below.''')
        content.add(help_label, False, True)

        wrapper_button_box = mforms.newBox(True)
        wrapper_button_box.set_padding(8)
        button_new_migration = mforms.newButton()
        button_new_migration.set_text('Start New Migration')
        #button_new_migration.add_clicked_callback(lambda x: x)
        wrapper_button_box.add(button_new_migration, False, True)
        content.add(wrapper_button_box, False, True)

        project_box = mforms.newBox(False)
        project_box.set_spacing(8)
        project_label = mforms.newLabel('Project Overview')
        project_label.set_style(mforms.BoldStyle)
        project_box.add(project_label, False, True)
        project_tree = mforms.newTreeView(mforms.TreeDefault)
        project_box.add(project_tree, True, True)
        project_button_box = mforms.newBox(True)
        project_button_box.set_spacing(8)
        button_rerun_migration = mforms.newButton()
        button_rerun_migration.set_text('Re-Run Migration')
        button_edit_migration = mforms.newButton()
        button_edit_migration.set_text('Edit Migration Project')
        project_button_box.add(button_rerun_migration, False, True)
        project_button_box.add(button_edit_migration, False, True)
        project_box.add(project_button_box, False, True)
        content.add(project_box, False, True)

        container.add(content, False, True)

        # Right side layout structure
        right_side_box = mforms.newBox(False)
        right_side_image = mforms.newImageBox()
        right_side_image.set_image('migration_background.png')
        right_side_image.set_image_align(mforms.TopRight)
        right_side_box.add(right_side_image, False, True)
        container.add(right_side_box, True, True)

        self.add(container, True, True)


    def start(self):
        try:
            import pyodbc
        except ImportError:
            mforms.Utilities.show_message_and_remember('Dependency Warning',
                   'Could not import the pyodbc python module. You need pyodbc 2.1.8 or newer for migrations from RDBMSes other than MySQL.',
                   'OK', '', '',
                   'wb.migration.nopyodbc',
                   "Don't show this message again")
        else:
            pyodbc_version = server_version_str2tuple(pyodbc.version)
            if pyodbc_version < (2, 1, 8):
                mforms.Utilities.show_message_and_remember('Dependency Warning',
                    '''We have detected that you have pyodbc %s installed but the migration tool requires pyodbc 2.1.8 or newer for migrations from RDBMSes other than MySQL.
Please install a supported pyodbc version. You may proceed with the currently installed version, but the migration may not succeed.
To install the latest version of pyodbc, execute "sudo easy_install pyodbc" from a command line shell.''' % pyodbc.version, 'OK', '', '',
                    'wb.migration.oldpyodbc',
                    "Don't show this message again")

        self.main.start()
            
        
