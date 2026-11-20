/*
 * Copyright (c) 2016, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include "mforms/home_screen_documents.h"
#include "base/string_utilities.h"
#include "base/file_utilities.h"
#include "mforms/app.h"

#include "mforms/home_screen.h"

using namespace mforms;

//----------------- DocumentEntry --------------------------------------------------------------------------------------

bool DocumentEntry::operator<(const DocumentEntry &other) const {
  return other.timestamp < timestamp; // Sort from newest do oldest.
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentEntry::setTitle(const std::string &t) {
  title = t;
}

//----------------------------------------------------------------------------------------------------------------------

std::string DocumentEntry::getAccessibilityDescription() {
  return title;
}

//----------------------------------------------------------------------------------------------------------------------

base::Accessible::Role DocumentEntry::getAccessibilityRole() {
  return Accessible::ListItem;
}

//----------------------------------------------------------------------------------------------------------------------

base::Rect DocumentEntry::getAccessibilityBounds() {
  return bounds;
}

//----------------------------------------------------------------------------------------------------------------------

std::string DocumentEntry::getAccessibilityDefaultAction() {
  return _("Open model");
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentEntry::accessibilityDoDefaultAction() {
  if (default_handler)
    default_handler((int)bounds.center().x, (int)bounds.center().y);
}

//----------------- DocumentsSection -----------------------------------------------------------------------------------

DocumentsSection::DocumentsSection(mforms::HomeScreen *owner) : HomeScreenSection("sidebar_modeling.png") {
  _owner = owner;

  _add_button.title = _("New model");
  _add_button.description = _("New model");
  _add_button.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::ActionNewEERModel, base::any());
  };

  _open_button.title = _("Open model");
  _open_button.description = _("Open model file");
  _open_button.defaultHandler = [this]() {
    _owner->trigger_callback(HomeScreenAction::ActionOpenEERModel, base::any());
  };

  _action_button.title = _("Create model");
  _action_button.description = _("Create a model from a database or SQL script");
  _action_button.defaultHandler = [this]() {
    if (_model_action_menu != nullptr)
      _model_action_menu->popup_at(this, static_cast<int>(_action_button.bounds.xcenter()),
      static_cast<int>(_action_button.bounds.ycenter()));
  };
}

//----------------------------------------------------------------------------------------------------------------------

DocumentsSection::~DocumentsSection() {
  if (_model_context_menu != nullptr)
    _model_context_menu->release();

  if (_model_action_menu != nullptr)
    _model_action_menu->release();
  deleteIcons();
}

//----------------------------------------------------------------------------------------------------------------------

std::size_t DocumentsSection::entry_from_point(int x, int y) {
  if (x < DOCUMENTS_LEFT_PADDING || x >= get_width() - DOCUMENTS_RIGHT_PADDING || y < DOCUMENTS_TOP_PADDING)
    return static_cast<std::size_t>(-1);
  size_t index = (y - DOCUMENTS_TOP_PADDING) / DOCUMENTS_ENTRY_HEIGHT;
  if (index < _filtered_documents.size())
    return index;
  return static_cast<std::size_t>(-1);
}

//----------------------------------------------------------------------------------------------------------------------

/**
 * Draws and icon followed by the given text. The given position is that of the upper left corner
 * of the image.
 */
