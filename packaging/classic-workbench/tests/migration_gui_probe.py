"""Run inside Classic with --run-script; input is ONLY a disposable fixture config.

Launched by migration_acceptance.py with its isolated profile and temporary Server.

Exercises real wizard pages, callbacks, GRT migration modules and wbcopytables.
Progress task functions run synchronously, so this is a workflow acceptance probe,
not a test of the native message loop or mouse/keyboard input dispatch.
"""

import json
import os
from pathlib import Path
import traceback

import grt
import mforms
import migration_main
from workbench.db_utils import MySQLConnection


WRITE_COUNTERS = {
    "Com_insert", "Com_insert_select", "Com_update", "Com_update_multi",
    "Com_delete", "Com_delete_multi", "Com_replace", "Com_replace_select",
    "Com_create_db", "Com_create_table", "Com_alter_table", "Com_drop_db",
    "Com_drop_table", "Com_truncate", "Com_create_view", "Com_drop_view",
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def connection(config, user, schema, name):
    conn = grt.classes.db_mgmt_Connection()
    conn.owner = grt.root.wb.rdbmsMgmt
    conn.name = name
    mysql = next(r for r in grt.root.wb.rdbmsMgmt.rdbms if r.name == "Mysql")
    conn.driver = next(d for d in mysql.drivers if d.name == "MysqlNative")
    conn.hostIdentifier = "Mysql@127.0.0.1:" + str(config["port"])
    for key, value in {
        "hostName": "127.0.0.1", "port": config["port"], "userName": user,
        "schema": schema, "timeout": 20, "useSSL": 3, "sslCA": config["ca"],
    }.items():
        conn.parameterValues[key] = value
    if user == config["source_user"] and config.get("source_charset"):
        conn.parameterValues["defaultCharSet"] = config["source_charset"]
    return conn


def rows(database, sql):
    result = database.executeQuery(sql)
    output = []
    valid = result.firstRow()
    while valid:
        output.append([result.stringByIndex(i) for i in range(1, result.numFields() + 1)])
        valid = result.nextRow()
    return output


def write_counters(database):
    counters = {name: int(value) for name, value in rows(database, "SHOW GLOBAL STATUS")
                if name in WRITE_COUNTERS}
    require(set(counters) == WRITE_COUNTERS, "Required Server statement counters are missing")
    return counters


def snapshot(database, schema, table="items", label="label"):
    # Identifiers originate only from the fixed test fixture, never user input.
    return rows(database, "SELECT CAST(id AS CHAR),CAST(cost AS CHAR),HEX(`%s`),"
                "HEX(payload),HEX(raw_key),HEX(notes),COALESCE(CAST(optional_value AS CHAR),'NULL') "
                "FROM `%s`.`%s` ORDER BY id" % (label, schema, table))


def tree_node(tree, tag):
    # These production trees do not enable TreeIndexOnTag; traverse real nodes.
    pending = [tree.root_node()]
    while pending:
        node = pending.pop()
        if node.get_tag() == tag:
            return node
        pending.extend(node.get_child(index) for index in range(node.count()))
    raise AssertionError("Mapped object has no native tree row: " + tag)


def select_connection(panel, conn, result):
    try:
        panel.connection = conn
    except SystemError as error:
        # Classic's existing GRT setter throws after a successful native selection
        # (ui_db_ConnectPanel.cpp); accept only its exact known diagnostic and then
        # independently verify that the live panel did select the intended object.
        if str(error) != "Cannot set connection value to non-initialized ui.db.ConnectionPanel instance":
            raise
        result["connection_setter_legacy_diagnostic"] = True
    actual = panel.connection
    require(actual is not None and actual.__id__ == conn.__id__, "Native connection panel selected another profile")
    for key in ("hostName", "port", "userName", "schema", "useSSL", "sslCA"):
        require(actual.parameterValues[key] == conn.parameterValues[key], "Connection parameter mismatch: " + key)


def probe(config, result):
    require(config.get("fixture") == "sdp-disposable-migration-v1", "Not a disposable fixture config")
    require(config["source_schema"] == "sdp_migration_source" and
            config["target_schema"] == "sdp_migration_target", "Unexpected test schema names")
    observer = MySQLConnection(connection(config, "root", "", "Disposable observer"), password="")
    observer.connect()
    plan = None
    try:
        require(rows(observer, "SELECT @@server_uuid,@@port") == [[config["uuid"], str(config["port"])]],
                "Fixture identity does not match the disposable Server")
        require(Path(rows(observer, "SELECT @@datadir")[0][0]).resolve() == Path(config["datadir"]).resolve(),
                "Fixture datadir mismatch")
        source_before = snapshot(observer, config["source_schema"])
        require(len(source_before) == 3, "Fixture must contain exactly three synthetic rows")
        require(rows(observer, "SELECT COUNT(*) FROM information_schema.schemata "
                               "WHERE schema_name='sdp_migration_target'") == [["0"]],
                "Target must not exist before preview")
        counters_before = write_counters(observer)

        result["phase"] = "source_target"
        source_conn = connection(config, config["source_user"], config["source_schema"], "Probe source")
        target_conn = connection(config, config["target_user"], "", "Probe target")
        # Both records live only in this process's disposable Classic profile.
        # DbConnectPanel selects registered records rather than arbitrary objects.
        grt.root.wb.rdbmsMgmt.storedConns.append(source_conn)
        grt.root.wb.rdbmsMgmt.storedConns.append(target_conn)
        wizard = migration_main.Migration()
        plan = wizard.plan
        # Avoid launching timers/threads while --run-script owns the GUI thread.
        # The original functions, controls and production callbacks are retained.
        for page in wizard.tabs:
            if hasattr(page, "_autostart"):
                page._autostart = False
        mforms.App.get().dock_view(wizard, "maintab")

        def active(title):
            page = wizard.active_tab
            require(page.identifier().strip() == title,
                    "Expected %s, got %s" % (title, page.identifier().strip()))
            result.setdefault("pages", []).append(title)
            return page

        def run_tasks(page):
            for task in page._tasks:
                if task.is_enabled:
                    task.func()
                    task.set_finished()
            page._tasks_finished = True
            page.go_next()

        overview = wizard._overview_page
        overview.source_selector.set_selected(overview.sources.index(source_conn) + 1)
        overview.target_selector.set_selected(overview.targets.index(target_conn) + 1)
        overview.read_schemas()
        require(wizard.active_tab.identifier().strip() == "Fetch Schema List",
                "Paired source/target entry did not reach schema discovery")
        wizard.go_previous_page()
        result["paired_start_verified"] = True
        wizard.start()
        source = active("Source Selection")
        select_connection(source.panel, source_conn, result)
        source.go_next()
        plan.migrationSource.password = config["password"]
        target = active("Target Selection")
        select_connection(target.panel, target_conn, result)
        target.go_next()
        plan.migrationTarget.password = config["password"]
        run_tasks(active("Fetch Schema List"))

        result["phase"] = "schema_selection"
        selection = active("Schemas Selection")
        selector = selection.schema_selector
        selected = [node for node in selector._schema_nodes if node.get_string(1) == config["source_schema"]]
        require(len(selected) == 1, "Synthetic source schema is absent from the real selector")
        selector._schema_tree_checked(selected, 0, "1")
        selection.go_next()
        require(list(plan.migrationSource.selectedSchemataNames) == [config["source_schema"]],
                "Schema selection callback did not select the synthetic schema")
        run_tasks(active("Reverse Engineer Source"))
        active("Source Objects").go_next()
        run_tasks(active("Migration"))

        result["phase"] = "mapping_preview"
        editor = active("Manual Editing")
        require(len(plan.migrationTarget.catalog.schemata) == 1, "Unexpected migrated schemas")
        schema = plan.migrationTarget.catalog.schemata[0]
        require(len(schema.tables) == 1, "Unexpected migrated tables")
        table = schema.tables[0]
        editor._cell_edited(tree_node(editor._tree, schema.__id__), 1, config["target_schema"])
        editor._cell_edited(tree_node(editor._tree, table.__id__), 1, "items_copy")
        editor._filter.set_selected(2)
        editor._filter_changed()
        editor.refresh()
        label = next(column for column in table.columns if column.oldName == "label")
        editor._columns_cell_edited(tree_node(editor._columns, label.__id__),
                                    editor.COL_TARGET_COLUMN, "display_name")
        require(schema.name == config["target_schema"] and table.name == "items_copy" and
                label.name == "display_name", "Real mapping callbacks did not update the catalog")
        require("items_copy" in table.temp_sql and "display_name" in table.temp_sql,
                "SQL preview did not regenerate after mapping edits")
        editor.go_advanced()
        editor.go_next()
        preview = active("SQL Preview")
        require("items_copy" in preview.editor.get_text(False),
                "Read-only SQL preview does not show the mapped target table")
        preview.go_next()
        options = active("Target Creation Options")
        counters_after = write_counters(observer)
        result["preview_statement_counters_before"] = counters_before
        result["preview_statement_counters_after"] = counters_after
        require(counters_before == counters_after,
                "Preview issued DDL/DML: " + repr({k: counters_after[k] - v for k, v in counters_before.items()
                                                 if counters_after[k] != v}))
        require(snapshot(observer, config["source_schema"]) == source_before, "Preview changed source rows")
        require(rows(observer, "SELECT COUNT(*) FROM information_schema.schemata "
                               "WHERE schema_name='sdp_migration_target'") == [["0"]],
                "Preview created the target schema")
        result["preview_zero_writes"] = True
        result["preview_sql"] = table.temp_sql
        result["mapping"] = {"schema": schema.name, "table": table.name, "column": label.name}

        # Explicit test action at the same boundary as the SQL'i uygula button.
        result["phase"] = "explicit_schema_apply"
        options._keep_schema.set_active(True)
        options._create_db.set_active(True)
        options._create_script.set_active(False)
        options.go_next()
        run_tasks(active("Create Schemas"))
        report = active("Create Target Results")
        require(not report._error_tables, "Schema creation reported failed tables")
        require(rows(observer, "SELECT COUNT(*) FROM sdp_migration_target.items_copy") == [["0"]],
                "Schema apply did not create the empty mapped table")
        report.go_next()

        result["phase"] = "explicit_data_transfer"
        setup = active("Data Transfer Setup")
        setup._copy_db.set_active(True)
        setup.CopyScript_radiobutton.set_active(False)
        setup.BulkCopyScript_radiobutton.set_active(False)
        setup._truncate_db.set_active(False)
        setup._worker_count.set_value("1")
        setup.go_next()
        transfer = active("Bulk Data Transfer")
        require(Path(plan.wbcopytables_path_bin).is_file(), "wbcopytables.exe is missing from the package")
        run_tasks(transfer)
        require(transfer._count_of_failed_tables == 0, "Native wbcopytables reported a failed table")
        active("Migration Report")
        target_rows = snapshot(observer, config["target_schema"], "items_copy", "display_name")
        result["source_rows"] = source_before
        result["target_rows"] = target_rows
        require(target_rows == source_before, "Copied BIGINT/DECIMAL/UTF8/BLOB/NULL values differ")
        require(snapshot(observer, config["source_schema"]) == source_before, "Transfer changed source rows")
        result.update(ok=True, phase="complete", rows_copied=len(target_rows), typed_rows=target_rows,
                      source_unchanged=True, engine="native GRT + real wizard callbacks + wbcopytables",
                      odbc_required=False, source_charset_override=config.get("source_charset", ""))
    finally:
        if plan is not None:
            plan.close()
        observer.disconnect()


def main():
    config = json.loads(Path(os.environ["SDP_MIGRATION_PROBE_CONFIG"]).read_text(encoding="utf-8"))
    result = {"ok": False, "phase": "initialization"}
    try:
        probe(config, result)
    except Exception as error:
        result["error"] = (type(error).__name__ + ": " + str(error)).replace(config["password"], "<redacted>")
        result["traceback"] = traceback.format_exc().replace(config["password"], "<redacted>")
    Path(config["result"]).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


main()
