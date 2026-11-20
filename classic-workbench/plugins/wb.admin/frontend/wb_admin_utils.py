# Copyright (c) 2007, 2021, Oracle and/or its affiliates.
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

import weakref
import sys
import json

from mforms import newLabel
import mforms
from workbench.log import log_error, log_debug3
from workbench.sdp_i18n import tr


CLASSIC_PAGE_TITLES = {
    "Server Status": "Server overview", "Client Connections": "Client sessions",
    "Users and Privileges": "Users and privileges", "Server Variables": "Variables",
    "Data Export": "SQL export", "Data Import": "SQL import",
    "Startup / Shutdown MySQL Server": "Server service", "Server Logs": "Server logs",
    "Options File": "Configuration", "Performance Reports": "Query reports",
    "Performance Dashboard": "Live monitoring", "Performance Schema Setup": "Measurement settings",
    "Performance Schema - Setup": "Measurement settings", "Status and System Variables": "Variables",
    "Data Import/Restore": "SQL import", "Startup / Shutdown": "Server service",
    "Dashboard": "Live monitoring",
}


def read_product_metrics(ctrl_be):
    """Read optional SDP facilities without installing them or treating absence as zero."""
    def scalar(sql):
        result = ctrl_be.exec_query(sql)
        if result and result.nextRow():
            return result.stringByIndex(1)
        return None

    def optional(sql, transform=str):
        try:
            value = scalar(sql)
            return transform(value) if value is not None else tr("Not reported")
        except Exception as exc:
            code = getattr(exc, "error", None)
            if code in (1044, 1045, 1142, 1227, 1370):
                return tr("Access denied")
            if code in (1049, 1146, 1305):
                return tr("Not installed")
            return tr("Unavailable")

    def commands(value):
        payload = json.loads(value)
        values = payload.get("commands")
        if not isinstance(values, list) or not all(isinstance(item, str) for item in values):
            raise ValueError("Invalid capability command list")
        known = {"create_model": "CREATE MODEL", "train_model": "TRAIN MODEL",
                 "predict_model": "PREDICT MODEL", "analyze_shorthand": "ANALYZE"}
        return ", ".join(known.get(item, item) for item in values) or tr("No optional commands enabled")

    metrics = [
        ("SDP SQL commands", optional("SELECT STARDATAPARADISE_CAPABILITIES()", commands),
         "Commands declared by this server; registry installation is checked separately."),
        ("Compatibility mode", optional("SELECT @@SESSION.sdp_compatibility_mode"),
         "Mode of this administration SQL session."),
        ("Model definitions", optional("SELECT COUNT(*) FROM sdp_models.sdp_model_definitions"),
         "Visible definitions in the default Classic model registry."),
        ("Trained model versions", optional("SELECT COUNT(*) FROM sdp_models.sdp_model_versions"),
         "Visible trained versions in the default Classic model registry."),
        ("Compute queue", optional("SELECT COUNT(*) FROM sdp_jobs.compute_jobs WHERE state IN ('queued','running')"),
         "Visible queued and running jobs; missing queue is not counted as zero."),
        ("History SQL routines", optional("SELECT COUNT(DISTINCT ROUTINE_NAME) FROM information_schema.ROUTINES "
                                         "WHERE ROUTINE_NAME IN ('history_log','history_read','history_diff')"),
         "Visible history_log, history_read and history_diff routine names."),
    ]
    try:
        result = ctrl_be.exec_query("SHOW SESSION STATUS LIKE 'Ssl_cipher'")
        cipher = result.stringByIndex(2) if result and result.nextRow() else None
        tls = cipher if cipher else (tr("Unencrypted") if cipher == "" else tr("Not reported"))
    except Exception:
        tls = tr("Unavailable")
    metrics.append(("Administration session TLS", tls,
                    "Cipher reported by this administration connection, not server TLS capability."))
    return metrics