void DocumentsSection::draw_icon_with_text(cairo_t *cr, int x, int y, cairo_surface_t *icon, const std::string &text) {
  base::Size imageSize;
  if (icon != nullptr) {
    imageSize = mforms::Utilities::getImageSize(icon);

    mforms::Utilities::paint_icon(cr, icon, x, y);
    x += (int)imageSize.width + 3;
  }

  cairo_text_extents_t extents;
  cairo_text_extents(cr, text.c_str(), &extents);

  cairo_set_source_rgb(cr, _textColor.red, _textColor.green, _textColor.blue);
  cairo_move_to(cr, x, (int)(y + imageSize.height / 2.0 + extents.height / 2.0));
  cairo_show_text(cr, text.c_str());
  cairo_stroke(cr);
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::draw_entry(cairo_t *cr, const DocumentEntry &entry, bool hot) {
  const bool dark = _owner->isDarkModeActive();
  if (hot) {
    base::Color fill = base::Color::parse(dark ? "#2A3A3E" : "#E1E9EB");
    cairo_set_source_rgb(cr, fill.red, fill.green, fill.blue);
    cairo_rectangle(cr, entry.bounds.left(), entry.bounds.top(), entry.bounds.width(), entry.bounds.height());
    cairo_fill(cr);
  }
  cairo_save(cr);
  cairo_rectangle(cr, entry.bounds.left(), entry.bounds.top(), entry.bounds.width(), entry.bounds.height());
  cairo_clip(cr);
  cairo_set_source_rgb(cr, _textColor.red, _textColor.green, _textColor.blue);
  const double x = entry.bounds.left() + 8;
  const double y = entry.bounds.top();
  cairo_set_line_width(cr, 1);
  cairo_rectangle(cr, x, y + 8, 12, 16);
  cairo_move_to(cr, x + 3, y + 13); cairo_line_to(cr, x + 9, y + 13);
  cairo_move_to(cr, x + 3, y + 17); cairo_line_to(cr, x + 9, y + 17);
  cairo_stroke(cr);
  cairo_select_font_face(cr, "Consolas", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13);
  std::string name = mforms::Utilities::shorten_string(cr, base::basename(entry.path), entry.bounds.width() * .30 - 32);
  cairo_move_to(cr, x + 24, y + 21);
  cairo_show_text(cr, name.c_str());
  cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  base::Color secondary = base::Color::parse(dark ? "#B7C7C8" : "#4F5759");
  cairo_set_source_rgb(cr, secondary.red, secondary.green, secondary.blue);
  const double folderWidth = entry.bounds.width() > 650 ? entry.bounds.width() * .45 : entry.bounds.width() * .65;
  std::string folder = mforms::Utilities::shorten_string(cr, entry.folder, folderWidth - 16);
  cairo_move_to(cr, entry.bounds.left() + entry.bounds.width() * .32, y + 21);
  cairo_show_text(cr, folder.c_str());
  if (entry.bounds.width() > 650) {
    cairo_move_to(cr, entry.bounds.left() + entry.bounds.width() * .78, y + 21);
    cairo_show_text(cr, entry.last_accessed.c_str());
  }
  base::Color rule = base::Color::parse(dark ? "#405359" : "#A3B4BC");
  cairo_set_source_rgb(cr, rule.red, rule.green, rule.blue);
  cairo_move_to(cr, entry.bounds.left(), entry.bounds.bottom() - .5);
  cairo_line_to(cr, entry.bounds.right(), entry.bounds.bottom() - .5);
  cairo_stroke(cr);
  cairo_restore(cr);
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::update_filtered_documents() {
  _filtered_documents.clear();
  _filtered_documents.reserve(_documents.size());
  switch (_display_mode) {
    case ModelsOnly: {
      // std::copy_if is C++11 only, so we do it manually.
      for (DocumentIterator source = _documents.begin(); source != _documents.end(); source++) {
        if (source->is_model)
          _filtered_documents.push_back(*source);
      }
      break;
    }

    case ScriptsOnly: {
      for (DocumentIterator source = _documents.begin(); source != _documents.end(); source++) {
        if (!source->is_model)
          _filtered_documents.push_back(*source);
      }
      break;
    }

    default: // Mixed mode. All types are shown.
      _filtered_documents = _documents;
  }
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::draw_selection_message(cairo_t *cr) {
  // Attach the message to the current active entry as this is what is used when
  // a connection is opened.
  ssize_t column = _active_entry % _entries_per_row;
  ssize_t row = _active_entry / _entries_per_row;
  int hotspot_x = (int)(DOCUMENTS_LEFT_PADDING + (column + 0.5) * DOCUMENTS_ENTRY_WIDTH);
  int hotspot_y = (int)(DOCUMENTS_TOP_PADDING + (row + 1) * DOCUMENTS_ENTRY_HEIGHT);
  base::Rect message_rect =
    base::Rect(hotspot_x - MESSAGE_WIDTH / 2, hotspot_y + POPUP_TIP_HEIGHT, MESSAGE_WIDTH, MESSAGE_HEIGHT);
  if (message_rect.pos.x < 10)
    message_rect.pos.x = 10;
  if (message_rect.right() > get_width() - 10)
    message_rect.pos.x = get_width() - message_rect.width() - 10;

  bool flipped = false;
  if (message_rect.bottom() > get_height() - 10) {
    flipped = true;
    message_rect.pos.y -= MESSAGE_HEIGHT + 2 * POPUP_TIP_HEIGHT + DOCUMENTS_ENTRY_HEIGHT - 10;
  }

  cairo_set_source_rgba(cr, _textColor.red, _textColor.green, _textColor.blue, 0.9);
  cairo_rectangle(cr, message_rect.left(), message_rect.top(), MESSAGE_WIDTH, MESSAGE_HEIGHT);
  cairo_move_to(cr, message_rect.left(), message_rect.top());
  if (flipped) {
    cairo_rel_line_to(cr, MESSAGE_WIDTH, 0);
    cairo_rel_line_to(cr, 0, MESSAGE_HEIGHT);
    cairo_line_to(cr, hotspot_x + POPUP_TIP_HEIGHT, message_rect.bottom());
    cairo_rel_line_to(cr, -POPUP_TIP_HEIGHT, POPUP_TIP_HEIGHT);
    cairo_rel_line_to(cr, -POPUP_TIP_HEIGHT, -POPUP_TIP_HEIGHT);
    cairo_line_to(cr, message_rect.left(), message_rect.bottom());
  } else {
    cairo_line_to(cr, hotspot_x - POPUP_TIP_HEIGHT, message_rect.top());
    cairo_rel_line_to(cr, POPUP_TIP_HEIGHT, -POPUP_TIP_HEIGHT);
    cairo_rel_line_to(cr, POPUP_TIP_HEIGHT, POPUP_TIP_HEIGHT);
    cairo_line_to(cr, message_rect.right(), message_rect.top());
    cairo_rel_line_to(cr, 0, MESSAGE_HEIGHT);
    cairo_rel_line_to(cr, -MESSAGE_WIDTH, 0);
  }

  cairo_fill(cr);

  cairo_set_font_size(cr, mforms::HomeScreenSettings::HOME_DETAILS_FONT_SIZE);
  cairo_font_extents_t extents;
  cairo_font_extents(cr, &extents);

  int y = (int)(message_rect.top() + extents.height + 4);

  cairo_set_source_rgb(cr, _textColor.red, _textColor.green, _textColor.blue);
  cairo_move_to(cr, message_rect.left() + 10, y);
  cairo_show_text(cr, _("Please select a connection"));

  y += (int)ceil(extents.height);
  cairo_move_to(cr, message_rect.left() + 10, y);
  cairo_show_text(cr, _("to open this script with."));

  std::string use_default = _("Use Default");
  cairo_text_extents_t text_extents;
  cairo_text_extents(cr, use_default.c_str(), &text_extents);
  int x = (int)(message_rect.left() + (MESSAGE_WIDTH - text_extents.width) / 2);
  y = (int)message_rect.bottom() - 15;
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, use_default.c_str());
  _use_default_button_rect = base::Rect(x - 7.5, y - ceil(text_extents.height) - 5.5, ceil(text_extents.width) + 16,
                                        ceil(text_extents.height) + 12);
  cairo_rectangle(cr, _use_default_button_rect.left(), _use_default_button_rect.top(), _use_default_button_rect.width(),
                  _use_default_button_rect.height());
  cairo_stroke(cr);

  _close_button_rect = base::Rect(message_rect.right() - imageWidth(_close_icon) - 4, message_rect.top() + 6,
                                  imageWidth(_close_icon), imageHeight(_close_icon));
  cairo_set_source_surface(cr, _close_icon, _close_button_rect.left(), _close_button_rect.top());
  cairo_paint(cr);
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::layout(cairo_t *cr) {
  if (!is_layout_dirty() && _layout_width == get_width())
    return;
  _layout_width = get_width();
  set_layout_dirty(false);
  const double actionLeft = std::max(100, get_width() - 348);
  _model_heading_rect = base::Rect(12, 25, 74, 18);
  _add_button.bounds = base::Rect(actionLeft, 5, 100, 26);
  _open_button.bounds = base::Rect(actionLeft + 104, 5, 100, 26);
  _action_button.bounds = base::Rect(actionLeft + 208, 5, 128, 26);
  update_filtered_documents();
  clearFocusableAreas();
  for (HomeAccessibleButton *button : {&_add_button, &_open_button, &_action_button}) {
    FocusableArea area;
    area.getBounds = [button]() { return button->bounds; };
    area.activate = button->defaultHandler;
    addFocusableArea(area);
  }
  for (size_t index = 0; index < _filtered_documents.size(); ++index) {
    _filtered_documents[index].bounds = base::Rect(DOCUMENTS_LEFT_PADDING,
      DOCUMENTS_TOP_PADDING + index * DOCUMENTS_ENTRY_HEIGHT,
      std::max(1, get_width() - DOCUMENTS_LEFT_PADDING - DOCUMENTS_RIGHT_PADDING), DOCUMENTS_ENTRY_HEIGHT);
    FocusableArea area;
    area.getBounds = [this, index]() { return _filtered_documents[index].bounds; };
    area.activate = [this, index]() {
      base::Point center = _filtered_documents[index].bounds.center();
      accessibleHandler(static_cast<int>(center.x), static_cast<int>(center.y));
    };
    area.showContextMenu = [this, index]() {
      _active_entry = static_cast<ssize_t>(index);
      if (_model_context_menu != nullptr) {
        const base::Rect &bounds = _filtered_documents[index].bounds;
        _model_context_menu->popup_at(this, static_cast<int>(bounds.left()), static_cast<int>(bounds.bottom()));
      }
    };
    addFocusableArea(area);
  }
  _entries_per_row = 1;
}

base::Size DocumentsSection::getLayoutSize(base::Size proposedSize) {
  size_t count = 0;
  for (const DocumentEntry &entry : _documents) {
    if ((_display_mode == ModelsOnly && entry.is_model) || (_display_mode == ScriptsOnly && !entry.is_model) ||
        _display_mode == Mixed)
      ++count;
  }
  proposedSize.height = std::max(proposedSize.height,
    static_cast<double>(DOCUMENTS_TOP_PADDING + count * DOCUMENTS_ENTRY_HEIGHT + 24));
  return proposedSize;
}

//----------------------------------------------------------------------------------------------------------------------

const char* DocumentsSection::getTitle() {
  return _("Models");
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::cancelOperation() {
  _pending_script = "";
  hide_connection_select_message();
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::setFocus() {
  focus();
}

//----------------------------------------------------------------------------------------------------------------------

bool DocumentsSection::canHandle(HomeScreenMenuType type) {
  switch (type) {
    case HomeMenuDocumentModelAction:
    case HomeMenuDocumentModel:
    case HomeMenuDocumentSQLAction:
    case HomeMenuDocumentSQL:
      return true;
    default:
      return false;
  }
  return false;
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::setContextMenu(mforms::Menu *menu, HomeScreenMenuType type) {
  if (canHandle(type) && type == HomeMenuDocumentModel) {
    if (_model_context_menu != NULL)
      _model_context_menu->release();
    _model_context_menu = menu;
    if (_model_context_menu != NULL)
      _model_context_menu->retain();

    menu->set_handler(std::bind(&DocumentsSection::handle_command, this, std::placeholders::_1));
  }
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::setContextMenuAction(mforms::Menu *menu, HomeScreenMenuType type) {
  if (canHandle(type) && type == HomeMenuDocumentModelAction) {
    if (_model_action_menu != NULL)
      _model_action_menu->release();
    _model_action_menu = menu;
    if (_model_action_menu != NULL)
      _model_action_menu->retain();

    menu->set_handler(std::bind(&DocumentsSection::handle_command, this, std::placeholders::_1));
  }
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::updateColors() {
  set_back_color(_owner->isDarkModeActive() ? "#172326" : "#EDF2F4");
  if (_owner->isDarkModeActive()) {
    _textColor = base::Color::parse("#ECF4F3");
  } else {
    _textColor = base::Color::parse("#102629");
  }
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::updateIcons() {
  deleteIcons();

  if (_owner->isDarkModeActive()) {
    _model_icon = mforms::Utilities::load_icon("wb_doc_model.png", true);
    _schema_icon = mforms::Utilities::load_icon("wb_tile_schema_dark.png", true);
    _time_icon = mforms::Utilities::load_icon("wb_tile_time_dark.png", true);
    _folder_icon = mforms::Utilities::load_icon("wb_tile_folder_mini_dark.png", true);
    _plus_icon = mforms::Utilities::load_icon("wb_tile_plus_dark.png");
    _sql_icon = mforms::Utilities::load_icon("wb_doc_sql.png");
    _size_icon = mforms::Utilities::load_icon("wb_tile_number_dark.png");
    _close_icon = mforms::Utilities::load_icon("wb_close.png");
    _open_icon = mforms::Utilities::load_icon("wb_tile_open_dark.png");
    _action_icon = mforms::Utilities::load_icon("wb_tile_more_dark.png");
  } else {
    _model_icon = mforms::Utilities::load_icon("wb_doc_model.png", true);
    _schema_icon = mforms::Utilities::load_icon("wb_tile_schema_light.png", true);
    _time_icon = mforms::Utilities::load_icon("wb_tile_time_light.png", true);
    _folder_icon = mforms::Utilities::load_icon("wb_tile_folder_mini_light.png", true);
    _plus_icon = mforms::Utilities::load_icon("wb_tile_plus_light.png");
    _sql_icon = mforms::Utilities::load_icon("wb_doc_sql.png");
    _size_icon = mforms::Utilities::load_icon("wb_tile_number_light.png");
    _close_icon = mforms::Utilities::load_icon("wb_close.png");
    _open_icon = mforms::Utilities::load_icon("wb_tile_open_light.png");
    _action_icon = mforms::Utilities::load_icon("wb_tile_more_light.png");
  }
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::repaint(cairo_t *cr, int areax, int areay, int areaw, int areah) {
  layout(cr);
  const bool dark = _owner->isDarkModeActive();
  const base::Color accent = base::Color::parse(dark ? "#8FE4C5" : "#0B5F41");
  const base::Color rule = base::Color::parse(dark ? "#405359" : "#A3B4BC");
  cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 16);
  cairo_set_source_rgb(cr, _textColor.red, _textColor.green, _textColor.blue);
  cairo_move_to(cr, 12, 25);
  cairo_show_text(cr, _("Models"));
  cairo_set_font_size(cr, 13);
  cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  for (HomeAccessibleButton *button : {&_add_button, &_open_button, &_action_button}) {
    cairo_set_source_rgb(cr, accent.red, accent.green, accent.blue);
    cairo_move_to(cr, button->bounds.left() + 8, button->bounds.top() + 18);
    cairo_show_text(cr, button->title.c_str());
  }
  cairo_set_source_rgb(cr, rule.red, rule.green, rule.blue);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, 12, 38.5); cairo_line_to(cr, get_width() - 12, 38.5);
  cairo_move_to(cr, 12, DOCUMENTS_TOP_PADDING - .5); cairo_line_to(cr, get_width() - 12, DOCUMENTS_TOP_PADDING - .5);
  cairo_stroke(cr);
  cairo_set_source_rgb(cr, _textColor.red, _textColor.green, _textColor.blue);
  cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  const double width = get_width() - DOCUMENTS_LEFT_PADDING - DOCUMENTS_RIGHT_PADDING;
  cairo_move_to(cr, 44, 56); cairo_show_text(cr, "Model");
  cairo_move_to(cr, 12 + width * .32, 56); cairo_show_text(cr, _("Location"));
  if (width > 650) {
    cairo_move_to(cr, 12 + width * .78, 56);
    cairo_show_text(cr, _("Last opened"));
  }
  if (_filtered_documents.empty()) {
    cairo_select_font_face(cr, "Segoe UI", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_move_to(cr, 20, 92);
    cairo_show_text(cr, _("Create a new model or open an existing .mwb file."));
  }
  for (size_t index = 0; index < _filtered_documents.size(); ++index) {
    const DocumentEntry &entry = _filtered_documents[index];
    if (entry.bounds.bottom() >= areay && entry.bounds.top() <= areay + areah)
      draw_entry(cr, entry, static_cast<ssize_t>(index) == _hot_entry);
  }
  if (_show_selection_message)
    draw_selection_message(cr);
  DrawBox::repaint(cr, areax, areay, areaw, areah);
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::add_document(const std::string &path, const time_t &time, const std::string schemas,
                                    long file_size) {
  DocumentEntry entry;
  entry.path = path;
  entry.timestamp = time;
  entry.schemas = schemas;
  entry.default_handler = std::bind(&DocumentsSection::accessibleHandler, this, std::placeholders::_1, std::placeholders::_2);

  entry.setTitle(base::strip_extension(base::basename(path)));
  if (entry.title.empty())
    entry.title = "???";    //  Don't use setTitle, because it would set the accessibility name to ???, which is an invalid name
  entry.is_model = base::hasSuffix(path, ".mwb") || base::hasSuffix(path, ".mwbd");
  entry.folder = base::dirname(path);

  if (time > 0) {
    struct tm *ptm = localtime(&time);
    char buffer[32];
    strftime(buffer, 32, "%d %b %y, %H:%M", ptm);
    entry.last_accessed = buffer;
  }
  if (file_size == 0)
    entry.size = "--";
  else {
// Format file size in human readable format. 1000 bytes per K on OSX, otherwise 1024.
#ifdef __APPLE__
    double unit_size = 1000;
#else
    double unit_size = 1024;
#endif
    int i = 0;
    double size = file_size;
    const char *units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    while (size > unit_size) {
      size /= unit_size;
      i++;
    }
    entry.size = base::strfmt("%.*f %s", i, size, units[i]);
  }
  _documents.push_back(entry);
  set_layout_dirty(true);
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::clear_documents() {
  _documents.clear();
  _filtered_documents.clear();
  _active_entry = -1;
  _hot_entry = -1;
  clearFocusableAreas();
  set_layout_dirty(true);
}

//----------------------------------------------------------------------------------------------------------------------

bool DocumentsSection::mouse_double_click(mforms::MouseButton button, int x, int y) {
  return this->mouse_click(button, x, y);
}

//----------------------------------------------------------------------------------------------------------------------

bool DocumentsSection::mouse_click(mforms::MouseButton button, int x, int y) {
  switch (button) {
    case mforms::MouseButtonLeft: {
      if (_show_selection_message && _close_button_rect.contains(x, y)) {
        _owner->cancelOperation();
        return true;
      }

      if (_add_button.bounds.contains(x, y)) {
        if (_display_mode != ModelsOnly) {
          _display_mode = ModelsOnly;
          update_filtered_documents();
          set_needs_repaint();
        }

        _owner->trigger_callback(HomeScreenAction::ActionNewEERModel, base::any());
        return true;
      }

      if (_open_button.bounds.contains(x, y)) {
        if (_display_mode != ModelsOnly) {
          _display_mode = ModelsOnly;
          update_filtered_documents();
          set_needs_repaint();
        }

        _owner->trigger_callback(HomeScreenAction::ActionOpenEERModel, base::any());
        return true;
      }

      if (_action_button.bounds.contains(x, y)) {
        if (_display_mode == ModelsOnly && _model_action_menu != NULL)
          _model_action_menu->popup_at(this, x, y);
      }

      if (_model_heading_rect.contains_flipped(x, y)) {
        _owner->cancelOperation();

        if (_display_mode != ModelsOnly) {
          _display_mode = ModelsOnly;
          update_filtered_documents();
          set_needs_repaint();
        }
        return true;
      }

      if (_sql_heading_rect.contains_flipped(x, y)) {
        if (_display_mode != ScriptsOnly) {
          _display_mode = ScriptsOnly;
          update_filtered_documents();
          set_needs_repaint();
        }
        return true;
      }

      if (_mixed_heading_rect.contains_flipped(x, y)) {
        _owner->cancelOperation();

        if (_display_mode != Mixed) {
          _display_mode = Mixed;
          update_filtered_documents();
          set_needs_repaint();
        }
        return true;
      }

      // Anything else.
      _active_entry = entry_from_point(x, y);
      if (_active_entry > -1) {
        _owner->cancelOperation();

        if (_filtered_documents[_active_entry].is_model)
          _owner->trigger_callback(HomeScreenAction::ActionOpenEERModelFromList,
                                   _filtered_documents[_active_entry].path);
        else {
          _pending_script = _filtered_documents[_active_entry].path;
          show_connection_select_message();
        }

        return true;
      }
    } break;

    case mforms::MouseButtonRight: {
      _owner->cancelOperation();

      if (_display_mode == ModelsOnly) {
        _active_entry = entry_from_point(x, y);
        if (_active_entry > -1 && _model_context_menu != NULL) {
          _model_context_menu->popup_at(this, x, y);
          return true;
        }
      }
    } break;

    default:
      break;
  }

  return false;
}

//----------------------------------------------------------------------------------------------------------------------

bool DocumentsSection::mouse_leave() {
  if (_hot_heading != Nothing || _hot_entry > -1) {
    _hot_heading = Nothing;
    _hot_entry = -1;
    set_needs_repaint();
    return true;
  }
  return false;
}

//----------------------------------------------------------------------------------------------------------------------

bool DocumentsSection::mouse_move(mforms::MouseButton button, int x, int y) {
  bool result = false;
  ssize_t entry = entry_from_point(x, y);
  if (entry != _hot_entry) {
    _hot_entry = entry;
    result = true;
  }

  if (entry == -1) {
    DisplayMode mode;
    // No document hit, but perhaps one of the titles.
    if (_model_heading_rect.contains_flipped(x, y))
      mode = ModelsOnly;
    else if (_sql_heading_rect.contains_flipped(x, y))
      mode = ScriptsOnly;
    else if (_mixed_heading_rect.contains_flipped(x, y))
      mode = Mixed;
    else
      mode = Nothing;

    if (mode != _hot_heading) {
      _hot_heading = mode;
      result = true;
    }
  }

  if (result)
    set_needs_repaint();

  return result;
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::handle_command(const std::string &command) {
  if (_active_entry > -1)
    _owner->handleContextMenu(_filtered_documents[_active_entry].path, command);
  else
    _owner->handleContextMenu(base::any(), command);

  _active_entry = -1;
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::show_connection_select_message() {
  _show_selection_message = true;
  set_needs_repaint();
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::hide_connection_select_message() {
  _show_selection_message = false;
  set_needs_repaint();
}

//----------------------------------------------------------------------------------------------------------------------

size_t DocumentsSection::getAccessibilityChildCount() {
  // Initial value due to the add/open/create EER Model icons
  int ret_val = 3;
  ret_val += (int)_filtered_documents.size();

  return ret_val;
}

//----------------------------------------------------------------------------------------------------------------------

base::Accessible* DocumentsSection::getAccessibilityChild(size_t index) {
  base::Accessible* accessible = nullptr;
  switch (index) {
    case 0:
      accessible = &_add_button;
      break;
    case 1:
      accessible = &_open_button;
      break;
    case 2:
      accessible = &_action_button;
      break;
    default: {
      index -= 3;

      if (index < _filtered_documents.size())
        accessible = &_filtered_documents[index];
    }
  }

  return accessible;
}

//----------------------------------------------------------------------------------------------------------------------

base::Accessible::Role DocumentsSection::getAccessibilityRole() {
  return Accessible::List;
}

//----------------------------------------------------------------------------------------------------------------------

base::Accessible* DocumentsSection::accessibilityHitTest(ssize_t x, ssize_t y) {
  base::Accessible* accessible = nullptr;

  if (_add_button.bounds.contains(static_cast<double>(x), static_cast<double>(y)))
    accessible = &_add_button;
  else if (_open_button.bounds.contains(static_cast<double>(x), static_cast<double>(y)))
    accessible = &_open_button;
  else if (_action_button.bounds.contains(static_cast<double>(x), static_cast<double>(y)))
    accessible = &_action_button;
  else {
    ssize_t entry = entry_from_point(static_cast<int>(x), static_cast<int>(y));

    if (entry != -1)
      accessible = &_filtered_documents[entry];
  }

  return accessible;
}

//----------------------------------------------------------------------------------------------------------------------

bool DocumentsSection::accessibleHandler(int x, int y) {
  mouse_move(MouseButtonLeft, x, y);
  return mouse_click(MouseButtonLeft, x, y);
}

//----------------------------------------------------------------------------------------------------------------------

void DocumentsSection::deleteIcons() {
  deleteSurface(_plus_icon);
  deleteSurface(_model_icon);
  deleteSurface(_sql_icon);
  deleteSurface(_schema_icon);
  deleteSurface(_time_icon);
  deleteSurface(_folder_icon);
  deleteSurface(_size_icon);
  deleteSurface(_close_icon);
  deleteSurface(_open_icon);
  deleteSurface(_action_icon);
}

//----------------------------------------------------------------------------------------------------------------------
