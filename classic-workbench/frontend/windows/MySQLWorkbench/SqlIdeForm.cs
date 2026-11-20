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
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

using MySQL.Controls;
using MySQL.Forms;
using MySQL.Base;
using MySQL.Grt.Db;
using MySQL.GUI.Workbench.Plugins;
using MySQL.Utilities;
using MySQL.Workbench;

namespace MySQL.GUI.Workbench
{
  public partial class SqlIdeForm : Plugins.DockablePlugin, IWorkbenchObserver
  {
    private List<RecordsetView> pendingRelayouts = new List<RecordsetView>();

    private SqlEditorFormWrapper dbSqlEditorBE;
    public SqlEditorFormWrapper Backend { get { return dbSqlEditorBE; } }

    protected WbContext wbContext;

    private int busyTab = -1;

    private bool canTrackChanges = false; // True when initialization is done and UI changes can be tracked.

    private bool outputVisibilySetInAdmin = false;
    private bool outputIsVisible = true;
    private bool restoreOutputArea = false;
    private ToolStripButton[] outputViewButtons;
    private bool secondarySidebarVisibilySetInAdmin = false;
    private bool secondarySidebarIsVisible = false;
    private bool restoreSeconarySidebar = false;
    private bool restoreTaskSidebar = false;
    private bool taskSidebarManuallySet = false;

    #region Initialization

    public SqlIdeForm(WbContext wbContext, UIForm uiForm)
      : base((uiForm as SqlEditorFormWrapper).grt_manager())
    {
      this.wbContext = wbContext;
      dbSqlEditorBE = uiForm as SqlEditorFormWrapper;
      Initialize();
      UpdateColors();
    }

