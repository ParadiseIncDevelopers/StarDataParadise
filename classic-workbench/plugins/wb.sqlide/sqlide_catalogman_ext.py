# Copyright (c) 2013, 2020, Oracle and/or its affiliates.
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

from workbench.log import log_error, log_warning
from mforms import IconStringColumnType, StringColumnType, LongIntegerColumnType, IntegerColumnType, NumberWithUnitColumnType
from workbench.notifications import NotificationCenter
from wb_admin_utils import make_panel_header
from workbench.utils import human_size, Version
from workbench.sdp_i18n import tr


def quote_identifier(value):
    return "`%s`" % str(value).replace("`", "``")


class SelectedTablePreview(mforms.Box):
    """Read-only metadata beside the schema's table list."""
    def __init__(self, editor):
        mforms.Box.__init__(self, False)
        self.set_managed()
        self.set_release_on_add()
        self.editor = editor
        self.schema = None
        self.table = None
        self.set_padding(8)
        self.set_spacing(8)
        self.heading = mforms.newLabel(tr("Select a table"))
        self.heading.set_style(mforms.BoldStyle)
        self.add(self.heading, False, True)
        self.note = mforms.newLabel(tr("Select a table to inspect its columns, indexes and SQL definition."))
        self.note.set_wrap_text(True)
        self.add(self.note, False, True)
        self.tabs = mforms.newTabView(mforms.TabViewClassic)
        self.add(self.tabs, True, True)
        self.columns = self._tree([(tr("Column"), 150), (tr("Type"), 140), (tr("Nullable"), 120), (tr("Key"), 70)])
        self.indexes = self._tree([(tr("Index"), 140), (tr("Column"), 150), (tr("Unique"), 80), (tr("Order"), 70)])
        self.tabs.add_page(self.columns, tr("Columns"))
        self.tabs.add_page(self.indexes, tr("Indexes"))
        self.definition = mforms.CodeEditor()
        self.definition.set_language(mforms.LanguageMySQL)
        self.definition.set_read_only(True)
        self.tabs.add_page(self.definition, tr("DDL"))
        self.edit = mforms.newButton()
        self.edit.set_text(tr("Edit definition"))
        self.edit.set_enabled(False)
        self.edit.add_clicked_callback(self.edit_definition)
        actions = mforms.newBox(True)
        actions.add_end(self.edit, False, False)
        self.add(actions, False, True)

    def _tree(self, columns):
        tree = mforms.newTreeView(mforms.TreeFlatList | mforms.TreeAltRowColors | mforms.TreeShowColumnLines)
        tree.set_row_height(26)
        for caption, width in columns:
            tree.add_column(mforms.StringColumnType, caption, width, False)
        tree.end_columns()
        return tree

    def _rows(self, sql, read_row):
        result = None
        try:
            result = self.editor.executeManagementQuery(sql, 0)
            if result and result.goToFirstRow():
                while True:
                    read_row(result)
                    if not result.nextRow():
                        break
        finally:
            if result:
                result.reset_references()

    def show_tables(self, schema, tables):
        self.columns.clear()
        self.indexes.clear()
        self.definition.set_read_only(False)
        self.definition.set_value("")
        self.definition.set_read_only(True)
        self.schema = schema
        self.table = tables[0][0] if tables else None
        self.edit.set_enabled(bool(self.table))
        if not self.table:
            self.heading.set_text(tr("Select a table"))
            self.note.set_text(tr("Select a table to inspect its columns, indexes and SQL definition."))
            return
        self.heading.set_text(self.table)
        self.note.set_text(tr("Read-only metadata from the current server."))
        name = "%s.%s" % (quote_identifier(schema), quote_identifier(self.table))
        try:
            def column_row(result):
                node = self.columns.add_node()
                for index, field in enumerate(("Field", "Type", "Null", "Key")):
                    value = result.stringFieldValueByName(field)
                    if field == "Null":
                        value = tr("Yes") if value == "YES" else tr("No")
                    node.set_string(index, value)
            self._rows("SHOW FULL COLUMNS FROM " + name, column_row)
            def index_row(result):
                node = self.indexes.add_node()
                node.set_string(0, result.stringFieldValueByName("Key_name"))
                node.set_string(1, result.stringFieldValueByName("Column_name"))
                node.set_string(2, tr("Yes") if result.stringFieldValueByName("Non_unique") == "0" else tr("No"))
                node.set_string(3, result.stringFieldValueByName("Seq_in_index"))
            self._rows("SHOW INDEX FROM " + name, index_row)
            def ddl_row(result):
                self.definition.set_read_only(False)
                self.definition.set_value(result.stringFieldValue(1))
                self.definition.set_read_only(True)
            self._rows("SHOW CREATE TABLE " + name, ddl_row)
        except Exception as error:
            self.note.set_text(tr("Metadata could not be read: %s") % error)

    def edit_definition(self):
        if self.schema is not None and self.table is not None:
            self.editor.alterLiveObject("db.Table", self.schema, self.table)

def show_schema_manager(editor, selection, table_maintenance=False):
    try:
        editor.executeManagementQuery("select 1", 0)
    except grt.DBError as e:
        mforms.Utilities.show_error(tr("Schema Inspector"), tr("Can not launch the Schema Inspector because the server is unreacheble."), tr("OK"), "", "")
        log_error("Can not launch the Schema Inspector because the server is unreacheble.\n")
        return False
    
    for schema_name in selection:
        sman = SchemaManager(editor, schema_name)
        dpoint = mforms.fromgrt(editor.dockingPoint)
        dpoint.dock_view(sman, "", 0)
        dpoint.select_view(sman)
        sman.set_title(schema_name)

        if table_maintenance:
            sman.show_table_maintenance()


def handleLiveTreeContextMenu(name, sender, args):
    menu = mforms.fromgrt(args['menu'])
    selection = args['selection']

    # Add extra menu items to the SQL editor live schema tree context menu
    schemas_selected = []


    for s in selection:
        if s.type == 'db.Schema':
            schemas_selected.append(s.name)
        else:
            return

    if schemas_selected:
        item = mforms.newMenuItem(tr("Schema Inspector"))
        item.add_clicked_callback(lambda: show_schema_manager(sender, schemas_selected))
        menu.insert_item(2, item)

