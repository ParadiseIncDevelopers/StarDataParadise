// StarDataParadise UI language catalogs. Framework-only; no network provider.
using System;
using System.Collections.Generic;
using System.IO;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace MySQL.Controls
{
  public static class SdpLanguage
  {
    private static readonly object sync = new object();
    private static Dictionary<string, string> messages;
    private static Dictionary<string, string> sources;
    private static string language;

    private static string Key(string value) { return value.Replace("&", "").Replace("_", ""); }

    private static void Load()
    {
      if (messages != null) return;
      messages = new Dictionary<string, string>(StringComparer.Ordinal);
      sources = new Dictionary<string, string>(StringComparer.Ordinal);
      language = Environment.GetEnvironmentVariable("SDP_UI_LANGUAGE") == "en" ? "en" : "tr";
      string directory = Environment.GetEnvironmentVariable("SDP_UI_LOCALES");
      if (String.IsNullOrEmpty(directory)) directory = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "locales");
      if (!Directory.Exists(directory)) return;
      var serializer = new JavaScriptSerializer();
      string[] files = Directory.GetFiles(directory, "tr*.json");
      Array.Sort(files, StringComparer.Ordinal);
      foreach (string file in files)
      {
        try
        {
          var catalog = serializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(file));
          foreach (var pair in catalog)
          {
            if (pair.Value == null) continue;
            messages[Key(pair.Key)] = pair.Value;
            if (!sources.ContainsKey(Key(pair.Value))) sources.Add(Key(pair.Value), pair.Key);
          }
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
        catch (ArgumentException) { }
        catch (InvalidOperationException) { }
      }
    }

    public static string Translate(string source)
    {
      if (String.IsNullOrEmpty(source)) return source;
      lock (sync)
      {
        Load();
        if (language == "en" && messages.ContainsKey(Key(source))) return source;
        string translated;
        return (language == "en" ? sources : messages).TryGetValue(Key(source), out translated) ? translated : source;
      }
    }

    public static void Apply(ToolStrip strip)
    {
      if (strip == null || strip.IsDisposed) return;
      foreach (ToolStripItem item in strip.Items) ApplyItem(item);
    }

    private static void ApplyItem(ToolStripItem item)
    {
      // Combo boxes and text entries contain enum values, identifiers or user input.
      if (!(item is ToolStripTextBox) && !(item is ToolStripComboBox)) item.Text = Translate(item.Text);
      item.ToolTipText = Translate(item.ToolTipText);
      var dropDown = item as ToolStripDropDownItem;
      if (dropDown != null) foreach (ToolStripItem child in dropDown.DropDownItems) ApplyItem(child);
    }

    public static void Apply(Control root)
    {
      if (root == null || root.IsDisposed) return;
      if (String.Equals(root.Tag as string, "sdp.data", StringComparison.Ordinal)) return;
      // Presentation controls only. Textboxes, SQL editors, tree row values and
      // grid cells are deliberately excluded to preserve SQL and server data.
      if (root is Label || root is ButtonBase || root is GroupBox || root is TabPage || root is Form)
        root.Text = Translate(root.Text);
      var strip = root as ToolStrip;
      if (strip != null) Apply(strip);
      var grid = root as DataGridView;
      if (grid != null) foreach (DataGridViewColumn column in grid.Columns) column.HeaderText = Translate(column.HeaderText);
      if (root.ContextMenuStrip != null) Apply(root.ContextMenuStrip);
      foreach (Control child in root.Controls) Apply(child);
    }
  }
}