    protected void Initialize()
    {
      Logger.LogInfo("WQE.net", "Launching SQL IDE\n");

      InitializeComponent();
      Font = new Font("Segoe UI", 9.75f, FontStyle.Regular, GraphicsUnit.Point);
      mainContentTabControl.Font = Font;
      mainContentTabControl.RenderWithGlow = false;
      mainContentTabControl.ContentPadding = Padding.Empty;
      mainContentTabControl.TabStyle = FlatTabControl.TabStyleType.QueryDocument;
      mainContentTabControl.BackgroundColor = Color.FromArgb(231, 238, 239);
      mainContentTabControl.ItemPadding = new Padding(10, 0, 5, 0);
      float queryScale;
      using (Graphics graphics = CreateGraphics())
        queryScale = graphics.DpiX / 96f;
      // Set the final geometry before the initial query document is docked.
      mainContentTabControl.ItemSize = new Size((int)Math.Round(120 * queryScale), (int)Math.Round(32 * queryScale));
      editorTabImageList.ImageSize = new Size((int)Math.Round(16 * queryScale), (int)Math.Round(16 * queryScale));
      string queryIcon = System.IO.Path.Combine(Application.StartupPath, "images", "ui", queryScale > 1 ? "sdp_file-code@2x.png" : "sdp_file-code.png");
      if (System.IO.File.Exists(queryIcon))
      {
        using (Image source = Image.FromFile(queryIcon))
          editorTabImageList.Images.Add(new Bitmap(source));
      }
      outputHeader.Text = SdpLanguage.Translate("Output");
      outputSelector.Items.Clear();
      outputSelector.Items.AddRange(new object[] { SdpLanguage.Translate("Messages"), SdpLanguage.Translate("Text output"), SdpLanguage.Translate("Query history") });

      Logger.LogDebug("WQE.net", 1, "Setup of generated UI done\n");
      outputSelector.SelectedIndex = 0;
      outputHeader.Text = String.Empty;
      outputHeader.Padding = Padding.Empty;
      outputHeader.HeaderPadding = Padding.Empty;
      outputSelector.Visible = false;
      outputPaneIcon.Visible = false;
      outputPageToolStrip.Padding = new Padding(6, 0, 6, 0);
      outputPageToolStrip.Height = (int)Math.Round(32 * queryScale);
      outputViewButtons = new ToolStripButton[3];
      string[] outputNames = { "Messages", "Query history", "Text output" };
      string[] outputIcons = { "message", "history", "file-code" };
      int[] outputIndices = { 0, 2, 1 };
      for (int outputIndex = 0; outputIndex < outputViewButtons.Length; ++outputIndex)
      {
        ToolStripButton button = new ToolStripButton(SdpLanguage.Translate(outputNames[outputIndex]));
        button.Name = "workspace.output." + outputIndex;
        button.Tag = outputIndices[outputIndex];
        button.Padding = new Padding(8, 3, 8, 3);
        button.Margin = new Padding(0, 0, 4, 0);
        button.Checked = outputIndices[outputIndex] == outputSelector.SelectedIndex;
        button.ImageScaling = ToolStripItemImageScaling.None;
        string imagePath = System.IO.Path.Combine(Application.StartupPath, "images", "ui", "sdp_" + outputIcons[outputIndex] + (queryScale > 1 ? "@2x.png" : ".png"));
        if (System.IO.File.Exists(imagePath))
          using (Image source = Image.FromFile(imagePath))
            button.Image = new Bitmap(source, new Size((int)Math.Round(16 * queryScale), (int)Math.Round(16 * queryScale)));
        button.Click += delegate(object sender, EventArgs args) { outputSelector.SelectedIndex = (int)((ToolStripButton)sender).Tag; };
        outputViewButtons[outputIndex] = button;
        outputPageToolStrip.Items.Add(button);
      }

      TabText = dbSqlEditorBE.get_title();

      Logger.LogDebug("WQE.net", 2, "Creating Log View\n");
      logView = new GridView(dbSqlEditorBE.log());
      logView.AutoScroll = true;
      logView.RowHeadersVisible = false;
      logView.Parent = actionPanel;
      logView.AllowAutoResizeColumns = false;

      // Some visual setup of the header.
      logView.ColumnHeadersBorderStyle = DataGridViewHeaderBorderStyle.Single;
      logView.ColumnHeadersDefaultCellStyle.Font = ControlUtilities.GetFont("Segoe UI", 9.75f);
      logView.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;

      dbSqlEditorBE.log().refresh_ui_cb(logView.ProcessModelRowsChange);
      logView.ProcessModelChange();
      logView.Columns[0].DefaultCellStyle.Alignment = DataGridViewContentAlignment.MiddleCenter;
      logView.Columns[0].AutoSizeMode = DataGridViewAutoSizeColumnMode.DisplayedCells;
      logView.Columns[1].AutoSizeMode = DataGridViewAutoSizeColumnMode.DisplayedCells;
      logView.Columns[2].AutoSizeMode = DataGridViewAutoSizeColumnMode.DisplayedCells;
      logView.Columns[3].AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
      logView.Columns[4].AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
      logView.Columns[5].AutoSizeMode = DataGridViewAutoSizeColumnMode.DisplayedCells;
      logView.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
      logView.ForeColor = Color.Black;
      logView.CellContextMenuStripNeeded += new DataGridViewCellContextMenuStripNeededEventHandler(logView_CellContextMenuStripNeeded);

      Logger.LogDebug("WQE.net", 1, "Creating History View\n");
      historyEntriesView = new GridView(dbSqlEditorBE.history().entries_model());
      historyEntriesView.AutoScroll = true;
      historyEntriesView.ForeColor = Color.Black;
      historyEntriesView.MultiSelect = false;
      historyEntriesView.RowHeadersVisible = false;
      historyEntriesView.Parent = historySplitContainer.Panel1;
      dbSqlEditorBE.history().entries_model().refresh_ui_cb(ProcessModelHistoryEntryRowsChange);
      historyEntriesView.ProcessModelChange();
      historyEntriesView.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
      historyEntriesView.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
      historyEntriesView.RowEnter += new DataGridViewCellEventHandler(historyEntriesView_RowEnter);
      historyEntriesView.CellContextMenuStripNeeded += historyEntriesView_CellContextMenuStripNeeded;
      historyEntriesView.SetRowSelected(0);
      {
        Logger.LogDebug("WQE.net", 1, "Setting up menu for History View\n");

        // TODO: replace by mforms backed menu. Then remove SqlIdeMenuManager.
        // See if WorkbenchToolbarManager.cs can go entirely when done.
        SqlIdeMenuManager.MenuContext popupMenuContext = new SqlIdeMenuManager.MenuContext();
        popupMenuContext.GetNodesMenuItems = historyEntriesView.Model.get_popup_items_for_nodes;
        popupMenuContext.GetSelectedNodes = historyEntriesView.SelectedNodes;
        popupMenuContext.TriggerNodesAction = historyEntriesView.Model.activate_popup_item_for_nodes;
        SqlIdeMenuManager.InitMenu(historyListMenuStrip, popupMenuContext);
      }

      Logger.LogDebug("WQE.net", 1, "Setting up History Details View\n");
      historyDetailsView = new GridView(dbSqlEditorBE.history().details_model());
      historyDetailsView.AutoScroll = true;
      historyDetailsView.ForeColor = Color.Black;
      historyDetailsView.RowHeadersVisible = false;
      historyDetailsView.Parent = historySplitContainer.Panel2;
      historyDetailsView.CellContextMenuStripNeeded += new DataGridViewCellContextMenuStripNeededEventHandler(historyDetailsView_CellContextMenuStripNeeded);
      dbSqlEditorBE.history().details_model().refresh_ui_cb(historyDetailsView.ProcessModelRowsChange);
      historyDetailsView.ProcessModelChange();
      historyDetailsView.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
      historyDetailsView.Columns[0].AutoSizeMode = DataGridViewAutoSizeColumnMode.DisplayedCells;
      historyDetailsView.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
      historyDetailsView.CellDoubleClick += new DataGridViewCellEventHandler(historyDetailsView_CellDoubleClick);

      Logger.LogDebug("WQE.net", 1, "Setting callbacks to backend\n");
      dbSqlEditorBE.set_post_query_cb(AfterExecSql);
      dbSqlEditorBE.exec_sql_task.progress_cb(OnExecSqlProgress);
      dbSqlEditorBE.set_busy_tab_cb(SetBusyTab);

      Logger.LogDebug("WQE.net", 1, "Setting up side bar\n");
      Control sidebar = dbSqlEditorBE.get_sidebar_control();
      sideArea.Controls.Add(sidebar);
      sidebar.Dock = DockStyle.Fill;

      // Help/snippets palette.
      Logger.LogDebug("WQE.net", 1, "Setting up secondary sidebar\n");
      Control palette = dbSqlEditorBE.get_palette_control();
      rightSplitContainer.Panel1.Controls.Add(palette);
      palette.Dock = DockStyle.Fill;
      palette.Show();

      UpdateColors();

      dbSqlEditorBE.set_docking_delegate(new MainPaneDockDelegate(this));

      ManagedNotificationCenter.AddObserver(this, "GNFormTitleDidChange");

      SdpLanguage.Apply(outputPageToolStrip);
      for (int i = 0; i < outputSelector.Items.Count; ++i)
        outputSelector.Items[i] = SdpLanguage.Translate(outputSelector.Items[i].ToString());

      SdpLanguage.Apply(outputPageToolStrip);
      foreach (DataGridView grid in new DataGridView[] { logView, historyEntriesView, historyDetailsView })
        foreach (DataGridViewColumn column in grid.Columns)
          column.HeaderText = SdpLanguage.Translate(column.HeaderText);

      Logger.LogDebug("WQE.net", 1, "Initialization done\n");
    }