class MaintenanceResultForm(mforms.Form):
    def __init__(self, results, checksum=False):
        mforms.Form.__init__(self, mforms.Form.main_form(), mforms.FormNormal)

        self.box = mforms.newBox(False)
        self.box.set_padding(12)
        self.box.set_spacing(8)
        self.set_content(self.box)

        self.tree = mforms.newTreeView(mforms.TreeFlatList|mforms.TreeAltRowColors|mforms.TreeShowColumnLines)

        self.tree.set_row_height(26)

        self.tree.set_font("Segoe UI 10")
        self.tree.set_selection_mode(mforms.TreeSelectMultiple)
        self.tree.set_row_height(26)
        self.box.add(self.tree, True, True)
        self.tree.add_column(mforms.StringColumnType, tr("Table"), 200, False)
        if checksum:
            self.tree.add_column(mforms.StringColumnType, tr("Checksum"), 100, False)
        else:
            self.tree.add_column(mforms.StringColumnType, tr("Action"), 80, False)
            self.tree.add_column(mforms.IconStringColumnType, tr("Message"), 400, False)
        self.tree.end_columns()

        self._checksum = checksum

        app = mforms.App.get()
        icon_path = {
            'status': app.get_resource_path('sdp_check.png'),
            'error': app.get_resource_path('sdp_alert-circle.png'),
            'info': '',
            'note': app.get_resource_path('sdp_message.png'),
            'warning': app.get_resource_path('sdp_alert-circle.png'),
        }

        ok = results.goToFirstRow()
        while ok:
            node = self.tree.add_node()
            node.set_string(0, results.stringFieldValue(0))
            if not checksum:
                node.set_string(1, results.stringFieldValue(1))
                node.set_icon_path(2, icon_path.get(results.stringFieldValue(2), ''))
                node.set_string(2, results.stringFieldValue(3))
                node.set_tag(results.stringFieldValue(2))
            else:
                node.set_string(1, results.stringFieldValue(1))
            ok = results.nextRow()

        bbox = mforms.newBox(True)
        self.box.add(bbox, False, True)

        copy = mforms.newButton()
        copy.add_clicked_callback(self.copy_to_clipboard)
        copy.set_text(tr("Copy"))
        bbox.add(copy, False, True)

        self.ok = mforms.newButton()
        self.ok.set_text(tr("OK"))
        self.ok.add_clicked_callback(self.close)
        bbox.add_end(self.ok, False, True)

        self.set_size(500, 400)


    def copy_to_clipboard(self):
        rows = []
        for node in self.tree.get_selection() or [self.tree.node_at_row(i) for i in range(self.tree.count())]:
            row = [node.get_string(0), node.get_string(1)]
            if not self._checksum:
                row.append(node.get_tag())
                row.append(node.get_string(2))
            rows.append("\t".join(row))
        mforms.Utilities.set_clipboard_text("\n".join(rows))

    def run(self):
        self.run_modal(self.ok, None)


def review_maintenance_sql(editor, query):
    form = mforms.Form(mforms.Form.main_form(), mforms.FormNormal)
    form.set_title(tr("Table maintenance - SQL review"))
    box = mforms.newBox(False)
    box.set_padding(12)
    box.set_spacing(8)
    form.set_content(box)
    box.add(mforms.newLabel(tr("Connection: %s") % editor.connection.name), False, True)
    code = mforms.CodeEditor()
    code.set_language(mforms.LanguageMySQL)
    code.set_text(query.encode("utf-8"))
    code.set_read_only(True)
    box.add(code, True, True)
    note = mforms.newLabel(tr("The selected maintenance action may lock the table. Results come from the server."))
    note.set_wrap_text(True)
    box.add(note, False, True)
    footer = mforms.newBox(True)
    footer.set_spacing(8)
    apply = mforms.newButton()
    apply.set_text(tr("Apply"))
    cancel = mforms.newButton()
    cancel.set_text(tr("Cancel"))
    footer.add_end(apply, False, True)
    footer.add_end(cancel, False, True)
    box.add(footer, False, True)
    form.set_size(760, 460)
    form.center()
    return form.run_modal(apply, cancel)


def do_table_maintenance(editor, action, selection, reviewed=False):
    query = action % ", ".join(selection)
    if not reviewed and not review_maintenance_sql(editor, query + ";"):
        return
    result = editor.executeManagementQuery(query, 1)

    form = MaintenanceResultForm(result, action.startswith("CHECKSUM"))
    form.set_title((action % "").strip())
    form.run()


