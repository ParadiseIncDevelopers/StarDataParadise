/*
 * Copyright (c) 2014, 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "mforms/home_screen_connections.h"

#include "mforms/menu.h"
#include "mforms/popup.h"
#include "mforms/imagebox.h"
#include "mforms/app.h"
#include "mforms/scrollpanel.h"
#include "base/string_utilities.h"
#include "base/file_utilities.h"
#include "base/log.h"
#include "base/any.h"

DEFAULT_LOG_DOMAIN("home");

#if defined(__APPLE__) || defined(_WIN64)
#define HAS_MIGRATION_ASSISTANT 1
#endif

using namespace base;
using namespace mforms;

namespace {
constexpr double kConnectionActionWidth = 104;
constexpr double kConnectionButtonWidth = kConnectionActionWidth - 8;

void homeColor(cairo_t *cr, const char *hex) {
  const base::Color color = base::Color::parse(hex);
  cairo_set_source_rgb(cr, color.red, color.green, color.blue);
}
void homeColor(cairo_t *cr, const char *hex, base::SystemColor systemRole) {
  if (base::Color::is_high_contrast_scheme()) {
    const base::Color color = base::Color::getSystemColor(systemRole);
    cairo_set_source_rgb(cr, color.red, color.green, color.blue);
  } else {
    homeColor(cr, hex);
  }
}
void homeText(cairo_t *cr, const std::string &text, double x, double y, double width,
              const char *color, bool mono = false, double size = 13, bool bold = false,
              base::SystemColor systemRole = base::TextColor) {
  cairo_select_font_face(cr, mono ? "Consolas" : "Segoe UI", CAIRO_FONT_SLANT_NORMAL,
                        bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
  homeColor(cr, color, systemRole);
  const auto display = mforms::Utilities::shorten_string(cr, text, std::max(1.0, width));
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, display.c_str());
}
void homeRule(cairo_t *cr, double x, double y, double width, bool dark) {
  homeColor(cr, dark ? "#405359" : "#A3B4BC", base::TextColor);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, x, y + 0.5);
  cairo_line_to(cr, x + width, y + 0.5);
  cairo_stroke(cr);
}
enum class HomeCommandIcon { Add, Edit, Test, Book };
void homeCommandIcon(cairo_t *cr, HomeCommandIcon icon, double x, double y, const char *color) {
  cairo_save(cr);
  homeColor(cr, color, base::TextColor);
  cairo_set_line_width(cr, 1.5);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
  if (icon == HomeCommandIcon::Add) {
    cairo_move_to(cr, x + 8, y + 3); cairo_line_to(cr, x + 8, y + 13);
    cairo_move_to(cr, x + 3, y + 8); cairo_line_to(cr, x + 13, y + 8);
  } else if (icon == HomeCommandIcon::Edit) {
    cairo_move_to(cr, x + 3, y + 10); cairo_line_to(cr, x + 10, y + 3);
    cairo_line_to(cr, x + 13, y + 6); cairo_line_to(cr, x + 6, y + 13);
    cairo_line_to(cr, x + 2, y + 14); cairo_close_path(cr);
    cairo_move_to(cr, x + 8, y + 5); cairo_line_to(cr, x + 11, y + 8);
  } else if (icon == HomeCommandIcon::Test) {
    cairo_move_to(cr, x + 4, y + 3); cairo_line_to(cr, x + 13, y + 8);
    cairo_line_to(cr, x + 4, y + 13); cairo_close_path(cr);
  } else {
    cairo_move_to(cr, x + 8, y + 4); cairo_line_to(cr, x + 3, y + 2);
    cairo_line_to(cr, x + 1, y + 2); cairo_line_to(cr, x + 1, y + 12);
    cairo_line_to(cr, x + 3, y + 12); cairo_line_to(cr, x + 8, y + 14);
    cairo_line_to(cr, x + 13, y + 12); cairo_line_to(cr, x + 15, y + 12);
    cairo_line_to(cr, x + 15, y + 2); cairo_line_to(cr, x + 13, y + 2);
    cairo_line_to(cr, x + 8, y + 4); cairo_line_to(cr, x + 8, y + 14);
  }
  cairo_stroke(cr);
  cairo_restore(cr);
}
void profileAvatar(cairo_t *cr, double x, double y, int variant) {
  static const char *mint[] = {
    "       22 ", "       12 ", "       1  ", " 1222221  ", "122333221 ",
    "1231113211", "1231413211", "1231413211", "122333221 ", " 1222221  "
  };
  static const char *violet[] = {
    "1       1 ", "12     21 ", "123222321 ", "122333221 ", "1231113211",
    "1231413211", "1231413211", "122333221 ", " 1222221  ", "  11111   "
  };
  const char **pixels = variant % 2 == 0 ? mint : violet;
  const char *colors[] = {"#173735", "#173735", variant % 2 == 0 ? "#72C89B" : "#A481CC",
                         variant % 2 == 0 ? "#B6F1C5" : "#D1B6EF", "#F4FFF8"};
  cairo_save(cr);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
  for (int row = 0; row < 10; ++row) {
    for (int column = 0; column < 10; ++column) {
      const char pixel = pixels[row][column];
      if (pixel >= '1' && pixel <= '4') {
        homeColor(cr, colors[pixel - '0']);
        cairo_rectangle(cr, x + column * 2, y + row * 2, 2, 2);
        cairo_fill(cr);
      }
    }
  }
  cairo_restore(cr);
}
}


std::string mforms::connection_avatar_icon(int variant) {
  const auto path = base::makePath(mforms::App::get()->get_user_data_folder(),
    variant % 2 == 0 ? "classic-avatar-mint.png" : "classic-avatar-violet.png");
  if (!base::file_exists(path)) {
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 20, 20);
    auto cr = cairo_create(surface);
    profileAvatar(cr, 0, 0, variant);
    cairo_surface_write_to_png(surface, path.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
  }
  return path;
}

//----------------- ConnectionsSection ---------------------------------------------------------------------------------

class mforms::ConnectionEntry : public base::Accessible {
  friend class ConnectionsSection;

private:
  //cairo_surface_t *_mbNewSticker;
  //bool _showNewSticker = false;
  cairo_surface_t *_migrationButton;
  base::Rect _migrationButtonBounds;

public:
  std::string connectionId;

protected:
  ConnectionsSection *owner;

  std::string title;
  std::string description;
  std::string user;
  std::string schema;
  std::string environment;
  std::string connectionMethod = "TCP/IP";
  int avatarVariant = 0;
  bool compute_strings; // True after creation to indicate the need to compute the final display strings.
  bool draw_info_tab;

  // For filtering we need the full strings.
  std::string search_title;
  std::string search_description;
  std::string search_user;
  std::string search_schema;

  base::Rect bounds;

  //------ Accessibility Methods ---------------------------------------------------------------------------------------

  virtual std::string getAccessibilityDescription() override {
    return title;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual std::string getAccessibilityTitle() override {
    return title;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual std::string getAccessibilityValue() override {
    std::string result = "host: " + description;
    if (!schema.empty())
      result += ", schema: " + schema;
    if (!user.empty())
      result += ", user: " + user;
    return result;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual Accessible::Role getAccessibilityRole() override {
    return Accessible::PushButton;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual base::Rect getAccessibilityBounds() override {
    return bounds;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void accessibilityDoDefaultAction() override {
    activate();
  };

  //--------------------------------------------------------------------------------------------------------------------

  virtual std::string getAccessibilityDefaultAction() override {
    return "click";
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual bool accessibilityGrabFocus() override {
    owner->setFocusOnEntry(this);
    return true;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void accessibilityShowMenu() override {
    if (owner->_connection_context_menu != nullptr) {
      owner->_connection_context_menu->popup_at(owner, static_cast<int>(bounds.xcenter()),
        static_cast<int>(bounds.ycenter()));
    }
  };

  //--------------------------------------------------------------------------------------------------------------------

  base::Rect getMigrationButtonBounds() {
    return _migrationButtonBounds;
  }

  //--------------------------------------------------------------------------------------------------------------------

  /**
   * Draws the icon followed by the given text. The given position is that of the upper left corner
   * of the image.
   */
  void draw_icon_with_text(cairo_t *cr, double x, double y, cairo_surface_t *icon, const std::string &text,
                           double alpha) {
    if (icon) {
      mforms::Utilities::paint_icon(cr, icon, x, y);
      x += imageWidth(icon) + 3;
    }

    base::Color titleColor = getTitleColor();
    cairo_set_source_rgba(cr, titleColor.red, titleColor.green, titleColor.blue, titleColor.alpha);

    std::vector<std::string> texts = base::split(text, "\n");

    for (size_t index = 0; index < texts.size(); index++) {
      cairo_text_extents_t extents;
      std::string line = texts[index];
      cairo_text_extents(cr, line.c_str(), &extents);

      cairo_move_to(cr, x, (int)(y + imageHeight(icon) / 2.0 + extents.height / 2.0 + (index * (extents.height + 3))));
      cairo_show_text(cr, line.c_str());
      cairo_stroke(cr);
    }
  }

  //--------------------------------------------------------------------------------------------------------------------