    #endregion

    #region Finalization

    private void Destroy()
    {
      Logger.LogInfo("WQE.net", "Shutting down SQL editor (" + TabText + ")\n");

      ManagedNotificationCenter.RemoveObserver(this, "GNFormTitleDidChange");

      historyDetailsView.Dispose();
      historyEntriesView.Dispose();

      dbSqlEditorBE.Dispose();
    }

    #endregion

    #region Refresh Messages

    /// <summary>
    /// Notifications coming in from the notification center.
    /// </summary>
    /// <param name="name">The name of the notification. We only get called for notifications we want.</param>
    /// <param name="sender">The object that caused the notification to be sent.</param>
    /// <param name="info">Name/Value string pairs for data to be passed.</param>
    public void HandleNotification(string name, IntPtr sender, Dictionary<string, string> info)
    {
      if (name == "GNFormTitleDidChange" && dbSqlEditorBE.form_id() == info["form"])
        TabText = dbSqlEditorBE.get_title();
    }

    public override void PerformCommand(string command)
    {
      Logger.LogDebug("WQE.net", 1, "performing command: + " + command + "\n");
      switch (command)
      {
        case "wb.toggleSidebar":
          if (IsAdminActive()) taskSidebarManuallySet = true;
          mainSplitContainer.Panel1Collapsed = !mainSplitContainer.Panel1Collapsed;
          wbContext.save_state("sidebar_visible", "query_editor", !mainSplitContainer.Panel1Collapsed);
          break;
        case "wb.toggleOutputArea":
          outputIsVisible = !outputIsVisible;
          if (IsAdminActive())
            outputVisibilySetInAdmin = true;
          contentSplitContainer.Panel2Collapsed = !contentSplitContainer.Panel2Collapsed;
          wbContext.save_state("output_visible", "query_editor", !contentSplitContainer.Panel2Collapsed);
          break;
        case "wb.toggleSecondarySidebar":
          secondarySidebarIsVisible = !secondarySidebarIsVisible;
          if (IsAdminActive())
            secondarySidebarVisibilySetInAdmin = true;
          mainContentSplitContainer.Panel2Collapsed = !mainContentSplitContainer.Panel2Collapsed;
          wbContext.save_state("support_sidebar_visible", "query_editor", !mainContentSplitContainer.Panel2Collapsed);
          break;
        case "close_editor":
          mainContentTabControl.CloseTabPage(mainContentTabControl.SelectedTab);
          break;
      }
    }

    private List<String> commands = new List<string>();

    #endregion

    #region SQL Script Execution

    private void SetBusyTab(int tab)
    {
      if (busyTab >= 0)
        mainContentTabControl.SetBusy(busyTab, false);
      busyTab = -1;
      if (tab >= 0)
      {
        mainContentTabControl.SetBusy(tab, true);
        busyTab = tab;
      }
    }

    private int OnExecSqlProgress(float progress, String msg)
    {
      logView.ProcessModelRowsChange();
      return 0;
    }

