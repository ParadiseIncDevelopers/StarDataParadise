/*
 * Copyright (c) 2008, 2019, Oracle and/or its affiliates. All rights reserved.
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
using System.ComponentModel;
using System.Drawing;
using System.Windows.Forms;

using MySQL.Controls;
using MySQL.Grt;
using MySQL.Grt.Db;

namespace MySQL.GUI.Workbench.Plugins
{
  public partial class DbMysqlSchemaEditor : ObjectEditorPlugin
  {
    #region Member Variables

    private MySQLSchemaEditorWrapper schemaEditorWrapper { get { return Backend as MySQLSchemaEditorWrapper; } } 
    protected bool settingOptions = false;

    #endregion

    #region Constructors

    public DbMysqlSchemaEditor(GrtManager manager, GrtValue value)
      : base(manager)
    {
      InitializeComponent();
      ReinitWithArguments(value);
      ConfigureClassicObjectEditor(mainTabControl);
      ConfigureSchemaPage();

      if (IsEditingLiveObject)
        AdjustEditModeControls(mainTabControl);
    }

    #endregion

    #region Form implemenation

    private void ConfigureSchemaPage()
    {
      nameTextBox.Parent = null;
      optCharset.Parent = null;
      optCollation.Parent = null;
      optComments.Parent = null;
      refactorButton.Parent = null;
      tabPage1.Controls.Clear();
      tabPage1.Padding = new Padding(8);
      var fields = new TableLayoutPanel();
      fields.Dock = DockStyle.Top;
      fields.AutoSize = true;
      fields.ColumnCount = 3;
      fields.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 128));
      fields.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
      fields.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 128));
      fields.RowCount = 4;
      string[] captions = { "Schema name", "Character set", "Collation", "Comment" };
      Control[] editors = { nameTextBox, optCharset, optCollation, optComments };
      for (int row = 0; row < editors.Length; row++)
      {
        fields.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var label = row == 3 ? label7 : new Label();
        label.Text = SdpLanguage.Translate(captions[row]);
        label.Dock = DockStyle.Fill;
        label.TextAlign = ContentAlignment.MiddleLeft;
        label.AutoSize = true;
        label.Margin = new Padding(0, 4, 8, 4);
        fields.Controls.Add(label, 0, row);
        editors[row].Dock = DockStyle.Fill;
        editors[row].Margin = new Padding(0, 4, 8, 4);
        fields.Controls.Add(editors[row], 1, row);
        if (row > 0) fields.SetColumnSpan(editors[row], 2);
        if (row == 3)
        {
          label.Visible = editors[row].Visible;
          editors[row].MinimumSize = new Size(0, 96);
        }
      }
      refactorButton.Text = SdpLanguage.Translate("Rename");
      refactorButton.Dock = DockStyle.Fill;
      refactorButton.AutoSize = true;
      fields.Controls.Add(refactorButton, 2, 0);
      tabPage1.Controls.Add(fields);
    }

    public override bool ReinitWithArguments(GrtValue value)
    {
      InitializingControls = true;
      SuspendLayout();

      try
      {
        Backend = new MySQLSchemaEditorWrapper(value);

        RefreshFormData();
      }
      finally
      {
        ResumeLayout();
        InitializingControls = false;
      }

      Invalidate();

      return true;
    }

    protected override void RefreshFormData()
    {
      if (settingOptions)
        return;

      if (schemaEditorWrapper.is_editing_live_object() && !schemaEditorWrapper.is_new_object())
        nameTextBox.Enabled = false;

      optComments.Visible = label7.Visible = !IsEditingLiveObject;

      try
      {
        InitializingControls = true;
        nameTextBox.Text = schemaEditorWrapper.get_name();

        var charsetLlist = schemaEditorWrapper.get_charset_list();
        optCharset.Items.Clear();
        optCharset.Items.AddRange(charsetLlist.ToArray());

        var charset = schemaEditorWrapper.get_schema_option_by_name("CHARACTER SET");
        var idx = optCharset.FindStringExact(charset);
        if (idx < 0)
          idx = 0;
        optCharset.SelectedIndex = idx;

        var selectedValue = "Default Charset";
        if (optCharset.SelectedItem != null)
          selectedValue = optCharset.SelectedItem.ToString();
        var collation = schemaEditorWrapper.get_charset_collation_list(selectedValue);
        optCollation.Items.Clear();
        optCollation.Items.AddRange(collation.ToArray());

        var collate = schemaEditorWrapper.get_schema_option_by_name("COLLATE");
        idx = optCollation.FindStringExact(collate);
        if (idx < 0)
          idx = 0;
        optCollation.SelectedIndex = idx;

        TabText = schemaEditorWrapper.get_title();
        optComments.Text = schemaEditorWrapper.get_comment();
        refactorButton.Enabled = schemaEditorWrapper.refactor_possible();
      }
      finally
      {
        InitializingControls = false;
      }
    }

    private void setSchemaOpt()
    {
      // When a UI value changes, update the schema options
      if (InitializingControls)
        return;

      settingOptions = true;
      try
      {
        // set charset/collation
        //If there is no "-" in optCollation.Text like in case of "Server default" collation and charset will be reset to ""
        if (optCharset.Text == "Default Charset")
          schemaEditorWrapper.set_schema_option_by_name("CHARACTER SET", "");
        else
          schemaEditorWrapper.set_schema_option_by_name("CHARACTER SET", optCharset.Text);

        if(optCollation.Text == "Default Collation")
          schemaEditorWrapper.set_schema_option_by_name("COLLATE", "");
        else
          schemaEditorWrapper.set_schema_option_by_name("COLLATE", optCollation.Text);

        if (!optComments.Text.Equals(schemaEditorWrapper.get_comment()))
          schemaEditorWrapper.set_comment(optComments.Text);

      }
      finally
      {
        settingOptions = false;
      }
    }

    private void DbMysqlSchemaEditor_Shown(object sender, EventArgs e)
    {
      if (nameTextBox.Enabled)
        nameTextBox.Focus();
    }

    #endregion

    #region Event handling

    private void nameTextBox_TextChanged(object sender, EventArgs e)
    {
      if (!InitializingControls && !nameTextBox.Text.Equals(schemaEditorWrapper.get_name()))
        schemaEditorWrapper.set_name(nameTextBox.Text);

      TabText = schemaEditorWrapper.get_title();
    }

    private void optCollation_SelectedIndexChanged(object sender, EventArgs e)
    {
      setSchemaOpt();
    }

    private void optCharset_SelectedIndexChanged(object sender, EventArgs e) {
      var cbCharset = (ComboBox)sender;
      if (cbCharset != null && cbCharset.SelectedItem != null) {
        var collation = schemaEditorWrapper.get_charset_collation_list(cbCharset.SelectedItem.ToString());
        optCollation.Items.Clear();
        optCollation.Items.AddRange(collation.ToArray());
        optCollation.SelectedIndex = 0;
        setSchemaOpt();
      }
    }

    private void optComments_TextChanged(object sender, EventArgs e)
    {
      setSchemaOpt();
    }

    private void refactorButton_Click(object sender, EventArgs e)
    {
      schemaEditorWrapper.refactor_catalog();
    }

    #endregion

  }
}