public:
  enum ItemPosition { First, Last, Other };

  //--------------------------------------------------------------------------------------------------------------------

  ConnectionEntry(ConnectionsSection *aowner) : owner(aowner), compute_strings(false) {
    draw_info_tab = true;
#ifdef HAS_MIGRATION_ASSISTANT
    _migrationButton = Utilities::load_icon("migration_btn.png", true);
#else
    _migrationButton = nullptr;
#endif
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual ~ConnectionEntry() {
    if (_migrationButton)
      deleteSurface(_migrationButton);
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual bool is_movable() const {
    return true;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual base::Color getTitleColor() const {
    return owner->_titleColor;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual base::Color getBackgroundColor(bool hot) const {
    return hot ? owner->_backgroundColorHot : owner->_backgroundColor;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual cairo_surface_t *get_background_icon() const {
    return owner->_sakila_icon;
  }

  //--------------------------------------------------------------------------------------------------------------------

  void draw_tile_background(cairo_t *cr, bool hot, double alpha, bool for_dragging) const {
    base::Color backColor = getBackgroundColor(hot);

    base::Rect bounds = this->bounds;
    if (for_dragging)
      bounds.pos = base::Point(0, 0);

    bounds.use_inter_pixel = false;
    cairo_rectangle(cr, bounds.left(), bounds.top(), bounds.width(), bounds.height());
    cairo_set_source_rgba(cr, backColor.red, backColor.green, backColor.blue, alpha);
    cairo_fill(cr);

    // Border.
    bounds.use_inter_pixel = true;
    cairo_rectangle(cr, bounds.left(), bounds.top(), bounds.width() - 1, bounds.height() - 1);

    if (owner->_owner->isDarkModeActive())
      cairo_set_source_rgba(cr, backColor.red + 0.1, backColor.green + 0.1, backColor.blue + 0.1, alpha);
    else
      cairo_set_source_rgba(cr, backColor.red - 0.05, backColor.green - 0.05, backColor.blue - 0.05, alpha);

    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    float image_alpha = 0.25;

    // Background icon.
    bounds.use_inter_pixel = false;
    cairo_surface_t *back_icon = get_background_icon();

    double x = bounds.left() + bounds.width() - imageWidth(back_icon);
    double y = bounds.top() + bounds.height() - imageHeight(back_icon);
    cairo_set_source_surface(cr, back_icon, x, y);
    cairo_paint_with_alpha(cr, image_alpha * alpha);
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void draw_tile(cairo_t *cr, bool hot, double alpha, bool for_dragging, bool on_migration = false) {
    base::Rect row = bounds;
    if (for_dragging)
      row.pos = base::Point(0, 0);
    const bool dark = owner->_owner->isDarkModeActive();
    const bool selected = owner->_selectedEntry.get() == this;
    const auto rowTextRole = selected ? base::SelectedControlTextColor : base::TextColor;
    const char *ink = dark ? "#ECF4F3" : "#102629";
    homeColor(cr, selected ? (dark ? "#234238" : "#DDEBE4") :
              hot ? (dark ? "#2A3A3E" : "#E1E9EB") : (dark ? "#172326" : "#EDF2F4"),
              selected ? base::SelectedControlColor : base::WindowBackgroundColor);
    cairo_rectangle(cr, row.left(), row.top(), row.width(), row.height());
    cairo_fill(cr);
    const bool narrow = row.width() < 696;
    const bool showEnvironment = row.width() >= 936;
    const double usable = std::max(200.0, row.width() - kConnectionActionWidth - (showEnvironment ? 80 : 0));
    const double profileWidth = usable * (narrow ? 0.48 : 0.30);
    const double userWidth = narrow ? 0 : usable * 0.28;
    const double hostX = row.left() + profileWidth + userWidth;
    const double baseline = row.top() + 21;
    if (!connectionId.empty()) {
      profileAvatar(cr, row.left() + 8, row.top() + (row.height() - 20) / 2, avatarVariant);
    } else {
      homeText(cr, is_movable() ? "+" : "<", row.left() + 10, baseline, 20, ink, false, 16, false, rowTextRole);
    }
    homeText(cr, title, row.left() + 36, narrow ? row.top() + 17 : baseline,
             profileWidth - 44, ink, true, 13, false, rowTextRole);
    if (connectionId.empty()) {
      homeText(cr, is_movable() ? "Ba\xc4\x9f""lant\xc4\xb1"" grubu" : "\xc3\x9c""st klas\xc3\xb6""re d\xc3\xb6""n", hostX + 8, baseline,
               row.width() - profileWidth - 100, ink, false, 13, false, rowTextRole);
    } else {
      homeText(cr, user, narrow ? row.left() + 36 : row.left() + profileWidth + 8,
               narrow ? row.top() + 35 : baseline, narrow ? profileWidth - 44 : userWidth - 16,
               narrow ? (dark ? "#B7C7C8" : "#4F5759") : ink, true, 13, false, rowTextRole);
      homeText(cr, description, hostX + 8, baseline, usable - profileWidth - userWidth - 16, ink, true, 13, false, rowTextRole);
      if (showEnvironment)
        homeText(cr, environment.empty() ? "\xe2\x80\x94""" : environment, row.left() + usable + 8, baseline, 64, ink, false, 13, false, rowTextRole);
    }
    _migrationButtonBounds = base::Rect();
    homeRule(cr, row.left(), row.bottom() - 1, row.width(), dark);
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void draw_tile_text(cairo_t *cr, double x, double y, double alpha) {
    if (compute_strings) {
      double available_width = bounds.width() - 24 - imageWidth(owner->_network_icon);
      description = mforms::Utilities::shorten_string(cr, description, available_width);

      available_width =
        bounds.center().x - x - imageWidth(owner->_user_icon) - 6; // -6 is the spacing between text and icons.
      user = mforms::Utilities::shorten_string(cr, user, available_width);

      schema = mforms::Utilities::shorten_string(cr, schema, available_width);
    }

    y = bounds.top() + 56 - imageHeight(owner->_user_icon);
    draw_icon_with_text(cr, x, y, owner->_user_icon, user, alpha);

    y = bounds.top() + 74 - imageHeight(owner->_network_icon);
    draw_icon_with_text(cr, x, y, owner->_network_icon, description, alpha);
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void activate(int x = -1, int y = -1) {
    if (x > 0 && y > 0 && _migrationButtonBounds.contains(x, y)) {
      owner->_owner->trigger_callback(HomeScreenAction::ActionOpenMigrationAssistant, connectionId);
    } else {
      owner->_owner->trigger_callback(HomeScreenAction::ActionOpenConnectionFromList, connectionId);
    }
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual mforms::Menu *context_menu() const {
    return owner->_connection_context_menu;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void menu_open(ItemPosition pos) const {
    mforms::Menu *menu = context_menu();

    menu->set_item_enabled(menu->get_item_index("edit_connection"), true);
    menu->set_item_enabled(menu->get_item_index("move_connection_to_group"), true);
    menu->set_item_enabled(menu->get_item_index("delete_connection"), true);
    menu->set_item_enabled(menu->get_item_index("delete_connection_all"), true);

    menu->set_item_enabled(menu->get_item_index("move_connection_to_top"), pos != First);
    menu->set_item_enabled(menu->get_item_index("move_connection_up"), pos != First);
    menu->set_item_enabled(menu->get_item_index("move_connection_down"), pos != Last);
    menu->set_item_enabled(menu->get_item_index("move_connection_to_end"), pos != Last);
  }

  //--------------------------------------------------------------------------------------------------------------------

};

//----------------------------------------------------------------------------------------------------------------------

class mforms::FolderEntry : public ConnectionEntry, public std::enable_shared_from_this<mforms::FolderEntry> {
protected:

  //--------------------------------------------------------------------------------------------------------------------

  virtual std::string getAccessibilityDescription() override {
    return "Connection Group";
  }

  virtual std::string getAccessibilityTitle() override {
    return title;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void accessibilityShowMenu() override {
    if (owner->_folder_context_menu != nullptr) {
      owner->_folder_context_menu->popup_at(owner, static_cast<int>(bounds.xcenter()),
        static_cast<int>(bounds.ycenter()));
    }
  };

  //--------------------------------------------------------------------------------------------------------------------

public:
  std::vector<std::shared_ptr<ConnectionEntry> > children;

  //--------------------------------------------------------------------------------------------------------------------

  FolderEntry(ConnectionsSection *aowner) : ConnectionEntry(aowner) {
    draw_info_tab = false;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void draw_tile_text(cairo_t *cr, double x, double y, double alpha) override {
    base::Color titleColor = getTitleColor();
    cairo_set_source_rgba(cr, titleColor.red, titleColor.green, titleColor.blue, titleColor.alpha);

    std::string info = std::to_string(children.size() - 1) + " " + _("Connections");
    y = bounds.top() + 55;
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, info.c_str());
    cairo_stroke(cr);
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual mforms::Menu *context_menu() const override {
    return owner->_folder_context_menu;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void menu_open(ItemPosition pos) const override {
    mforms::Menu *menu = context_menu();

    menu->set_item_enabled(menu->get_item_index("move_connection_to_top"), pos != First);
    menu->set_item_enabled(menu->get_item_index("move_connection_up"), pos != First);
    menu->set_item_enabled(menu->get_item_index("move_connection_down"), pos != Last);
    menu->set_item_enabled(menu->get_item_index("move_connection_to_end"), pos != Last);
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void activate(int, int) override {
    owner->change_to_folder(shared_from_this());
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual base::Color getTitleColor() const override {
    return owner->_folderTitleColor;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual base::Color getBackgroundColor(bool hot) const override {
    return hot ? owner->_folderBackgroundColorHot : owner->_folderBackgroundColor;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual cairo_surface_t *get_background_icon() const override {
    return owner->_folder_icon;
  }

  //--------------------------------------------------------------------------------------------------------------------

};

class mforms::FolderBackEntry : public ConnectionEntry {
protected:

  //--------------------------------------------------------------------------------------------------------------------

  virtual void accessibilityShowMenu() override {
  };

  //--------------------------------------------------------------------------------------------------------------------

  virtual std::string getAccessibilityDescription() override {
      return "Back";
  }

  //--------------------------------------------------------------------------------------------------------------------

public:

  FolderBackEntry(ConnectionsSection *aowner) : ConnectionEntry(aowner) {
    title = "Geri";
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual bool is_movable() const override {
    return false;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual base::Color getTitleColor() const override {
    return owner->_folderTitleColor;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual base::Color getBackgroundColor(bool hot) const override {
    return hot ? owner->_backTileBackgroundColorHot : owner->_backTileBackgroundColor;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual cairo_surface_t *get_background_icon() const override {
    return owner->_folder_icon;
  }

  //--------------------------------------------------------------------------------------------------------------------

  /**
   * Separate tile drawing for the special back tile (to return from a folder).
   */
  virtual void draw_tile(cairo_t *cr, bool hot, double alpha, bool for_dragging, bool for_migration = false) override {
    ConnectionEntry::draw_tile(cr, hot, alpha, for_dragging, for_migration);
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual mforms::Menu *context_menu() const override {
    return nullptr;
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void menu_open(ItemPosition pos) const override {
  }

  //--------------------------------------------------------------------------------------------------------------------

  virtual void activate(int, int) override {
    owner->change_to_folder(std::shared_ptr<FolderEntry>());
  }

  //--------------------------------------------------------------------------------------------------------------------

};

//----------------- ConnectionsWelcomeScreen ---------------------------------------------------------------------------

ConnectionsWelcomeScreen::ConnectionsWelcomeScreen(HomeScreen *owner) : _owner(owner), _closeIcon(nullptr) {
  _closeHomeScreenButton.title = "Kar\xc5\x9f\xc4\xb1""lamay\xc4\xb1"" daralt";
  _closeHomeScreenButton.description = _closeHomeScreenButton.title;
  _closeHomeScreenButton.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::CloseWelcomeMessage, base::any());
  };
  _browseDocButton.title = "H\xc4\xb1""zl\xc4\xb1"" ba\xc5\x9f""lang\xc4\xb1\xc3\xa7""";
  _browseDocButton.description = _browseDocButton.title;
  _browseDocButton.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::ActionOpenDocs, base::any());
  };
  _heading = "StarDataParadise\xe2\x80\x99""a ho\xc5\x9f"" geldiniz";
  _content = {"Bir sunucuya ba\xc4\x9f""lan\xc4\xb1""n, \xc5\x9f""eman\xc4\xb1""z\xc4\xb1"" tasarlay\xc4\xb1""n veya verilerinizi ta\xc5\x9f\xc4\xb1""y\xc4\xb1""n."};
  for (auto button : {&_browseDocButton, &_closeHomeScreenButton}) {
    FocusableArea area;
    area.activate = button->defaultHandler;
    area.getBounds = [button]() { return button->bounds; };
    addFocusableArea(area);
  }
}

//----------------------------------------------------------------------------------------------------------------------

ConnectionsWelcomeScreen::~ConnectionsWelcomeScreen() {
  deleteSurface(_closeIcon);
}

//----------------------------------------------------------------------------------------------------------------------

base::Size ConnectionsWelcomeScreen::getLayoutSize(base::Size proposedSize) {
  _totalHeight = proposedSize.width < 720 ? 80 : 58;
  return base::Size(proposedSize.width, _totalHeight);
}

//----------------------------------------------------------------------------------------------------------------------

Accessible::Role ConnectionsWelcomeScreen::getAccessibilityRole() {
  return base::Accessible::Pane;
}

//----------------------------------------------------------------------------------------------------------------------

std::string ConnectionsWelcomeScreen::getAccessibilityTitle() {
  return _heading;
}

//----------------------------------------------------------------------------------------------------------------------

std::string ConnectionsWelcomeScreen::getAccessibilityDescription() {
  return "Home Screen Welcome Page";
}

//----------------------------------------------------------------------------------------------------------------------

std::string ConnectionsWelcomeScreen::getAccessibilityValue() {
  std::string result;
  for (auto &line : _content)
    result += line + "\n";
  return result;
}

//----------------------------------------------------------------------------------------------------------------------

size_t ConnectionsWelcomeScreen::getAccessibilityChildCount() { return 2; }

//----------------------------------------------------------------------------------------------------------------------

Accessible* ConnectionsWelcomeScreen::getAccessibilityChild(size_t index) {
  if (index == 0) return &_browseDocButton;
  if (index == 1) return &_closeHomeScreenButton;
  return nullptr;
}

//----------------------------------------------------------------------------------------------------------------------

base::Rect ConnectionsWelcomeScreen::getAccessibilityBounds() {
  return base::Rect(0, 0, get_width(), get_height());
}

//----------------------------------------------------------------------------------------------------------------------

Accessible* ConnectionsWelcomeScreen::accessibilityHitTest(ssize_t x, ssize_t y) {
  for (auto button : {&_browseDocButton, &_closeHomeScreenButton})
    if (button->bounds.contains(static_cast<double>(x), static_cast<double>(y))) return button;
  return nullptr;
}

//----------------------------------------------------------------------------------------------------------------------

void ConnectionsWelcomeScreen::repaint(cairo_t *cr, int areax, int areay, int areaw, int areah) {
  const bool dark = _owner->isDarkModeActive();
  const bool narrow = get_width() < 720;
  homeColor(cr, dark ? "#1D2C30" : "#E7EEEF", base::WindowBackgroundColor);
  cairo_paint(cr);
  const char *ink = dark ? "#ECF4F3" : "#102629";
  homeText(cr, _heading, 12, 25, get_width() - 52, ink, false, 18, true);
  homeText(cr, _content[0], 12, 46, get_width() - (narrow ? 24 : 188), dark ? "#B7C7C8" : "#4F5759");
  const double linkX = narrow ? 12 : get_width() - 166;
  const double linkY = narrow ? 70 : 45;
  const char *accent = dark ? "#8FE4C5" : "#0B5F41";
  homeCommandIcon(cr, HomeCommandIcon::Book, linkX, linkY - 13, accent);
  homeText(cr, _browseDocButton.title, linkX + 22, linkY, 110, accent);
  _browseDocButton.bounds = base::Rect(linkX - 2, linkY - 18, 136, 26);
  _closeHomeScreenButton.bounds = base::Rect(get_width() - 34, 8, 26, 26);
  homeColor(cr, ink, base::TextColor);
  cairo_set_line_width(cr, 1.5);
  cairo_move_to(cr, get_width() - 27, 23);
  cairo_line_to(cr, get_width() - 21, 17);
  cairo_line_to(cr, get_width() - 15, 23);
  cairo_stroke(cr);
  homeRule(cr, 0, get_height() - 1, get_width(), dark);
  DrawBox::repaint(cr, areax, areay, areaw, areah);
}

//----------------------------------------------------------------------------------------------------------------------

void ConnectionsWelcomeScreen::updateColors() {
  if (_owner->isDarkModeActive()) {
    _textColor = base::Color::parse("#F4F4F4");
  } else {
    _textColor = base::Color::parse("#505050");
  }
}

//----------------------------------------------------------------------------------------------------------------------

void ConnectionsWelcomeScreen::updateIcons() {
  cairo_surface_destroy(_closeIcon);
  if (_owner->isDarkModeActive())
    _closeIcon = Utilities::load_icon("home_screen_close_dark.png", true);
  else
    _closeIcon = Utilities::load_icon("home_screen_close_light.png", true);
}

//----------------------------------------------------------------------------------------------------------------------

bool ConnectionsWelcomeScreen::mouse_click(mforms::MouseButton button, int x, int y) {
  if (button == MouseButtonLeft) {
    HomeAccessibleButton *button = dynamic_cast<HomeAccessibleButton *>(accessibilityHitTest(x, y));
    if (button != nullptr) {
      button->accessibilityDoDefaultAction();
      return true;
    }
  }
  return false;
}

//----------------- ConnectionsMigrationBanner ---------------------------------------------------------------------------

ConnectionsMigrationBanner::ConnectionsMigrationBanner(HomeScreen *owner) : _owner(owner) {
  logDebug("Creating Connections Migration Assistant Banner\n");  \

  _closeMigrationAssistantBannerButton.title = "Close MigrationAssistant Screen";
  _closeMigrationAssistantBannerButton.description = "Close MigrationAssistant Screen";
  _closeMigrationAssistantBannerButton.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::CloseMigrationAssistantBannerMessage, base::any());
  };

  _migrationLearnMoreButton.title = "Learn more >";
  _migrationLearnMoreButton.description = "Open Migration Doc";
  _migrationLearnMoreButton.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::ActionOpenMigrationDoc, base::any());
  };

  _closeIcon = nullptr;
  _migrationBanner = nullptr;

  // Load newsticker if lock file does not exist
  //std::string userdir = mforms::App::get()->get_user_data_folder();
  //std::string lockfile = userdir + "/ma_lock_fisrt.lck";
  //FILE *f = fopen(lockfile.c_str(), "r");
  //if (!f) {
  //  _mbNewSticker = Utilities::load_icon("newsticker.png", true);
  //  _showNewSticker = true;
  //} else {
  //  _mbNewSticker = nullptr;
  //  _showNewSticker = false;
  //  fclose(f);
  //}
}

//----------------------------------------------------------------------------------------------------------------------

Accessible *ConnectionsMigrationBanner::accessibilityHitTest(ssize_t x, ssize_t y) {
  if (_closeMigrationAssistantBannerButton.bounds.contains(static_cast<double>(x), static_cast<double>(y))) {
    return &_closeMigrationAssistantBannerButton;
  }
  if (_migrationLearnMoreButton.bounds.contains(static_cast<double>(x), static_cast<double>(y))) {
    return &_migrationLearnMoreButton;
  }

  return nullptr;
}

//----------------------------------------------------------------------------------------------------------------------

ConnectionsMigrationBanner::~ConnectionsMigrationBanner() {
  deleteSurface(_closeIcon);
  deleteSurface(_migrationBanner);
  //deleteSurface(_mbNewSticker);
}

//----------------------------------------------------------------------------------------------------------------------

base::Size ConnectionsMigrationBanner::getLayoutSize(base::Size proposedSize) {
  return base::Size(proposedSize.width, _totalHeight); // Height doesn't change. Constant content.
}

//----------------------------------------------------------------------------------------------------------------------

void ConnectionsMigrationBanner::repaint(cairo_t *cr, int areax, int areay, int areaw, int areah) {
  Size size = Utilities::getImageSize(_closeIcon);
  _closeMigrationAssistantBannerButton.bounds = base::Rect(get_width() - size.width - 8, 8, size.width, size.height);

  cairo_save(cr);
  mforms::Utilities::paint_icon(cr, _closeIcon, _closeMigrationAssistantBannerButton.bounds.left(),
                                _closeMigrationAssistantBannerButton.bounds.top());

  int yoffset = 100;

  // draw migration assistant banner, scaled and centered
  size = Utilities::getImageSize(_migrationBanner);
  double banner_scale = 0.6;
  double banner_width = size.width * banner_scale;
  double banner_height = size.height * banner_scale;
  double x = (get_width() - banner_width) / 2.0;
  cairo_save(cr);
  cairo_translate(cr, floor(x), floor(yoffset));
  cairo_scale(cr, banner_scale, banner_scale);
  cairo_set_source_surface(cr, _migrationBanner, 0, 0);
  cairo_paint(cr);
  // Place _migrationLearnMoreButton as a dummy clickable area (no text) in bottom right corner of banner with 15px
  // margin
  double btn_width = banner_width * 0.20;
  double btn_height = banner_height * 0.25;
  double btn_x = floor(x + banner_width - btn_width);
  double btn_y = floor(yoffset + banner_height - btn_height);
  _migrationLearnMoreButton.bounds = base::Rect(btn_x, btn_y, btn_width - 15, btn_height - 15);

  // Draw newsticker in the top right corner (centered on corner)
  //if (_showNewSticker && _mbNewSticker) {
  //  Size sticker_size = Utilities::getImageSize(_mbNewSticker);
  //  double sticker_x = x + banner_width;
  //  double sticker_y = -sticker_size.height / 2.0;
  //  cairo_save(cr);
  //  cairo_set_source_surface(cr, _mbNewSticker, sticker_x, sticker_y);
  //  cairo_paint(cr);
  //}

  _totalHeight = yoffset + (int)banner_height + 20;

  cairo_restore(cr);
}

//----------------------------------------------------------------------------------------------------------------------

void ConnectionsMigrationBanner::updateIcons() {
  cairo_surface_destroy(_closeIcon);
  if (_owner->isDarkModeActive())
    _closeIcon = Utilities::load_icon("home_screen_close_dark.png", true);
  else
    _closeIcon = Utilities::load_icon("home_screen_close_light.png", true);

  cairo_surface_destroy(_migrationBanner);
  _migrationBanner = Utilities::load_icon("migration_banner.png", true);

  //cairo_surface_destroy(_mbNewSticker);
  //_mbNewSticker = Utilities::load_icon("new_sticker.png", true);
}

//----------------------------------------------------------------------------------------------------------------------

bool ConnectionsMigrationBanner::mouse_click(mforms::MouseButton button, int x, int y) {
  if (button == MouseButtonLeft) {
    HomeAccessibleButton *button = dynamic_cast<HomeAccessibleButton *>(accessibilityHitTest(x, y));
    if (button != nullptr) {
      button->accessibilityDoDefaultAction();
      return true;
    }
  }
  return false;
}

//------------------ ConnectionsSection --------------------------------------------------------------------------------

ConnectionsSection::ConnectionsSection(HomeScreen *owner)
  : HomeScreenSection("sidebar_wb.png"),
    _search_box(true),
    _search_text(mforms::SmallSearchEntry),
    _showWelcomeHeading(true), 
    _showMigrationAssistantBanner(true) {
  _owner = owner;
  _welcomeScreen = nullptr;
  _migrationAssistantBanner = nullptr;
  _container = nullptr;
  _connection_context_menu = nullptr;
  _folder_context_menu = nullptr;
  _generic_context_menu = nullptr;
  _drag_index = -1;
  _drop_index = -1;
  _filtered = false;

  _folder_icon = nullptr;
  _network_icon = nullptr;
  _plus_icon = nullptr;
  _sakila_icon = nullptr;

  _user_icon = nullptr;
  _manage_icon = nullptr;

  std::vector<std::string> formats;
  formats.push_back(mforms::HomeScreenSettings::TILE_DRAG_FORMAT); // We allow dragging tiles to reorder them.
  formats.push_back(mforms::DragFormatFileName);                   // We accept sql script files to open them.
  register_drop_formats(this, formats);

  _search_box.set_name("Connection Search Box");

  _search_box.set_spacing(5);
  _search_text.set_size(100, -1);
  // These are native child windows over a Cairo canvas. Give each an opaque
  // surface so native background painting does not have to replay the canvas.
  const std::string searchSurface = base::Color::is_high_contrast_scheme()
    ? base::Color::getSystemColor(base::WindowBackgroundColor).to_html()
    : owner->isDarkModeActive() ? "#172326" : "#EDF2F4";
  set_back_color(searchSurface);
  _search_box.set_back_color(searchSurface);
  _search_text.set_back_color(searchSurface);

#ifdef _MSC_VER
  _search_text.set_bordered(false);
  _search_text.set_size(-1, 26);
  _search_text.set_font(mforms::HomeScreenSettings::HOME_NORMAL_FONT);
  _search_box.set_size(280, 26);
#else
  _search_box.set_padding(8, 1, 8, 5);
  _search_box.set_size(160, 25);
#endif

#ifdef _MSC_VER
  mforms::ImageBox *image = mforms::manage(new mforms::ImageBox, false);
  image->set_image("search_sidebar.png");
  image->set_image_align(mforms::MiddleCenter);
  _search_box.add(image, false, false);
#endif
  _search_text.set_name("Search Text");
  _search_text.set_placeholder_text("Ba\xc4\x9f""lant\xc4\xb1"" ara");
  _search_text.set_bordered(false);
  _search_box.add(&_search_text, true, true);
  scoped_connect(_search_text.signal_changed(), std::bind(&ConnectionsSection::on_search_text_changed, this));
  scoped_connect(_search_text.signal_action(),
                 std::bind(&ConnectionsSection::on_search_text_action, this, std::placeholders::_1));
  add(&_search_box, mforms::TopLeft);

  set_padding(12, 4, 12, 0);
  scoped_connect(signal_resized(), [this]() {
    const int width = std::max(140, std::min(360, get_width() - 330));
    if (_search_box.get_width() != width || _search_box.get_height() != 26)
      _search_box.set_size(width, 26);
  });

  _add_button.title = "Yeni ba\xc4\x9f""lant\xc4\xb1""";
  _add_button.description = "Yeni ba\xc4\x9f""lant\xc4\xb1""";
  _add_button.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::ActionNewConnection, base::any());
  };

  _manage_button.title = "Ba\xc4\x9f""lant\xc4\xb1""lar\xc4\xb1"" y\xc3\xb6""net";
  _manage_button.description = "Ba\xc4\x9f""lant\xc4\xb1""lar\xc4\xb1"" y\xc3\xb6""net";
  _manage_button.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::ActionManageConnections, base::any());
  };

  _rescanButton.title = "Yerel sunucular\xc4\xb1"" tara";
  _rescanButton.description = "Yerel sunucular\xc4\xb1"" tara";
  _rescanButton.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::RescanLocalServers, base::any());
  };
  _on_migration = false;
  _connectButton.title = "Ba\xc4\x9f""lan";
  _connectButton.description = "Se\xc3\xa7""ili ba\xc4\x9f""lant\xc4\xb1""y\xc4\xb1"" a\xc3\xa7""";
  _connectButton.defaultHandler = [this]() { openSelectedConnection(); };
  _editButton.title = "Profili d\xc3\xbc""zenle";
  _editButton.description = _editButton.title;
  _editButton.defaultHandler = [this]() {
    if (_selectedEntry && !_selectedEntry->connectionId.empty())
      _owner->handleContextMenu(_selectedEntry->connectionId, "edit_connection");
  };
  _testButton.title = "Ba\xc4\x9f""lant\xc4\xb1""y\xc4\xb1"" s\xc4\xb1""na";
  _testButton.description = _testButton.title;
  _testButton.defaultHandler = [this]() {
    if (_selectedEntry && !_selectedEntry->connectionId.empty())
      _owner->handleContextMenu(_selectedEntry->connectionId, "test_connection");
  };
  _clearButton.title = "Aramay\xc4\xb1"" temizle";
  _clearButton.description = _clearButton.title;
  _clearButton.defaultHandler = [this]() { _search_text.set_value(""); };

}

//------------------------------------------------------------------------------------------------

ConnectionsSection::~ConnectionsSection() {
  if (_connection_context_menu != NULL)
    _connection_context_menu->release();
  if (_folder_context_menu != NULL)
    _folder_context_menu->release();
  if (_generic_context_menu != NULL)
    _generic_context_menu->release();

  deleteSurface(_folder_icon);
  deleteSurface(_network_icon);
  deleteSurface(_plus_icon);
  deleteSurface(_sakila_icon);
  deleteSurface(_user_icon);
  deleteSurface(_manage_icon);
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::updateColors() {
  if (_owner->isDarkModeActive()) {
    _titleColor = base::Color::parse("#F4F4F4");
    _folderTitleColor = base::Color::parse("#F0F0F0");
    _backgroundColor = base::Color::parse("#505050");
    _backgroundColorHot = base::Color::parse("#626160");
    _folderBackgroundColor = base::Color::parse("#3477a6");
    _folderBackgroundColorHot = base::Color::parse("#4699b8");
    _backTileBackgroundColor = base::Color::parse("#d9532c");
    _backTileBackgroundColorHot = base::Color::parse("#d97457");
  } else {
    _titleColor = base::Color::parse("#505050");
    _folderTitleColor = base::Color::parse("#F0F0F0");
    _backgroundColor = base::Color::parse("#F4F4F4");
    _backgroundColorHot = base::Color::parse("#D5D5D5");
    _folderBackgroundColor = base::Color::parse("#3477a6");
    _folderBackgroundColorHot = base::Color::parse("#4699b8");
    _backTileBackgroundColor = base::Color::parse("#d9532c");
    _backTileBackgroundColorHot = base::Color::parse("#d97457");

#ifndef __APPLE__
    _search_text.set_front_color("#102629");
    _search_text.set_placeholder_color("#4F5759");
    _search_text.set_back_color("#EDF2F4");
#endif
  }

  if (_owner->isDarkModeActive()) {
    _search_text.set_front_color("#ECF4F3");
    _search_text.set_placeholder_color("#B7C7C8");
    _search_text.set_back_color("#172326");
  }
  if (base::Color::is_high_contrast_scheme()) {
    _search_text.set_front_color(base::Color::getSystemColor(base::TextColor).to_html());
    _search_text.set_placeholder_color(base::Color::getSystemColor(base::TextColor).to_html());
    _search_text.set_back_color(base::Color::getSystemColor(base::WindowBackgroundColor).to_html());
  }
  const std::string surface = base::Color::is_high_contrast_scheme()
    ? base::Color::getSystemColor(base::WindowBackgroundColor).to_html()
    : _owner->isDarkModeActive() ? "#172326" : "#EDF2F4";
  set_back_color(surface);
  _search_box.set_back_color(surface);
  if (_welcomeScreen != nullptr)
    _welcomeScreen->updateColors();
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::updateIcons() {
  if (_owner->isDarkModeActive()) {
    deleteSurface(_sakila_icon);
    _sakila_icon = mforms::Utilities::load_icon("wb_tile_sakila_dark.png");

    deleteSurface(_manage_icon);
    _manage_icon = mforms::Utilities::load_icon("wb_tile_manage_dark.png");

    deleteSurface(_folder_icon);
    _folder_icon = mforms::Utilities::load_icon("wb_tile_folder.png");

    deleteSurface(_network_icon);
    _network_icon = mforms::Utilities::load_icon("wb_tile_network_dark.png");

    deleteSurface(_plus_icon);
    _plus_icon = mforms::Utilities::load_icon("wb_tile_plus_dark.png");

    deleteSurface(_user_icon);
    _user_icon = mforms::Utilities::load_icon("wb_tile_user_dark.png");
  } else {
    deleteSurface(_sakila_icon);
    _sakila_icon = mforms::Utilities::load_icon("wb_tile_sakila_light.png");

    deleteSurface(_manage_icon);
    _manage_icon = mforms::Utilities::load_icon("wb_tile_manage_light.png");

    deleteSurface(_folder_icon);
    _folder_icon = mforms::Utilities::load_icon("wb_tile_folder.png");

    deleteSurface(_network_icon);
    _network_icon = mforms::Utilities::load_icon("wb_tile_network_light.png");

    deleteSurface(_plus_icon);
    _plus_icon = mforms::Utilities::load_icon("wb_tile_plus_light.png");

    deleteSurface(_user_icon);
    _user_icon = mforms::Utilities::load_icon("wb_tile_user_light.png");
  }

  if (_welcomeScreen != nullptr) {
    _welcomeScreen->updateIcons();
  }
  if (_migrationAssistantBanner != nullptr) {
    _migrationAssistantBanner->updateIcons();
  }
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::focus_search_box() {
  _search_text.focus();
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::showWelcomeHeading(bool state) {
  _showWelcomeHeading = state;
  if (_welcomeScreen != nullptr)
    _welcomeScreen->show(state);

  set_layout_dirty(true);
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::showMigrationBanner(bool state) {
  _showMigrationAssistantBanner = state;
  if (_migrationAssistantBanner != nullptr)
    _migrationAssistantBanner->show(state);

  set_layout_dirty(true);
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::on_search_text_changed() {
  std::string filter = _search_text.get_string_value();
  _filtered_connections.clear();

  _filtered = !filter.empty();
  if (_filtered) {
    ConnectionVector current_connections = !_active_folder ? _connections : _active_folder->children;
    for (ConnectionIterator iterator = current_connections.begin(); iterator != current_connections.end(); ++iterator) {
      // Always keep the first entry if we are in a folder. It's not filtered.
      if (_active_folder && (iterator == current_connections.begin()))
        _filtered_connections.push_back(*iterator);
      else if (base::contains_string((*iterator)->search_title, filter, false) ||
               base::contains_string((*iterator)->search_description, filter, false) ||
               base::contains_string((*iterator)->search_user, filter, false) ||
               base::contains_string((*iterator)->search_schema, filter, false))
        _filtered_connections.push_back(*iterator);
    }
  }

  const auto &visible = displayed_connections();
  if (_filtered && (!_selectedEntry || _selectedEntry->connectionId.empty() ||
      std::find(visible.begin(), visible.end(), _selectedEntry) == visible.end())) {
    const auto first = std::find_if(visible.begin(), visible.end(), [](const std::shared_ptr<ConnectionEntry> &entry) {
      return !entry->connectionId.empty();
    });
    selectEntry(first == visible.end() ? nullptr : *first);
  }
  updateFocusableAreas();
  set_layout_dirty(true);
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::on_search_text_action(mforms::TextEntryAction action) {
  if (action == mforms::EntryEscape)
    _search_text.set_value("");
  else if (action == mforms::EntryActivate)
    openSelectedConnection();
  else if (action == mforms::EntryKeyDown || action == mforms::EntryKeyUp) {
    keyPress(action == mforms::EntryKeyDown ? mforms::KeyDown : mforms::KeyUp, mforms::ModifierNoModifier);
    focus();
  }
}

//------------------------------------------------------------------------------------------------

/**
 * Computes the index for the given position, regardless if that is actually backed by an existing
 * entry or not.
 *
 * This will not work in section separated folders, but it doesn't matter
 * atm because this is only used for drag/drop
 */
ssize_t ConnectionsSection::calculate_index_from_point(int x, int y) {
  if (x < CONNECTIONS_LEFT_PADDING || x >= get_width() - CONNECTIONS_RIGHT_PADDING || y < CONNECTIONS_TOP_PADDING)
    return -1;
  const int index = (y - CONNECTIONS_TOP_PADDING) / rowHeight(get_width());
  return index < static_cast<int>(displayed_connections().size()) ? index : -1;
}

//----------------------------------------------------------------------------------------------------------------------

std::shared_ptr<ConnectionEntry> ConnectionsSection::entry_from_point(int x, int y) const {
  std::shared_ptr<ConnectionEntry> entry;

  ConnectionVector connections(displayed_connections());
  for (ConnectionVector::iterator conn = connections.begin(); conn != connections.end(); ++conn) {
    if ((*conn)->bounds.contains(x, y)) {
      entry = *conn;
      break;
    }
  }

  return entry;
}

//----------------------------------------------------------------------------------------------------------------------

std::shared_ptr<ConnectionEntry> ConnectionsSection::entry_from_index(ssize_t index) const {
  ssize_t count = displayed_connections().size();
  if (index >= 0 && index < count) {
    return displayed_connections()[index];
  }
  return std::shared_ptr<ConnectionEntry>();
}

//----------------------------------------------------------------------------------------------------------------------

base::Rect ConnectionsSection::bounds_for_entry(size_t index, size_t width) {
  const int height = rowHeight(static_cast<int>(width));
  return base::Rect(CONNECTIONS_LEFT_PADDING, CONNECTIONS_TOP_PADDING + static_cast<double>(index) * height,
                    std::max(1.0, static_cast<double>(width) - CONNECTIONS_LEFT_PADDING - CONNECTIONS_RIGHT_PADDING), height);
}

//----------------------------------------------------------------------------------------------------------------------

/**
 * Returns the connection id stored under the given index. Returns an empty string if the index
 * describes a folder or back tile.
 * Properly takes into account if we are in a folder or not and if we have filtered entries currently.
 */
std::string ConnectionsSection::connectionIdFromIndex(ssize_t index) {
  if (index < 0 || (_active_folder && index == 0))
    return "";

  return displayed_connections()[index]->connectionId;
}

//----------------------------------------------------------------------------------------------------------------------

void ConnectionsSection::repaint(cairo_t *cr, int areax, int areay, int areaw, int areah) {
  if (is_layout_dirty()) {
    set_layout_dirty(false);
    if (getContainer()->get_parent()) getContainer()->get_parent()->relayout();
  }
  const bool dark = _owner->isDarkModeActive();
  const char *ink = dark ? "#ECF4F3" : "#102629";
  const char *muted = dark ? "#B7C7C8" : "#4F5759";
  const char *accent = dark ? "#8FE4C5" : "#0B5F41";
  homeColor(cr, dark ? "#172326" : "#EDF2F4", base::WindowBackgroundColor);
  cairo_paint(cr);
  const double width = std::max(1, get_width() - 24);
  _add_button.bounds = base::Rect(get_width() - 144, 4, 132, 26);
  _manage_button.bounds = base::Rect(get_width() - 282, 4, 130, 26);
  homeCommandIcon(cr, HomeCommandIcon::Add, _add_button.bounds.left() + 6, 9, accent);
  homeText(cr, "Yeni ba\xc4\x9f""lant\xc4\xb1""", _add_button.bounds.left() + 28, 22, 100, accent);
  homeText(cr, "Profilleri y\xc3\xb6""net", _manage_button.bounds.left() + 8, 22, 120, accent);
  homeRule(cr, 12, 33, width, dark);
  const bool narrow = get_width() < 720;
  const bool showEnvironment = get_width() >= 960;
  const double usable = std::max(200.0, width - kConnectionActionWidth - (showEnvironment ? 80 : 0));
  const double profileWidth = usable * (narrow ? 0.48 : 0.30);
  const double userWidth = narrow ? 0 : usable * 0.28;
  homeText(cr, _active_folder ? "Profil / " + _active_folder->title : "Profil", 20, 51, profileWidth - 16, ink, false, 13, true);
  if (!narrow) homeText(cr, "Kullan\xc4\xb1""c\xc4\xb1""", 20 + profileWidth, 51, userWidth - 16, ink, false, 13, true);
  homeText(cr, "Sunucu", 20 + profileWidth + userWidth, 51, usable - profileWidth - userWidth - 16, ink, false, 13, true);
  if (showEnvironment) homeText(cr, "Ortam", 20 + usable, 51, 64, ink, false, 13, true);
  homeRule(cr, 12, 57, width, dark);
  const auto &connections = displayed_connections();
  _connectButton.bounds = _editButton.bounds = _testButton.bounds = _clearButton.bounds = _rescanButton.bounds = base::Rect();
  for (size_t index = 0; index < connections.size(); ++index) {
    auto entry = connections[index];
    entry->bounds = bounds_for_entry(index, get_width());
    entry->draw_tile(cr, entry == _hot_entry, 1, false);
    if (entry == _selectedEntry && !entry->connectionId.empty()) {
      _connectButton.bounds = base::Rect(entry->bounds.right() - kConnectionActionWidth + 4,
                                       entry->bounds.top() + 3, kConnectionButtonWidth, 26);
      homeColor(cr, dark ? "#9ADBCB" : "#043133", base::SelectedControlColor);
      const auto &b = _connectButton.bounds;
      cairo_move_to(cr, b.left(), b.top());
      cairo_line_to(cr, b.right(), b.top());
      cairo_line_to(cr, b.right(), b.bottom() - 3);
      cairo_line_to(cr, b.right() - 3, b.bottom());
      cairo_line_to(cr, b.left(), b.bottom());
      cairo_close_path(cr);
      cairo_fill(cr);
      homeText(cr, _openingConnection ? "A\xc3\xa7\xc4\xb1""l\xc4\xb1""yor\xe2\x80\xa6""" : "Ba\xc4\x9f""lan", b.left() + 8, b.top() + 18, b.width() - (_openingConnection ? 16 : 34),
               dark ? "#102629" : "#FFFFFF", false, 13, true, base::SelectedControlTextColor);
      if (!_openingConnection) {
        // Draw the Enter mark directly: Cairo's toy font API has no fallback
        // for this glyph on every installed Segoe UI version.
        cairo_save(cr);
        homeColor(cr, dark ? "#102629" : "#FFFFFF", base::SelectedControlTextColor);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, b.right() - 22.5, b.top() + 6);
        cairo_line_to(cr, b.right() - 22.5, b.top() + 20);
        cairo_stroke(cr);
        cairo_set_line_width(cr, 1.5);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
        cairo_move_to(cr, b.right() - 8, b.top() + 8);
        cairo_line_to(cr, b.right() - 8, b.top() + 15);
        cairo_line_to(cr, b.right() - 17, b.top() + 15);
        cairo_move_to(cr, b.right() - 14, b.top() + 12);
        cairo_line_to(cr, b.right() - 17, b.top() + 15);
        cairo_line_to(cr, b.right() - 14, b.top() + 18);
        cairo_stroke(cr);
        cairo_restore(cr);
      }
    }
    if (static_cast<ssize_t>(index) == _drop_index) {
      const auto &b = entry->bounds;
      homeColor(cr, accent, base::TextColor);
      const double y = _drop_position == DropPositionRight ? b.bottom() : b.top();
      cairo_rectangle(cr, b.left(), y, b.width(), 2);
      cairo_fill(cr);
    }
  }
  double detailTop = CONNECTIONS_TOP_PADDING + static_cast<double>(connections.size()) * rowHeight(get_width()) + 8;
  if (connections.empty()) {
    homeText(cr, _filtered ? "Araman\xc4\xb1""zla e\xc5\x9f""le\xc5\x9f""en ba\xc4\x9f""lant\xc4\xb1"" bulunamad\xc4\xb1""." : "Hen\xc3\xbc""z kay\xc4\xb1""tl\xc4\xb1"" ba\xc4\x9f""lant\xc4\xb1""n\xc4\xb1""z yok.",
             20, 87, width - 16, ink);
    _clearButton.bounds = _filtered ? base::Rect(12, 99, 138, 26) : base::Rect();
    _rescanButton.bounds = base::Rect(_filtered ? 160 : 12, 99, 166, 26);
    if (_filtered) homeText(cr, _clearButton.title, 20, 117, 130, accent);
    homeText(cr, _rescanButton.title, _rescanButton.bounds.left() + 8, 117, 154, accent);
  } else if (_selectedEntry && !_selectedEntry->connectionId.empty()) {
    homeRule(cr, 12, detailTop, width, dark);
    profileAvatar(cr, 20, detailTop + 7, _selectedEntry->avatarVariant);
    homeText(cr, _selectedEntry->title, 48, detailTop + 22, 144, ink, true);
    const double methodWidth = get_width() >= 800 ? 160 : 80;
    homeText(cr, _selectedEntry->connectionMethod, 202, detailTop + 22, methodWidth, muted, true);
    _editButton.bounds = base::Rect(220 + methodWidth, detailTop + 4, 126, 26);
    _testButton.bounds = base::Rect(354 + methodWidth, detailTop + 4, 130, 26);
    homeCommandIcon(cr, HomeCommandIcon::Edit, _editButton.bounds.left() + 6, detailTop + 9, accent);
    homeCommandIcon(cr, HomeCommandIcon::Test, _testButton.bounds.left() + 6, detailTop + 9, accent);
    homeText(cr, _editButton.title, _editButton.bounds.left() + 28, detailTop + 22, 94, accent);
    homeText(cr, _testButton.title, _testButton.bounds.left() + 28, detailTop + 22, 98, accent);
    if (!showEnvironment && get_width() >= 650)
      homeText(cr, _selectedEntry->environment.empty() ? "Ortam \xe2\x80\x94""" : _selectedEntry->environment,
               _testButton.bounds.right() + 12, detailTop + 22, width - _testButton.bounds.right() - 12, muted);
  }
  const double footer = std::max(detailTop + 42, static_cast<double>(get_height() - 22));
  homeRule(cr, 0, footer, get_width(), dark);
  homeText(cr, std::to_string(connections.size()) + (_active_folder ? " \xc3\xb6\xc4\x9f""e" : " kay\xc4\xb1""tl\xc4\xb1"" profil"),
           12, footer + 16, 220, muted, false, 12);
  homeText(cr, "\xe2\x86\x91"" \xe2\x86\x93"" Se\xc3\xa7""    Enter Ba\xc4\x9f""lan", get_width() - 198, footer + 16, 186, muted, false, 12);
  mforms::DrawBox::repaint(cr, areax, areay, areaw, areah);
}

//----------------------------------------------------------------------------------------------------------------------

base::Size ConnectionsSection::getLayoutSize(base::Size proposedSize) {
  const size_t count = displayed_connections().size();
  const double height = CONNECTIONS_TOP_PADDING + static_cast<double>(std::max(size_t(2), count)) * rowHeight(static_cast<int>(proposedSize.width)) + 72;
  return base::Size(proposedSize.width, height);
}

//----------------------------------------------------------------------------------------------------------------------

const char* ConnectionsSection::getTitle() { return "Ba\xc4\x9f""lant\xc4\xb1""lar"; }

//----------------------------------------------------------------------------------------------------------------------

int ConnectionsSection::rowHeight(int width) const {
  return width < 720 ? 44 : 32;
}

void ConnectionsSection::selectEntry(const std::shared_ptr<ConnectionEntry> &entry) {
  _selectedEntry = entry;
  _selectedConnectionId = entry ? entry->connectionId : "";
  set_needs_repaint();
}

void ConnectionsSection::openSelectedConnection() {
  if (!_selectedEntry || _openingConnection) return;
  auto entry = _selectedEntry;
  _openingConnection = true;
  set_needs_repaint();
  try {
    entry->activate();
    _openingConnection = false;
  } catch (...) {
    _openingConnection = false;
    set_needs_repaint();
    throw;
  }
  set_needs_repaint();
}

bool ConnectionsSection::keyPress(KeyCode code, ModifierKey modifiers) {
  const auto &entries = displayed_connections();
  if ((code == KeyUp || code == KeyDown) && !entries.empty()) {
    const auto current = std::find(entries.begin(), entries.end(), _selectedEntry);
    const int index = current == entries.end() ? 0 :
      std::max(0, std::min(static_cast<int>(entries.size()) - 1,
                          static_cast<int>(current - entries.begin()) + (code == KeyDown ? 1 : -1)));
    selectEntry(entries[index]);
    setFocusOnArea(entries[index]->bounds.center());
    if (auto scroll = dynamic_cast<mforms::ScrollPanel *>(get_parent()))
      scroll->scroll_to(static_cast<int>(entries[index]->bounds.left()), static_cast<int>(entries[index]->bounds.top()));
    return true;
  }
  return DrawBox::keyPress(code, modifiers);
}

void ConnectionsSection::cancelOperation() {
  // noop
}

//----------------------------------------------------------------------------------------------------------------------

void ConnectionsSection::setFocus() {
  _search_text.focus();
}

//----------------------------------------------------------------------------------------------------------------------

bool ConnectionsSection::canHandle(HomeScreenMenuType type) {
  switch (type) {
    case HomeMenuConnection:
    case HomeMenuConnectionGroup:
    case HomeMenuConnectionGeneric:
      return true;
    default:
      return false;
  }
  return false;
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::setContextMenu(mforms::Menu *menu, HomeScreenMenuType type) {
  if (canHandle(type)) {
    switch (type) {
      case HomeMenuConnectionGroup:
        if (_folder_context_menu != NULL)
          _folder_context_menu->release();
        _folder_context_menu = menu;
        if (_folder_context_menu != NULL) {
          _folder_context_menu->retain();
          menu->set_handler(std::bind(&ConnectionsSection::handle_folder_command, this, std::placeholders::_1));
        }
        break;

      case HomeMenuConnection:
        if (_connection_context_menu != NULL)
          _connection_context_menu->release();
        _connection_context_menu = menu;
        if (_connection_context_menu != NULL) {
          _connection_context_menu->retain();
          menu->set_handler(std::bind(&ConnectionsSection::handle_command, this, std::placeholders::_1));
        }
        break;

      default:
        if (_generic_context_menu != NULL)
          _generic_context_menu->release();
        _generic_context_menu = menu;
        if (_generic_context_menu != NULL) {
          _generic_context_menu->retain();
          menu->set_handler(std::bind(&ConnectionsSection::handle_command, this, std::placeholders::_1));
        }
        break;
    }

    if (menu != NULL)
      scoped_connect(menu->signal_will_show(), std::bind(&ConnectionsSection::menu_open, this));
  }
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::setContextMenuAction(mforms::Menu *menu, HomeScreenMenuType type) {
  // pass
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::addConnection(const std::string &connectionId, const std::string &title,
                                       const std::string &description, const std::string &user,
                                       const std::string &schema) {
  std::shared_ptr<ConnectionEntry> entry;

  entry = std::shared_ptr<ConnectionEntry>(new ConnectionEntry(this));

  entry->connectionId = connectionId;
  entry->title = title;
  entry->description = description;
  entry->user = user;
  entry->schema = schema;
  unsigned int identity = 2166136261u;
  for (unsigned char value : connectionId) identity = (identity ^ value) * 16777619u;
  entry->avatarVariant = static_cast<int>(identity % 2);
  if (getConnectionInfoCallback) {
    const auto info = getConnectionInfoCallback(connectionId);
    const auto avatar = info.find("avatarVariant");
    const auto environment = info.find("environment");
    const auto method = info.find("connectionMethod");
    if (avatar != info.end() && avatar->second.is<int>()) entry->avatarVariant = avatar->second.as<int>();
    if (environment != info.end() && environment->second.is<std::string>()) entry->environment = environment->second.as<std::string>();
    if (method != info.end() && method->second.is<std::string>()) entry->connectionMethod = method->second.as<std::string>();
  }
  entry->compute_strings = true;

  entry->search_title = title;
  entry->search_description = description;
  entry->search_user = user;
  entry->search_schema = schema;

  std::string::size_type slash_position = title.find("/");
  if (slash_position != std::string::npos) {
    // A child entry->
    std::string parent_name = title.substr(0, slash_position);
    entry->title = title.substr(slash_position + 1);
    entry->search_title = entry->title;
    bool found_parent = false;
    for (ConnectionIterator iterator = _connections.begin(); iterator != _connections.end(); iterator++) {
      if ((*iterator)->title == parent_name) {
        if (FolderEntry *folder = dynamic_cast<FolderEntry *>(iterator->get())) {
          found_parent = true;
          folder->children.push_back(entry);
          break;
        }
      }
    }

    // If the parent was not found, a folder should be created
    if (!found_parent) {
      std::shared_ptr<FolderEntry> folder(new FolderEntry(this));

      folder->description = parent_name;
      folder->title = parent_name;
      folder->compute_strings = true;
      folder->search_title = parent_name;

      folder->children.push_back(std::shared_ptr<ConnectionEntry>(new FolderBackEntry(this)));
      folder->children.push_back(entry);
      _connections.push_back(std::dynamic_pointer_cast<ConnectionEntry>(folder));
      if (!_active_folder_title_before_refresh_start.empty() &&
          _active_folder_title_before_refresh_start == folder->title) {
        _active_folder = std::dynamic_pointer_cast<FolderEntry>(_connections.back());
        _active_folder_title_before_refresh_start.clear();
      }
    }
  } else
    _connections.push_back(entry);

  if (_filtered) on_search_text_changed();
  else updateFocusableAreas();
  set_layout_dirty(true);
}

void ConnectionsSection::updateFocusableAreas() {
  clearFocusableAreas();
  const auto &entries = displayed_connections();
  auto selected = std::find(entries.begin(), entries.end(), _selectedEntry);
  if (selected == entries.end()) {
    auto remembered = std::find_if(entries.begin(), entries.end(), [this](const std::shared_ptr<ConnectionEntry> &entry) {
      return !_selectedConnectionId.empty() && entry->connectionId == _selectedConnectionId;
    });
    auto first = entries.begin();
    if (_filtered) first = std::find_if(entries.begin(), entries.end(), [](const std::shared_ptr<ConnectionEntry> &entry) {
      return !entry->connectionId.empty();
    });
    selectEntry(remembered != entries.end() ? *remembered : first == entries.end() ? nullptr : *first);
  }
  for (const auto &entry : entries) {
    FocusableArea area;
    area.activate = [this, entry]() { selectEntry(entry); openSelectedConnection(); };
    area.getBounds = [entry]() { return entry->bounds; };
    area.showContextMenu = [this, entry]() {
      _entry_for_menu = entry;
      if (entry->context_menu())
        entry->context_menu()->popup_at(this, static_cast<int>(entry->bounds.xcenter()), static_cast<int>(entry->bounds.ycenter()));
    };
    addFocusableArea(area);
  }
  for (auto button : {&_add_button, &_manage_button, &_editButton, &_testButton, &_clearButton, &_rescanButton}) {
    FocusableArea area;
    area.activate = [button]() { if (!button->bounds.empty()) button->defaultHandler(); };
    area.getBounds = [button]() { return button->bounds; };
    if ((button == &_editButton || button == &_testButton) && (!_selectedEntry || _selectedEntry->connectionId.empty())) continue;
    if (button == &_clearButton && (!_filtered || !entries.empty())) continue;
    if (button == &_rescanButton && !entries.empty()) continue;
    addFocusableArea(area);
  }
}

//------------------------------------------------------------------------------------------------

bool ConnectionsSection::setFocusOnEntry(ConnectionEntry const* entry) {
  for (const auto &candidate : displayed_connections()) {
    if (candidate.get() == entry) { selectEntry(candidate); break; }
  }
  return setFocusOnArea(entry->bounds.center());
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::clear_connections(bool clear_state) {
  if (clear_state) {
    _filtered = false;
    _filtered_connections.clear();
    _search_text.set_value("");
    _active_folder_title_before_refresh_start = "";
  } else {
    if (_active_folder)
      _active_folder_title_before_refresh_start = _active_folder->title;
  }
  clearFocusableAreas();
  _entry_for_menu.reset();
  _active_folder.reset();
  _connections.clear();
  _hot_entry.reset();
  _selectedEntry.reset();
  _filtered_connections.clear();

  set_layout_dirty(true);
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::change_to_folder(std::shared_ptr<FolderEntry> folder) {
  if (_active_folder && !folder) {
    // Returning to root list.
    _active_folder.reset();
    _filtered = false;
    _search_text.set_value("");
    updateFocusableAreas();
    set_layout_dirty(true);
  } else if (folder) {
    _active_folder = folder;
    // Drilling into a folder.
    _filtered = false;
    _search_text.set_value("");
    updateFocusableAreas();
    set_layout_dirty(true);
  }
}

//--------------------------------------------------------------------------------------------------

bool ConnectionsSection::mouse_down(mforms::MouseButton button, int x, int y) {
  mforms::DrawBox::mouse_down(button, x, y);
  if (button == mforms::MouseButtonLeft && _hot_entry)
    _mouse_down_position = base::Rect(x - 4, y - 4, 8, 8); // Center a 8x8 pixels rect around the mouse position.

  return false;                                            // Continue with standard mouse handling.
}

//--------------------------------------------------------------------------------------------------

bool ConnectionsSection::mouse_up(mforms::MouseButton button, int x, int y) {
  _mouse_down_position = base::Rect();
  return false;
}

//--------------------------------------------------------------------------------------------------

bool ConnectionsSection::mouse_double_click(mforms::MouseButton button, int x, int y) {
  if (button != MouseButtonLeft) return false;
  auto entry = entry_from_point(x, y);
  if (!entry) return false;
  selectEntry(entry);
  openSelectedConnection();
  return true;
}

//--------------------------------------------------------------------------------------------------

bool ConnectionsSection::mouse_click(mforms::MouseButton button, int x, int y) {
  _hot_entry = entry_from_point(x, y);
  if (button == MouseButtonLeft) {
    for (auto action : {&_add_button, &_manage_button, &_connectButton, &_editButton, &_testButton, &_clearButton, &_rescanButton}) {
      if (action->bounds.contains(x, y)) {
        action->defaultHandler();
        return true;
      }
    }
    if (_hot_entry) {
      selectEntry(_hot_entry);
      focus();
      setFocusOnArea(_hot_entry->bounds.center());
      if (_hot_entry->connectionId.empty()) openSelectedConnection();
      return true;
    }
  } else if (button == MouseButtonRight) {
    Menu *menu = _generic_context_menu;
    if (_hot_entry) {
      selectEntry(_hot_entry);
      _entry_for_menu = _hot_entry;
      menu = _hot_entry->context_menu();
    }
    if (menu) menu->popup_at(this, x, y);
    return menu != nullptr;
  }
  return false;
}

//------------------------------------------------------------------------------------------------

bool ConnectionsSection::mouse_leave() {
  if (_hot_entry) {
    _hot_entry.reset();
    set_needs_repaint();
  }
  return false;
}

//------------------------------------------------------------------------------------------------

bool ConnectionsSection::mouse_move(mforms::MouseButton button, int x, int y) {
  std::shared_ptr<ConnectionEntry> entry = entry_from_point(x, y);
  bool was_on_migration = _on_migration;
  _on_migration = entry && entry->getMigrationButtonBounds().contains(x, y);

  if (entry && !_mouse_down_position.empty() && (!_mouse_down_position.contains(x, y))) {
    if (!entry->is_movable()) {
      _mouse_down_position = base::Rect();
      return true;
    }

    if (button == mforms::MouseButtonNone) // Cancel drag if the mouse button was released.
      return true;

    return do_tile_drag(calculate_index_from_point(x, y), x, y);
  } else {
    // Only do hit tracking if no mouse button is pressed to avoid situations like
    // mouse down outside any tile, drag over a tile, release mouse button -> click
    // (or hover effects in general).
    if (button == mforms::MouseButtonNone) {
      if (entry != _hot_entry) {
        _hot_entry = entry;
        set_needs_repaint();
        return true;
      } else if (was_on_migration != _on_migration) {
        set_needs_repaint();
        return true;
      }
    }
  }

  return false;
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::handle_command(const std::string &command) {
  std::string item;
  if (_entry_for_menu) {
    if (_active_folder) {
      if (command == "delete_connection_all") {
        // We only want to delete all connections in the active group. This is the same as
        // removing the group entirely, since the group is formed by connections in it.
        _entry_for_menu = _active_folder;
        handle_folder_command("delete_connection_group");
        return;
      } else {
        item = _entry_for_menu->connectionId;
      }
    } else {
      item = _entry_for_menu->connectionId;
    }
  }

  _owner->handleContextMenu(item, command);
  _entry_for_menu.reset();
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::handle_folder_command(const std::string &command) {
  {
    // We have to pass on a valid connection (for the group name).
    // All child items have the same group name (except the dummy entry for the back tile).
    std::string title;
    if (_entry_for_menu)
      title = _entry_for_menu->title;

    title += "/";

    _owner->handleContextMenu(title, command);
    _entry_for_menu.reset();
  }
}

//------------------------------------------------------------------------------------------------

void ConnectionsSection::menu_open() {
  if (_entry_for_menu) {
    ConnectionVector const& items(displayed_connections());

    if (items.empty())
      _entry_for_menu->menu_open(ConnectionEntry::Other);
    else if (items.front() == _entry_for_menu)
      _entry_for_menu->menu_open(ConnectionEntry::First);
    else if (items.back() == _entry_for_menu)
      _entry_for_menu->menu_open(ConnectionEntry::Last);
    else
      _entry_for_menu->menu_open(ConnectionEntry::Other);
  }
}

//------------------------------------------------------------------------------------------------

std::string ConnectionsSection::getAccessibilityTitle() {
  return "Ba\xc4\x9f""lant\xc4\xb1""lar";
}

//------------------------------------------------------------------------------------------------

size_t ConnectionsSection::getAccessibilityChildCount() {
  size_t count = displayed_connections().size();
  for (auto action : {&_add_button, &_manage_button, &_connectButton, &_editButton, &_testButton, &_clearButton, &_rescanButton})
    if (!action->bounds.empty()) ++count;
  return count;
}

//------------------------------------------------------------------------------------------------

base::Accessible* ConnectionsSection::getAccessibilityChild(size_t index) {
  const auto &entries = displayed_connections();
  if (index < entries.size()) return entries[index].get();
  index -= entries.size();
  for (auto action : {&_add_button, &_manage_button, &_connectButton, &_editButton, &_testButton, &_clearButton, &_rescanButton}) {
    if (action->bounds.empty()) continue;
    if (index == 0) return action;
    --index;
  }
  return nullptr;
}

//------------------------------------------------------------------------------------------------

base::Accessible::Role ConnectionsSection::getAccessibilityRole() {
  return Accessible::List;
}

//------------------------------------------------------------------------------------------------

base::Accessible* ConnectionsSection::accessibilityHitTest(ssize_t x, ssize_t y) {
  for (auto action : {&_add_button, &_manage_button, &_connectButton, &_editButton, &_testButton, &_clearButton, &_rescanButton})
    if (action->bounds.contains(static_cast<double>(x), static_cast<double>(y))) return action;
  const auto entry = entry_from_point(static_cast<int>(x), static_cast<int>(y));
  return entry ? entry.get() : nullptr;
}

//------------------------------------------------------------------------------------------------

bool ConnectionsSection::do_tile_drag(ssize_t index, int x, int y) {
  _hot_entry.reset();
  set_needs_repaint();

  if (index >= 0) {
    mforms::DragDetails details;
    details.allowedOperations = mforms::DragOperationMove;
    details.location = base::Point(x, y);

    details.image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, get_width() - 24), rowHeight(get_width()));
    cairo_t *cr = cairo_create(details.image);
    base::Rect bounds = bounds_for_entry(index, get_width());
    details.hotspot.x = x - bounds.pos.x;
    details.hotspot.y = y - bounds.pos.y;

    // We know we have no back tile here.
    std::shared_ptr<ConnectionEntry> entry = entry_from_index(index);
    if (entry) {
      entry->draw_tile(cr, false, 1, true);

      _drag_index = index;
      mforms::DragOperation operation =
        do_drag_drop(details, entry.get(), mforms::HomeScreenSettings::TILE_DRAG_FORMAT);

      _mouse_down_position = base::Rect();
      cairo_surface_destroy(details.image);
      cairo_destroy(cr);

      _drag_index = -1;
      _drop_index = -1;
      set_layout_dirty(true);

      if (operation == mforms::DragOperationMove) // The actual move is done in the drop delegate method.
        return true;
    }
  }
  return false;
}

//------------------------------------------------------------------------------------------------

// Drop delegate implementation.
mforms::DragOperation ConnectionsSection::drag_over(View *sender, base::Point p,
                                                    mforms::DragOperation allowedOperations,
                                                    const std::vector<std::string> &formats) {
  if (allowedOperations == mforms::DragOperationNone)
    return allowedOperations;

  if (std::find(formats.begin(), formats.end(), mforms::DragFormatFileName) != formats.end()) {
    // Indicate we can accept files if one of the connection tiles is hit.
    std::shared_ptr<ConnectionEntry> entry = entry_from_point((int)p.x, (int)p.y);

    if (!entry)
      return mforms::DragOperationNone;

    if (entry->connectionId.empty())
      return mforms::DragOperationNone;

    if (_hot_entry != entry) {
      _hot_entry = entry;
      set_needs_repaint();
    }
    return allowedOperations & mforms::DragOperationCopy;
  }

  if (std::find(formats.begin(), formats.end(), mforms::HomeScreenSettings::TILE_DRAG_FORMAT) != formats.end()) {
    // A tile is being dragged. Find the target index and drop location for visual feedback.
    // Computation here is more relaxed than the normal hit test as we want to allow dropping
    // left, right and below the actual tiles area too as well as between tiles.
    if (p.y < CONNECTIONS_TOP_PADDING) {
      if (_drop_index > -1) {
        _drop_index = -1;
        set_needs_repaint();
      }
      return mforms::DragOperationNone;
    }

    const int count = static_cast<int>(displayed_connections().size());
    if (count == 0) return DragOperationNone;
    int index = std::min(count - 1, static_cast<int>((p.y - CONNECTIONS_TOP_PADDING) / rowHeight(get_width())));
    const auto entry = entry_from_index(index);
    const auto bounds = bounds_for_entry(index, get_width());
    DropPosition position = p.y >= bounds.ycenter() ? DropPositionRight : DropPositionLeft;
    if (entry->connectionId.empty() && p.y > bounds.top() + bounds.height() / 3 && p.y < bounds.bottom() - bounds.height() / 3)
      position = DropPositionOn;

    // Check that the drop position does not resolve to the dragged item.
    // Don't allow dragging a group on a group either.
    if (_drag_index > -1 &&
        (index == _drag_index || (index + 1 == _drag_index && position == mforms::DropPositionRight) ||
         (index - 1 == _drag_index && position == mforms::DropPositionLeft) ||
         (position == mforms::DropPositionOn && dynamic_cast<FolderEntry *>(entry_from_index(_drag_index).get())))) {
      index = -1;
    } else if (!_filtered && _active_folder && index == 0 && position == mforms::DropPositionLeft) {
      position = mforms::DropPositionOn; // Drop on back tile.
    }

    if (_drop_index != index || _drop_position != position) {
      _drop_index = index;
      _drop_position = position;
      set_needs_repaint();
    }

    return mforms::DragOperationMove;
  }

  return mforms::DragOperationNone;
}

//----------------------------------------------------------------------------------------------------------------------

mforms::DragOperation ConnectionsSection::files_dropped(View *sender, base::Point p,
                                                        mforms::DragOperation allowedOperations,
                                                        const std::vector<std::string> &file_names) {
  std::shared_ptr<ConnectionEntry> entry = entry_from_point(static_cast<int>(p.x), static_cast<int>(p.y));
  if (!entry)
    return mforms::DragOperationNone;

  if (!entry->connectionId.empty()) {
    // Allow only sql script files to be dropped.
    std::vector<std::string> files;
    for (size_t i = 0; i < file_names.size(); ++i)
      if (base::tolower(base::extension(file_names[i])) == ".sql")
        files.push_back(file_names[i]);

    if (files.size() == 0)
      return mforms::DragOperationNone;

    HomeScreenDropFilesInfo dInfo;
    dInfo.connectionId = entry->connectionId;
    dInfo.files = files;
    _owner->trigger_callback(HomeScreenAction::ActionFilesWithConnection, dInfo);
  }

  return mforms::DragOperationCopy;
}

//----------------------------------------------------------------------------------------------------------------------

mforms::DragOperation ConnectionsSection::data_dropped(mforms::View *sender, base::Point p,
                                                       mforms::DragOperation allowedOperations, void *data,
                                                       const std::string &format) {
  if (format == mforms::HomeScreenSettings::TILE_DRAG_FORMAT && _drop_index > -1) {
    mforms::DragOperation result = mforms::DragOperationNone;

    // Can be invalid if we move a group.
    std::string connectionId = connectionIdFromIndex(_drag_index);
    ConnectionEntry *source_entry = static_cast<ConnectionEntry *>(data);

    std::shared_ptr<ConnectionEntry> entry;
    if (_filtered) {
      if (_drop_index < (int)_filtered_connections.size())
        entry = _filtered_connections[_drop_index];
    } else if (_active_folder) {
      if (_drop_index < (int)_active_folder->children.size())
        entry = _active_folder->children[_drop_index];
    } else {
      if (_drop_index < (int)_connections.size())
        entry = _connections[_drop_index];
    }

    if (!entry)
      return result;

    bool is_back_tile = entry->title == "< back";

    // Drop target is a group.
    HomeScreenDropInfo dropInfo;
    if (!connectionId.empty()) {
      dropInfo.valueIsConnectionId = true;
      dropInfo.value = connectionId;
    } else
      dropInfo.value = source_entry->title + "/";

    if (_drop_position == mforms::DropPositionOn) {
      // Drop on a group (or back tile).
      if (is_back_tile)
        dropInfo.group = "*Ungrouped*";
      else
        dropInfo.group = entry->title;
      _owner->trigger_callback(HomeScreenAction::ActionMoveConnectionToGroup, dropInfo);
    } else {
      // Drag from one position to another within a group (root or active group).
      size_t to = _drop_index;
      if (_active_folder)
        to--; // The back tile has no representation in the global list.
      if (_drop_position == mforms::DropPositionRight)
        to++;
      dropInfo.to = to;
      _owner->trigger_callback(HomeScreenAction::ActionMoveConnection, dropInfo);
    }
    result = mforms::DragOperationMove;

    _drop_index = -1;
    set_layout_dirty(true);

    return result;
  }
  return mforms::DragOperationNone;
}

//----------------------------------------------------------------------------------------------------------------------

mforms::View *ConnectionsSection::getContainer() {
  if (_container == nullptr) {
    _container = mforms::manage(new mforms::Box(false));
    _container->set_name("Home Screen Content Host");
    _welcomeScreen = mforms::manage(new ConnectionsWelcomeScreen(_owner));
    if (!_showWelcomeHeading)
      _welcomeScreen->show(false);
    _welcomeScreen->set_name("Home Screen Welcome Page");
    _welcomeScreen->setInternalName("welcomeScreen");
    _welcomeScreen->set_layout_dirty(true);

#ifdef HAS_MIGRATION_ASSISTANT
    _migrationAssistantBanner = mforms::manage(new ConnectionsMigrationBanner(_owner));
    if (!_showMigrationAssistantBanner) {
      _migrationAssistantBanner->show(false);
    }
    _container->add(_migrationAssistantBanner, false, true);
#else
    _migrationAssistantBanner = nullptr;
#endif

    _container->add(this, true, true);
  }
  return _container;
}

//----------------------------------------------------------------------------------------------------------------------

ConnectionsWelcomeScreen *ConnectionsSection::getWelcomeView() {
  getContainer();
  return _welcomeScreen;
}

//----------------------------------------------------------------------------------------------------------------------

mforms::View *ConnectionsSection::get_parent() const {
  return _container->get_parent();
}

//----------------------------------------------------------------------------------------------------------------------

ConnectionsSection::ConnectionVector const& ConnectionsSection::displayed_connections() const {
  if (_filtered)
    return _filtered_connections;
  else if (_active_folder)
    return _active_folder->children;
  else
    return _connections;
}

//----------------------------------------------------------------------------------------------------------------------