    private void AfterExecSql()
    {
      Logger.LogDebug("WQE.net", 1, "Post processing sql execution\n");

      if (dbSqlEditorBE.exec_sql_error_count() > 0)
      {
        outputSelector.SelectedIndex = 0;
        dbSqlEditorBE.show_output_area();
      }

      logView.ProcessModelRowsChange();
      logView.AutoResizeColumn(1);
      logView.AutoResizeColumn(2);
      logView.AutoResizeColumn(5);

      if (logView.Rows.Count > 0)
        logView.SetRowSelected(logView.Rows.Count - 1);
    }

    void onSizeChanged(object sender, EventArgs e)
    {
      foreach (RecordsetView view in pendingRelayouts)
      {
        // Workaround for data grids added while the form was minimized.
        // They refuse to compute their own layout until a new resize event occurs.
        view.GridView.Dock = DockStyle.None;
        view.GridView.Dock = DockStyle.Fill;
      }
      pendingRelayouts.Clear();
    }
    #endregion

    #region Event handling

    void mainContentTabControl_SelectedIndexChanged(object sender, EventArgs e)
    {
      Logger.LogDebug("WQE.net", 2, "Switching main content tab\n");

      dbSqlEditorBE.view_switched();
      
      // Switch off output and secondary sidebar if we are switching to an admin page and
      // those were not explicitly switched on previously. Restore them when switching away.
      if (IsAdminActive())
      {
        if (!taskSidebarManuallySet && !mainSplitContainer.Panel1Collapsed)
        {
          restoreTaskSidebar = true;
          mainSplitContainer.Panel1Collapsed = true;
          dbSqlEditorBE.set_tool_item_checked("wb.toggleSidebar", false);
        }
        if (!outputVisibilySetInAdmin && outputIsVisible)
        {
          restoreOutputArea = true;
          outputIsVisible = false;
          contentSplitContainer.Panel2Collapsed = true;
          dbSqlEditorBE.set_tool_item_checked("wb.toggleOutputArea", false);

          // Do not save this temporary state.
        }
        if (!secondarySidebarVisibilySetInAdmin && secondarySidebarIsVisible)
        {
          restoreSeconarySidebar = true;
          secondarySidebarIsVisible = false;
          mainContentSplitContainer.Panel2Collapsed = true;
          dbSqlEditorBE.set_tool_item_checked("wb.toggleSecondarySidebar", false);
        }
      }
      else
      {
        if (restoreTaskSidebar && !taskSidebarManuallySet)
        {
          restoreTaskSidebar = false;
          mainSplitContainer.Panel1Collapsed = false;
          dbSqlEditorBE.set_tool_item_checked("wb.toggleSidebar", true);
        }
        if (restoreOutputArea) // Only set if not manually adjusted in an admin page and it was visible previously.
        {
          restoreOutputArea = false;
          outputIsVisible = true;
          contentSplitContainer.Panel2Collapsed = false;
          dbSqlEditorBE.set_tool_item_checked("wb.toggleOutputArea", true);
        }
        if (restoreSeconarySidebar) // Ditto.
        {
          restoreSeconarySidebar = false;
          secondarySidebarIsVisible = true;
          mainContentSplitContainer.Panel2Collapsed = false;
          dbSqlEditorBE.set_tool_item_checked("wb.toggleSecondarySidebar", true);
        }
      }
      Logger.LogDebug("WQE.net", 2, "Done switching main content tab\n");
    }

    void editorTabControl_TabClosing(object sender, MySQL.Controls.TabClosingEventArgs e)
    {
      Logger.LogDebug("WQE.net", 1, "About to close tab: " + e.page.Text + "\n");

      // Any docked app view (plugin)?
      if (e.page.Tag is AppViewDockContent)
        e.canClose = (e.page.Tag as AppViewDockContent).CanCloseDocument();
      else
      {
        // One of the object editors.
        ITabDocument tabDocument = mainContentTabControl.ActiveDocument;
        if (tabDocument is IWorkbenchDocument)
          e.canClose = (tabDocument as IWorkbenchDocument).CanCloseDocument();
      }
 
      Logger.LogDebug("WQE.net", 1, "Can close tab: " + e.canClose + "\n");
    }

    private void editorTabControl_TabMoving(object sender, TabMovingEventArgs e)
    {
      Logger.LogDebug("WQE.net", 1, "Moving editor tab\n");
 
      dbSqlEditorBE.sql_editor_reorder(e.MovedPage.Tag as AppViewDockContent, e.ToIndex);
    }

    #endregion

    #region IWorkbenchDocument implementation

    public override UIForm BackendForm
    {
      get { return dbSqlEditorBE; }
    }

    #endregion