class ObjectManager(mforms.Box):
    filter = None
    icon_column = None
    bad_icon_path = "task_error.png"
    node_name = None
    actions = []
    def __init__(self, editor, schema):
        mforms.Box.__init__(self, False)
        self.set_managed()
        self.set_release_on_add()

        self.schema = schema
        self.editor = editor
        
        self.target_version = Version.fromgrt(editor.serverVersion)

        self.main = mforms.newBox(False)
        self.add(self.main, True, True)

        self.error_heading = mforms.newLabel("")
        self.error_heading.set_style(mforms.BoldStyle)
        self.error_body = mforms.newLabel("")
        self.error_box = mforms.newBox(False)
        self.error_box.set_spacing(8)
        self.error_box.set_padding(8)
        self.error_box.add(self.error_heading, True, False)
        self.error_box.add(self.error_body, True, False)
        self.add(self.error_box, True, False)
        self.error_box.show(False)

        self.main.set_padding(8)
        self.main.set_spacing(8)

        self.tree = mforms.newTreeView(mforms.TreeFlatList|mforms.TreeAltRowColors|mforms.TreeShowColumnLines)

        self.tree.set_row_height(26)

        self.tree.set_font("Segoe UI 10")
        self.tree.set_selection_mode(mforms.TreeSelectMultiple)
        
        #Check if there is method to load the columns, if not, skip.
        if hasattr(self, "preload_columns") and callable(getattr(self, "preload_columns")):
            self.preload_columns()
        
        for field, type, caption, width, min_version in self.columns:
            if min_version and not self.target_version.is_supported_mysql_version_at_least(Version.fromstr(min_version)):
                continue
            self.tree.add_column(type, caption, width, False)
        self.tree.end_columns()
        self.tree.set_allow_sorting(True)
        self.main.add(self.tree, True, True)

        self.menu = mforms.newContextMenu()
        self.menu.add_will_show_callback(self.menu_will_show)
        self.tree.add_activated_callback(self.on_activate)
        self.tree.set_context_menu(self.menu)

        icons = {"db.Table": "sdp_table.png", "db.Column": "sdp_columns.png", "db.Index": "sdp_key.png",
                 "db.View": "sdp_table.png", "db.Trigger": "sdp_bolt.png", "db.Procedure": "sdp_code.png",
                 "db.Function": "sdp_function.png", "db.Event": "sdp_clock.png", "db.Grants": "sdp_key.png",
                 "db.Constraints": "sdp_link.png", "db.Partitions": "sdp_columns-3.png"}
        self.icon_path = mforms.App.get().get_resource_path(icons.get(self.klass, "sdp_file-code.png"))
        self.bad_icon_path = mforms.App.get().get_resource_path(self.bad_icon_path)

        self.row_count = mforms.newLabel("")
        self.row_count.set_text("");
        
        self.refresh_btn = mforms.newButton()
        self.refresh_btn.set_text(tr("Refresh"))
        self.refresh_btn.set_min_size(100, -1)
        self.refresh_btn.add_clicked_callback(self.refresh)

        self.bbox = mforms.newBox(True)
        self.bbox.set_spacing(8)
        self.main.add(self.bbox, False, True)

        self.bbox.add(self.row_count, False, True)
        self.bbox.add_end(self.refresh_btn, False, True)

        for caption, callback_name in self.actions:
            if not caption:
                self.bbox.add(mforms.newLabel(" "), False, True)
                continue
            btn = mforms.newButton()
            btn.set_text(caption)
            btn.add_clicked_callback(getattr(self, callback_name))
            self.bbox.add(btn, False, True)
          
    def show_error(self, title, msg):
        self.main.show(False)
        self.error_box.show(True)
        self.error_heading.set_text(title)
        self.error_body.set_text(msg)

    def refresh_row_count(self):
        self.row_count.set_text(tr("Count: %d") % self.tree.count())

    def on_activate(self, node, col):
        from sqlide_tableman_ext import show_table_inspector

        if show_table_inspector is not None:
            if self.klass == 'db.Table': 
                show_table_inspector(self.editor, [(self.schema, node.get_string(self.name_column))])
            elif self.klass == 'db.Index' and hasattr(self, 'parent_name_column'):
                show_table_inspector(self.editor, [(self.schema, node.get_string(self.parent_name_column))], "indexes")

    def menu_will_show(self, item):
        # item is the parent node which will be None when the
        # context menu has just being opened.
        # So when the call is done for a sub-menu, no reset is needed.
        if item is None:
            self.menu.remove_all()

            if item is None:
                parent_nodes = {}
                selection = grt.List()
                pobj = None
                for node in self.tree.get_selection():
                    name = node.get_string(self.name_column)
                    obj = grt.classes.db_query_LiveDBObject()
                    obj.name = name
                    obj.schemaName = self.schema
                    obj.type = self.klass
                    if hasattr(self, 'parent_name_column'):
                        if hasattr(self, 'table'):
                            parent_name = self.table
                        else:
                            parent_name = node.get_string(self.parent_name_column)
                        
                        if parent_name in parent_nodes:
                            obj.owner = parent_nodes[parent_name]
                        else:
                            pobj = grt.classes.db_query_LiveDBObject()
                            obj.owner = pobj
                            pobj.type = 'db.Table'
                            pobj.name = parent_name
                            pobj.schemaName = self.schema
                            parent_nodes[parent_name]= pobj
                    selection.append(obj)

                if not selection and self.node_name:
                    obj = grt.classes.db_query_LiveDBObject()
                    obj.schemaName = self.schema
                    obj.type = self.node_name
                    selection.append(obj)

                    sobj = grt.classes.db_query_LiveDBObject()
                    sobj.schemaName = self.schema
                    sobj.name = self.schema
                    sobj.type = "db.Schema"
                    obj.owner = sobj


                separator = mforms.newMenuItem("", mforms.SeparatorMenuItem)
                separator.set_name("Bottom Plugins Separator")
                separator.setInternalName("bottom_plugins_separator")
                self.menu.add_item(separator)
                self.menu.add_item_with_title(tr("Refresh"), self.refresh, "Refresh", "refresh")

                args = grt.Dict()
                args["selection"] = selection
                args["menu"] = mforms.togrt(self.menu, "ContextMenu")
                args['schema_inspector'] = True
                NotificationCenter().send("GRNLiveDBObjectMenuWillShow", self.editor, args)
                
    def get_query(self):
        cols = []
        for field_obj, ctype, caption, width, min_version in self.columns:
            if min_version and not self.target_version.is_supported_mysql_version_at_least(Version.fromstr(min_version)):
                continue
            try:
                cols.append("`%s`" % field_obj['field'])
            except:
                cols.append("`%s`" % field_obj)
        return self.show_query % {'schema' : self.schema, 'columns' : ", ".join(cols)}

    def preload_data(self,query):
        try:
            rset = self.editor.executeManagementQuery(query, 0)
        except grt.DBError as e:
            if e.args[1] == 1044 or e.args[1] == 1142:
                mforms.Utilities.show_error(tr("Access Error"), tr("The current user does not have enough privileges to execute %s.\n\n%s")%(query, e.args[0]), tr("OK"), "", "")
            else:
                mforms.Utilities.show_error(tr("SQL error"), tr("An error occurred retrieving information about the schema.\nQuery: %s\nError: %s")%(query, e.args[0]), tr("OK"), "", "")
            return
        ok = rset.goToFirstRow()
        while ok:
            if not self.filter or self.filter(rset):
                node = self.tree.add_node()
                if self.is_row_corrupted(rset):
                    node.set_icon_path(self.icon_column, self.bad_icon_path)
                elif self.icon_column is not None:
                    node.set_icon_path(self.icon_column, self.icon_path)
                i = 0
                for field_obj, ctype, caption, width, min_version in self.columns:
                    if min_version and not self.target_version.is_supported_mysql_version_at_least(Version.fromstr(min_version)):
                        continue
                    format_func = None
                    field = None
                    try:
                        format_func = field_obj['format_func']
                    except:
                        pass
                    try:
                        field = field_obj['field']
                    except:
                        if self.target_version.is_supported_mysql_version_at_least(8, 0, 0)  and type(field) is str:
                            field = field_obj.upper();
                        else:
                            field = field_obj
                    if ctype is mforms.IntegerColumnType:
                        if type(field) is int:
                            node.set_int(i, rset.intFieldValue(field) or 0)
                        else:
                            node.set_int(i, rset.intFieldValueByName(field) or 0)
                    elif ctype is mforms.LongIntegerColumnType:
                        if type(field) is int:
                            node.set_long(i, int(rset.stringFieldValue(field) or 0))
                        else:
                            node.set_long(i, int(rset.stringFieldValueByName(field) or 0))
                    else:
                        if type(field) is int:
                            node.set_string(i, rset.stringFieldValue(field) or "" if format_func is None else format_func(rset.stringFieldValue(field)))
                        else:
                            node.set_string(i, rset.stringFieldValueByName(field) or "" if format_func is None else format_func(rset.stringFieldValueByName(field)))
                    i += 1
            ok = rset.nextRow()

    def refresh(self):
        self.tree.clear()
        self.preload_data(self.get_query())
        self.refresh_row_count()


    def inspect_table(self):
        node = self.tree.get_selected_node()
        if node:
            self.on_activate(node, 0)

    def is_row_corrupted(self, rset):
        return False


