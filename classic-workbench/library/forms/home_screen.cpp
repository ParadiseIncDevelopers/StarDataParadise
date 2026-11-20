/*
 * Copyright (c) 2009, 2019, Oracle and/or its affiliates. All rights reserved.
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
#include "base/file_utilities.h"
#include "base/threading.h"
#include "base/log.h"

#include "mforms/popup.h"
#include "mforms/menu.h"
#include "mforms/menubar.h"
#include "mforms/utilities.h"
#include "mforms/drawbox.h"
#include "mforms/textentry.h"
#include "mforms/imagebox.h"
#include "mforms/scrollpanel.h"

#include "mforms/home_screen.h"
#include "mforms/home_screen_helpers.h"
#include "mforms/home_screen_connections.h"

#include "base/any.h"
#include "base/accessibility.h"

using namespace base;
using namespace mforms;

//----------------- ShortcutSection ------------------------------------------------------------------------------------

SidebarEntry::SidebarEntry() : owner(nullptr), canSelect(false), icon(nullptr) {
}

//----------------------------------------------------------------------------------------------------------------------

std::string SidebarEntry::getAccessibilityDescription() {
  return title;
}

//----------------------------------------------------------------------------------------------------------------------

Accessible::Role SidebarEntry::getAccessibilityRole() {
  return Accessible::PushButton;
}

//----------------------------------------------------------------------------------------------------------------------

Rect SidebarEntry::getAccessibilityBounds() {
  return acc_bounds;
}

//----------------------------------------------------------------------------------------------------------------------

void SidebarEntry::accessibilityDoDefaultAction() {
  if (owner != nullptr) {
    owner->mouse_move(MouseButtonLeft, (int)acc_bounds.center().x, (int)acc_bounds.center().y);
    owner->mouse_click(MouseButtonLeft, (int)acc_bounds.center().x, (int)acc_bounds.center().y);
  }
}

//----------------- SidebarSection -------------------------------------------------------------------------------------

std::string SidebarEntry::getAccessibilityDefaultAction() {
  return "click";
}

//----------------- SidebarSection -------------------------------------------------------------------------------------

SidebarSection::SidebarSection(HomeScreen *owner) {
  _owner = owner;
  _hotEntry = nullptr;
  _activeEntry = nullptr;
}

//----------------------------------------------------------------------------------------------------------------------

SidebarSection::~SidebarSection() {
  for (auto &it : _entries) {
    deleteSurface(it.first->icon);
    delete it.first;
  }
  _entries.clear();
}

//----------------------------------------------------------------------------------------------------------------------

void SidebarSection::updateColors() {
  _indicatorColor = base::Color::parse(_owner->isDarkModeActive() ? "#66CEB6" : "#098874");
  set_needs_repaint();
}

//----------------------------------------------------------------------------------------------------------------------

void SidebarSection::drawTriangle(cairo_t *cr, int x1, int y1, int x2, int y2, float alpha) {
  cairo_set_source_rgba(cr, _indicatorColor.red, _indicatorColor.green, _indicatorColor.blue, alpha);
  cairo_move_to(cr, x2, y1 + abs(y2 - y1) / 3);
  cairo_line_to(cr, x1 + abs(x2 - x1) * 0.6, y1 + abs(y2 - y1) / 2);
  cairo_line_to(cr, x2, y2 - abs(y2 - y1) / 3);
  cairo_fill(cr);
}

//----------------------------------------------------------------------------------------------------------------------

void SidebarSection::repaint(cairo_t *cr, int areax, int areay, int areaw, int areah) {
  const bool dark = _owner->isDarkModeActive();
  const base::Color ink = base::Color::parse(dark ? "#ECF4F3" : "#102629");
  const base::Color rule = base::Color::parse(dark ? "#405359" : "#A3B4BC");
  const base::Color hover = base::Color::parse(dark ? "#2A3A3E" : "#E1E9EB");
  int left = 12;
  const int height = get_height();
  for (auto &item : _entries) {
    SidebarEntry *entry = item.first;
    cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL,
                           entry == _activeEntry ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, entry->title.c_str(), &extents);
    int width = static_cast<int>(ceil(extents.x_advance)) + 44;
    entry->acc_bounds = base::Rect(left, 0, width, height);
    if (entry == _hotEntry) {
      cairo_set_source_rgb(cr, hover.red, hover.green, hover.blue);
      cairo_rectangle(cr, left, 0, width, height);
      cairo_fill(cr);
    }
    cairo_set_source_rgb(cr, ink.red, ink.green, ink.blue);
    cairo_set_line_width(cr, 1.5);
    // The three glyphs share a compact 18-pixel grid and square line ends.
    double x = left + 10.5, y = (height - 18) / 2.0 + 0.5;
    if (item.second == nullptr) {
      cairo_move_to(cr, x + 7, y + 2);
      cairo_line_to(cr, x + 1, y + 2);
      cairo_line_to(cr, x + 1, y + 16);
      cairo_line_to(cr, x + 7, y + 16);
      cairo_move_to(cr, x + 5, y + 9);
      cairo_line_to(cr, x + 17, y + 9);
      cairo_move_to(cr, x + 12, y + 4);
      cairo_line_to(cr, x + 17, y + 9);
      cairo_line_to(cr, x + 12, y + 14);
    } else if (dynamic_cast<ConnectionsSection *>(item.second) != nullptr) {
      cairo_rectangle(cr, x + 5, y + 5, 8, 7);
      cairo_move_to(cr, x + 7, y + 1); cairo_line_to(cr, x + 7, y + 5);
      cairo_move_to(cr, x + 11, y + 1); cairo_line_to(cr, x + 11, y + 5);
      cairo_move_to(cr, x + 9, y + 12); cairo_line_to(cr, x + 9, y + 17);
    } else {
      cairo_rectangle(cr, x, y + 1, 5, 5);
      cairo_rectangle(cr, x, y + 12, 5, 5);
      cairo_rectangle(cr, x + 12, y + 6, 5, 5);
      cairo_move_to(cr, x + 5, y + 3.5); cairo_line_to(cr, x + 8, y + 3.5);
      cairo_line_to(cr, x + 8, y + 14.5); cairo_line_to(cr, x + 5, y + 14.5);
      cairo_move_to(cr, x + 8, y + 8.5); cairo_line_to(cr, x + 12, y + 8.5);
    }
    cairo_stroke(cr);
    cairo_move_to(cr, left + 34, (height - 18) / 2.0 + 14);
    cairo_show_text(cr, entry->title.c_str());
    if (entry == _activeEntry) {
      cairo_set_source_rgb(cr, _indicatorColor.red, _indicatorColor.green, _indicatorColor.blue);
      cairo_rectangle(cr, left, height - 2, width, 2);
      cairo_fill(cr);
    }
    left += width + 4;
  }
  cairo_set_source_rgb(cr, rule.red, rule.green, rule.blue);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, 0, height - 0.5);
  cairo_line_to(cr, get_width(), height - 0.5);
  cairo_stroke(cr);
  DrawBox::repaint(cr, areax, areay, areaw, areah);
}

//----------------------------------------------------------------------------------------------------------------------

int SidebarSection::shortcutFromPoint(int x, int y) {
  for (size_t index = 0; index < _entries.size(); ++index) {
    if (_entries[index].first->acc_bounds.contains(x, y))
      return static_cast<int>(index);
  }
  return -1;
}

//----------------------------------------------------------------------------------------------------------------------

/**
 * Adds a new sidebar entry to the internal list. The function performs some sanity checks.
 */