    private void editorTabControlMouseClick(object sender, MouseEventArgs e)
    {
      switch (e.Button)
      {
        case MouseButtons.Right:
          {
            int clickedIndex = mainContentTabControl.TabIndexFromPosition(e.Location);
            if (clickedIndex < 0)
              return;

            // Keep the found index for later handling in the item click handler.
            editorTabsContextMenuStrip.Tag = clickedIndex;

            copyFullPathToClipboardToolStripMenuItem.Enabled = dbSqlEditorBE.sql_editor_path(clickedIndex).Length > 0;
            Point p = mainContentTabControl.PointToScreen(e.Location);
            editorTabsContextMenuStrip.Show(p);
          }
          break;
      }
    }

    private void editorTabContextMenuItemClick(object sender, EventArgs e)
    {
      ToolStripMenuItem item = sender as ToolStripMenuItem;
      int clickedTab = (int)item.Owner.Tag;

      TabPage page = mainContentTabControl.TabPages[clickedTab];
      switch (item.Tag as string)
      {
        case "1": // Close editor.
          mainContentTabControl.CloseTabPage(page);
          break;
        case "2": // Close all editors.
          // Close them backwards and only exactly the number of editors that are there when we started.
          // Because on close of the last one a new empty one is created which we do not want to close again.
          // Additionally, some tabs might not be closable due to changed content and the user refusing
          // to close them.
          for (int i = mainContentTabControl.TabCount - 1; i >= 0; i--)
            mainContentTabControl.CloseTabPage(mainContentTabControl.TabPages[i]);
          break;
        case "3": // Close all editors but the clicked one.
          // Similar to case 2, but we don't need to care for a newly created editor, as we always
          // keep one open.
          List<TabPage> open_tabs = new List<TabPage>();
          
          for(int index = 0; index < mainContentTabControl.TabCount; index++)
          {
            if(mainContentTabControl.TabPages[index] != page)
              open_tabs.Add(mainContentTabControl.TabPages[index]);
          }

          foreach(TabPage open_page in open_tabs)
            mainContentTabControl.CloseTabPage(open_page);

          break;

        case "5": // Close all of the same type.
          ITabDocument document = mainContentTabControl.DocumentFromIndex(clickedTab);
          bool closeDocument = document != null; // Only two types of pages: documents (table editors) or sql editors.
          for (int i = mainContentTabControl.TabCount - 1; i >= 0; i--)
          {
            if (closeDocument ^ mainContentTabControl.DocumentFromIndex(i) == null)
              mainContentTabControl.CloseTabPage(mainContentTabControl.TabPages[i]);
          }
          break;

        default: // Copy full path to clipboard (only valid for SQL editors).
          dbSqlEditorBE.handle_tab_menu_action(item.Tag as string, clickedTab);
          break;
      }
      Logger.LogDebug("WQE.net", 1, "Done invoking editor menu command\n");
    }

    private GridView logView;

    #region History Panel

    private GridView historyEntriesView;
    private GridView historyDetailsView;

    ContextMenuStrip logViewMenuStrip = null;
    ContextMenuStrip historyListMenuStrip = new ContextMenuStrip();
    ContextMenuStrip historyDetailsMenuStrip = null;

    private void historyEntriesView_CellContextMenuStripNeeded(object sender, DataGridViewCellContextMenuStripNeededEventArgs e)
    {
      e.ContextMenuStrip = historyListMenuStrip;
    }

    void historyDetailsView_CellContextMenuStripNeeded(object sender, DataGridViewCellContextMenuStripNeededEventArgs e)
    {
      if (historyDetailsMenuStrip == null)
      {
        // Prepare context menu strip on first use. Since this is actually an mforms managed
        // menu we don't get item clicks from there. So we hook into the click events here too.
        // TODO: this entire handling needs a general review.
        historyDetailsMenuStrip = dbSqlEditorBE.history().get_details_context_menu();
        foreach (ToolStripItem item in historyDetailsMenuStrip.Items)
          item.Click += new EventHandler(historyDetailsMenuItemClick);
      }

      if (historyDetailsView.SelectedRows.Count > 0)
        e.ContextMenuStrip = historyDetailsMenuStrip;
    }

    private void logView_CellContextMenuStripNeeded(object sender, DataGridViewCellContextMenuStripNeededEventArgs e)
    {
      if (logViewMenuStrip == null)
      {
        // Prepare context menu strip on first use.
        logViewMenuStrip = dbSqlEditorBE.get_log_context_menu();
      }

      {
        List<DataGridViewRow> rows = logView.GetSelectedRows();
        List<int> indexes = new List<int>(rows.Count);
        foreach (DataGridViewRow row in rows)
        {
          indexes.Add(row.Index);
        }
        dbSqlEditorBE.set_log_selection(indexes);
      }

      e.ContextMenuStrip = logViewMenuStrip;
    }

    enum HistoryAction { HistoryAppendToEditor, HistoryReplaceEditor, HistoryCopyToClipboard };