class TableManager(ObjectManager):
    caption = tr("Tables")
    klass = "db.Table"
    node_name = "tables"
    icon_column = 0
    
    table_names = None

    show_query = "show table status from `%(schema)s` where Comment <> 'VIEW'"
    name_column = 0
    columns = [("Name", IconStringColumnType, tr("Name"), 180, None),
               ("Engine", StringColumnType, tr("Engine"), 60, None),
               ("Version", StringColumnType, tr("Version"), 50, None),
               ("Row_format", StringColumnType, tr("Row Format"), 100, None),
               ("Rows", LongIntegerColumnType, tr("Rows"), 80, None),
               ("Avg_row_length", LongIntegerColumnType, tr("Avg Row Length"), 100, None),
               ({'field' : "Data_length", 'format_func' : lambda x: human_size(int(x)) if x else ""}, NumberWithUnitColumnType, tr("Data Length"), 100, None),
               ({'field' : "Max_data_length", 'format_func' : lambda x: human_size(int(x)) if x else ""}, NumberWithUnitColumnType, tr("Max Data Length"), 100, None),
               ({'field' : "Index_length", 'format_func' : lambda x: human_size(int(x)) if x else ""}, NumberWithUnitColumnType, tr("Index Length"), 100, None),
               ({'field' : "Data_free", 'format_func' : lambda x: human_size(int(x)) if x else ""}, NumberWithUnitColumnType, tr("Data Free"), 80, None),
               ("Auto_increment", LongIntegerColumnType, tr("Auto Increment"), 80, None),
               ("Create_time", StringColumnType, tr("Create Time"), 150, None),
               ("Update_time", StringColumnType, tr("Update Time"), 150, None),
               ("Check_time", StringColumnType, tr("Check Time"), 150, None),
               ("Collation", StringColumnType, tr("Collation"), 100, None),
               ("Checksum", StringColumnType, tr("Checksum"), 80, None),
               ("Comment", StringColumnType, tr("Comment"), 500, None)
               ]

    def is_row_corrupted(self, rset):
        return not rset.stringFieldValueByName("Engine")


    def refresh(self):
        self.table_names = []
        ObjectManager.refresh(self)


    def filter(self, rset):
        self.table_names.append(rset.stringFieldValueByName("Name"))
        return True

    def get_selection(self):
        selection = []
        for node in self.tree.get_selection():
            obj = grt.classes.db_query_LiveDBObject()
            obj.name = node.get_string(self.name_column)
            obj.schemaName = self.schema
            obj.type = self.klass
            selection.append(obj)
        return selection



class TableMaintenancePanel(mforms.Box):
    def __init__(self, editor):
        mforms.Box.__init__(self, False)
        self.set_managed()
        self.set_release_on_add()

        self.editor = editor

        self.set_spacing(8)
        self.set_padding(8)

        heading = mforms.newLabel(tr("Table maintenance"))
        heading.set_style(mforms.BoldStyle)
        self.add(heading, False, True)

        help = mforms.newLabel(tr("Select tables and click the operation you want to perform.\nNOTE: Some commands may require locking tables until completion,\nwhich may take a long time for large tables."))
        self.add(help, False, True)

        self.operation_selector = mforms.newSelector()
        self.operation_selector.add_items(["ANALYZE", "OPTIMIZE", "CHECK", "CHECKSUM"])
        self.operation_selector.set_selected(0)
        self.add(self.operation_selector, False, True)
        self._operation_groups = []

        self.scroll = mforms.newScrollPanel(0)
        self.add(self.scroll, True, True)
        self.content = mforms.newBox(False)
        self.content.set_spacing(8)
        self.content.set_padding(12)
        self.scroll.add(self.content)

        self._buttons = []

        def make_command_box(callable, title, desc, tooltip, options = None, extra_options = None):
            content = mforms.newBox(False)
            content.set_spacing(8)
            self.content.add(content, False, True)
            self._operation_groups.append(content)
            l = mforms.newLabel(title.strip())
            l.set_style(mforms.BoldStyle)
            content.add(l, False, True)

            l = mforms.newLabel(desc)
            l.set_wrap_text(True)
            content.add(l, False, True)

            if extra_options:
                content.add(extra_options, False, True)

            hb = mforms.newBox(True)
            hb.set_spacing(12)

            l = mforms.newImageBox()
            l.set_image(mforms.App.get().get_resource_path("mini_notice.png"))
            l.set_tooltip(tooltip)
            hb.add(l, False, True)

            for o in options:
                hb.add(o, False, True)

            btn = mforms.newButton()
            btn.add_clicked_callback(callable)
            btn.set_text(tr("Review SQL…"))
            hb.add_end(btn, False, True)

            self._buttons.append(btn)

            content.add(hb, False, True)

        self.analyze_local = mforms.newCheckBox()
        self.analyze_local.set_text(tr("Don't write to BINLOG (local)"))

        make_command_box(self.analyze_table, "Analyze Table",
                         """Analyzes and stores the key distribution for a table.
During the analysis, the table is locked with a read lock for InnoDB and MyISAM.""",
                         """With InnoDB tables, when you enable the innodb_stats_persistent option, you must run ANALYZE TABLE after loading substantial data into an InnoDB table, or creating a new index for one.

MySQL uses the stored key distribution to decide the order in which tables should be joined when you perform a join on something other than a constant. In addition, key distributions can be used when deciding which indexes to use for a specific table within a query.""",
                         [self.analyze_local])


        if editor.serverVersion.majorNumber > 5 or (editor.serverVersion.majorNumber == 5 and editor.serverVersion.minorNumber >= 6):
            extra_options = mforms.newBox(True)
            extra_options.set_spacing(4)

            self.optimize_ft_only = mforms.newCheckBox()
            self.optimize_ft_only.set_text(tr("Optimize FULLTEXT only"))
            self.optimize_ft_only.add_clicked_callback(self.optimize_ft_only_toggled)
            extra_options.add(self.optimize_ft_only, False, True)

            l = mforms.newLabel("")
            l.set_size(20, -1)
            extra_options.add(l, False, True)

            extra_options.add(mforms.newLabel(tr("Number of words to optimize per run:")), False, True)
            self.optimize_ft_numwords = mforms.newTextEntry()
            self.optimize_ft_numwords.set_size(50, -1)
            self.optimize_ft_numwords.set_enabled(False)
            extra_options.add(self.optimize_ft_numwords, False, True)

            result = editor.executeManagementQuery("SHOW VARIABLES LIKE 'innodb_ft_num_word_optimize'", 0)
            if result and result.goToFirstRow():
                self.optimize_ft_numwords.set_value(result.stringFieldValue(1))
        else:
            extra_options = None
            self.optimize_ft_only = None

        self.optimize_local = mforms.newCheckBox()
        self.optimize_local.set_text(tr("Don't write to BINLOG (local)"))

        make_command_box(self.optimize_table, "\n\n"+"Optimize Table",
                         """Reorganizes the physical storage of table data and associated index data, 
to reduce storage space and improve I/O efficiency when accessing the table.""",
                         """The exact changes made to each table depend on the storage engine used by that table.

Use OPTIMIZE TABLE in these cases, depending on the type of table:

* After doing substantial insert, update, or delete operations on an InnoDB table that has its own .ibd file. The table and indexes are reorganized, and disk space can be reclaimed for use by the operating system.

* After doing substantial insert, update, or delete operations on columns that are part of a FULLTEXT index in an InnoDB table. Set the configuration option innodb_optimize_fulltext_only=1 first. To keep the index maintenance period to a reasonable time, set the innodb_ft_num_word_optimize option to specify how many words to update in the search index, and run Optimize Table until the search index is fully updated.""",
                         [self.optimize_local], extra_options)


        self.check_scan_opt = mforms.newSelector()
        self.check_scan_opt.add_items(["", "Quick", "Medium", "Extended", "For Upgrade"])

        self.check_fast_opt = mforms.newCheckBox()
        self.check_fast_opt.set_text(tr("Fast"))

        self.check_changed_opt = mforms.newCheckBox()
        self.check_changed_opt.set_text(tr("Changed"))

        options = [self.check_scan_opt, self.check_fast_opt, self.check_changed_opt]

        make_command_box(self.check_table, "\n\n"+"Check Table",
                         """CHECK TABLE checks a table or tables for errors. 
For MyISAM tables, the key statistics are updated as well.""",
                         """The FOR UPGRADE option checks whether the named tables are compatible with the current version of MySQL. With FOR UPGRADE, the server checks each table to determine whether there have been any incompatible changes in any of the table's data types or indexes since the table was created.
                             
Check Options:
- QUICK    Do not scan the rows to check for incorrect links. Applies to InnoDB and MyISAM tables and views.
- FAST    Check only tables that have not been closed properly. Applies only to MyISAM tables and views; ignored for InnoDB.
- CHANGED    Check only tables that have been changed since the last check or that have not been closed properly. Applies only to MyISAM tables and views; ignored for InnoDB.
- MEDIUM    Scan rows to verify that deleted links are valid. This also calculates a key checksum for the rows and verifies this with a calculated checksum for the keys. Applies only to MyISAM tables and views; ignored for InnoDB.
- EXTENDED    Do a full key lookup for all keys for each row. This ensures that the table is 100% consistent, but takes a long time. Applies only to MyISAM tables and views; ignored for InnoDB.""",
                         options)

        # The manual is full of scary warnings about this one, maybe better just leave user perform it by hand
