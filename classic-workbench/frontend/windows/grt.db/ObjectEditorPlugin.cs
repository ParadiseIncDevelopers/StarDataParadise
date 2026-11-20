/*
 * Copyright (c) 2007, 2019, Oracle and/or its affiliates. All rights reserved.
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
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

using MySQL.Base;
using MySQL.Controls;
using MySQL.Forms;
using MySQL.Grt;
using MySQL.Workbench;

namespace MySQL.GUI.Workbench.Plugins
{
  public class ClassicObjectTreeView : Aga.Controls.Tree.TreeViewAdv
  {
    public override void DrawNode(Aga.Controls.Tree.TreeNodeAdv node, Aga.Controls.Tree.DrawContext context)
    {
      if (node.IsSelected && !SystemInformation.HighContrast)
      {
        using (var brush = new SolidBrush(Color.FromArgb(221, 235, 228)))
          context.Graphics.FillRectangle(brush, new Rectangle(0, context.Bounds.Top, Width, RowHeight));
        context.DrawSelection = Aga.Controls.Tree.DrawSelectionMode.None;
      }
      base.DrawNode(node, context);
    }
  }

  /// <summary>
  /// Generic GRT Object Editor
  /// </summary>
  public partial class ObjectEditorPlugin : DockablePlugin
  {
    #region Member Variables

    /// <summary>
    /// Specifies if the controls on the form are currently getting initialized
    /// </summary>
    private bool initializingControls = false;

    private BaseEditorWrapper backend;
    private SqlEditorWrapper editorBackend;

    private SplitContainer mainSplitContainer = null;
    Button revertLiveObjectButton;
    Button applyLiveObjectButton;

    #endregion

    #region Constructors

    public ObjectEditorPlugin()
      : base()
    {
    }

    public ObjectEditorPlugin(GrtManager manager)
      : base(manager)
    {
    }

    public virtual bool ReinitWithArguments(GrtValue value)
    {
      return false;
    }

    #endregion

    #region Properties

    protected bool InitializingControls
    {
      get { return initializingControls; }
      set { initializingControls = value; }
    }

    public new Control ActiveControl
    {
      get
      {
        if (null != ContainerForm)
          return ContainerForm.ActiveControl;
        else
          return ActiveControl;
      }
      set
      {
        if (null != ContainerForm)
          ContainerForm.ActiveControl = value;
        else
          ActiveControl = value;
      }
    }

    public ObjectEditorView ContainerForm { get; set; }

    protected BaseEditorWrapper Backend
    {
      get { return backend; }
      set
      {
        if (backend != null)
          backend.Dispose();
        if (editorBackend != null)
        {
          Control editor = editorBackend.get_editor_container();
          if (editor.Parent != null)
            editor.Parent.Controls.Remove(editor);
          editor.Dispose();
          editorBackend.Dispose();
          editorBackend = null;
        }

        backend = value;
        if (backend != null)
        {
          backend.set_refresh_ui_handler(CallRefreshFormData);
          backend.set_refresh_partial_ui_handler(CallRefreshPartialFormData);
        }
      }
    }

    #endregion

    #region Notifications

    protected virtual void EditorTextChanged()
    {
      TabText = backend.get_title();
    }

    #endregion

    #region IWorkbenchDocument Interface

    public override UIForm BackendForm
    {
      get { return backend; }
    }

    public override void RefreshGUI(MySQL.Workbench.RefreshType refresh, String str, IntPtr ptr)
    {
    }

    public override void PerformCommand(String command)
    {
    }

    public override DockablePlugin FindPluginOfType(Type type)
    {
      if (GetType() == type)
        return this;
      return null;
    }

    public override bool ClosePluginOfType(Type type)
    {
      if (GetType() == type)
      {
        Close();
        return true;
      }
      return false;
    }

    public override bool CanCloseDocument()
    {
      bool result = base.CanCloseDocument();

      if (!result)
        return false;

      result = backend.can_close();
      if (!result)
        return false;

      return true;
    }

    public override void CloseDocument()
    {
      if (backend != null)
        backend.disable_auto_refresh();

      base.CloseDocument();
    }

    #endregion

    #region Form Implementation

    protected void ConfigureClassicObjectEditor(FlatTabControl tabs)
    {
      Font = new Font("Segoe UI", 9.75f, FontStyle.Regular, GraphicsUnit.Point);
      StyleClassicControl(this);
      SdpLanguage.Apply(this);
      tabs.Alignment = TabAlignment.Top;
      tabs.TabStyle = FlatTabControl.TabStyleType.Classic;
      tabs.Font = Font;
      tabs.ItemSize = new Size(56, Math.Max(28, Font.Height + 12));
      tabs.ItemPadding = new Padding(10, 0, 10, 0);
      tabs.ContentPadding = Padding.Empty;
      tabs.RenderWithGlow = false;
      tabs.BackgroundColor = SystemInformation.HighContrast ? SystemColors.Control : Color.FromArgb(237, 242, 244);
    }

    private void StyleClassicControl(Control control)
    {
      if (control.GetType().Name.IndexOf("Scintilla", StringComparison.OrdinalIgnoreCase) >= 0) return;
      control.Font = Font;
      if (Object.Equals(control.Tag, "sdp.data")) return;
      if (control is TabPage || control is Panel || control is GroupBox)
        control.BackColor = SystemInformation.HighContrast ? SystemColors.Control : Color.FromArgb(237, 242, 244);
      if (control is Label || control is TabPage || control is GroupBox || control is Button || control is CheckBox || control is RadioButton)
      {
        control.ForeColor = SystemInformation.HighContrast ? SystemColors.ControlText : Color.FromArgb(16, 38, 41);
        control.Text = SdpLanguage.Translate(control.Text);
      }
      Aga.Controls.Tree.TreeViewAdv tree = control as Aga.Controls.Tree.TreeViewAdv;
      if (tree != null)
      {
        tree.RowHeight = Math.Max(26, Font.Height + 10);
        tree.FullRowSelect = true;
        tree.GridColor = Color.FromArgb(210, 221, 225);
        foreach (Aga.Controls.Tree.TreeColumn column in tree.Columns)
        {
          column.Header = SdpLanguage.Translate(column.Header);
          column.TooltipText = SdpLanguage.Translate(column.TooltipText);
        }
      }
      Button button = control as Button;
      if (button != null)
      {
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderColor = Color.FromArgb(163, 180, 188);
        button.MinimumSize = new Size(button.MinimumSize.Width, Math.Max(28, Font.Height + 10));
      }
      PictureBox picture = control as PictureBox;
      if (picture != null && picture.Width >= 32 && picture.Height >= 32) picture.Visible = false;
      Panel panel = control as Panel;
      if (panel != null && panel.Padding == new Padding(1)) panel.Padding = Padding.Empty;
      foreach (Control child in control.Controls) StyleClassicControl(child);
    }

    protected static Image ClassicEditorIcon(string name)
    {
      string path = Path.Combine(Application.StartupPath, "images", "ui", "sdp_" + name + ".png");
      if (!File.Exists(path)) return null;
      using (Image source = Image.FromFile(path)) return new Bitmap(source);
    }

    protected void ConfigureClassicDefinitionPage(TabPage page, Control editor, TextBox name, string caption)
    {
      editor.Parent = null;
      name.Parent = null;
      page.Controls.Clear();
      page.Padding = new Padding(8);
      var heading = new TableLayoutPanel();
      heading.Dock = DockStyle.Top;
      heading.Height = Math.Max(34, Font.Height + 14);
      heading.ColumnCount = 3;
      heading.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 116));
      heading.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 240));
      heading.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
      var label = new Label();
      label.Text = SdpLanguage.Translate(caption);
      label.Dock = DockStyle.Fill;
      label.TextAlign = ContentAlignment.MiddleLeft;
      heading.Controls.Add(label, 0, 0);
      name.Dock = DockStyle.Fill;
      name.Margin = new Padding(0, 4, 8, 4);
      heading.Controls.Add(name, 1, 0);
      var hint = new Label();
      hint.Text = SdpLanguage.Translate("The name is read from the SQL definition.");
      hint.ForeColor = SystemInformation.HighContrast ? SystemColors.ControlText : Color.FromArgb(79, 87, 89);
      hint.Dock = DockStyle.Fill;
      hint.TextAlign = ContentAlignment.MiddleLeft;
      hint.AutoEllipsis = true;
      heading.Controls.Add(hint, 2, 0);
      editor.Dock = DockStyle.Fill;
      editor.Padding = Padding.Empty;
      editor.BackColor = SystemInformation.HighContrast ? SystemColors.Window : Color.White;
      page.Controls.Add(editor);
      page.Controls.Add(heading);
    }

    public Control SetupEditorOnHost(Control host, bool coloring)
    {
      if (backend != null && editorBackend == null)
        editorBackend = SqlEditorWrapper.get_sql_editor(backend);

      if (editorBackend != null)
      {
        if (coloring)
        {
          editorBackend.set_sql_check_enabled(true);
          editorBackend.set_language("mysql");
        }
        else
        {
          editorBackend.set_sql_check_enabled(false);
          editorBackend.set_language("");
        }

        Control editor = editorBackend.get_editor_container(); // The host of code editor, toolbar etc.
        host.Controls.Add(editor);
        editor.Dock = DockStyle.Fill;

        return editor;
      }

      return null;
    }

    public bool IsEditingLiveObject
    {
      get { return (null == backend) ? false : backend.is_editing_live_object(); }
    }

    protected void AdjustEditModeControls(Control mainTabControl)
    {
      bool isEditingLiveObject = IsEditingLiveObject;
      bool isEditingLiveObjectUI = ((mainSplitContainer != null) && (mainTabControl.Parent == mainSplitContainer.Panel1));
      if (isEditingLiveObject == isEditingLiveObjectUI)
        return;

      Control mainContainer = (isEditingLiveObjectUI) ? mainSplitContainer.Parent : mainTabControl.Parent;
      mainContainer.SuspendLayout();

      try
      {
        if (isEditingLiveObject)
        {
          if (null == mainSplitContainer)
          {
            mainSplitContainer = new SplitContainer();
            mainSplitContainer.Dock = DockStyle.Fill;
            mainSplitContainer.Orientation = Orientation.Horizontal;
            mainSplitContainer.SplitterWidth = 1;
            mainSplitContainer.FixedPanel = FixedPanel.Panel2;

            FlowLayoutPanel liveObjectControlsPanel = new FlowLayoutPanel();
            liveObjectControlsPanel.FlowDirection = FlowDirection.RightToLeft;
            liveObjectControlsPanel.WrapContents = false;
            liveObjectControlsPanel.Padding = new Padding(8, 4, 8, 4);
            liveObjectControlsPanel.Parent = mainSplitContainer.Panel2;
            liveObjectControlsPanel.Dock = DockStyle.Fill;

            {
              revertLiveObjectButton = new Button();
              revertLiveObjectButton.UseVisualStyleBackColor = false;
              revertLiveObjectButton.Parent = liveObjectControlsPanel;
              revertLiveObjectButton.FlatStyle = FlatStyle.Flat;
              revertLiveObjectButton.FlatAppearance.BorderColor = Color.FromArgb(163, 180, 188);
              revertLiveObjectButton.BackColor = Color.FromArgb(237, 242, 244);
              revertLiveObjectButton.AutoSize = true;
              revertLiveObjectButton.MinimumSize = new Size(100, Math.Max(28, Font.Height + 12));
              revertLiveObjectButton.Text = SdpLanguage.Translate("Revert");
              revertLiveObjectButton.Location = new Point(
                mainSplitContainer.Panel2.ClientSize.Width - revertLiveObjectButton.Width - revertLiveObjectButton.Margin.Right,
                revertLiveObjectButton.Margin.Top);
              revertLiveObjectButton.Click += new EventHandler(revertChangesToLiveObjectButton_Click);
            }
            {
              applyLiveObjectButton = new Button();
              applyLiveObjectButton.UseVisualStyleBackColor = false;
              applyLiveObjectButton.Parent = liveObjectControlsPanel;
              applyLiveObjectButton.FlatStyle = FlatStyle.Flat;
              applyLiveObjectButton.FlatAppearance.BorderSize = 0;
              applyLiveObjectButton.BackColor = Color.FromArgb(4, 49, 51);
              applyLiveObjectButton.ForeColor = Color.White;
              applyLiveObjectButton.AutoSize = true;
              applyLiveObjectButton.MinimumSize = new Size(140, Math.Max(28, Font.Height + 12));
              applyLiveObjectButton.Text = SdpLanguage.Translate("Review SQL");
              applyLiveObjectButton.Location = new Point(
                revertLiveObjectButton.Location.X - revertLiveObjectButton.Margin.Left - applyLiveObjectButton.Width - applyLiveObjectButton.Margin.Right,
                revertLiveObjectButton.Location.Y);
              applyLiveObjectButton.Click += new EventHandler(applyChangesToLiveObjectButton_Click);
            }

            liveObjectControlsPanel.Controls.SetChildIndex(applyLiveObjectButton, 0);
            mainSplitContainer.Panel2MinSize = revertLiveObjectButton.Height + revertLiveObjectButton.Margin.Vertical + 8;
            mainSplitContainer.SplitterDistance = mainContainer.ClientSize.Height - mainSplitContainer.Panel2MinSize - mainSplitContainer.SplitterWidth;

          }

          mainSplitContainer.Parent = mainTabControl.Parent;
          mainTabControl.Parent = mainSplitContainer.Panel1;

       }
        else
        {
          mainSplitContainer.Parent = null;
          mainTabControl.Parent = mainContainer;
        }
      }
      finally
      {
        mainContainer.ResumeLayout();
      }
    }

    private void applyChangesToLiveObjectButton_Click(object sender, EventArgs e)
    {
      backend.apply_changes_to_live_object();
    }

    private void revertChangesToLiveObjectButton_Click(object sender, EventArgs e)
    {
      backend.revert_changes_to_live_object();
    }

    public override string TabText
    {
      set
      {
        base.TabText = value;
        if (null != ContainerForm)
          ContainerForm.TabText = value;
      }
    }

    public bool ShouldCloseOnObjectDelete(String oid)
    {
      return backend.should_close_on_delete_of(oid);
    }

    protected virtual void RefreshFormData() {}

    private void CallRefreshFormData() 
    {
      if (InvokeRequired)
        Invoke((Action)(() => RefreshFormData()));
      else
        RefreshFormData();
    }

    protected virtual void RefreshPartialFormData(int what)
    {
      // Some general handling for all plugins. For handling of other refresh types
      // override this method in the particular plugin.
      switch ((BaseEditorWrapper.PartialRefreshType)what)
      {
        case BaseEditorWrapper.PartialRefreshType.RefreshTextChanged:
          EditorTextChanged();
          break;
      }
    }

    private void CallRefreshPartialFormData(int what) 
    {
      if (InvokeRequired)
        Invoke((Action)(() => RefreshPartialFormData(what)));
      else
        RefreshPartialFormData(what);
    }

    public void ActivateEditor()
    {
      ActiveControl = editorBackend.get_editor_container();
    }

    #endregion


  }
}