    void historyDetailsMenuItemClick(object sender, EventArgs e)
    {
      Logger.LogDebug("WQE.net", 1, "Invoking history details menu command\n");
      ToolStripItem item = sender as ToolStripItem;
      switch (item.Tag.ToString())
      {
        case "copy_row":
          loadSelectedHistoryItems(HistoryAction.HistoryCopyToClipboard);
          break;
        case "append_selected_items":
          loadSelectedHistoryItems(HistoryAction.HistoryAppendToEditor);
          break;
        case "replace_sql_script":
          loadSelectedHistoryItems(HistoryAction.HistoryReplaceEditor);
          break;
      }
      Logger.LogDebug("WQE.net", 1, "Done invoking history details menu command\n");
    }

    void historyEntriesView_RowEnter(Object sender, DataGridViewCellEventArgs e)
    {
      dbSqlEditorBE.history().current_entry(e.RowIndex);
    }

    void loadSelectedHistoryItems(HistoryAction action)
    {
      Logger.LogDebug("WQE.net", 1, "Loading selected history items\n");
      if (null == historyEntriesView.CurrentRow)
        return;

      List<int> sel_indexes = new List<int>();
      foreach (DataGridViewRow row in historyDetailsView.SelectedRows)
        sel_indexes.Add(row.Index);
      sel_indexes.Sort();
      String sql = dbSqlEditorBE.restore_sql_from_history(historyEntriesView.CurrentRow.Index, sel_indexes);
      String action_name = "";
      switch (action)
      {
      case HistoryAction.HistoryAppendToEditor:
        action_name = "append";
        break;
      case HistoryAction.HistoryReplaceEditor:
        action_name = "replace";
        break;
      case HistoryAction.HistoryCopyToClipboard:
        action_name = "copy";
        break;
      }
      dbSqlEditorBE.handle_history_action(action_name, sql);
      Logger.LogDebug("WQE.net", 1, "Done loading selected history items\n");
    }

    void historyDetailsView_CellDoubleClick(Object sender, DataGridViewCellEventArgs e)
    {
      loadSelectedHistoryItems(HistoryAction.HistoryAppendToEditor);
    }

    private void ProcessModelHistoryEntryRowsChange()
    {
      Logger.LogDebug("WQE.net", 1, "Processing history entry changes\n");
      historyEntriesView.ProcessModelRowsChange();
      int selected_entry = dbSqlEditorBE.history().current_entry();

      if (selected_entry != -1)
        historyEntriesView.SetRowSelected(selected_entry);
    }

    #endregion

    #region Event Handling

    private void DbSqlEditor_Shown(object sender, EventArgs e)
    {
      LoadFormState();
      canTrackChanges = true;

      Logger.LogInfo("WQE.net", "SQL IDE UI is ready\n");
    }

    private void mainSplitContainer_SplitterMoved(object sender, SplitterEventArgs e)
    {
      if (!canTrackChanges || IsDisposed || Disposing)
        return;

      wbContext.save_state("sidebar_width", "query_editor", mainSplitContainer.SplitterDistance);
    }

    private void contentSplitContainer_SplitterMoved(object sender, SplitterEventArgs e)
    {
      if (!canTrackChanges || IsDisposed || Disposing)
        return;

      int splitterDistance = contentSplitContainer.Height - contentSplitContainer.SplitterWidth - contentSplitContainer.SplitterDistance;
      wbContext.save_state("output_height", "query_editor", splitterDistance);
    }

    private void mainContentSplitContainer_SplitterMoved(object sender, SplitterEventArgs e)
    {
      if (!canTrackChanges || IsDisposed || Disposing)
        return;

      int splitterDistance = mainContentSplitContainer.Width - mainContentSplitContainer.SplitterWidth - mainContentSplitContainer.SplitterDistance;
      wbContext.save_state("support_sidebar_width", "query_editor", splitterDistance);
    }

    private void editorSplitContainer_SplitterMoved(object sender, SplitterEventArgs e)
    {
      if (!canTrackChanges || IsDisposed || Disposing)
        return;

      SplitContainer container = sender as SplitContainer;
      int splitterDistance = container.Height - container.SplitterWidth - container.SplitterDistance;
      wbContext.save_state("recordset_height", "query_editor", splitterDistance);
    }

    void outputPaneIndexChanged(object sender, System.EventArgs e)
    {
      ToolStripComboBox selector = sender as ToolStripComboBox;
      if (outputViewButtons != null)
        foreach (ToolStripButton button in outputViewButtons)
          button.Checked = (int)button.Tag == selector.SelectedIndex;
      if (outputPageContent.Controls.Count > 0)
        outputPageContent.Controls.RemoveAt(0);
      switch (selector.SelectedIndex)
      {
        case 0:
          outputPageContent.Controls.Add(actionPanel);
          actionPanel.Dock = DockStyle.Fill;
          break;
        case 1:
          outputPageContent.Controls.Add(textOutputPage);
          textOutputPage.Dock = DockStyle.Fill;
          break;
        case 2:
          outputPageContent.Controls.Add(historyPage);
          historyPage.Dock = DockStyle.Fill;
          break;
      }
    }