#        make_command_box("\n"+"Repair Table",
#                         """""",
#                         """
#                             """)

        self.checksum_quick = mforms.newCheckBox()
        self.checksum_quick.set_text(tr("Quick (if supported)"))
        options = [self.checksum_quick]
        make_command_box(self.checksum_table, "\n\n"+"Checksum Table",
                         """CHECKSUM TABLE reports a checksum for the contents of a table.""",
                         """You can use this statement to verify that the contents are the same before and after a backup, rollback, or other operation that is intended to put the data back to a known state.""", options)

        self.operation_selector.add_changed_callback(self.operation_changed)
        self.operation_changed()

    def operation_changed(self):
        selected = self.operation_selector.get_selected_index()
        for index, group in enumerate(self._operation_groups):
            group.show(index == selected)

    def optimize_ft_only_toggled(self):
        self.optimize_ft_numwords.set_enabled(self.optimize_ft_only.get_active())
    

    def analyze_table(self):
        local = " NO_WRITE_TO_BINLOG" if self.analyze_local.get_active() else ""
        do_table_maintenance(self.editor, "ANALYZE%s TABLE %%s" % local, self.table_list)

    def optimize_table(self):
        local = " NO_WRITE_TO_BINLOG" if self.optimize_local.get_active() else ""
        action = "OPTIMIZE%s TABLE %%s" % local
        query = action % ", ".join(self.table_list)
        setup = None
        restore = None
        if self.optimize_ft_only:
            old_value = None
            res = self.editor.executeManagementQuery("SELECT @@innodb_optimize_fulltext_only", 0)
            if res and res.goToFirstRow():
                old_value = res.stringFieldValue(0).strip().upper() in ("ON", "1")
            words = self.optimize_ft_numwords.get_string_value()
            if not words.isdigit():
                mforms.Utilities.show_error(tr("Invalid value"), tr("The word limit must be an integer."), tr("Close"), "", "")
                return
            setup = "SET GLOBAL innodb_optimize_fulltext_only=%s, innodb_ft_num_word_optimize=%s" % ("1" if self.optimize_ft_only.get_active() else "0", words)
            if old_value is not None:
                restore = "SET GLOBAL innodb_optimize_fulltext_only=%s" % ("1" if old_value else "0")

        script = ";\n".join(statement for statement in (setup, query, restore) if statement) + ";"
        if not review_maintenance_sql(self.editor, script):
            return
        if setup:
            self.editor.executeManagementCommand(setup, 1)
        try:
            do_table_maintenance(self.editor, action, self.table_list, reviewed=True)
        finally:
            if restore:
                self.editor.executeManagementCommand(restore, 1)


    def check_table(self):
        option = self.check_scan_opt.get_string_value()
        option += " FAST" if self.check_fast_opt.get_active() else ""
        option += " CHANGED" if self.check_changed_opt.get_active() else ""
        do_table_maintenance(self.editor, "CHECK TABLE %%s %s" % option, self.table_list)

    def checksum_table(self):
        option = " QUICK" if self.checksum_quick.get_active() else ""
        do_table_maintenance(self.editor, "CHECKSUM TABLE %%s%s" % option, self.table_list)


    def show_tables(self, schema, tables):
        self.table_list = ["`%s`.`%s`" % (schema.replace("`", "``"), table.replace("`", "``")) for table, engine in tables]
        for b in self._buttons:
            b.set_enabled(len(self.table_list) > 0)


