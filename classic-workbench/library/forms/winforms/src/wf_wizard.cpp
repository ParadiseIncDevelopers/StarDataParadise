/*
 * Copyright (c) 2009, 2018, Oracle and/or its affiliates. All rights reserved.
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

#include "wf_base.h"
#include "wf_view.h"
#include "wf_form.h"
#include "wf_wizard.h"

#include "base/log.h"
#include "base/string_utilities.h"

using namespace System::Drawing;
using namespace System::Drawing::Drawing2D;
using namespace System::IO;
using namespace System::Windows::Forms;

using namespace MySQL;
using namespace MySQL::Forms;

DEFAULT_LOG_DOMAIN(DOMAIN_MFORMS_WRAPPER)

//----------------- BufferedPanel ------------------------------------------------------------------

ref class BufferedPanel : public FlowLayoutPanel {
public:
  BufferedPanel() : FlowLayoutPanel() {
    DoubleBuffered = true;
  }
};

//----------------- WizardForm ---------------------------------------------------------------------

ref class MySQL::Forms::WizardForm : public Form {
public:
  FlowLayoutPanel ^ sidebar;
  Label ^ title;
  Button ^ extraButton;
  TableLayoutPanel ^ footer;
  FlowLayoutPanel ^ buttonBar;
  Button ^ backButton;
  Button ^ nextButton;
  Button ^ cancelButton;
  Panel ^ content; // This is where the actual wizard content is docked to.

  mforms::Wizard *backend;

  //------------------------------------------------------------------------------------------------

  WizardForm() : Form() {
  }

  //------------------------------------------------------------------------------------------------

  void SetupLayout() {
    sidebar = gcnew BufferedPanel;
    title = gcnew Label;
    extraButton = gcnew Button;
    footer = gcnew TableLayoutPanel;
    buttonBar = gcnew FlowLayoutPanel;
    backButton = gcnew Button;
    nextButton = gcnew Button;
    cancelButton = gcnew Button;
    content = gcnew Panel;

    BackColor = SystemInformation::HighContrast ? SystemColors::Control : Color::FromArgb(237, 242, 244);
    ForeColor = SystemInformation::HighContrast ? SystemColors::ControlText : Color::FromArgb(16, 38, 41);
    Font = gcnew Drawing::Font("Segoe UI", 9.75f, FontStyle::Regular, GraphicsUnit::Point);
    FormBorderStyle = System::Windows::Forms::FormBorderStyle::Sizable;
    MaximizeBox = false;
    MinimizeBox = false;
    StartPosition = FormStartPosition::CenterScreen;
    CancelButton = cancelButton;
    if (File::Exists("images/icons/MySQLWorkbench.ico"))
      Icon = gcnew Drawing::Icon("images/icons/MySQLWorkbench.ico", Drawing::Size(16, 16));

    // The footer with buttons.
    Controls->Add(footer);
    footer->RowCount = 1;
    footer->ColumnCount = 2;
    footer->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Percent, 100));
    footer->ColumnStyles->Add(gcnew ColumnStyle(SizeType::AutoSize));
    footer->AutoSize = true;
    footer->Dock = DockStyle::Bottom;
    footer->Padding = System::Windows::Forms::Padding(10);
    footer->Controls->Add(extraButton, 0, 0);

    buttonBar->FlowDirection = FlowDirection::LeftToRight;
    buttonBar->WrapContents = false;
    buttonBar->AutoSizeMode = System::Windows::Forms::AutoSizeMode::GrowAndShrink;
    buttonBar->Controls->Add(backButton);
    buttonBar->Controls->Add(cancelButton);
    buttonBar->Controls->Add(nextButton);
    buttonBar->Dock = DockStyle::Right;
    buttonBar->AutoSize = true;
    buttonBar->Margin = System::Windows::Forms::Padding(0);
    footer->Controls->Add(buttonBar, 1, 0);

    // We need to keep a reference to the wizard in the buttons to be able
    // to notify it when the buttons are clicked.
    backButton->Text = CppStringToNative(_("Back"));
    backButton->UseMnemonic = true;
    backButton->Click += gcnew EventHandler(this, &WizardForm::BackClick);
    StyleButton(backButton, false);

    nextButton->Text = CppStringToNative(_("Next"));
    nextButton->UseMnemonic = true;
    nextButton->Click += gcnew EventHandler(this, &WizardForm::NextClick);
    StyleButton(nextButton, true);

    cancelButton->Text = CppStringToNative(_("Cancel"));
    cancelButton->Click += gcnew EventHandler(this, &WizardForm::CancelClick);
    StyleButton(cancelButton, false);

    extraButton->Text = CppStringToNative(_("Advanced"));
    extraButton->UseMnemonic = true;
    extraButton->Click += gcnew EventHandler(this, &WizardForm::ExtraClick);
    StyleButton(extraButton, false);

    // The side bar with background painting and list of strings.
    Controls->Add(sidebar);
    sidebar->Dock = DockStyle::Left;
    sidebar->ForeColor = ForeColor;
    sidebar->FlowDirection = FlowDirection::TopDown;
    sidebar->WrapContents = false;
    sidebar->AutoScroll = true;
    sidebar->Padding = System::Windows::Forms::Padding(8);
    Graphics ^ graphics = CreateGraphics();
    sidebar->Width = (int)System::Math::Round(184 * graphics->DpiX / 96.0f);
    delete graphics;
    sidebar->Paint += gcnew PaintEventHandler(this, &WizardForm::SidebarPaint);

    // Title
    Controls->Add(title);
    title->AutoSize = false;
    try {
      title->Font = gcnew Drawing::Font("Segoe UI", 11.0f, FontStyle::Bold, GraphicsUnit::Point);
    } catch (System::ArgumentException ^ e) {
      // Argument exception pops up when the system cannot find the Regular font style (corrupt font).
      logError("WizardWrapper::c_tor setting title font failed. %s\n", e->Message);
    }

    title->ForeColor = ForeColor;
    title->BackColor = SystemInformation::HighContrast ? SystemColors::Control : Color::FromArgb(231, 238, 239);
    title->Location = Point(sidebar->Width, 0);
    title->Padding = System::Windows::Forms::Padding(12, 8, 12, 8);

    // Other initialization.
    Controls->Add(content);

    // Resize as last action. Triggers layout of the elements.
    Size = Drawing::Size(800, 600);
  }

  void StyleButton(Button ^ button, bool primary) {
    button->FlatStyle = FlatStyle::Flat;
    button->FlatAppearance->BorderSize = primary ? 0 : 1;
    button->FlatAppearance->BorderColor = SystemInformation::HighContrast ? SystemColors::ControlText : Color::FromArgb(163, 180, 188);
    button->UseVisualStyleBackColor = false;
    button->AutoSize = true;
    button->Padding = System::Windows::Forms::Padding(10, 2, 10, 2);
    button->MinimumSize = Drawing::Size(88, 28);
    button->BackColor = SystemInformation::HighContrast ? SystemColors::Control :
      (primary ? Color::FromArgb(4, 49, 51) : Color::FromArgb(237, 242, 244));
    button->ForeColor = SystemInformation::HighContrast ? SystemColors::ControlText : (primary ? Color::White : ForeColor);
  }

  //------------------------------------------------------------------------------------------------

  /**
   * Draw the flat step rail and its neutral divider.
   *
   * @param sender The control for which the paint event is triggered (the side bar).
   * @param arguments Additional data needed to paint the background.
   */
  void SidebarPaint(System::Object ^ sender, PaintEventArgs ^ args) {
    Graphics ^ g = args->Graphics;

    SolidBrush surface(SystemInformation::HighContrast ? SystemColors::Control : Color::FromArgb(231, 238, 239));
    g->FillRectangle(% surface, 0, 0, sidebar->Width, sidebar->Height);
    Pen divider(SystemInformation::HighContrast ? SystemColors::ControlText : Color::FromArgb(163, 180, 188));
    g->DrawLine(% divider, sidebar->Width - 1, 0, sidebar->Width - 1, sidebar->Height);
  }

  //------------------------------------------------------------------------------------------------

  void NextClick(System::Object ^ sender, EventArgs ^ args) {
    backend->next_clicked();
  }

  //------------------------------------------------------------------------------------------------

  void BackClick(System::Object ^ sender, System::EventArgs ^ args) {
    backend->back_clicked();
  }

  //------------------------------------------------------------------------------------------------

  void CancelClick(System::Object ^ sender, EventArgs ^ args) {
    if (backend->_cancel_slot())
      //((Form ^) btn->TopLevelControl)->Close();
      Close();
  }

  //------------------------------------------------------------------------------------------------

  void ExtraClick(System::Object ^ sender, EventArgs ^ args) {
    backend->extra_clicked();
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnResize(EventArgs ^ args) override {
    __super ::OnResize(args);

    if (sidebar == nullptr)
      return;

    System::Drawing::Rectangle area = DisplayRectangle;
    title->Size = Drawing::Size(System::Math::Max(0, area.Width - sidebar->Width), title->PreferredHeight);
    title->Location = Point(sidebar->Width, 0);

    content->Size = Drawing::Size(System::Math::Max(0, area.Width - sidebar->Width), System::Math::Max(0, area.Height - footer->Height - title->Height));
    content->Location = Point(sidebar->Width, title->Height);
  }

  //------------------------------------------------------------------------------------------------

  void SetContent(Control ^ control) {
    // Remove old stuff if there is some.
    if (content->Controls->Count > 0) {
      content->Controls[0]->Dock = DockStyle::None;
      content->Controls->Clear();
    }

    if (control != nullptr) {
      content->Controls->Add(control);
      control->Dock = DockStyle::Fill;

      // Focus the new page to allow mnemonics to works consistently.
      control->Focus();
    }
  }

  //------------------------------------------------------------------------------------------------

  void SetStepList(const std::vector<std::string> &steps) {
    SuspendLayout();
    try {
      sidebar->Controls->Clear();

      for each(std::string entry in steps) {
          Label ^ label = gcnew Label();
          label->Text = CppStringToNative(entry.substr(1));
          FontStyle style = FontStyle::Regular;
          switch (entry[0]) {
            case '*': // current task
              label->ForeColor = ForeColor;
              style = FontStyle::Bold;
              break;

            case '.': // executed task
              label->ForeColor = ForeColor;
              break;

            case '-': // open task
              label->ForeColor = SystemInformation::HighContrast ? SystemColors::ControlText : Color::FromArgb(79, 87, 89);
              break;
          }

          try {
            label->Font = gcnew Drawing::Font("Segoe UI", 9.75f, style, GraphicsUnit::Point);
          } catch (System::ArgumentException ^ e) {
            // Argument exception pops up when the system cannot find the Regular font style (corrupt font).
            logError("WizardWrapper::set_step_list setting label font failed. %s\n", e->Message);
          }

          label->BackColor = entry[0] == '*' ? (SystemInformation::HighContrast ? SystemColors::Highlight : Color::FromArgb(221, 235, 228)) : Drawing::Color::Transparent;
          if (entry[0] == '*' && SystemInformation::HighContrast) label->ForeColor = SystemColors::HighlightText;
          label->Padding = System::Windows::Forms::Padding(8, 6, 8, 6);
          label->Margin = System::Windows::Forms::Padding(0, 0, 0, 4);
          int width = System::Math::Max(40, sidebar->Width - sidebar->Padding.Horizontal - SystemInformation::VerticalScrollBarWidth);
          label->Size = Drawing::Size(width, label->GetPreferredSize(Drawing::Size(width, 0)).Height);
          sidebar->Controls->Add(label);
        }
    } finally {
      ResumeLayout();
    }
  }

  //------------------------------------------------------------------------------------------------
};

