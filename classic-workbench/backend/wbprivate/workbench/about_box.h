/*
 * Copyright (c) 2013, 2018, Oracle and/or its affiliates. All rights reserved.
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

#ifndef _ABOUT_BOX_H_
#define _ABOUT_BOX_H_

#include "wb_backend_public_interface.h"

#include "mforms/form.h"
#include "mforms/box.h"
#include "mforms/tabview.h"
#include "mforms/label.h"
#include "mforms/textbox.h"
#include "mforms/button.h"
#include "mforms/imagebox.h"

namespace wb {
  /**
   * Local product, license and diagnostic information.
   */
  class MYSQLWBBACKEND_PUBLIC_FUNC AboutBox : public mforms::Form {
  private:
    mforms::Box _content, _heading, _title_box, _actions;
    mforms::TabView _tabs;
    mforms::ImageBox _logo;
    mforms::Label _title, _version;
    mforms::TextBox _product, _licenses, _diagnostics;
    mforms::Button _copy, _logs, _license_folder, _close;
    std::string _diagnostic_text;

  public:
    AboutBox(const std::string &edition);

    static void show_about(const std::string &edition);
  };
}

#endif // _ABOUT_BOX_H_
