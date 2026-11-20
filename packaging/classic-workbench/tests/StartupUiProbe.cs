// Run against the actual packaged assemblies. No Workbench context or database
// is created; the optional preview hosts the production controls for visual QA.
using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal static class StartupUiProbe
{
  private static string runtime;
  private static int checks;
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
  private static extern bool SetDllDirectory(string path);
  [DllImport("user32.dll")]
  private static extern bool SetProcessDPIAware();

  private static void Check(bool condition, string name)
  {
    if (!condition) throw new Exception(name);
    ++checks;
    Console.WriteLine("PASS " + name);
  }

  private static object Field(object target, string name)
  {
    return target.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).GetValue(target);
  }

  [STAThread]
  private static int Main(string[] args)
  {
    try
    {
      runtime = Path.GetFullPath(args[0]);
      SetDllDirectory(runtime);
      AppDomain.CurrentDomain.AssemblyResolve += delegate(object sender, ResolveEventArgs e) {
        string path = Path.Combine(runtime, new AssemblyName(e.Name).Name + ".dll");
        return File.Exists(path) ? Assembly.LoadFrom(path) : null;
      };
      SetProcessDPIAware();
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);
      var controls = Assembly.LoadFrom(Path.Combine(runtime, "WBControls.dll"));
      var tabType = controls.GetType("MySQL.Controls.FlatTabControl", true);
      var tabs = (TabControl)Activator.CreateInstance(tabType);
      var tabHost = new Form();
      tabHost.ShowInTaskbar = false;
      tabHost.StartPosition = FormStartPosition.Manual;
      tabHost.Location = new Point(-2000, -2000);
      tabHost.Controls.Add(tabs);
      tabs.Size = new Size(700, 160);
      tabs.Margin = Padding.Empty;
      tabType.GetProperty("TabStyle").SetValue(tabs,
        Enum.Parse(tabType.GetNestedType("TabStyleType"), "QueryDocument"), null);
      tabType.GetProperty("ShowCloseButton").SetValue(tabs, true, null);
      tabs.ItemSize = new Size(120, 19);
      tabs.TabPages.Add("Query 1");
      tabs.SelectedIndex = 0;
      tabHost.Show();
      Application.DoEvents();
      var getRect = tabType.GetMethod("GetTabRect", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly);
      var itemSize = tabType.GetProperty("ItemSize", BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
        ?? typeof(TabControl).GetProperty("ItemSize");
      Check(((Rectangle)getRect.Invoke(tabs, new object[] { 0 })).Height == 19, "initial tab geometry cached");
      itemSize.SetValue(tabs, new Size(120, 32), null);
      using (var bitmap = new Bitmap(tabs.Width, tabs.Height))
      {
        using (Graphics graphics = Graphics.FromImage(bitmap))
          tabType.GetMethod("DrawControl", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(tabs, new object[] { graphics });
        Rectangle first = (Rectangle)getRect.Invoke(tabs, new object[] { 0 });
        Color pixel = bitmap.GetPixel(first.Left + 4, first.Bottom - 3);
        Check(pixel.ToArgb() == Color.White.ToArgb(),
          "selected first tab fills its complete height (pixel=" + pixel + ", selected=" + tabs.SelectedIndex + ")");
      }
      Check(((Rectangle)getRect.Invoke(tabs, new object[] { 0 })).Height == 32,
        "first tab refreshes height without switching tabs");
      tabs.TabPages.Add("SQL File 3*");
      Check(((Rectangle)getRect.Invoke(tabs, new object[] { 1 })).Height == 32, "new tab matches first tab height");
      itemSize.SetValue(tabs, new Size(140, 40), null);
      Check(((Rectangle)getRect.Invoke(tabs, new object[] { 0 })).Height == 40 &&
        ((Rectangle)getRect.Invoke(tabs, new object[] { 1 })).Height == 40, "all cached tabs refresh on size change");
      itemSize.SetValue(tabs, new Size(120, 32), null);

      var application = Assembly.LoadFrom(Path.Combine(runtime, "StarDataParadise.exe"));
      var mainType = application.GetType("MySQL.GUI.Workbench.MainForm", true);
      var main = (Form)Activator.CreateInstance(mainType, true);
      var name = mainType.GetProperty("PendingConnectionName");
      var cancel = mainType.GetProperty("PendingConnectionCancel");
      int cancellations = 0;
      var status = (ToolStrip)Field(main, "mainStatusStrip");
      status.Enabled = false;
      var commands = new ToolStrip();
      main.Controls.Add(commands);
      name.SetValue(main, "QA connection", null);
      cancel.SetValue(main, (Action)delegate { ++cancellations; }, null);
      var strip = (StatusStrip)Field(main, "connectionWaitStrip");
      var button = (ToolStripButton)Field(main, "connectionWaitCancel");
      var label = (ToolStripStatusLabel)Field(main, "connectionWaitLabel");
      Check(strip.Parent == main && !main.Modal && strip.Enabled, "wait progress stays inside main window");
      Check(label.Text.Contains("QA connection") && button.Enabled, "wait shows connection and cancel action");
      bool english = Environment.GetEnvironmentVariable("SDP_UI_LANGUAGE") == "en";
      Check(label.Text.StartsWith(english ? "Opening SQL Editor..." : "SQL editörü açılıyor…"),
        "wait status uses selected interface language");
      Check(!commands.Enabled && !status.Enabled, "commands stay locked while connecting");
      button.PerformClick();
      mainType.GetMethod("RequestConnectionCancel", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(main, null);
      Check(cancellations == 1 && !button.Enabled, "repeated cancellation invokes worker once");
      cancel.SetValue(main, null, null);
      Check(commands.Enabled && !status.Enabled && name.GetValue(main, null) == null,
        "completion restores prior command state and clears connection");
      Check(!strip.Visible, "completion hides progress strip");
      name.SetValue(main, "QA connection", null);
      cancel.SetValue(main, (Action)delegate { ++cancellations; }, null);
      Check(button.Enabled && label.Text.Contains("QA connection"), "next connection resets cancellation state");
      Console.WriteLine("PASS " + checks + " startup UI checks");

      if (args.Length > 1 && args[1] == "--preview")
      {
        // Only reparent for the optional preview; tests above exercise MainForm.
        using (var preview = new Form())
        {
          preview.Text = "StarDataParadise - Startup UI QA";
          preview.ClientSize = new Size(1000, 420);
          preview.Font = new Font("Segoe UI", 9.75f);
          preview.BackColor = Color.White;
          tabs.Dock = DockStyle.Fill;
          preview.Controls.Add(tabs);
          preview.Controls.Add(strip);
          Application.Run(preview);
        }
      }
      tabs.Dispose();
      tabHost.Dispose();
      main.Dispose();
      return 0;
    }
    catch (Exception error) { Console.Error.WriteLine(error); return 1; }
  }
}