class TableManagerParent(mforms.Splitter):
    def __init__(self, editor, schema):
        mforms.Splitter.__init__(self, True, False)
        self.set_managed()
        self.set_release_on_add()

        self.schema = schema
        self.editor = editor
        self.summary_view = TableManager(editor, schema)
        self.add(self.summary_view, 380, True)

        self.node_name = self.summary_view.node_name
        self.caption = self.summary_view.caption

        self.summary_view.tree.add_changed_callback(self.table_selection_changed)

        self.goback_btn = mforms.newButton()
        self.goback_btn.set_text(tr("Summary list"))
        self.goback_btn.add_clicked_callback(self.switch_back)
        self.summary_view.bbox.add(self.goback_btn, False, True)
        self.goback_btn.show(False)

        self.inspect_btn = mforms.newButton()
        self.inspect_btn.set_text(tr("Inspect Table"))
        self.inspect_btn.add_clicked_callback(self.summary_view.inspect_table)
        self.summary_view.bbox.add_end(self.inspect_btn, False, True)

        self.maintenance_btn = mforms.newButton()
        self.maintenance_btn.set_text(tr("Maintenance"))
        self.maintenance_btn.add_clicked_callback(self.switch_maintenance)
        self.summary_view.bbox.add(self.maintenance_btn, False, True)

        self.right_view = SelectedTablePreview(self.editor)
        self.add(self.right_view, 360)
        self.set_divider_position(640)

    @property
    def table_names(self):
        return self.summary_view.table_names

    def refresh(self):
        self.summary_view.refresh()
        if self.summary_view.tree.count():
            self.summary_view.tree.select_node(self.summary_view.tree.node_at_row(0))
        self.table_selection_changed()


    def switch_back(self):
        if self.right_view:
            self.remove(self.right_view)
            self.right_view = None

        self.right_view = SelectedTablePreview(self.editor)
        self.add(self.right_view, 360)
        self.set_divider_position(640)
        self.table_selection_changed()

        self.summary_view.refresh_btn.show(True)
        self.maintenance_btn.show(True)
        self.goback_btn.show(False)
        self.inspect_btn.show(True)


    def switch_maintenance(self):
        if self.right_view:
            self.remove(self.right_view)
        self.right_view = TableMaintenancePanel(self.editor)
        self.add(self.right_view)
        self.relayout()
        self.right_view.relayout()
        self.set_divider_position(250)

        self.right_view.show_tables(self.schema, self.selected_tables())

        self.summary_view.refresh_btn.show(False)
        self.maintenance_btn.show(False)
        self.goback_btn.show(True)
        self.inspect_btn.show(False)


    def selected_tables(self):
        tables = []
        for node in self.summary_view.tree.get_selection():
            tables.append((node.get_string(0), node.get_string(1)))
        return tables


    def table_selection_changed(self):
        if self.right_view:
            self.right_view.show_tables(self.schema, self.selected_tables())

class ColumnManager(ObjectManager):
    caption = tr("Columns")
    klass = "db.Column"
    node_name = "columns"
    filter = None
    show_query = "select * from information_schema.columns where table_schema = '%(schema)s'"
    icon_column = 1
    name_column = 1
    parent_name_column = 0
    columns = [("TABLE_NAME", StringColumnType, tr("Table"), 150, None),
               ("COLUMN_NAME", IconStringColumnType, tr("Column"), 150, None),
               ("COLUMN_TYPE", StringColumnType, tr("Type"), 120, None),
               ("COLUMN_DEFAULT", StringColumnType, tr("Default Value"), 120, None),
               ("IS_NULLABLE", StringColumnType, tr("Nullable"), 50, None),
               ("CHARACTER_SET_NAME", StringColumnType, tr("Character Set"), 80, None),
               ("COLLATION_NAME", StringColumnType, tr("Collation"), 100, None),
               ("PRIVILEGES", StringColumnType, tr("Privileges"), 200, None),
               ("EXTRA", StringColumnType, tr("Extra"), 100, None),
               ("COLUMN_COMMENT", StringColumnType, tr("Comments"), 200, None)]
    #actions = [("Manage Indexes", "manage")]


class IndexManager(ObjectManager):
    caption = tr("Indexes")
    klass = "db.Index"
    node_name = "indexes"
    filter = None
    show_query = "show index from `%(table)s` from `%(schema)s`"
    parent_name_column = 0
    name_column = 1
    icon_column = 0
    columns = [("Table", IconStringColumnType, tr("Table"), 200, None),
               ("Key_name", IconStringColumnType, tr("Name"), 200, None),
               ({'field' : "Non_unique", 'format_func' : lambda val: tr("Yes") if val != "1" else tr("No")}, StringColumnType, tr("Unique"), 50, None),
               ("Index_type", StringColumnType, tr("Index Type"), 50, None),
               ("Index_comment", StringColumnType, tr("Index Comment"), 200, "5.5"),
               ("Column_name", StringColumnType, tr("Column"), 100, None),
               ("Seq_in_index", IntegerColumnType, tr("Seq in Index"), 100, None),
               ("Packed", StringColumnType, tr("Packed"), 50, None),
               ("Collation", StringColumnType, tr("Collation"), 50, None),
               ("Cardinality", StringColumnType, tr("Cardinality"), 50, None),
               ("Sub_part", StringColumnType, tr("Sub part"), 50, None),
               ("Null", StringColumnType, tr("NULL"), 50, None),
               ("Comment", StringColumnType, tr("Comment"), 200, None)]
    
    #actions = [("Manage Indexes", "manage")]

    def __init__(self, editor, schema, owner):
        ObjectManager.__init__(self, editor, schema)

        self.owner= owner
        self.table_manager = owner.tab_tables
            
    def preload_columns(self):
        if self.target_version.is_supported_mysql_version_at_least(8, 0, 4):
            self.columns.append(("Visible", StringColumnType, "Visible", 75, None))

    def manage(self):
        self.owner.show_index_manager()


    def refresh(self):
        self.tree.clear()
        for table in self.table_manager.table_names or []:
            try:
                rset = self.editor.executeManagementQuery(self.show_query % {'table' : table.replace("`", "``"), 'schema' : self.schema.replace("`", "``")}, 0)
            except grt.DBError as err:
                log_warning("Error querying index info for %s.%s: %s\n" % (self.schema, table, err[0]))
                continue
            ok = rset.goToFirstRow()
            while ok:
                if not self.filter or self.filter(rset):
                    node = self.tree.add_node()
                    node.set_icon_path(0, self.icon_path)
                    i = 0
                    for field_obj, ctype, caption, width, min_version in self.columns:
                        if min_version and not self.target_version.is_supported_mysql_version_at_least(Version.fromstr(min_version)):
                            continue
                        format_func = None
                        field = None
                        try:
                            format_func = field_obj['format_func']
                            field = field_obj['field']
                        except:
                            field = field_obj

                        if ctype == mforms.IntegerColumnType:
                            if type(field) is int:
                                node.set_int(i, rset.intFieldValue(field) or 0)
                            else:
                                node.set_int(i, rset.intFieldValueByName(field) or 0)
                        else:
                            if type(field) is int:
                                node.set_string(i, rset.stringFieldValue(field) or "" if format_func is None else format_func(rset.stringFieldValue(field)))
                            else:
                                node.set_string(i, rset.stringFieldValueByName(field) or "" if format_func is None else format_func(rset.stringFieldValueByName(field)))
                        i += 1
                ok = rset.nextRow()
        self.refresh_row_count()


