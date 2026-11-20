// A host for visual QA of the real packaged ExceptionDialog. It creates no
// Workbench context, connections, profiles, or substitute dialog controls.
// Usage: ErrorDialogProbe.exe <runtime-directory> [captured-exception.txt]
using System;
using System.IO;
using System.Drawing;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

internal static class ErrorDialogProbe
{
  private static string runtimeDirectory;

  [DllImport("user32.dll")]
  private static extern bool SetProcessDPIAware();

  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  private static extern bool SetDllDirectory(string path);

  [STAThread]
  private static int Main(string[] arguments)
  {
    string resultPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "error-dialog-probe-result.txt");
    string identity = String.Empty;
    try
    {
      if (arguments.Length < 1 || arguments.Length > 2)
        throw new ArgumentException("Usage: ErrorDialogProbe.exe <runtime-directory> [captured-exception.txt]");
      runtimeDirectory = Path.GetFullPath(arguments[0]);
      string assemblyPath = Path.Combine(runtimeDirectory, "StarDataParadise.exe");
      if (!File.Exists(assemblyPath)) throw new FileNotFoundException("The actual runtime executable is required.", assemblyPath);
      string capturedException = arguments.Length == 2 ? File.ReadAllText(Path.GetFullPath(arguments[1]), Encoding.UTF8) : null;
      if (!SetDllDirectory(runtimeDirectory)) throw new System.ComponentModel.Win32Exception();
      Environment.CurrentDirectory = runtimeDirectory;
      AppDomain.CurrentDomain.AssemblyResolve += ResolveRuntimeAssembly;
      SetProcessDPIAware();
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);

      Assembly application = Assembly.LoadFrom(assemblyPath);
      Type dialog = application.GetType("MySQL.GUI.Workbench.ExceptionDialog", true);
      File.WriteAllText(resultPath, "stage=before_actual_type_initializer\r\nruntime=" + runtimeDirectory + "\r\n", Encoding.UTF8);
      RuntimeHelpers.RunClassConstructor(dialog.TypeHandle);
      File.AppendAllText(resultPath, "stage=actual_type_initialized\r\n", Encoding.UTF8);
      FieldInfo singleton = dialog.GetField("singleton", BindingFlags.NonPublic | BindingFlags.Static);
      Form actualForm = singleton == null ? null : singleton.GetValue(null) as Form;
      if (actualForm == null) throw new MissingFieldException(dialog.FullName, "singleton");
      // Test-host visibility only. Keep the production dialog body and its
      // controls intact while exposing this owned window to desktop QA tools.
      actualForm.ShowInTaskbar = true;
      File.AppendAllText(resultPath, "fixture_flag=actual_dialog_show_in_taskbar\r\n", Encoding.UTF8);
      File.AppendAllText(resultPath, "stage=actual_singleton_resolved; handle_created=" + actualForm.IsHandleCreated + "\r\n", Encoding.UTF8);
      actualForm.Load += delegate { File.AppendAllText(resultPath, "event=actual_dialog_load\r\n", Encoding.UTF8); };
      actualForm.Shown += delegate {
        File.AppendAllText(resultPath, "event=actual_dialog_shown; handle=" + actualForm.Handle + "; bounds=" + actualForm.Bounds + "; visible=" + actualForm.Visible + "\r\n", Encoding.UTF8);
      };
      actualForm.FormClosing += delegate(object sender, FormClosingEventArgs eventArgs) {
        File.AppendAllText(resultPath, "event=actual_dialog_closing; reason=" + eventArgs.CloseReason + "; result=" + actualForm.DialogResult + "\r\n", Encoding.UTF8);
      };
      MethodInfo show = null;
      foreach (MethodInfo method in dialog.GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly))
      {
        ParameterInfo[] parameters = method.GetParameters();
        if (method.Name == "Show" && parameters.Length == 3 &&
            parameters[0].ParameterType == typeof(string) && parameters[1].ParameterType == typeof(string) &&
            parameters[2].ParameterType.FullName == "MySQL.Workbench.WbContext")
          show = method;
      }
      if (show == null) throw new MissingMethodException(dialog.FullName, "Show(string, string, WbContext)");
      using (SHA256 sha = SHA256.Create())
      using (FileStream stream = File.OpenRead(assemblyPath))
        identity = "assembly=" + application.FullName + Environment.NewLine +
          "sha256=" + BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();

      string message = "Dizin aralık dışındaydı. Negatif bir değer olmamalı ve koleksiyonun boyutundan küçük olmalıdır.\r\nParametre adı: index";
      string details = "System.ArgumentOutOfRangeException: " + message + "\r\n" +
        "   konum: System.Collections.ArrayList.get_Item(Int32 index)\r\n" +
        "   konum: System.Windows.Forms.ImageList.ImageCollection.IndexOfKey(String key)\r\n" +
        "   konum: MySQL.GUI.Workbench.MainForm.DockDocument(ITabDocument document, Boolean main, Boolean activate), satır 1646\r\n\r\n" +
        "Kaynak: düzeltilen avatar açılış hatasının kaydedilmiş çağrı zinciri özeti.";
      if (!String.IsNullOrEmpty(capturedException))
      {
        details = capturedException;
        int firstLine = capturedException.IndexOfAny(new char[] { '\r', '\n' });
        message = firstLine < 0 ? capturedException : capturedException.Substring(0, firstLine);
      }
      File.AppendAllText(resultPath, "status=showing_actual_dialog\r\n" + identity + "\r\n", Encoding.UTF8);
      File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "error-dialog-probe-expected.txt"),
        message + Environment.NewLine + Environment.NewLine + details, Encoding.UTF8);
      int result = 0;
      using (Form host = new Form())
      {
        host.Name = "ErrorDialogProbeHost";
        host.Text = "Classic - Hata penceresi denemesi";
        host.Size = new Size(980, 620);
        host.StartPosition = FormStartPosition.CenterScreen;
        host.ShowInTaskbar = true;
        host.BackColor = Color.FromArgb(237, 242, 244);
        host.Shown += delegate {
          File.AppendAllText(resultPath, "event=probe_host_shown; handle=" + host.Handle + "\r\n", Encoding.UTF8);
          host.BeginInvoke(new Action(delegate {
            try
            {
              show.Invoke(null, new object[] { message, details, null });
              File.AppendAllText(resultPath, "status=closed_normally\r\n", Encoding.UTF8);
            }
            catch (Exception invocationError)
            {
              Exception error = invocationError is TargetInvocationException && invocationError.InnerException != null ? invocationError.InnerException : invocationError;
              File.AppendAllText(resultPath, "status=failed\r\n" + error, Encoding.UTF8);
              result = 1;
            }
            finally { host.Close(); }
          }));
        };
        Application.Run(host);
      }
      return result;
    }
    catch (Exception exception)
    {
      Exception error = exception is TargetInvocationException && exception.InnerException != null ? exception.InnerException : exception;
      File.WriteAllText(resultPath, "status=failed\r\n" + identity + "\r\n" + error, Encoding.UTF8);
      return 1;
    }
  }

  private static Assembly ResolveRuntimeAssembly(object sender, ResolveEventArgs arguments)
  {
    string name = new AssemblyName(arguments.Name).Name;
    foreach (string extension in new string[] { ".dll", ".exe" })
    {
      string path = Path.Combine(runtimeDirectory, name + extension);
      if (File.Exists(path)) return Assembly.LoadFrom(path);
    }
    return null;
  }
}
