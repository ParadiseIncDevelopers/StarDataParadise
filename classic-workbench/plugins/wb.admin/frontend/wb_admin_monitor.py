# Copyright (c) 2007, 2019, Oracle and/or its affiliates. All rights reserved.
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
from workbench.sdp_i18n import tr
from wb_admin_utils import ClassicProductMetrics

UPDATE_INTERVAL = 3


def runtime_metric_values(status):
    """Return truthful cached counters, distinguishing missing counters from zero."""
    def value(name):
        raw = status.get(name)
        return str(raw) if raw is not None else tr("Not reported")

    values = [
        ("Connected sessions", value("Threads_connected")),
        ("Active sessions", value("Threads_running")),
        ("Completed SQL statements", value("Questions")),
        ("Received bytes", value("Bytes_received")),
        ("Sent bytes", value("Bytes_sent")),
    ]
    try:
        total = float(status["Innodb_buffer_pool_pages_total"])
        free = float(status["Innodb_buffer_pool_pages_free"])
        buffer = "%.1f%%" % (100.0 * (total - free) / total) if total > 0 else tr("Not reported")
    except (KeyError, ValueError, TypeError):
        buffer = tr("Not reported")
    values.append(("InnoDB buffer usage", buffer))
    return values


class WbAdminMonitor(mforms.Box):
    """Flat native status/readout instead of legacy textured bitmap widgets."""
    def __init__(self, server_profile, ctrl_be, include_product=True):
        mforms.Box.__init__(self, False)
        self.set_managed()
        self.set_release_on_add()
        self.set_spacing(12)
        self.ctrl_be = ctrl_be
        self._timer = None
        self._labels = {}
        title = mforms.newLabel(tr("Server activity"))
        title.set_style(mforms.BoldStyle)
        self.add(title, False, True)
        self.status_label = mforms.newLabel(tr("Not reported"))
        self.status_label.set_style(mforms.BoldStyle)
        self.add(self.status_label, False, True)
        table = mforms.newTable()
        table.set_column_count(2)
        table.set_row_count(6)
        table.set_column_spacing(12)
        table.set_row_spacing(8)
        for index, (caption, value) in enumerate(runtime_metric_values(ctrl_be.status_variables)):
            label = mforms.newLabel(tr(caption))
            result = mforms.newLabel(value)
            result.set_style(mforms.BoldStyle)
            table.add(label, 0, 1, index, index + 1, mforms.HFillFlag | mforms.VFillFlag)
            table.add(result, 1, 2, index, index + 1, mforms.HFillFlag | mforms.VFillFlag)
            self._labels[caption] = result
        self.add(table, False, True)
        note = mforms.newLabel(tr("Counters are read from the server; no operating-system CPU estimate is shown."))
        note.set_style(mforms.SmallStyle)
        note.set_wrap_text(True)
        self.add(note, False, True)
        if include_product:
            self.product_metrics = ClassicProductMetrics(ctrl_be)
            self.add(self.product_metrics, False, True)
        self._timer = mforms.Utilities.add_timeout(UPDATE_INTERVAL, self.update)

    def update(self):
        for caption, value in runtime_metric_values(self.ctrl_be.status_variables):
            self._labels[caption].set_text(value)
        self.refresh_status(self.ctrl_be.is_server_running(verbose=False))
        return True

    def refresh_status(self, status):
        text = {"running": "Running", "started": "Running", "stopped": "Stopped",
                "offline": "Offline"}.get(status, "Not reported")
        self.status_label.set_text(tr(text))

    def stop(self):
        if self._timer:
            mforms.Utilities.cancel_timeout(self._timer)
            self._timer = None