class ClassicProductMetrics(mforms.Box):
    """Dense, flat SDP capability readout shared by overview and performance."""
    def __init__(self, ctrl_be):
        mforms.Box.__init__(self, False)
        self.set_managed()
        self.set_release_on_add()
        self.ctrl_be = ctrl_be
        self.set_spacing(6)
        row = mforms.newBox(True)
        row.set_spacing(8)
        icon = mforms.newImageBox()
        icon.set_image(mforms.App.get().get_resource_path("sdp_database-cog.png"))
        row.add(icon, False, True)
        title = mforms.newLabel(tr("SDP SQL capabilities"))
        title.set_style(mforms.BoldStyle)
        row.add(title, True, True)
        button = mforms.newButton()
        button.set_text(tr("Refresh"))
        button.add_clicked_callback(self.refresh)
        row.add_end(button, False, True)
        self.add(row, False, True)
        self.table = mforms.newTable()
        self.table.set_column_count(2)
        self.table.set_row_count(7)
        self.table.set_column_spacing(12)
        self.table.set_row_spacing(6)
        self.add(self.table, False, True)
        self.rows = []
        self.refresh()

    def refresh(self):
        for control in self.rows:
            self.table.remove(control)
        self.rows = []
        for index, (caption, value, explanation) in enumerate(read_product_metrics(self.ctrl_be)):
            label = mforms.newLabel(tr(caption))
            label.set_tooltip(tr(explanation))
            label.set_text_align(mforms.MiddleLeft)
            content = mforms.newLabel(value)
            content.set_style(mforms.BoldStyle)
            content.set_tooltip(tr(explanation))
            content.set_text_align(mforms.MiddleLeft)
            self.table.add(label, 0, 1, index, index + 1, mforms.HFillFlag | mforms.VFillFlag)
            self.table.add(content, 1, 2, index, index + 1, mforms.HFillFlag | mforms.VFillFlag)
            self.rows.extend((label, content))


def classic_page_header(title, description, right_view=None):
    """Shared dense header for all administrator pages, without decorative art."""
    row = mforms.newBox(True)
    row.set_padding(8)
    row.set_spacing(12)
    row.set_back_color("#EDF2F4")
    heading = mforms.newLabel(tr(CLASSIC_PAGE_TITLES.get(description, description)))
    heading.set_style(mforms.BoldStyle)
    heading.set_text_align(mforms.MiddleLeft)
    row.add(heading, False, True)
    profile = mforms.newLabel(title)
    profile.set_text_align(mforms.MiddleLeft)
    row.add(profile, True, True)
    if right_view:
        row.add_end(right_view, False, True)
    return row

#-------------------------------------------------------------------------------

def weakcb(object, cbname):
    """Create a callback that holds a weak reference to the object. When passing a callback
    for mforms, use this to create a ref to it and prevent circular references that are never freed.
    """
    def call(ref, cbname):
        callback = getattr(ref(), cbname, None)
        if callback is None:
            print("Object has no callback %s"%cbname)
        else:
            return callback()

    return lambda ref=weakref.ref(object): call(ref, cbname)


def no_remote_admin_warning_label(server_instance_settings):
    warning = newLabel(management_requirement(server_instance_settings))
    warning.set_style(mforms.BoldStyle)
    warning.set_wrap_text(True)
    warning.set_text_align(mforms.TopLeft)
    return warning


def management_requirement(profile):
    if profile.uses_ssh:
        return tr("SSH management is not connected. Configure the SSH host and credentials in this connection profile.")
    if profile.uses_wmi:
        return tr("Windows management is not connected. Check the WMI settings in this connection profile.")
    return tr("Host management is not configured. Choose a management method in this connection profile to use service and file actions.")


def make_panel_header(icon, title, subtitle, button=None):
    return classic_page_header(title, subtitle, button)

def show_error_page(parent, text):
    if not hasattr(parent, '_error_label'):
        parent._error_label = None

    if parent._error_label is None:
        parent._error_label = mforms.newLabel(text)
        parent._error_label.set_style(mforms.BoldStyle)
        parent._error_label.set_text_align(mforms.MiddleCenter)
        parent.add(parent._error_label, True, True)

def remove_error_page_if_exists(parent):
    if not hasattr(parent, '_error_label'):
        parent._error_label = None

    if parent._error_label:
        parent.remove(parent._error_label)

