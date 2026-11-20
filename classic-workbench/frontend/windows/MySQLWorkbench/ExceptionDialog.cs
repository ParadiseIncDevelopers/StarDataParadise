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

using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using MySQL.Workbench;

namespace MySQL.GUI.Workbench
{
  public partial class ExceptionDialog : Form
  {
    private static ExceptionDialog singleton = new ExceptionDialog();

    private String errorInfo = String.Empty;
    private bool detailsExpanded;
    private bool updatingLayout;

    protected ExceptionDialog()
    {
      InitializeComponent();
      Icon = (Icon)Properties.Resources.MySQLWorkbench.Clone();
      string markPath = Path.Combine(Path.GetDirectoryName(typeof(ExceptionDialog).Assembly.Location), "images", "icons", "MySQLWorkbench-32.png");
      if (File.Exists(markPath))
        using (Image mark = Image.FromFile(markPath)) pictureBox1.Image = new Bitmap(mark);
      else
      {
        pictureBox1.Visible = false;
        headerLayout.ColumnStyles[0].Width = 0;
      }
    }

    public static void Show(String message, String info, WbContext wbcontext)
    {
      if (singleton.IsDisposed)
        singleton = new ExceptionDialog();
      singleton.messageLabel.Text = message ?? String.Empty;
      singleton.errorInfo = (message ?? String.Empty) + Environment.NewLine + Environment.NewLine + (info ?? String.Empty);
      singleton.richTextBox1.Text = singleton.errorInfo;
      singleton.copyStatusLabel.Text = String.Empty;
      singleton.detailsExpanded = false;
      singleton.UpdateDetails(true);
      Form owner = Form.ActiveForm;
      if (owner == singleton || owner == null || owner.IsDisposed)
      {
        owner = null;
        foreach (Form candidate in Application.OpenForms)
          if (candidate != singleton && candidate.Visible && !candidate.IsDisposed)
          {
            owner = candidate;
            if (candidate is MainForm) break;
          }
      }
      singleton.StartPosition = owner == null ? FormStartPosition.CenterScreen : FormStartPosition.CenterParent;
      if (owner == null) singleton.ShowDialog();
      else singleton.ShowDialog(owner);
    }

    protected override void OnShown(EventArgs e)
    {
      base.OnShown(e);
      UpdateDetails(true);
      closeButton.Focus();
    }

    protected override void OnResize(EventArgs e)
    {
      base.OnResize(e);
      if (layout != null && IsHandleCreated)
        UpdateDetails(false);
    }

    private void detailsButton_Click(object sender, EventArgs e)
    {
      detailsExpanded = !detailsExpanded;
      UpdateDetails(true);
      if (detailsExpanded)
        richTextBox1.Focus();
    }

    private void UpdateDetails(bool resizeWindow)
    {
      if (updatingLayout || layout == null)
        return;
      updatingLayout = true;
      try
      {
        float scale;
        using (Graphics graphics = CreateGraphics())
          scale = graphics.DpiY / 96.0f;
        int width = Math.Max(120, ClientSize.Width - layout.Padding.Horizontal);
        messageLabel.MaximumSize = new Size(width - messagePanel.Padding.Horizontal - SystemInformation.VerticalScrollBarWidth, 0);
        Size measured = TextRenderer.MeasureText(messageLabel.Text, messageLabel.Font,
          new Size(messageLabel.MaximumSize.Width, Int32.MaxValue), TextFormatFlags.WordBreak | TextFormatFlags.NoPrefix);
        int messageHeight = Math.Max((int)(36 * scale), Math.Min((int)(120 * scale), measured.Height + 4));
        layout.RowStyles[1].Height = messageHeight + messagePanel.Margin.Vertical;
        layout.RowStyles[4].Height = (int)(17 * scale);
        richTextBox1.Visible = detailsExpanded;
        layout.RowStyles[3].SizeType = detailsExpanded ? SizeType.Percent : SizeType.Absolute;
        layout.RowStyles[3].Height = detailsExpanded ? 100 : 0;
        detailsButton.Text = detailsExpanded ? "Ayrıntıları gizle" : "Teknik ayrıntıları göster";
        detailsButton.AccessibleDescription = detailsExpanded ? "Teknik ayrıntılar açık" : "Teknik ayrıntılar kapalı";
        int fixedHeight = layout.Padding.Vertical +
          headerLayout.GetPreferredSize(new Size(width, 0)).Height + headerLayout.Margin.Vertical +
          messageHeight + messagePanel.Margin.Vertical +
          detailsButton.GetPreferredSize(Size.Empty).Height + detailsButton.Margin.Vertical +
          (int)(17 * scale) + footerLayout.GetPreferredSize(new Size(width, 0)).Height;
        int frameHeight = Height - ClientSize.Height;
        Rectangle working = Screen.FromControl(this).WorkingArea;
        MinimumSize = new Size(Math.Min((int)(520 * scale), working.Width),
          Math.Min(fixedHeight + frameHeight + (detailsExpanded ? (int)(120 * scale) : 0), working.Height));
        if (resizeWindow)
          ClientSize = new Size(ClientSize.Width, Math.Min(fixedHeight + (detailsExpanded ? (int)(220 * scale) : 0), working.Height - frameHeight));
      }
      finally
      {
        updatingLayout = false;
      }
    }

    private void copyInfoButton_Click(object sender, EventArgs e)
    {
      CopyDiagnosticInfo();
    }

    private void copyStackTraceToClipboardToolStripMenuItem_Click(object sender, EventArgs e)
    {
      CopyDiagnosticInfo();
    }

    private void CopyDiagnosticInfo()
    {
      try
      {
        if (errorInfo.Length > 0)
          Clipboard.SetText(errorInfo);
        copyStatusLabel.Text = "Kopyalandı";
      }
      catch (ExternalException)
      {
        copyStatusLabel.Text = "Panoya erişilemedi.";
      }
    }

  }
}
