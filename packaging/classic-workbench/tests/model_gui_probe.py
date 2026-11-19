# -*- coding: utf-8 -*-
"""Run in a fresh Classic acceptance instance with --run-script.

Creates a real editable .mwb model through the native GRT model APIs. It does
not connect to a database, change application preferences, or close a model.
Set SDP_MODEL_PROBE_OUTPUT to override the output directory; the default is
the repository's out/classic-home-acceptance directory.
"""

import json
import os
from pathlib import Path
import traceback

import grt


OUTPUT_DIR = Path(os.environ.get("SDP_MODEL_PROBE_OUTPUT") or
                  Path(__file__).resolve().parents[3] / "out" / "classic-home-acceptance").resolve()
MODEL_PATH = os.path.join(OUTPUT_DIR, "kitaplik.mwb")
RESULT_PATH = os.path.join(OUTPUT_DIR, "model-fixture-result.json")


def build_fixture():
    if grt.root.wb.doc:
        raise RuntimeError("Use a fresh acceptance instance: a model is already open.")
    if os.path.exists(MODEL_PATH):
        raise RuntimeError("Fixture already exists; use openModel instead of overwriting it.")

    grt.modules.Workbench.newDocument()
    document = grt.root.wb.doc
    assert document and len(document.physicalModels) == 1
    model = document.physicalModels[0]
    model.name = "Kitaplık"
    model.figureNotation = "workbench"
    model.connectionNotation = "crowsfoot"

    # Per-document settings make visual acceptance independent of the tester's
    # older saved defaults, without changing their application preferences.
    model.options["useglobal"] = 0
    model.options["workbench.physical.TableFigure:TitleFont"] = "Segoe UI Bold 13"
    model.options["workbench.physical.TableFigure:SectionFont"] = "Segoe UI Bold 13"
    model.options["workbench.physical.TableFigure:ItemsFont"] = "Consolas 13"
    model.options["workbench.physical.TableFigure:ShowColumnTypes"] = 1

    catalog = model.catalog
    if catalog.schemata:
        schema = catalog.schemata[0]
        assert len(schema.tables) == 0
    else:
        schema = grt.classes.db_mysql_Schema()
        schema.owner = catalog
        catalog.schemata.append(schema)
    schema.name = "kitaplik"

    def table(name):
        result = schema.addNewTable("db.mysql")
        result.name = name
        result.tableEngine = "InnoDB"
        return result

    def column(owner, name, sql_type, primary=False):
        result = grt.classes.db_mysql_Column()
        result.owner = owner
        result.name = name
        result.setParseType(sql_type, catalog.simpleDatatypes)
        result.isNotNull = 1
        owner.addColumn(result)
        if primary:
            owner.addPrimaryKeyColumn(result)
        return result

    authors = table("yazarlar")
    author_id = column(authors, "id", "INT", primary=True)
    column(authors, "ad", "VARCHAR(100)")

    books = table("kitaplar")
    column(books, "id", "INT", primary=True)
    book_author_id = column(books, "yazar_id", "INT")
    column(books, "baslik", "VARCHAR(200)")

    foreign_key = books.createForeignKey("fk_kitaplar_yazarlar")
    foreign_key.referencedTable = authors
    foreign_key.columns.append(book_author_id)
    foreign_key.referencedColumns.append(author_id)
    foreign_key.mandatory = 1
    foreign_key.referencedMandatory = 1
    foreign_key.many = 1
    foreign_key.deleteRule = "RESTRICT"
    foreign_key.updateRule = "CASCADE"
    assert foreign_key.checkCompleteness() == 1

    diagram = model.addNewDiagram(0)
    diagram.name = "Kitaplık"
    diagram.options["ShowGrid"] = 1
    diagram.options["ShowPageGrid"] = 0
    diagram.zoom = 1.0
    first = diagram.placeTable(authors, 70.0, 100.0)
    second = diagram.placeTable(books, 430.0, 100.0)
    first.color = "#336F66"
    second.color = "#336F66"
    # The component listener may have already created the visual relation.
    connection = diagram.getConnectionForForeignKey(foreign_key)
    if not connection:
        connection = diagram.createConnectionForForeignKey(foreign_key)
    assert connection
    assert len(diagram.figures) == 2
    assert len(diagram.connections) == 1
    assert authors.isPrimaryKeyColumn(author_id) == 1
    assert books.isForeignKeyColumn(book_author_id) == 1

    grt.modules.Workbench.activateDiagram(diagram)
    grt.modules.Workbench.saveModelAs(MODEL_PATH)
    assert os.path.isfile(MODEL_PATH) and os.path.getsize(MODEL_PATH) > 0

    return {
        "status": "PASS",
        "model_path": MODEL_PATH,
        "schema": schema.name,
        "tables": [{"name": item.name, "columns": len(item.columns)} for item in schema.tables],
        "diagram_figures": len(diagram.figures),
        "diagram_connections": len(diagram.connections),
        "foreign_key": {
            "name": foreign_key.name,
            "source": "kitaplar.yazar_id",
            "target": "yazarlar.id",
            "many": foreign_key.many,
            "complete": foreign_key.checkCompleteness(),
        },
    }


os.makedirs(OUTPUT_DIR, exist_ok=True)
try:
    result = build_fixture()
except Exception:
    result = {"status": "FAIL", "traceback": traceback.format_exc()}
with open(RESULT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
print(json.dumps(result, ensure_ascii=False, indent=2))
if result["status"] != "PASS":
    raise RuntimeError("Model acceptance fixture failed; see " + RESULT_PATH)