class WbAdminValidationBase:
    def __init__(self, error_message = ""):
        self._error_message = error_message

    def validate(self):
        pass

    def set_error_message(self, error_message):
        self._error_message = error_message

    def errorScreen(self):
        warning = newLabel(self._error_message)
        warning.set_wrap_text(True)
        warning.set_text_align(mforms.TopLeft)
        warning.show(True)
        return warning


ValidationErrorServerNotRunning = tr("No server connection. Connect to a running server to open this page.")
ValidationErrorBadConfigFile = tr("No configuration file path is set in this profile. Configure the profile and choose the server configuration file.")


class WbAdminValidationConnection(WbAdminValidationBase):
    def __init__(self, ctrl_be):
        super().__init__(ValidationErrorServerNotRunning)
        self._ctrl_be = ctrl_be

    def validate(self):
        return self._ctrl_be.is_sql_connected()


class WbAdminValidationConfigFile(WbAdminValidationBase):
    def __init__(self, instance_info):
        super().__init__(ValidationErrorBadConfigFile)
        self._instance_info = instance_info

    def validate(self):
        return self._instance_info.config_file_path

class WbAdminValidationRemoteAccess(WbAdminValidationBase):
    def __init__(self, instance_info):
        super().__init__()
        
        self._instance_info = instance_info

        self.set_error_message(management_requirement(self._instance_info))

    def validate(self):
        return self._instance_info.admin_enabled

#
# New features can be easily implemented, now...like:
#   - Auto page update
#       - Update the page periodically (like vars screen). Several pages will benefit from this
#       - Show error screen as soon as the validation don't pass and vice-versa
#   - Show loading screen before the UI page (good for slower tabs, like config file tab, maybe others)
#   - Force minimum height/width depending on the header/body/footer minimuns
#
class WbAdminTabBase(mforms.Box):
    def __init__(self, ctrl_be, instance_info, main_view):
        mforms.Box.__init__(self, False)

        self.set_managed()
        self.set_release_on_add()

        self._instance_info = instance_info
        self._ctrl_be = ctrl_be
        self._main_view = main_view

        self._page_active = False
        self._error_screen_displayed = False

        self._page_header = mforms.newBox(False)

        if sys.platform.lower() == "darwin": # No scrollbox on macOS as this is not needed and breaks selection.
                self._page_body = mforms.newBox(False)
        else:
                self._page_body = mforms.newScrollPanel()
        self._page_body.set_padding(8)

        self._page_footer = mforms.newBox(False)

        self.add(self._page_header, False, True)
        self.add(self._page_body, True, True)
        self.add(self._page_footer, False, True)

        self._header_contents = None
        self._body_contents = None
        self._footer_contents = None

         #if sys.platform.lower() != "darwin": # No scrollbox on macOS as this is not needed and breaks selection.
                #self._body_scroller = mforms.newScrollPanel()
                #self._page_body.add(self._body_scroller, True, True)

        self._validations = []

    @property
    def editor(self):
        return self._main_view.editor

    @property
    def backend(self):
        return self._ctrl_be

    @property
    def ctrl_be(self):
        return self._ctrl_be

    @property
    def instance_info(self):
        return self._instance_info

    @property
    def server_profile(self):
        return self._instance_info

    @property
    def main_view(self):
        return self._main_view

    def validate(self):
        for validation in self._validations:
            if not validation.validate():
                return validation
        return None

    def add_validation(self, validation):
        self._validations.append(validation)

    def create_standard_header(self, icon, title, description, rightViewObject = None):
        return classic_page_header(title, description, rightViewObject)

    def set_header(self, header):
        if self._header_contents:
            self._page_header.remove(self._header_contents)
        self._header_contents = header
        self._header_contents.set_padding(8)
        self._page_header.add(self._header_contents, True, True)

    def set_standard_header(self, icon, title, description):
        self.set_header(self.create_standard_header(icon, title, description))

    def set_body_contents(self, body_contents):
        if self._body_contents:
            if isinstance(self._page_body, mforms.Box):
              self._page_body.remove(self._body_contents)
            else:
              self._page_body.remove()

        self._body_contents = body_contents

        if sys.platform.lower() == "darwin":
                self._page_body.add(self._body_contents, True, True)
        else:
                self._page_body.add(self._body_contents)

    def set_error_screen(self, failedValidation):
        box = mforms.newBox(False)
        box.set_padding(12)
        box.set_spacing(8)
        box.add(failedValidation.errorScreen(), False, True)
        actions = mforms.newBox(True)
        actions.set_spacing(8)
        configure = mforms.newButton()
        configure.set_text("Profili yapılandır")
        configure.add_clicked_callback(self.configure_profile)
        actions.add(configure, False, True)
        retry = mforms.newButton()
        retry.set_text("Yeniden dene")
        retry.add_clicked_callback(weakcb(self, "page_activated"))
        actions.add(retry, False, True)
        box.add(actions, False, True)
        self.set_body_contents(box)
        self._error_screen_displayed = True

    def configure_profile(self):
        # The existing editor persists local/SSH management and configuration paths.
        import wb_admin_grt
        wb_admin_grt.openConnectionSettings(self.editor)

    def set_footer(self, footer):

        if self._footer_contents:
            self._page_footer.remove(self._footer_contents)
        self._footer_contents = footer
        self._footer_contents.set_name("Footer")
        self._footer_contents.set_padding(8)
        self._page_footer.add(self._footer_contents, True, True)

    def ui_created(self):
        return False if self._body_contents is None else True

    def needs_to_create_ui(self):
        if self._error_screen_displayed:
            return True

        if not self._body_contents:
            return True

        return False

    def validation_failed_notification(self, failed_validation):
        pass

    def validation_successful_notification(self):
        pass

    def create_ui(self):
        pass

    def update_ui(self):
        pass

    def page_active(self):
        return self._page_active

    def page_activated(self):
        self._page_active = True
        try:
            self.suspend_layout()

            failed_validation = self.validate()

            if failed_validation:
                # create error screen
                self.validation_failed_notification(failed_validation)
                self.set_error_screen(failed_validation)
            elif self.needs_to_create_ui():
                self.validation_successful_notification()
                self._error_screen_displayed = False
                self.set_body_contents(self.create_ui())
            else:
                self.update_ui()
        except Exception as e:
            import traceback
            traceback.print_exc()
            log_error("Exception activating the page - %s" % str(e))
            # An unsupported server capability must leave an actionable error
            # surface rather than an empty administrator document.
            failed = WbAdminValidationBase("Sayfa yüklenemedi.\n%s" % str(e))
            self.set_error_screen(failed)
        finally:
            self.resume_layout()

    def page_deactivated(self):
        self._page_active = False


