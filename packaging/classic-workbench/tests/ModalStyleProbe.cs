using System;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;
using MySQL.Controls;

class ModalStyleProbe
{
  static int checks;
  static void Check(bool value, string message)
  {
    if (!value) throw new Exception(message);
    ++checks;
    Console.WriteLine("PASS " + message);
  }

  [STAThread]
  static int Main()
  {
    try
    {
      Application.EnableVisualStyles();
      SdpModalStyle.Initialize();
      using (Form dialog = new Form())
      using (Panel content = new Panel { Dock = DockStyle.Fill })
      using (Button apply = new Button { Text = "Uygula", TabIndex = 2, DialogResult = DialogResult.OK })
      using (Button cancel = new Button { Text = "Vazgeç", Top = 40, TabIndex = 3, DialogResult = DialogResult.Cancel })
      using (Timer firstFrame = new Timer { Interval = 80 })
      {
        content.Controls.Add(apply);
        content.Controls.Add(cancel);
        dialog.Controls.Add(content);
        dialog.AcceptButton = apply;
        dialog.CancelButton = cancel;
        dialog.Shown += delegate
        {
          firstFrame.Start();
        };
        firstFrame.Tick += delegate
        {
          firstFrame.Stop();
            try
            {
              Check(SystemInformation.HighContrast || apply.BackColor == Color.FromArgb(4, 49, 51),
                "modal entry styles nested buttons");
              Check(SystemInformation.HighContrast || cancel.ForeColor == Color.White,
                "cancel uses white modal action text");
              Check(dialog.AcceptButton == apply && dialog.CancelButton == cancel && apply.TabIndex == 2,
                "default, cancel and keyboard order are preserved");
              apply.Enabled = false;
              Check(!apply.Enabled && (SystemInformation.HighContrast || apply.BackColor != cancel.BackColor),
                "disabled action remains disabled and visually distinct");
              apply.Enabled = true;
              Check(SystemInformation.HighContrast || apply.BackColor == cancel.BackColor,
                "reenabled action regains modal color");
              Button validate = new Button { Text = "Planı doğrula", Top = 80 };
              content.Controls.Add(validate);
              Check(SystemInformation.HighContrast || validate.BackColor == apply.BackColor,
                "buttons added after opening inherit the modal color");
              Button native = new Button { Text = "Yönetici", FlatStyle = FlatStyle.System, Top = 120 };
              content.Controls.Add(native);
              Check(native.FlatStyle == FlatStyle.System, "native security button rendering is preserved");
              apply.Focus();
              Check(apply.Focused && apply.TabStop, "action retains keyboard focus and tab access");
              using (Bitmap pixels = new Bitmap(apply.Width, apply.Height))
              {
                apply.DrawToBitmap(pixels, new Rectangle(Point.Empty, pixels.Size));
                Check(SystemInformation.HighContrast || pixels.GetPixel(8, 8).ToArgb() == apply.BackColor.ToArgb(),
                  "native button paints the requested solid fill");
              }
              dialog.DialogResult = DialogResult.Cancel;
            }
            catch (Exception error) { failure = error; dialog.Close(); }
        };
        Check(dialog.ShowDialog() == DialogResult.Cancel, "modal returns its original cancel result");
        if (failure != null) throw failure;
      }
      Console.WriteLine("PASS " + checks + " modal style checks");
      return 0;
    }
    catch (Exception error) { Console.Error.WriteLine(error); return 1; }
  }
  static Exception failure;
}