void SidebarSection::addEntry(const std::string &title, const std::string &icon_name, HomeScreenSection *section,
                              std::function<void()> callback, bool canSelect) {
  SidebarEntry *entry = new SidebarEntry;

  entry->callback = callback;
  entry->canSelect = canSelect;
  entry->owner = this;
  entry->title = title;
  if (icon_name == "sidebar_wb.png")
    entry->title = _("Connections");
  else if (icon_name == "sidebar_modeling.png")
    entry->title = _("Models");
  else if (icon_name == "sidebar_migration.png")
    entry->title = _("Data Migration");

  entry->icon = mforms::Utilities::load_icon(icon_name, true);
  if (entry->icon == nullptr)
    throw std::runtime_error("Icon not found: " + icon_name);

  _entries.push_back({ entry, section });
  FocusableArea area;
  area.getBounds = [entry]() { return entry->acc_bounds; };
  area.activate = [this, entry]() {
    const base::Point center = entry->acc_bounds.center();
    mouse_click(MouseButtonLeft, static_cast<int>(center.x), static_cast<int>(center.y));
  };
  addFocusableArea(area);
  if (_activeEntry == nullptr && entry->canSelect && section != nullptr) {
    _activeEntry = entry;
    // If this is first entry, we need to show it.
    section->getContainer()->get_parent()->show(true);
  }

  set_layout_dirty(true);
}

