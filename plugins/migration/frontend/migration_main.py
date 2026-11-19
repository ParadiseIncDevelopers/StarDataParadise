# Copyright (c) 2012, 2018, Oracle and/or its affiliates. All rights reserved.
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

import sys
import os
import locale
import platform

import grt
import mforms

import migration

import migration_ui_style
import migration_toolbars
import migration_step_sidebar
import migration_sql_preview
import migration_overview
import migration_project_management

import migration_source_selection
import migration_schema_selection
import migration_object_selection
import migration_object_migration
import migration_object_editing
import migration_schema_creation
import migration_data_transfer
import migration_summary


def plat_icon(icon):
    prefix, ext = os.path.splitext(icon)
    if sys.platform != "darwin" and not prefix.endswith("_win"):
        icon = prefix+"_win" + ext
    return icon

#===============================================================================
#
#===============================================================================
class Migration(mforms.AppView):
    def __init__(self):
        mforms.AppView.__init__(self, False, "Veri Taşıma", "migration", True)
        
        self.plan = migration.MigrationPlan()

        self.toolbars_mgr = migration_toolbars.ToolBarManager()
        self.active_tab = None
        self.old_active_tab = None

        self.set_spacing(1)
        self.set_back_color("#A3B4BC")
        
        self.ui_profile = migration_ui_style.UIProfile()

        self.create_navigation()
        self.workspace = mforms.newBox(True)
        self.workspace.set_spacing(1)
        self.workspace.set_back_color("#A3B4BC")
        self.add(self.workspace, True, True)
        self.left_side_cont = self.create_tasks_side()
        self.workspace.add(self.left_side_cont, False, True)
        self.tasks_side.add_section("Overview", "Başlangıç", "BAŞLANGIÇ", 0)
        self.tasks_side.add_section("SourceTarget", "Kaynak ve hedef", "KAYNAK VE HEDEF", 0)
        self.tasks_side.add_section("ObjectMigration", "Tablo eşleştirme", "TABLO EŞLEŞTİRME", 0)
        self.tasks_side.add_section("Preview", "Önizleme", "ÖNİZLEME", 0)
        self.tasks_side.add_section("DataMigration", "Aktarım", "AKTARIM", 0)
        self.tasks_side.add_section("Report", "Rapor", "RAPOR", 0)

        self.tasks_side.add_on_section_command_callback(self.section_clicked)
        self.left_side_cont.set_size(176, -1)
        self.left_side_cont.set_back_color("#E7EEEF")
        self.tasks_side.set_back_color("#E7EEEF")

        self.background = mforms.newPanel(mforms.TransparentPanel)
        self.background.set_title("Veri taşıma")
                
        self._page_trail = []
        self.tabs      = []
        self._name2page = {}

        if platform.system() == 'Windows':
            self.content_box = mforms.newBox(False)
            self.page_heading = mforms.newLabel("")
            self.page_heading.set_font("Segoe UI bold 12")
            self.page_heading.set_front_color("#102629")
            self.page_heading.set_size(-1, 32)
            self.heading_box = mforms.newBox(False)
            self.heading_box.set_padding(12, 8, 12, 0)
            self.heading_box.add(self.page_heading, False, True)
            wrapper = mforms.newBox(False)
            wrapper.set_back_color("#EDF2F4")
            wrapper.add(self.heading_box, False, True)
            wrapper.add(self.content_box, True, True)
            self.background.add(wrapper)
            self.background.set_back_color("#EDF2F4")
            self.content_box.set_back_color("#EDF2F4")
        else:
            self.tabview   = mforms.newTabView(True)
            self.background.add(self.tabview)
            self.background.set_back_image("migration_background.png", mforms.TopRight)

        self.workspace.add(self.background, True, True)

        self._advancing = True
        self.content = {}
        self.add_content()
      
        self._selecting_entry = False
        
        # Load current user numeric locale:
        locale.setlocale(locale.LC_NUMERIC, '')
        
        self.tasks_side.select_entry(self._overview_page.identifier())
        self.section_clicked(self._overview_page.identifier())

    #---------------------------------------------------------------------------
    def create_navigation(self):
        self.home = mforms.App.get().view_at_index(0)
        home_menu = self.home.get_menubar() if self.home else None
        if home_menu:
            def copy_items(source, destination):
                for index in range(source.item_count()):
                    original = source.get_item(index)
                    item = mforms.newMenuItem(original.get_title(), original.get_type())
                    if original.get_shortcut():
                        item.set_shortcut(original.get_shortcut())
                    if original.item_count():
                        copy_items(original, item)
                    elif original.getInternalName() in ("home_connections", "home_models"):
                        item.add_clicked_callback(lambda action=original: self.open_home_section(action))
                    else:
                        item.add_clicked_callback(original.callback)
                    destination.add_item(item)
            self.classic_menu = mforms.MenuBar()
            self.classic_menu.set_managed()
            copy_items(home_menu, self.classic_menu)
            self.set_menubar(self.classic_menu)
        navigation = mforms.newBox(True)
        navigation.set_padding(8, 0, 8, 0)
        navigation.set_spacing(8)
        navigation.set_back_color("#E7EEEF")
        for title, command in (("Bağlantılar", "home_connections"), ("Modeller", "home_models"),
                               ("Veri Taşıma", None)):
            column = mforms.newBox(False)
            button = mforms.newButton(mforms.ToolButton)
            button.set_text(title)
            button.set_font("Segoe UI bold 9.75" if command is None else "Segoe UI 9.75")
            button.set_size(-1, 30)
            if command and home_menu:
                item = home_menu.find_item(command)
                button.add_clicked_callback(lambda action=item: self.open_home_section(action))
            column.add(button, False, True)
            rule = mforms.newPanel(mforms.TransparentPanel)
            rule.set_size(-1, 2)
            rule.set_back_color("#098874" if command is None else "#E7EEEF")
            column.add(rule, False, True)
            navigation.add(column, False, True)
        self.add(navigation, False, True)

    def open_home_section(self, action):
        if self.home and action:
            mforms.App.get().select_view(self.home)
            action.callback()

    #---------------------------------------------------------------------------
    def close(self):
        # Restore default locale:
        locale.setlocale(locale.LC_NUMERIC, 'C')
        if self.on_close:
            self.on_close()
        super(Migration, self).close()
        
    
    def cleanup(self):
        if self.plan:
            self.plan.close()
        self.plan = None

    #---------------------------------------------------------------------------
    def add_content(self):
        self._overview_page = migration_overview.MainView(self)
        self.content[self._overview_page.identifier()] = self._overview_page

        #cont = migration_project_management.MainView(self)
        #self.content["project_management"] = cont


        cont = migration_source_selection.SourceMainView(self)
        self.content[cont.identifier()] = cont
        self._page_trail = [cont] # this is the initial wizard page

        cont = migration_source_selection.TargetMainView(self)
        self.content[cont.identifier()] = cont
        
        cont = migration_source_selection.FetchProgressView(self)
        self.content[cont.identifier()] = cont

        cont = migration_schema_selection.SchemaMainView(self)
        self.content[cont.identifier()] = cont

        cont = migration_schema_selection.ReverseEngineerProgressView(self)
        self.content[cont.identifier()] = cont

        cont = migration_object_selection.ObjectMainView(self)
        self.content[cont.identifier()] = cont
        
        #cont = migration_object_migration.MigrationOptionsView(self)
        #self.content[cont.identifier()] = cont
        
        cont = migration_object_migration.MigrationProgressView(self)
        self.content[cont.identifier()] = cont

        cont = migration_object_editing.MainView(self)
        self.content[cont.identifier()] = cont

        cont = migration_sql_preview.MainView(self)
        self.content[cont.identifier()] = cont

        cont = migration_schema_creation.MainView(self)
        self.content[cont.identifier()] = cont

        cont = migration_schema_creation.CreationProgressView(self)
        self.content[cont.identifier()] = cont

        cont = migration_schema_creation.CreationReportView(self)
        self.content[cont.identifier()] = cont

        cont = migration_data_transfer.SetupMainView(self)
        self.content[cont.identifier()] = cont

        cont = migration_data_transfer.TransferMainView(self)
        self.content[cont.identifier()] = cont

        cont = migration_summary.FinalReportView(self)
        self.content[cont.identifier()] = cont


    #---------------------------------------------------------------------------
    def create_tasks_side(self):
        side_cont = mforms.newPanel(mforms.TransparentPanel)
        side_cont.set_title("Adımlar")
        self.tasks_side = migration_step_sidebar.StepSidebar(self)

        side_cont.add(self.tasks_side)
        return side_cont

    #---------------------------------------------------------------------------
    def section_clicked(self, entry_id):
        if self._selecting_entry:
            return
        
        new_active = self.content[entry_id]

        if new_active != self.active_tab:
            self._selecting_entry = True

            self.old_active_tab = self.active_tab
            self.active_tab = new_active

            if platform.system() == 'Windows':
                if self.old_active_tab:
                    self.old_active_tab.show(False)
                self.active_tab.show(True)
            else:
                i = self._name2page.get(entry_id, None)
                self.tabview.set_active_tab(i)

            self.tab_changed()
            self.tasks_side.select_entry(entry_id)

            self._selecting_entry = False

    #---------------------------------------------------------------------------
    def tab_changed(self):
        if self.old_active_tab and hasattr(self.old_active_tab, "page_deactivated"):
            self.old_active_tab.page_deactivated()

        if self.active_tab is not None and hasattr(self.active_tab, "page_activated"):
            self.active_tab.page_activated(self._advancing)
            migration_ui_style.style_page(self.active_tab)
            title = migration_ui_style.page_title(self.active_tab.identifier())
            self.background.set_title(title)
            if hasattr(self, "page_heading"):
                self.page_heading.set_text(title)

    #---------------------------------------------------------------------------
    def add_content_page(self, page, section_id, item_name, icon_name):
        item_name = migration_ui_style.page_title(item_name)
        entry = self.tasks_side.add_section_entry(section_id, page.identifier(), item_name, item_name, plat_icon(icon_name + ".png"), mforms.TaskEntryLink)
        
        if platform.system() == 'Windows':
            self.content_box.add(page, True, True)
            i = len(self._name2page)
            page.show(False)
        else:
            i = self.tabview.add_page(page, "")

        self.tabs.append(page)
        self._name2page[page.identifier()] = i

    #---------------------------------------------------------------------------
    def add_wizard_page(self, page, section_id, item_name):
        if item_name in ("SQL Preview", "Target Creation Options"):
            section_id = "Preview"
        elif item_name in ("Create Schemas", "Create Target Results"):
            section_id = "DataMigration"
        item_name = migration_ui_style.page_title(item_name)
        entry = self.tasks_side.add_section_entry(section_id, page.identifier(), item_name, item_name, plat_icon("migration_check_open.png"), mforms.TaskEntryPlainItem)
        
        if platform.system() == 'Windows':
            self.content_box.add(page, True, True)
            i = len(self._name2page)
            page.show(False)
        else:
            i = self.tabview.add_page(page, "")

        self.tabs.append(page)
        self._name2page[page.identifier()] = i

    #---------------------------------------------------------------------------
    
    
    def can_go_back(self):
        return len(self._page_trail) > 1

    def can_go_next(self):
        return not self._page_trail or self._page_trail[-1] != self.tabs[-1]


    def start(self):
        self._advancing = True

        next = self.tabs[1]
        self._page_trail = [next]
        self.tasks_side.set_section_entry_icon(next.identifier(), plat_icon("migration_check_current.png"))
        self.tasks_side.select_entry(next.identifier())
        self.section_clicked(next.identifier())
        return next


    def go_next_page(self, skip_count=1):
        self._advancing = True
    
        current = self._page_trail[-1]
        self.tasks_side.set_section_entry_icon(current.identifier(), plat_icon("migration_check_done.png"))
        
        i = self.tabs.index(current)+skip_count
        next = self.tabs[i]
        while True:
            if not next.should_skip():
                break
            next.page_skipped()
            i += 1
            next = self.tabs[i]
        self._page_trail.append(next)
        self.tasks_side.set_section_entry_icon(next.identifier(), plat_icon("migration_check_current.png"))
        self.tasks_side.select_entry(next.identifier())
        self.section_clicked(next.identifier())
        return next


    def go_previous_page(self):
        self._advancing = False

        current = self._page_trail[-1]
        
        self._page_trail.pop()
        next = self._page_trail[-1]
        self.tasks_side.set_section_entry_icon(next.identifier(), plat_icon("migration_check_current.png"))
        self.tasks_side.select_entry(next.identifier())
        self.section_clicked(next.identifier())
        return next

