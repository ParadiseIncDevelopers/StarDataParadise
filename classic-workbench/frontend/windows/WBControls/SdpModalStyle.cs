// Shared StarDataParadise action colors for application-owned modal dialogs.
using System;
using System.Drawing;
using System.Runtime.CompilerServices;
using System.Windows.Forms;

namespace MySQL.Controls
{
  public static class SdpModalStyle
  {
    private sealed class Subscription
    {
      public bool NativeButton;
    }
    private sealed class ModalPaintFilter : IMessageFilter
    {
      public bool PreFilterMessage(ref Message message)
      {
        // EnterThreadModal can precede registration in Application.OpenForms.
        // Style before the first queued paint, after WinForms sets Form.Modal.
        if (message.Msg == 0x000F)
        {
          Control control = Control.FromChildHandle(message.HWnd);
          Form form = control == null ? null : control.FindForm();
          Subscription ignored;
          if (form != null && form.Modal && !subscriptions.TryGetValue(form, out ignored)) Apply(form);
        }
        return false;
      }
    }
    private static readonly ConditionalWeakTable<Control, Subscription> subscriptions =
      new ConditionalWeakTable<Control, Subscription>();
    private static bool initialized;

    // Install on the application's UI thread, before opening its first dialog.
    public static void Initialize()
    {
      if (initialized) return;
      initialized = true;
      Application.AddMessageFilter(new ModalPaintFilter());
    }

    public static void Apply(Control root)
    {
      if (root == null || root.IsDisposed) return;
      if (root.InvokeRequired)
      {
        root.Invoke(new MethodInvoker(delegate { Apply(root); }));
        return;
      }
      Subscription ignored;
      if (!subscriptions.TryGetValue(root, out ignored))
      {
        Button original = root as Button;
        ignored = new Subscription { NativeButton = original != null && original.FlatStyle == FlatStyle.System };
        subscriptions.Add(root, ignored);
        root.ControlAdded += Added;
        root.EnabledChanged += Changed;
        root.SystemColorsChanged += Changed;
      }
      Button button = root as Button;
      // Explicit system rendering is required by native elevation/shield buttons.
      if (button != null && !ignored.NativeButton) Paint(button, SystemInformation.HighContrast);
      foreach (Control child in root.Controls) Apply(child);
    }

    private static void Added(object sender, ControlEventArgs args) { Apply(args.Control); }
    private static void Changed(object sender, EventArgs args) { Apply((Control)sender); }

    private static void Paint(Button button, bool highContrast)
    {
      if (highContrast)
      {
        button.FlatStyle = FlatStyle.System;
        button.UseVisualStyleBackColor = true;
        button.BackColor = SystemColors.Control;
        button.ForeColor = SystemColors.ControlText;
        return;
      }
      button.FlatStyle = FlatStyle.Flat;
      button.UseVisualStyleBackColor = false;
      button.BackColor = button.Enabled ? Color.FromArgb(4, 49, 51) : Color.FromArgb(163, 180, 188);
      button.ForeColor = button.Enabled ? Color.White : Color.FromArgb(16, 38, 41);
      button.FlatAppearance.BorderSize = 1;
      button.FlatAppearance.BorderColor = button.BackColor;
      button.FlatAppearance.MouseOverBackColor = Color.FromArgb(11, 69, 70);
      button.FlatAppearance.MouseDownBackColor = Color.FromArgb(1, 36, 38);
      // WinForms retains its focus cue, default-button frame, mnemonic and click path.
      button.Invalidate();
    }
  }
}
