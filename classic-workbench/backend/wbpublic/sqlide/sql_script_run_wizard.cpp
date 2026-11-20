/*
 * Copyright (c) 2007, 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA 
 */

#include "grtdb/db_helpers.h"
#include "sql_script_run_wizard.h"

#include "mforms/code_editor.h"
#include "mforms/selector.h"
#include "mforms/button.h"
#include "mforms/filechooser.h"
#include "base/string_utilities.h"

//--------------------------------------------------------------------------------------------------

SqlScriptReviewPage::SqlScriptReviewPage(grtui::WizardForm *form, GrtVersionRef version, std::string algorithm,
                                         std::string lock)
  : grtui::WizardPage(form, "review"), _box(false), _preview_box(true), _summary_box(false) {
  set_title(_("Review SQL changes"));
  set_short_title(_("SQL preview"));
  set_padding(12);
  set_spacing(8);

  _box.set_spacing(8);
  add(&_box, true, true);
  _target_heading.set_style(mforms::BoldStyle);
  _target_heading.set_wrap_text(true);
  _box.add(&_target_heading, false, true);

  _page_heading.set_text(_("SQL script - Read only"));
  _page_heading.set_style(mforms::BoldStyle);
  _box.add(&_page_heading, false, true);

  // Online DDL options. Structure is very similar as in the preferences, just on one line, instead 2.
  // Add them only if online DDL options were given (which only happens for meta data changes).
  // This is not supposed to be shown for pre 5.6 servers
  if (!algorithm.empty() && !lock.empty() &&
      (version.is_valid() && bec::is_supported_mysql_version_at_least(version, 5, 6))) {
    // The passed in values for online DDL algorithm and lock are used to properly
    // initialize the selectors.
    mforms::Box *line_box = mforms::manage(new mforms::Box(true));
    line_box->set_spacing(8);
    _box.add(line_box, false, true);

    mforms::Label *label = mforms::manage(new mforms::Label(_("Algorithm:")));
    line_box->add(label, false, false);

    _algorithm_selector = mforms::manage(new mforms::Selector());
    scoped_connect(_algorithm_selector->signal_changed(), std::bind(&SqlScriptReviewPage::option_changed, this));
    _algorithm_selector->add_item(_("Default"));
    _algorithm_selector->add_item(_("In place"));
    _algorithm_selector->add_item(_("Copy"));
    if (algorithm == "INPLACE")
      _algorithm_selector->set_selected(1);
    else if (algorithm == "COPY")
      _algorithm_selector->set_selected(2); // else leave it at 0.
    _algorithm_selector->set_size(130, -1);
    _algorithm_selector->set_tooltip(
      _("If the currently connected server supports online DDL then use the selected "
        "algorithm as default. This setting can also be adjusted for each alter operation."));
    line_box->add(_algorithm_selector, false, false);

    label = mforms::manage(new mforms::Label(_("Lock:")));
    line_box->add(label, false, false);

    _lock_selector = mforms::manage(new mforms::Selector());
    scoped_connect(_lock_selector->signal_changed(), std::bind(&SqlScriptReviewPage::option_changed, this));
    _lock_selector->add_item(_("Default"));
    _lock_selector->add_item(_("None"));
    _lock_selector->add_item(_("Shared"));
    _lock_selector->add_item(_("Exclusive"));
    if (lock == "NONE")
      _lock_selector->set_selected(1);
    else if (lock == "SHARED")
      _lock_selector->set_selected(2);
    else if (lock == "EXCLUSIVE")
      _lock_selector->set_selected(3); // else leave it at 0.
    _lock_selector->set_size(130, -1);
    _lock_selector->set_tooltip(
      _("If the currently connected server supports online DDL then use the selected "
        "lock as default. This setting can also be adjusted for each alter operation."));
    line_box->add(_lock_selector, false, false);
  } else {
    _lock_selector = 0;
    _algorithm_selector = 0;
  }

  _sql_editor = mforms::manage(new mforms::CodeEditor());
  if (!version.is_valid() || version->majorNumber() < 5)
    _sql_editor->set_language(mforms::LanguageMySQL);
  else {
    switch (version->minorNumber()) {
      case 6:
        _sql_editor->set_language(mforms::LanguageMySQL56);
        break;

      case 7:
        _sql_editor->set_language(mforms::LanguageMySQL57);
        break;
        
      default:
        _sql_editor->set_language(mforms::LanguageMySQL);
    }
  }
  _preview_box.set_spacing(10);
  _summary_box.set_size(170, -1);
  _summary_box.set_spacing(8);
  auto summary_title = mforms::manage(new mforms::Label(_("Operation")));
  summary_title->set_style(mforms::BoldStyle);
  _summary_box.add(summary_title, false, true);
  _operation_heading.set_wrap_text(true);
  _summary_box.add(&_operation_heading, false, true);
  _preview_box.add(&_summary_box, false, true);
  _preview_box.add(_sql_editor, true, true);
  _box.add(&_preview_box, true, true);
  _caution.set_wrap_text(true);
  _box.add(&_caution, false, true);
}