//----------------------------------------------------------------------------------------------------------------------

HomeScreenSection *SidebarSection::getActive() {
  if (_activeEntry != nullptr) {
    for (auto &it : _entries) {
      if (it.first == _activeEntry)
        return it.second;
    }
  }

  return nullptr;
}

//----------------------------------------------------------------------------------------------------------------------

void SidebarSection::setActive(HomeScreenSection *section) {
  SidebarEntry *entryForSection = nullptr;
  for (auto &it : _entries) {
    if (it.second == section) {
      if (it.first == _activeEntry)
        return; // Section already active. Nothing to do.

      entryForSection = it.first;
    }
  }

  if (_activeEntry != nullptr) {
    for (auto &it : _entries) {
      if (it.first == _activeEntry)
        if (it.second != nullptr)
          it.second->getContainer()->get_parent()->show(false);
    }
  }

  _activeEntry = entryForSection;
  section->getContainer()->get_parent()->show(true);
  set_needs_repaint();
}

//----------------------------------------------------------------------------------------------------------------------

bool SidebarSection::mouse_click(mforms::MouseButton button, int x, int y) {
  int index = shortcutFromPoint(x, y);
  _hotEntry = index >= 0 ? _entries[index].first : nullptr;
  switch (button) {
    case mforms::MouseButtonLeft: {
      if (_hotEntry != nullptr && _hotEntry->canSelect) {
        _activeEntry = _hotEntry;
        set_needs_repaint();
      }

      if (_hotEntry != nullptr && _hotEntry->callback)
        _hotEntry->callback();
    }
      break;

    default:
      break;
  }
  return false;
}

//----------------------------------------------------------------------------------------------------------------------

bool SidebarSection::mouse_leave() {
  if (_hotEntry != nullptr) {
    _hotEntry = nullptr;
    set_needs_repaint();
    return true;
  }
  return false;
}

//----------------------------------------------------------------------------------------------------------------------

bool SidebarSection::mouse_move(mforms::MouseButton button, int x, int y) {
  SidebarEntry *shortcut = nullptr;
  int row = shortcutFromPoint(x, y);
  if (row > -1)
    shortcut = _entries[row].first;
  if (shortcut != _hotEntry) {
    _hotEntry = shortcut;
    set_needs_repaint();
    return true;
  }
  return false;
}

//----------------------------------------------------------------------------------------------------------------------

size_t SidebarSection::getAccessibilityChildCount() {
  return _entries.size();
}

//----------------------------------------------------------------------------------------------------------------------

Accessible *SidebarSection::getAccessibilityChild(size_t index) {
  Accessible *accessible = nullptr;
  if (index < _entries.size())
    accessible = _entries[index].first;

  return accessible;
}

//----------------------------------------------------------------------------------------------------------------------

Accessible::Role SidebarSection::getAccessibilityRole() {
  return Accessible::List;
}

//----------------------------------------------------------------------------------------------------------------------

Accessible *SidebarSection::accessibilityHitTest(ssize_t x, ssize_t y) {
  Accessible *accessible = nullptr;
  int row = shortcutFromPoint(static_cast<int>(x), static_cast<int>(y));
  if (row != -1)
    accessible = _entries[row].first;

  return accessible;
}

//----------------- HomeScreen -----------------------------------------------------------------------------------------

HomeScreen::HomeScreen() : AppView(false, _("Home"), "home", true), _welcomeHost(false), _darkMode(false) {
  set_name("Home Screen");
  setInternalName("homeScreen");

  _welcomeHost.set_name("Home Screen Welcome Host");
  add(&_welcomeHost, false, true);

  _sidebarSection = new SidebarSection(this);
  _sidebarSection->set_name("homeScreenSideBar");
  _sidebarSection->set_size(-1, 32);
  add(_sidebarSection, false, true);

  Box::scoped_connect(signal_resized(), std::bind(&HomeScreen::on_resize, this));
  NotificationCenter::get()->add_observer(this, "GNColorsChanged");
  NotificationCenter::get()->add_observer(this, "GNBackingScaleChanged");
}