    #endregion

    #region Form Layout

    private void SafeAssignSplitterDistance(SplitContainer container, int distance)
    {
      // If the splitter is within any of the min size areas of the two panels set it to the center
      // between both. If that still fails it can only mean the container is smaller than the sum of
      // the min sizes. In that case don't assign a splitter position at all.
      // Note: the orientation gives the orientation of the splitter, not the layout of the container.
      int size = container.Orientation == Orientation.Horizontal ? container.Height : container.Width;
      if (distance < container.Panel1MinSize ||
        distance > size - container.Panel2MinSize)
        distance = (size - container.Panel2MinSize - container.Panel1MinSize) / 2;

      try
      {
      if (distance > 0)
        container.SplitterDistance = distance;
      }
      catch (Exception)
      {
      	// Ignore stupid splitter distance errors. The SplitContainer really should adjust values
        // outside the valid range to a meaningful position.
      }
    }

    private void LoadFormState()
    {
      Logger.LogDebug("WQE.net", 1, "Loading form state\n");
      float scale;
      using (Graphics graphics = CreateGraphics())
        scale = graphics.DpiX / 96f;
      int sidebarDefault = (int)Math.Round(220 * scale);

      // Object side bar.
      int splitterDistance = wbContext.read_state("sidebar_width", "query_editor", sidebarDefault);
      if (splitterDistance < 0)
        splitterDistance = sidebarDefault;

      SafeAssignSplitterDistance(mainSplitContainer, splitterDistance);

      // Output tab. Distance measured from bottom (for easy default value).
      int storedSize = wbContext.read_state("output_height", "query_editor", (int)Math.Round(180 * scale));
      SafeAssignSplitterDistance(contentSplitContainer,
        contentSplitContainer.Height - contentSplitContainer.SplitterWidth - storedSize);

      // Support side bar. Distance measured from right (for easy default value).
      storedSize = wbContext.read_state("support_sidebar_width", "query_editor", (int)Math.Round(280 * scale));
      SafeAssignSplitterDistance(mainContentSplitContainer,
        mainContentSplitContainer.Width - mainContentSplitContainer.SplitterWidth - storedSize);

      // Visibility of the sidebar/output areas.
      bool visible = wbContext.read_state("sidebar_visible", "query_editor", true);
      dbSqlEditorBE.set_tool_item_checked("wb.toggleSidebar", visible);
      mainSplitContainer.Panel1Collapsed = !visible;

      outputIsVisible = wbContext.read_state("output_visible", "query_editor", true);
      dbSqlEditorBE.set_tool_item_checked("wb.toggleOutputArea", outputIsVisible);
      contentSplitContainer.Panel2Collapsed = !outputIsVisible;

      secondarySidebarIsVisible = wbContext.read_state("support_sidebar_visible", "query_editor", false);
      dbSqlEditorBE.set_tool_item_checked("wb.toggleSecondarySidebar", secondarySidebarIsVisible);
      mainContentSplitContainer.Panel2Collapsed = !secondarySidebarIsVisible;
    }
    
    /// <summary>
    /// Docks the given document to the editor tab control (object editors and plugins).
    /// </summary>
    public TabPage DockDocument(ITabDocument document, bool activate)
    {
      Logger.LogDebug("WQE.net", 1, "Docking new document (activate it: " + activate + ")\n");
      if (document is DockablePlugin)
        (document as DockablePlugin).UpdateColors();

      if (!mainContentTabControl.HasDocument(document))
      {
        int index = mainContentTabControl.AddDocument(document);
        var appView = document as AppViewDockContent;
        if (appView != null && appView.GetContextName() == "db.query.QueryBuffer" && editorTabImageList.Images.Count > 0)
          mainContentTabControl.TabPages[index].ImageIndex = 0;
      }
      if (activate)
        document.Activate();

      return mainContentTabControl.SelectedTab;
    }

    public void UndockDocument(ITabDocument document)
    {
      mainContentTabControl.RemoveDocument(document);
    }

    public TabPage SelectedDocument()
    {
      if (mainContentTabControl.TabCount == 0)
        return null;
      return mainContentTabControl.SelectedTab;
    }

    public int DocumentCount()
    {
      return mainContentTabControl.TabCount;
    }

    public TabPage GetDocument(int i)
    {
      if (i < 0 || i > mainContentTabControl.TabCount)
        return null;
      return mainContentTabControl.TabPages[i];
    }

    #endregion

    #region Miscellaneous