//--------------------------------------------------------------------------------------------------

SqlScriptReviewPage::~SqlScriptReviewPage() {
  // No need to release _algorithm_selector and _lock_selector. They are managed with release_on_add.
  _sql_editor->release();
};

//--------------------------------------------------------------------------------------------------

void SqlScriptReviewPage::enter(bool advancing) {
  set_script_text(values().get_string("sql_script"));
  std::string connection = values().get_string("target_connection", "");
  std::string object = values().get_string("target_object", "");
  std::string schema = values().get_string("target_schema", "");
  if (!schema.empty() && !object.empty())
    object = schema + "." + object;
  _target_heading.set_text(connection + (!object.empty() ? " - " + object : ""));
  _target_heading.show(!connection.empty() || !object.empty());
  bool data_changes = values().get_string("change_kind", "schema") == "data";
  _operation_heading.set_text(std::string(data_changes ? _("Row changes") : _("Object definition")) +
                              (object.empty() ? "" : "\n" + object));
  _caution.set_text(data_changes ? _("Pending row changes are sent to the server only after Apply.") :
      _("DDL changes on the target may be permanent. Review the script before applying."));
  grtui::WizardPage::enter(advancing);
}

void SqlScriptReviewPage::set_script_text(const std::string &text) {
  _sql_editor->set_features(mforms::FeatureReadOnly, false);
  _sql_editor->set_value(text);
  _sql_editor->set_features(mforms::FeatureReadOnly, true);
}

//--------------------------------------------------------------------------------------------------

bool SqlScriptReviewPage::advance() {
  std::string sql = base::trim(_sql_editor->get_text(false));

  if (sql.empty())
    return false;
  else
    values().gset("sql_script", sql);

  return grtui::WizardPage::advance();
}

//--------------------------------------------------------------------------------------------------

std::string SqlScriptReviewPage::next_button_caption() {
  return _("Apply");
}

std::string SqlScriptReviewPage::extra_button_caption() {
  return _("Save script...");
}

void SqlScriptReviewPage::extra_clicked() {
  mforms::FileChooser file(mforms::SaveFile);
  file.set_title(_("Save SQL script"));
  file.set_extensions(_("SQL files (*.sql)|*.sql"), "sql");
  if (!file.run_modal()) return;
  try {
    base::setTextFileContent(file.get_path(), _sql_editor->get_text(false));
  } catch (const std::exception &error) {
    mforms::Utilities::show_error(_("Could not save script"), error.what(), _("Close"));
  }
}

//--------------------------------------------------------------------------------------------------

void SqlScriptReviewPage::option_changed() {
  SqlScriptRunWizard *wizard = dynamic_cast<SqlScriptRunWizard *>(_form);
  if (wizard != NULL && wizard->regenerate_script) {
    static std::string algorithms[] = {"DEFAULT", "INPLACE", "COPY"};
    std::string algorithm = algorithms[_algorithm_selector->get_selected_index()];

    static std::string locks[] = {"DEFAULT", "NONE", "SHARED", "EXCLUSIVE"};
    std::string lock = locks[_lock_selector->get_selected_index()];

    set_script_text(wizard->regenerate_script(algorithm, lock));
  }
}

//----------------- SqlScriptApplyPage -------------------------------------------------------------