//----------------------------------------------------------------------------------------------------------------------

HomeScreen::~HomeScreen() {
  NotificationCenter::get()->remove_observer(this);
  clear_subviews();  // Remove our sections or the View d-tor will try to release them.

  delete _sidebarSection;
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::setDarkMode(bool dark) {
  _darkModeOverride = true;
  _darkMode = dark;
  updateColors();
}

void HomeScreen::updateColors() {
  if (!_darkModeOverride)
    _darkMode = mforms::App::get()->isDarkModeActive();
  if (_darkMode) {
    set_back_color("#172326");
  } else {
    set_back_color("#EDF2F4");
  }

  _sidebarSection->set_back_color(_darkMode ? "#1D2C30" : "#E7EEEF");

  _sidebarSection->updateColors();

  for (auto section: _sections) {
    section->updateColors();
    section->updateIcons(); // Also refresh icons, even though no resolution change happen (icon colors might change).
  }
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::updateIcons() {
  for (auto section: _sections)
    section->updateIcons();
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::addSection(HomeScreenSection *section) {
  if (section == nullptr)
    throw std::runtime_error("Empty HomeScreenSection given");

  _sections.push_back(section);

  if (auto connections = dynamic_cast<ConnectionsSection *>(section))
    _welcomeHost.add(connections->getWelcomeView(), false, true);

  mforms::ScrollPanel *scroll = mforms::manage(new mforms::ScrollPanel(mforms::ScrollPanelNoAutoScroll));
  scroll->set_name("Home Screen Main Panel");
  scroll->setInternalName("Home Screen Main Panel");
  scroll->add(section->getContainer());
  add(scroll, true, true);

  scroll->show(false);

  bool isCallbackOnly = section->callback ? true : false;
  _sidebarSection->addEntry(section->getTitle(), section->getIcon(), section, [this, isCallbackOnly, section]() {
    if (isCallbackOnly)
      section->callback();
    else {
      for (auto &it : _sections) {
        if (it != section)
          it->getContainer()->get_parent()->show(false);
        else
          it->getContainer()->get_parent()->show(true);
      }
    }
  }, !isCallbackOnly);
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::addSectionEntry(const std::string &title, const std::string &icon_name, std::function<void()> callback,
                                 bool canSelect) {
  _sidebarSection->addEntry(title, icon_name, nullptr, callback, canSelect);
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::trigger_callback(HomeScreenAction action, const any &object) {
  onHomeScreenAction(action, object);
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::cancelOperation() {
  for (auto &it : _sections)
    it->cancelOperation();
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::set_menu(mforms::Menu *menu, HomeScreenMenuType type) {
  switch (type) {
    case HomeMenuConnection:
    case HomeMenuConnectionGroup:
    case HomeMenuConnectionGeneric: {
      for (auto &it : _sections)
        it->setContextMenu(menu, type);
      break;
    }
    case HomeMenuDocumentModelAction: {
      for (auto &it : _sections)
        it->setContextMenuAction(menu, type);
      break;
    }

    case HomeMenuDocumentModel:
      for (auto &it : _sections)
        it->setContextMenu(menu, type);
      break;

    case HomeMenuDocumentSQLAction:
      for (auto &it : _sections)
        it->setContextMenuAction(menu, type);
      break;

    case HomeMenuDocumentSQL:
      for (auto &it : _sections)
        it->setContextMenu(menu, type);
      break;
  }
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::on_resize() {
  // Resize changes the layout so if there is pending script loading the popup is likely misplaced.
  cancelOperation();
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::setup_done() {
  if (_sidebarSection->getActive()) {
    _sidebarSection->getActive()->setFocus();
  }
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::showSection(size_t index) {
  if (ConnectionsSection::opening_section != nullptr) return;
  if (index < _sections.size()) {
    if (_sidebarSection != nullptr) {
      _sidebarSection->setActive(_sections[index]);
      _sidebarSection->getActive()->setFocus();
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------

void HomeScreen::handle_notification(const std::string &name, void *sender, NotificationInfo &info) {
  if (name == "GNColorsChanged") {
    updateColors();
  } else if (name == "GNBackingScaleChanged") {
    updateIcons();
  }
}

//----------------------------------------------------------------------------------------------------------------------