    public override void UpdateColors()
    {
      base.UpdateColors();

      Logger.LogDebug("WQE.net", 1, "Updating display colors\n");
      outputPageToolStrip.Renderer = new FlatMainToolStripRenderer();
      outputPageToolStrip.Font = Font;
      outputPageToolStrip.GripStyle = ToolStripGripStyle.Hidden;
      outputPageToolStrip.BackColor = SystemInformation.HighContrast ? SystemColors.Control : Color.FromArgb(231, 238, 239);
      outputPageToolStrip.ForeColor = SystemInformation.HighContrast ? SystemColors.ControlText : Color.FromArgb(16, 38, 41);
      StyleWorkspaceGrid(logView);
      StyleWorkspaceGrid(historyEntriesView);
      StyleWorkspaceGrid(historyDetailsView);

      mainSplitContainer.BackColor = Conversions.GetApplicationColor(ApplicationColor.AppColorMainBackground, false);
      contentSplitContainer.BackColor = Conversions.GetApplicationColor(ApplicationColor.AppColorMainBackground, false);
      mainContentSplitContainer.BackColor = Conversions.GetApplicationColor(ApplicationColor.AppColorMainBackground, false);
      rightSplitContainer.BackColor = Conversions.GetApplicationColor(ApplicationColor.AppColorMainBackground, false);
      historySplitContainer.BackColor = Conversions.GetApplicationColor(ApplicationColor.AppColorMainBackground, false);

      // The side area has the sidebar which is rooted on a split container.
      if (sideArea.Controls.Count > 0)
        sideArea.Controls[0].BackColor = Conversions.GetApplicationColor(ApplicationColor.AppColorMainBackground, false);
    }

    private void StyleWorkspaceGrid(DataGridView grid)
    {
      if (grid == null) return;
      grid.EnableHeadersVisualStyles = false;
      grid.BorderStyle = BorderStyle.None;
      grid.BackgroundColor = SystemInformation.HighContrast ? SystemColors.Window : Color.White;
      grid.GridColor = SystemInformation.HighContrast ? SystemColors.WindowText : Color.FromArgb(209, 220, 224);
      grid.Font = Font;
      grid.ColumnHeadersDefaultCellStyle.Font = Font;
      grid.ColumnHeadersDefaultCellStyle.BackColor = SystemInformation.HighContrast ? SystemColors.Control : Color.FromArgb(231, 238, 239);
      grid.ColumnHeadersDefaultCellStyle.ForeColor = SystemInformation.HighContrast ? SystemColors.ControlText : Color.FromArgb(16, 38, 41);
      grid.DefaultCellStyle.SelectionBackColor = SystemInformation.HighContrast ? SystemColors.Highlight : Color.FromArgb(221, 235, 228);
      grid.DefaultCellStyle.SelectionForeColor = SystemInformation.HighContrast ? SystemColors.HighlightText : Color.FromArgb(16, 38, 41);
    }

    public override bool CanCloseDocument()
    {
      Logger.LogDebug("WQE.net", 1, "Processing CanCloseDocument()\n");

      return Backend.can_close();
    }

    /// <summary>
    /// Returns true if the currently active page in the content tab control is an admin page.
    /// </summary>
    /// <returns></returns>
    private bool IsAdminActive()
    {
      ITabDocument document = mainContentTabControl.ActiveDocument;
      if (document is AppViewDockContent)
      {
        string context = (document as AppViewDockContent).GetContextName();
        if (context == "administrator" || context == "sdp_tools")
          return true;
      }
      return false;
    }

    #endregion

  }

  #region DockingPoint delegate implementation

  public class MainPaneDockDelegate : ManagedDockDelegate
  {
    private SqlIdeForm owner = null;

    public MainPaneDockDelegate(SqlIdeForm owner)
      : base(null)
    {
      this.owner = owner;
    }

    protected override void Dispose(bool disposing)
    {
      base.Dispose(disposing);
    }

    public override String get_type(Object representedObject)
    {
      return "db.query.Editor:main"; // Same as defined in wb_sql_editor_form.h
    }

    public override void dock_view(Object representedObject, AppViewDockContent view, String arg1, int arg2)
    {
      TabPage page = owner.DockDocument(view, true);
      page.Tag = view;
    }

    public override void undock_view(Object representedObject, AppViewDockContent view)
    {
      owner.UndockDocument(view);
    }

    public override bool select_view(Object representedObject, AppViewDockContent view)
    {
      view.Activate();
      return true;
    }

    public override void set_view_title(Object representedObject, AppViewDockContent view, String str)
    {
      view.TabText = str;
    }

    /// <summary>
    /// Meant to return the size of the host of the view, not the view itself.
    /// </summary>
    public override Size get_size(Object representedObject)
    {
      return new Size(0, 0);
    }

    public override AppViewDockContent selected_view()
    {
      TabPage tab = owner.SelectedDocument();
      if (tab != null)
        return tab.Tag as AppViewDockContent;
      return null;
    }

    public override int view_count()
    {
      return owner.DocumentCount();
    }

    public override AppViewDockContent view_at_index(int i)
    {
      TabPage tab = owner.GetDocument(i);
      if (tab != null)
        return tab.Tag as AppViewDockContent;
      return null;
    }
  };

  #endregion

}