SqlScriptApplyPage::SqlScriptApplyPage(grtui::WizardForm *form)
  : grtui::WizardProgressPage(form, "apply", true), _err_count(0) {
  set_title(_("Applying SQL changes"));
  set_short_title(_("Apply result"));

  /*TaskRow *task=*/add_async_task(_("Execute SQL statements"),
                                   std::bind(&SqlScriptApplyPage::execute_sql_script, this),
                                   _("Executing SQL statements..."));

  end_adding_tasks(_("SQL script applied."));

  {
    _abort_btn = mforms::manage(new mforms::Button());
    _abort_btn->set_text(_("Stop"));
    _abort_btn->signal_clicked()->connect(std::bind(&SqlScriptApplyPage::abort_exec, this));
    _progress_bar_box->add_end(_abort_btn, false, true);
  }
  set_status_text("");
}

void SqlScriptApplyPage::abort_exec() {
  auto wizard = dynamic_cast<SqlScriptRunWizard *>(_form);
  if (wizard && wizard->abort_apply)
    wizard->abort_apply();
}

int SqlScriptApplyPage::on_error(long long err_code, const std::string &err_msg, const std::string &err_sql) {
  std::string sql = base::strip_text(err_sql, true, true);
  _log += _("Error");
  if (err_code >= 0)
    _log += base::strfmt(" %lli", err_code);
  _log += base::strfmt(": %s\n", err_msg.c_str());
  if (!err_sql.empty())
    _log += base::strfmt(_("SQL Statement:\n%s\n"), sql.c_str());
  _log += "\n";
  return 0;
}

int SqlScriptApplyPage::on_exec_progress(float progress) {
  update_progress(progress, "");
  return 0;
}

int SqlScriptApplyPage::on_exec_stat(long success_count, long err_count) {
  _err_count = err_count;
  return 0;
}

grt::ValueRef SqlScriptApplyPage::do_execute_sql_script(const std::string &sql_script) {
  bec::GRTManager::get()->run_once_when_idle(
    this, std::bind(&SqlScriptApplyPage::add_log_text, this, std::string(_("Executing:\n")) + sql_script + "\n"));

  apply_sql_script(sql_script);

  if (_err_count) {
    values().gset("has_errors", 1);
    bec::GRTManager::get()->run_once_when_idle(this, std::bind(&SqlScriptApplyPage::add_log_text, this, _log));
    throw std::runtime_error(_("There was an error while applying the SQL script to the database."));
  } else {
    bec::GRTManager::get()->run_once_when_idle(
      this,
      std::bind(&SqlScriptApplyPage::add_log_text, this, _("SQL script was successfully applied to the database.")));
  }

  return grt::ValueRef();
}

bool SqlScriptApplyPage::execute_sql_script() {
  values().gset("applied", 1);
  values().gset("has_errors", 0);
  std::string sql_script = values().get_string("sql_script");

  // apply_sql_script(sql_script);

  execute_grt_task(std::bind(&SqlScriptApplyPage::do_execute_sql_script, this, sql_script), false);

  return true;
}

std::string SqlScriptApplyPage::next_button_caption() {
  return _("Close");
}

bool SqlScriptApplyPage::allow_back() {
  return !_busy && !_done;
}

bool SqlScriptApplyPage::allow_next() {
  return !_busy && values().get_int("has_errors") == 0;
}

bool SqlScriptApplyPage::allow_cancel() {
  return values().get_int("has_errors") != 0;
}

void SqlScriptApplyPage::enter(bool advancing) {
  if (dynamic_cast<SqlScriptRunWizard *>(_form)->abort_apply)
    _abort_btn->show(true);
  else
    _abort_btn->show(false);

  if (advancing) {
    _log_text.set_value("");
  }
  WizardProgressPage::enter(advancing);
}

//-----------------------
// SqlScriptRunWizard
//-----------------------

SqlScriptRunWizard::SqlScriptRunWizard(GrtVersionRef version, std::string algorithm, std::string lock)
  : grtui::WizardForm() {
  set_name("Script Run Wizard");
  setInternalName("script_run_wizard");
  set_title(_("Review SQL changes"));
  set_size(920, 620);
  review_page = new SqlScriptReviewPage(this, version, algorithm, lock);
  add_page(mforms::manage(review_page));
  apply_page = new SqlScriptApplyPage(this);
  add_page(mforms::manage(apply_page));
  values().gset("has_errors", 0);
  values().gset("applied", 0);
}

bool SqlScriptRunWizard::has_errors() {
  return values().get_int("has_errors") != 0;
}

bool SqlScriptRunWizard::applied() {
  return values().get_int("applied") != 0;
}