class GrantsManager(ObjectManager):
    caption = tr("Privileges")
    klass = "db.Grants"
    node_name = "table_privileges"
    name_column = 0
    columns = [("Host", IconStringColumnType, tr("Host"), 100, None),
           ("User", StringColumnType, tr("User"), 100, None),
           ("Db", StringColumnType, tr("Scope"), 100, None),
           ("Select_priv", StringColumnType, tr("Select"), 50, None),
           ("Insert_priv", StringColumnType, tr("Insert"), 50, None),
           ("Update_priv", StringColumnType, tr("Update"), 50, None),
           ("Delete_priv", StringColumnType, tr("Delete"), 50, None),
           ("Create_priv", StringColumnType, tr("Create"), 50, None),
           ("Drop_priv", StringColumnType, tr("Drop"), 50, None),
           ("Grant_priv", StringColumnType, tr("Grant"), 50, None),
           ("References_priv", StringColumnType, tr("References"), 50, None),
           ("Index_priv", StringColumnType, tr("Index"), 50, None),
           ("Alter_priv", StringColumnType, tr("Alter"), 50, None),
           ("Create_tmp_table_priv", StringColumnType, tr("Create tmp table"), 50, None),
           ("Lock_tables_priv", StringColumnType, tr("Lock Tables"), 50, None),
           ("Create_view_priv", StringColumnType, tr("Create View"), 50, None),
           ("Create_routine_priv", StringColumnType, tr("Create Routine"), 50, None),
           ("Alter_routine_priv", StringColumnType, tr("Alter Routine"), 50, None),
           ("Execute_priv", StringColumnType, tr("Execute"), 50, None),
           ("Event_priv", StringColumnType, tr("Event"), 50, None),
           ("Trigger_priv", StringColumnType, tr("Trigger"), 50, None),
           ]

    def get_query(self):
        if len(self.columns) == 0: # Probably user doesn't have privileges to list the privilege tables.
            return []

        output = []
        fields = []
        fields_where = []
        for field_obj, ctype, caption, width, min_version in self.columns:
            if min_version and not self.target_version.is_supported_mysql_version_at_least(Version.fromstr(min_version)):
                continue
            field = None
            try:
                field = field_obj['field']
            except:
                field = field_obj

            if field == "scope":
                continue
            
            if field not in ['Db']:
                fields.append(field)
            if field not in ['User','Host','Db']:
                fields_where.append("%s = 'Y'" % field.replace(" ","_"))

        output.append("SELECT '<global>' as Db,%(sel_fields)s FROM mysql.user WHERE %(where_fields)s" % {'sel_fields' : ",".join(fields), 'where_fields' : " OR ".join(fields_where)})

        output.append("SELECT Db,%(sel_fields)s FROM mysql.db WHERE '%(schema)s' like db" % {'sel_fields' : ",".join(fields), 'schema' : self.schema})

        return output
    
    def refresh(self):
        self.tree.clear()
        for query in self.get_query():
            self.preload_data(query)
        self.refresh_row_count()


class ViewManager(ObjectManager):
    caption = tr("Views")
    klass = "db.View"
    node_name = "views"
    filter = None
    show_query = "show table status from `%(schema)s` where Comment = 'VIEW'"
    name_column = 0
    icon_column = 0
    columns = [(0, IconStringColumnType, tr("Name"), 400, None)]


class TriggerManager(ObjectManager):
    caption = tr("Triggers")
    klass = "db.Trigger"
    node_name = "triggers"
    show_query = "show triggers from `%(schema)s`"
    name_column = 0
    icon_column = 0
    columns = [("Trigger", IconStringColumnType, tr("Name"), 200, None),
               ("Event", StringColumnType, tr("Event"), 100, None),
               ("Table", StringColumnType, tr("Table"), 100, None),
               ("Timing", StringColumnType, tr("Timing"), 100, None),
               ("Created", StringColumnType, tr("Created"), 100, None),
               ("sql_mode", StringColumnType, tr("SQL Mode"), 100, None),
               ("Definer", StringColumnType, tr("Definer"), 100, None),
               ("character_set_client", StringColumnType, tr("Client Character Set"), 100, None),
               ("collation_connection", StringColumnType, tr("Connection Collation"), 100, None),
               ("Database Collation", StringColumnType, tr("Database Collation"), 100, None),
               ]


class ProcedureManager(ObjectManager):
    caption = tr("Procedures")
    klass = "db.StoredProcedure"
    node_name = "storedProcedures"
    show_query = "show procedure status where Db = '%(schema)s'"
    name_column = 0
    icon_column = 0
    columns = [("Name", IconStringColumnType, tr("Name"), 200, None),
               ("Type", StringColumnType, tr("Type"), 100, None),
               ("Definer", StringColumnType, tr("Definer"), 100, None),
               ("Modified", StringColumnType, tr("Modified"), 100, None),
               ("Created", StringColumnType, tr("Created"), 100, None),
               ("Security_type", StringColumnType, tr("Security Type"), 100, None),
               ("character_set_client", StringColumnType, tr("Client Character Set"), 100, None),
               ("collation_connection", StringColumnType, tr("Connection Collation"), 100, None),
               ("Database Collation", StringColumnType, tr("Database Collation"), 100, None),
               ("Comment", StringColumnType, tr("Comment"), 400, None),
               ]


class FunctionManager(ObjectManager):
    caption = tr("Functions")
    klass = "db.Function"
    node_name = "functions"
    show_query = "show function status where Db = '%(schema)s'"
    name_column = 0
    icon_column = 0
    columns = ProcedureManager.columns