//----------------- WizardWrapper ------------------------------------------------------------------

WizardWrapper::WizardWrapper(mforms::Wizard *backend, mforms::Form *owner)
  : FormWrapper(backend, owner, mforms::FormDialogFrame) {
}

//--------------------------------------------------------------------------------------------------

bool WizardWrapper::create(mforms::Wizard *backend, mforms::Form *owner) {
  WizardWrapper *wrapper = new WizardWrapper(backend, owner);
  WizardForm ^ form = WizardWrapper::Create<WizardForm>(backend, wrapper);
  form->backend = backend;
  form->SetupLayout();

  return true;
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_title(mforms::Wizard *backend, const std::string &title) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  if (backend->getInternalName() == "script_run_wizard") {
    form->sidebar->Visible = false;
    form->sidebar->Width = 0;
  }
  form->Text = CppStringToNative(title);
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::run_modal(mforms::Wizard *backend) {
  WizardWrapper *wrapper = backend->get_data<WizardWrapper>();
  wrapper->GetManagedObject<Form>()->ShowDialog(wrapper->_owner);
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::close(mforms::Wizard *backend) {
  Form ^ form = WizardWrapper::GetManagedObject<Form>(backend);
  form->Close();
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_content(mforms::Wizard *backend, mforms::View *view) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  if (view == NULL)
    form->SetContent(nullptr);
  else {
    ViewWrapper *wrapper = view->get_data<ViewWrapper>();
    wrapper->set_resize_mode(AutoResizeMode::ResizeNone);
    form->SetContent(wrapper->GetControl());
  }
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_heading(mforms::Wizard *backend, const std::string &heading) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  form->title->Text = CppStringToNative(heading);
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_step_list(mforms::Wizard *backend, const std::vector<std::string> &steps) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  form->SetStepList(steps);
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_allow_cancel(mforms::Wizard *backend, bool flag) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  form->cancelButton->Enabled = flag;
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_allow_back(mforms::Wizard *backend, bool flag) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  form->backButton->Enabled = flag;
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_allow_next(mforms::Wizard *backend, bool flag) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  form->nextButton->Enabled = flag;
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_show_extra(mforms::Wizard *backend, bool flag) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  form->extraButton->Visible = flag;
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_extra_caption(mforms::Wizard *backend, const std::string &caption) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  form->extraButton->Text = CppStringToNative(caption);
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::set_next_caption(mforms::Wizard *backend, const std::string &caption) {
  WizardForm ^ form = WizardWrapper::GetManagedObject<WizardForm>(backend);
  if (caption.empty())
    form->nextButton->Text = CppStringToNative(_("Next"));
  else
    form->nextButton->Text = CppStringToNative(caption)->Replace("_", "&");
}

//--------------------------------------------------------------------------------------------------

void WizardWrapper::init() {
  mforms::ControlFactory *f = mforms::ControlFactory::get_instance();

  f->_wizard_impl.create = &WizardWrapper::create;
  f->_wizard_impl.set_title = &WizardWrapper::set_title;
  f->_wizard_impl.run_modal = &WizardWrapper::run_modal;
  f->_wizard_impl.close = &WizardWrapper::close;
  f->_wizard_impl.set_content = &WizardWrapper::set_content;
  f->_wizard_impl.set_heading = &WizardWrapper::set_heading;
  f->_wizard_impl.set_step_list = &WizardWrapper::set_step_list;
  f->_wizard_impl.set_allow_cancel = &WizardWrapper::set_allow_cancel;
  f->_wizard_impl.set_allow_back = &WizardWrapper::set_allow_back;
  f->_wizard_impl.set_allow_next = &WizardWrapper::set_allow_next;
  f->_wizard_impl.set_show_extra = &WizardWrapper::set_show_extra;
  f->_wizard_impl.set_extra_caption = &WizardWrapper::set_extra_caption;
  f->_wizard_impl.set_next_caption = &WizardWrapper::set_next_caption;
}

//--------------------------------------------------------------------------------------------------
