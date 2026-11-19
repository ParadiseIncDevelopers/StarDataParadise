# Copyright (c) 2007, 2020, Oracle and/or its affiliates. All rights reserved.
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
import sys


PAGE_TITLES = {
    "Overview": "Kaynak ve hedef",
    "SQL Preview": "SQL önizlemesi",
    "Source Selection": "Kaynak bağlantısı",
    "Target Selection": "Hedef bağlantısı",
    "Fetch Schema List": "Şemaları oku",
    "Fetch Schemas List": "Şemaları oku",
    "Schemas Selection": "Şema seçimi",
    "Schema Selection": "Şema seçimi",
    "Reverse Engineer Source": "Tablo yapısını oku",
    "Source Objects": "Tabloları seç",
    "Migration": "Türleri eşleştir",
    "Manual Editing": "Tablo eşleştirme",
    "Target Creation Options": "SQL önizleme ve seçenekler",
    "Create Schemas": "Hedef şemayı oluştur",
    "Create Target Results": "Oluşturma sonuçları",
    "Data Transfer Setup": "Aktarım seçenekleri",
    "Bulk Data Transfer": "Verileri aktar",
    "Migration Report": "Aktarım raporu",
}


def page_title(name):
    return PAGE_TITLES.get(name.strip(), name.strip())


def style_page(page):
    """Apply the Classic compact palette only to migration pages."""
    page.set_back_color("#EDF2F4")
    page.set_font("Segoe UI 9.75")
    page.content.set_padding(12)
    page.content.set_spacing(8)
    page.content.set_back_color("#EDF2F4")
    if hasattr(page, "button_box"):
        page.button_box.set_padding(8)
        page.button_box.set_spacing(8)
        page.button_box.set_back_color("#E7EEEF")
        page.back_button.set_text("Geri")
        page.cancel_button.set_text("İptal")
        page.cancel_button.show(False)
        caption = {
            "Migration Report": "Bitir",
            "Manual Editing": "Önizlemeye geç",
            "SQL Preview": "Aktarım seçenekleri",
            "Target Creation Options": "SQL'i uygula",
            "Data Transfer Setup": "Aktarımı başlat",
        }.get(page.identifier().strip(), "İleri")
        page.next_button.set_text(caption)
        page.next_button.set_back_color("#096E56")
        page.next_button.set_front_color("#FFFFFF")
        page.next_button.set_font("Segoe UI 9.75")
        page.next_button.set_size(190, 26)
        page.back_button.set_back_color("#EDF2F4")
        page.back_button.set_front_color("#102629")
        page.back_button.set_font("Segoe UI 9.75")
        page.back_button.set_size(76, 26)
        page.cancel_button.set_size(-1, 26)
    for name in ("_tree", "_columns"):
        tree = getattr(page, name, None)
        if tree is not None:
            tree.set_font("Consolas 10")
            tree.set_row_height(30)

#-------------------------------------------------------------------------------
def newHeaderLabel(text):
    widget = mforms.newPanel(mforms.StyledHeaderPanel)
    label = mforms.newLabel(text)
    widget.add(label)
    widget.set_text  = new.instancemethod(mforms.Label.set_text,  label, mforms.Label)
    widget.set_color = new.instancemethod(mforms.Label.set_color, label, mforms.Label)
    widget.set_style = new.instancemethod(mforms.Label.set_style, label, mforms.Label)
    widget.set_text_align = new.instancemethod(mforms.Label.set_text_align, label, mforms.Label)
    return widget

#===============================================================================
#
#===============================================================================
class wbOS(object):
    unknown = "unknown"
    windows = "windows"
    linux   = "linux"
    darwin  = "darwin"

    def __setattr__(self, name, value):
        raise NotImplementedError

#===============================================================================
#
#===============================================================================
class UIProfile(object):
    def __init__(self):
        self.styles = {
                        wbOS.windows :
                             {
                               'main' : (lambda x: x.set_back_color("#293852"),
                                         lambda x: x.set_spacing(6)
                                        ),
                               'page' : (lambda x: x.set_back_color("#ffffff"),),
                               'sidebar-label' : (lambda x: x.set_back_color("#4a6184"),
                                                  lambda x: x.set_color("#ffffff"),
                                                  lambda x: x.set_size(-1, 25)
                                                 ),
                               'content-label' : (lambda x: x.set_back_color("#4a6184"),
                                                  lambda x: x.set_color("#ffffff"),
                                                  lambda x: x.set_size(-1, 25)
                                                 ),
                               'subsection-label' : (lambda x: x.set_back_color("#d9e2ef"),
                                                     lambda x: x.set_color("#000000")
                                                    ),
                               'option-search-panel' : (lambda x: x.set_back_color("#bdc7de"),
                                                        lambda x: x.set_size(-1, 26)
                                                       )
                             },
                        wbOS.linux :
                             {
                               'main' : (lambda x: x.set_padding(0),
                                         lambda x: x.set_spacing(1)
                                        ),
                               'page' : (lambda x: None,),
                               'sidebar-label' : (lambda x: x.set_color("#ffffff"),
                                                  lambda x: x.set_style(mforms.BoldStyle),
                                                  lambda x: x.set_size(-1, 24),
                                                 ),
                               'content-label' : (lambda x: x.set_color("#ffffff"),
                                                  lambda x: x.set_style(mforms.BoldStyle),
                                                  lambda x: x.set_size(-1, 24)
                                                 ),
                               'subsection-label' : (lambda x: x.set_back_color("#d9e2ef"),
                                                     lambda x: x.set_color("#000000")
                                                    ),
                               'option-search-panel' : (
                                                        lambda x: x.set_size(-1, 26),
                                                       )
                             },
                        wbOS.darwin :
                             {
                               'main' : (lambda x: x.set_spacing(1),
                                         lambda x: x.set_back_color("#bbbbbb")),
                               'page' : (lambda x: None, ),
                               'sidebar-label' : (lambda x: x.set_back_color("#efefef"),
                                                  lambda x: x.set_color("#454545"),
                                                  lambda x: x.set_size(-1, 25)
                                                 ),
                               'content-label' : (lambda x: x.set_color("#454545"),
                                                  lambda x: x.set_style(mforms.SmallBoldStyle),
                                                  lambda x: x.set_text_align(mforms.MiddleCenter),
                                                  lambda x: x.set_size(-1, 22)
                                                 ),
                               'subsection-label' : (lambda x: x.set_style(mforms.SmallBoldStyle),
                                                     lambda x: x.set_color("#484950")
                                                    ),
                               'option-search-panel' : (lambda x: x.set_back_color("#f1f1f1"),
                                                        lambda x: x.set_size(-1, 26)
                                                       )
                             }
                      }

    def host_os(self):
        if hasattr(sys, 'getwindowsversion'):
            return wbOS.windows
        elif ('inux' in sys.platform):
            return wbOS.linux
        elif ('arwin' in sys.platform):
            return wbOS.darwin
        return wbOS.unknown

    #---------------------------------------------------------------------------
    def apply_style(self, target, style_name):
        style = None
        os_ui_profile = self.styles.get(self.host_os(), None)
        if os_ui_profile:
            style = os_ui_profile.get(style_name)

        if style is not None:
            for style_part in style:
                style_part(target)
        else:
            print("OS profile has no style '%s'" % style_name)

    @staticmethod
    def newHeaderLabel(text):
        return newHeaderLabel(text)