class MessageButtonPanel(mforms.Table):
    def __init__(self, icon, title, text, button1, button2=None):
        mforms.Table.__init__(self)
        self.set_managed()
        self.set_release_on_add()

        self.set_padding(-1) # center contents
        self.set_column_spacing(12)
        self.set_row_spacing(12)
        self.set_row_count(4)
        self.set_column_count(2)
        self._buttonBox = None
        if icon:
            image = mforms.newImageBox()
            image.set_image(mforms.App.get().get_resource_path(icon))
            self.add(image, 0, 1, 0, 3, 0)


        self._label = mforms.newLabel(title)
        self._label.set_style(mforms.BigBoldStyle)
        self._label.set_text_align(mforms.MiddleCenter)
        self.add(self._label, 1, 2, 0, 1, mforms.VFillFlag | mforms.HFillFlag)

        self.add(mforms.newLabel(text), 1, 2, 1, 2, mforms.VFillFlag | mforms.HFillFlag)

        if button1 or button2:
            self._buttonBox = mforms.newBox(True)
            self._buttonBox.set_spacing(12)

        if button1:
            button_caption, button_action = button1
            self._button = mforms.newButton()
            self._button.set_text(button_caption)
            self._button.add_clicked_callback(button_action)
            self._buttonBox.add_end(self._button, False, True)
        else:
            self._button = None

        if button2:
            button_caption, button_action = button2
            self._alt_button = mforms.newButton()
            self._alt_button.set_text(button_caption)
            self._alt_button.add_clicked_callback(button_action)
            self._buttonBox.add_end(self._alt_button, False, True)
        else:
            self._alt_button = None

        if button1 or button2:
            self.add(self._buttonBox, 1, 2, 2, 3, mforms.VFillFlag | mforms.HFillFlag)
