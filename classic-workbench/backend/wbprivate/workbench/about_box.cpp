/*
 * Copyright (c) 2013, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include "base/string_utilities.h"
#include "cairo/cairo.h"
#include "base/file_utilities.h"
#include "base/log.h"
#include "mforms/app.h"
#include "mforms/utilities.h"
#include "about_box.h"
#include "wb_version.h"

using namespace wb;

AboutBox::AboutBox(const std::string &edition)
  : Form(mforms::Form::main_form(), mforms::FormResizable),
    _content(false), _heading(true), _title_box(false), _actions(true),
    _tabs(mforms::TabViewClassic), _product(mforms::BothScrollBars),
    _licenses(mforms::BothScrollBars), _diagnostics(mforms::BothScrollBars) {
  set_title("StarDataParadise Classic - Hakkında");
  set_name("StarDataParadise Classic About");
  set_back_color("#EDF2F4");
  _content.set_padding(12);
  _content.set_spacing(12);
  _heading.set_spacing(12);
  _title_box.set_spacing(4);
  _logo.set_image(mforms::App::get()->get_resource_path("MySQLWorkbench-128.png"));
  _logo.set_scale_contents(true);
  _logo.set_size(48, 48);
  _heading.add(&_logo, false, false);
  _title.set_text("StarDataParadise Classic");
  _title.set_style(mforms::BigBoldStyle);
  _title_box.add(&_title, false, true);
  std::string version = base::strfmt("%i.%i.%i - yapı %i - %s - %i bit", APP_MAJOR_NUMBER, APP_MINOR_NUMBER,
    APP_RELEASE_NUMBER, APP_BUILD_NUMBER, APP_RELEASE_TYPE, static_cast<int>(sizeof(void *) * 8));
  _version.set_text(version);
  _title_box.add(&_version, false, true);
  _heading.add(&_title_box, true, true);
  _content.add(&_heading, false, true);

  const auto install_path = mforms::App::get()->baseDir();
  const auto license_directory = base::makePath(install_path, "licenses");
  _diagnostic_text = "StarDataParadise Classic\n" + version + "\n" +
    "Derleme: " __DATE__ " " __TIME__ "\n" +
    "Dağıtım: " + edition + "\n" +
    "Kurulum: " + install_path + "\n" +
    "Kullanıcı ayarları: " + mforms::App::get()->get_user_data_folder() + "\n" +
    "Günlük: " + base::Logger::log_filename() + "\n" +
    "Cairo: " + cairo_version_string() + "\n";
  _product.set_read_only(true);
  _product.set_bordered(false);
  _product.set_value("StarDataParadise veritabanı çalışma alanı\n\n" + version +
    "\n\nDerleme: " __DATE__ " " __TIME__ "\n\nKurulum\n" + install_path +
    "\n\nKaynak bileşenlerin telif ve lisans bildirimleri Bileşen lisansları sekmesinde korunur.");
  _tabs.add_page(&_product, "Ürün bilgisi");
  _licenses.set_read_only(true);
  _licenses.set_bordered(false);
  std::string license_text;
  auto license_path = mforms::App::get()->get_resource_path("License.txt");
  if (license_path.empty()) license_path = base::makePath(install_path, "License.txt");
  try {
    license_text = base::getTextFileContent(license_path);
  } catch (const std::exception &error) {
    license_text = std::string("Lisans dosyası okunamadı: ") + error.what();
  }
  _licenses.set_value("Bileşen bildirimleri\n" + license_directory + "\n\n" + license_text);
  _tabs.add_page(&_licenses, "Bileşen lisansları");
  _diagnostics.set_read_only(true);
  _diagnostics.set_bordered(false);
  _diagnostics.set_monospaced(true);
  _diagnostics.set_value(_diagnostic_text);
  _tabs.add_page(&_diagnostics, "Tanılama");
  _content.add(&_tabs, true, true);
  _actions.set_spacing(8);
  _copy.set_text("Sürüm bilgisini kopyala");
  _copy.signal_clicked()->connect([this]() { mforms::Utilities::set_clipboard_text(_diagnostic_text); });
  _actions.add(&_copy, false, true);
  _logs.set_text("Günlük klasörü");
  _logs.set_enabled(!base::Logger::log_dir().empty());
  _logs.signal_clicked()->connect([]() { mforms::Utilities::open_url(base::Logger::log_dir()); });
  _actions.add(&_logs, false, true);
  _license_folder.set_text("Bileşen dosyaları");
  _license_folder.set_enabled(base::file_exists(license_directory));
  _license_folder.signal_clicked()->connect([license_directory]() { mforms::Utilities::open_url(license_directory); });
  _actions.add(&_license_folder, false, true);
  _close.set_text("Kapat");
  _actions.add_end(&_close, false, true);
  _content.add(&_actions, false, true);
  set_content(&_content);
  set_size(760, 500);
}

void AboutBox::show_about(const std::string &edition) {
  AboutBox dialog(edition);
  dialog.center();
  dialog.run_modal(nullptr, &dialog._close);
}