class EventManager(ObjectManager):
    caption = tr("Events")
    klass = "GrtObject"
    node_name = "events"
    show_query = "show events from `%(schema)s`"
    name_column = 0
    icon_column = 0
    columns = [("Name", IconStringColumnType, tr("Name"), 200, None),
               ("Definer", StringColumnType, tr("Row Format"), 100, None),
               ("Time zone", StringColumnType, tr("Time Zone"), 100, None),
               ("Type", StringColumnType, tr("Type"), 100, None),
               ("Execute at", StringColumnType, tr("Execute at"), 100, None),
               ("Interval value", StringColumnType, tr("Interval value"), 100, None),
               ("Interval field", StringColumnType, tr("Interval field"), 100, None),
               ("Starts", StringColumnType, tr("Starts"), 100, None),
               ("Ends", StringColumnType, tr("Ends"), 100, None),
               ("Status", StringColumnType, tr("Status"), 100, None),
               ("Originator", StringColumnType, tr("Originator"), 100, None),
               ("character_set_client", StringColumnType, tr("Client Character Set"), 100, None),
               ("collation_connection", StringColumnType, tr("Connection Collation"), 100, None),
               ("Database Collation", StringColumnType, tr("Database Collation"), 100, None)
               ]

class SchemaInfoPanel(mforms.Box):
    caption = tr("General")
    node_name = "informations"
    def __init__(self, editor, schema):
        mforms.Box.__init__(self, False)
        self.set_managed()
        self.set_release_on_add()
        
        self.editor = editor
        self._schema = schema
        
        self.table = mforms.newTable()
        self.table.set_row_count(7)
        self.table.set_column_count(2)
        self.table.set_row_spacing(8)
        self.table.set_column_spacing(8)

        def make_title(t):
            l = mforms.newLabel(t)
            l.set_style(mforms.BoldStyle)
            return l

        self.panel_header_box = mforms.newBox(True)
        self.panel_header_box.set_padding(10)
        self.panel_header_box.add(make_panel_header("sdp_database@2x.png", self.editor.connection.name, "%s" % (schema)), False, True)
        
        self.add(self.panel_header_box, False, True)

        self.table.add(make_title(tr("Schema Details")), 0, 2, 0, 1, mforms.HFillFlag)

        self.table.add(mforms.newLabel(tr("Default collation:")),              0, 1, 3, 4, mforms.HFillFlag)
        self.col_default_collation_name = mforms.newLabel("")
        self.col_default_collation_name.set_style(mforms.BoldStyle)
        self.table.add(self.col_default_collation_name,                    1, 2, 3, 4, mforms.HFillFlag|mforms.HExpandFlag)
        
        self.table.add(mforms.newLabel(tr("Default character set:")),           0, 1, 4, 5, mforms.HFillFlag)
        self.col_default_character_set_name = mforms.newLabel("")
        self.col_default_character_set_name.set_style(mforms.BoldStyle)
        self.table.add(self.col_default_character_set_name,                1, 2, 4, 5, mforms.HFillFlag|mforms.HExpandFlag)
        
        self.table.add(mforms.newLabel(tr("Table count:")),                    0, 1, 5, 6, mforms.HFillFlag)
        self.table_count = mforms.newLabel("")
        self.table_count.set_style(mforms.BoldStyle)
        self.table.add(self.table_count,                                   1, 2, 5, 6, mforms.HFillFlag|mforms.HExpandFlag)
        
        self.table.add(mforms.newLabel(tr("Estimated schema size:")), 0, 1, 6, 7, mforms.HFillFlag)
        self.database_size = mforms.newLabel("")
        self.database_size.set_style(mforms.BoldStyle)
        self.table.add(self.database_size,                                 1, 2, 6, 7, mforms.HFillFlag|mforms.HExpandFlag)

        tbox = mforms.Box(False)
        tbox.set_spacing(8)
        tbox.set_padding(12)
        tbox.add(self.table, True, True)
        
        self.add(tbox, False, True)
    
    def refresh(self):
        try:
            rset = self.editor.executeManagementQuery("select * from information_schema.schemata WHERE schema_name = '%s'" % self._schema, 0)
        except grt.DBError as e:
            log_error("select * from information_schema.schemata WHERE schema_name = '%s' : %s\n" % (self._schema, e))
            rset = None
            
        if rset:
            ok = rset.goToFirstRow()
            
            if ok:
                for col in rset.columns:
                    if hasattr(self, "col_%s" % col.name.lower()):
                        getattr(self, "col_%s" % col.name.replace(" ", "_").lower()).set_text(rset.stringFieldValueByName(col.name))
                        
        try:
            rset = self.editor.executeManagementQuery("select sum(data_length) + sum(index_length) database_size,count(*) table_count from information_schema.tables WHERE table_schema = '%s'" % self._schema, 0)
        except grt.DBError as e:
            log_error("select sum(data_length) + sum(index_length) database_size,count(*) table_count from information_schema.tables WHERE table_schema = '%s' : %s\n" % (self._schema, e))
            rset = None
            
        if rset:
            ok = rset.goToFirstRow()
            if ok:
                self.database_size.set_text(human_size(rset.floatFieldValueByName("database_size")))
                self.table_count.set_text(rset.stringFieldValueByName("table_count"))

class SchemaManager(mforms.AppView):
    def __init__(self, editor, schema_name):
        mforms.AppView.__init__(self, False, tr("Schema Inspector"), "schema_inspector", False)

        self.editor = editor

        heading = mforms.newBox(True)
        heading.set_padding(8)
        heading.set_spacing(8)
        heading.add(make_panel_header("sdp_database@2x.png", editor.connection.name, schema_name), True, True)
        refresh = mforms.newButton()
        refresh.set_text(tr("Refresh"))
        refresh.set_min_size(100, -1)
        refresh.add_clicked_callback(self.refresh)
        heading.add_end(refresh, False, True)
        self.add(heading, False, True)

        self.tabview = mforms.newTabView(mforms.TabViewClassic)
        self.add(self.tabview, True, True)

        self.pages = []

        tabs = [SchemaInfoPanel, TableManagerParent, ColumnManager, IndexManager, TriggerManager, ViewManager, ProcedureManager, FunctionManager, GrantsManager]
        if self.editor.serverVersion.majorNumber > 5 or (self.editor.serverVersion.majorNumber == 5 and self.editor.serverVersion.minorNumber >= 1):
            tabs.append(EventManager)
        # tabs.append(AccessManager)
        for Tab in tabs:
            try:
                if Tab is IndexManager:
                    tab = Tab(editor, schema_name, self)
                else:
                    tab = Tab(editor, schema_name)
                setattr(self, "tab_"+tab.node_name, tab)
                self.pages.append(tab)
                self.tabview.add_page(tab, tab.caption)
            except Exception:
                import traceback
                log_error("Error initializing tab %s: %s\n" % (tab.node_name, traceback.format_exc()))
        self.refresh()


    def show_table_maintenance(self):
        self.tab_tables.switch_maintenance()


    def refresh(self):
        for tab in self.pages:
            tab.refresh()


