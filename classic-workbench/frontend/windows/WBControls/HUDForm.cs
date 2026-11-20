/*
 * Copyright (c) 2010, 2019, Oracle and/or its affiliates. All rights reserved.
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
using System.Drawing.Drawing2D;
using System.Windows.Forms;

using MySQL.Utilities.SysUtils;
using System.Threading;

namespace MySQL.Utilities
{
  public partial class HUDForm : Form
  {
    // Note: currentInstance relies on the fact that we never have more than one HUDForm instance.
    static private HUDForm currentInstance = null;
    static private Object instanceLock = new Object();

    public delegate bool CancelDelegate();
    public delegate void ReadyDelegate();

    private Size baseSize;
    private float bitmapDpi = 96;
    private int animationSteps = 6;
    private bool useAnimation = false;
    private RectangleF cancelButtonBounds;

    private CancelDelegate cancelDelegate = null;
    private Bitmap contentBitmap;

    HUDForm()
    {
      InitializeComponent();
      using (Graphics graphics = CreateGraphics())
        bitmapDpi = graphics.DpiX;
      float scale = bitmapDpi / 96f;
      baseSize = new Size((int)Math.Round(420 * scale), (int)Math.Round(132 * scale));
      label1.Font = new Font("Segoe UI", 9.75f, FontStyle.Bold, GraphicsUnit.Point);
      label2.Font = new Font("Segoe UI", 9.75f, FontStyle.Regular, GraphicsUnit.Point);
      pictureBox1.Visible = false;
      cancelButtonPicture.Visible = false;
      SetStyle(ControlStyles.Selectable, false);
      UpdateStyles();

      cancelButtonPicture.UseWaitCursor = false;
      cancelButtonPicture.Cursor = Cursors.AppStarting;
    }

    public static DialogResult ShowModal(String title, String message, bool animated,
      ReadyDelegate notifyReady, CancelDelegate deleg)
    {
      using (HUDForm instance = new HUDForm())
      {
        // Don't use lock() here as we might dead lock otherwise.
        if (!Monitor.TryEnter(instanceLock))
        {
          Application.DoEvents(); // See if we can let the application finish pending stuff first.
          if (!Monitor.TryEnter(instanceLock))
            return DialogResult.Abort; // Still no lock possible? In this case we can only leave without showing the HUD form.
        }

        currentInstance = instance;
        Monitor.Exit(instanceLock);

        instance.label1.Text = FriendlyWaitTitle(title);
        instance.label2.Text = message;
        instance.cancelDelegate = deleg;

        // Can happen the form is cancelled already while we are still doing the setup 
        // (separate thread).
        if (instance.IsDisposed || instance.Disposing)
        {
          if (Monitor.TryEnter(instanceLock))
          {
            currentInstance = null;
            Monitor.Exit(instanceLock);
          }
          return DialogResult.Abort;
        }

        instance.Reshow(animated, true);

        // Need to set Visible back to false to avoid an exception in ShowDialog about an already
        // visible window. Doesn't usually have a visual effect.
        instance.Visible = false;

        if (notifyReady != null)
          notifyReady();

        DialogResult result = DialogResult.Abort;
        if (!instance.IsDisposed && !instance.Disposing)
          result = instance.ShowDialog();
        if (Monitor.TryEnter(instanceLock))
        {
          instance.Dispose();
          currentInstance = null;
          Monitor.Exit(instanceLock);
        }

        return result;
      }
    }

    public static void Show(String title, String message, bool animated)
    {
      lock (instanceLock) // Hard lock here. Must never happen.
      {
        HUDForm instance = new HUDForm();
        currentInstance = instance;
        instance.label1.Text = FriendlyWaitTitle(title);
        instance.label2.Text = message;

        instance.Reshow(animated, false);
      }
    }

    public void Reshow(bool animated, bool modal)
    {
      lock (instanceLock)
      {
        // If there's no current instance then we are just a left over that is going to
        // be closed/disposed shortly anyway.
        if (currentInstance == null)
          return;

        Form mainForm = Application.OpenForms["MainForm"];
        if (mainForm == null)
          throw new Exception("HUD display needs main form to be named \"MainForm\" to find it.");

        mainForm.Update();

        // The (transparent) HUD will cover the entire application window.
        Bounds = mainForm.Bounds;

        PrepareBitmap(modal);

        // Don't use animations in a terminal session (remote desktop).
        useAnimation = animated && !SystemInformation.TerminalServerSession;

        if (IsDisposed || Disposing)
          return;

        if (useAnimation)
        {
          ControlUtilities.SetBitmap(this, contentBitmap, 0);
          Win32.ShowWindow(Handle, (uint)SW.SHOWNOACTIVATE);
          for (float i = 1; i <= animationSteps; i++)
            ControlUtilities.SetBitmap(this, contentBitmap, (int)(i / animationSteps * 255));
        }
        else
        {
          ControlUtilities.SetBitmap(this, contentBitmap, 255);
          Win32.ShowWindow(Handle, (uint)SW.SHOWNOACTIVATE);
        }
      }
    }

    /// <summary>
    /// Hides the HUD form and releases any used bitmap resource.
    /// </summary>
    private void DoHide()
    {
      if (useAnimation && contentBitmap != null)
      {
        for (float i = animationSteps; i >= 0; i--)
          ControlUtilities.SetBitmap(this, contentBitmap, (int)(i / animationSteps * 255));
      }

      base.Hide();

      if (contentBitmap != null)
      {
        contentBitmap.Dispose();
        contentBitmap = null;
      }
      cancelDelegate = null;
    }

    /// <summary>
    /// Reintroduced Hide() function to allow a fade out animation.
    /// Call only from main thread.
    /// </summary>
    public new void Hide()
    {
      if (!IsHandleCreated)
        return;

      Form mainForm = Application.OpenForms["MainForm"];
      if (mainForm != null)
          mainForm.Update();

      DoHide();
    }

    /// <summary>
    /// Hide the HUD and return OK as dialog result.
    /// Always called in the context of the main thread with active lock.
    /// </summary>
    protected void DoFinish()
    {
      if (IsHandleCreated)
        Hide();
      this.DialogResult = DialogResult.Abort;
      Dispose();
      currentInstance = null;
    }

    /// <summary>
    /// Called when the HUD is no longer needed. Returns success for the modal result.
    /// </summary>
    public static void Finish()
    {
      if (currentInstance == null)
        return;

      lock (instanceLock)
      {
        if (currentInstance.InvokeRequired)
          currentInstance.Invoke((Action)(() => currentInstance.DoFinish()));
        else
          currentInstance.DoFinish();
      }
    }

    #region Properties

    public static bool IsVisible
    {
      get {
        return (currentInstance == null) ? false : currentInstance.Visible;
      }
    }

    #endregion

    #region Drawing

    private static string FriendlyWaitTitle(string title)
    {
      return (title ?? "").Replace("MySQL Workbench", "StarDataParadise Classic")
        .Replace("MySQLWorkbench", "StarDataParadise Classic")
        .Replace("StarDataParadise Workbench", "StarDataParadise Classic")
        .Replace("SDP_CONNECTION", "Bağlanıyor…");
    }

    // Layered hosting and worker completion remain unchanged. The visual is a
    // compact rectangular panel with a single neutral separator and text action.
    protected void PrepareBitmap(bool showCancelButton)
    {
      if (contentBitmap != null) contentBitmap.Dispose();
      contentBitmap = new Bitmap(Width, Height);
      contentBitmap.SetResolution(bitmapDpi, bitmapDpi);
      using (Graphics graphics = Graphics.FromImage(contentBitmap))
      {
        float scale = bitmapDpi / 96f;
        var bounds = new Rectangle((Width - baseSize.Width) / 2, (Height - baseSize.Height) / 2,
          baseSize.Width, baseSize.Height);
        Color surface = SystemInformation.HighContrast ? SystemColors.Window : Color.FromArgb(237, 242, 244);
        Color ink = SystemInformation.HighContrast ? SystemColors.WindowText : Color.FromArgb(16, 38, 41);
        Color muted = SystemInformation.HighContrast ? SystemColors.WindowText : Color.FromArgb(79, 87, 89);
        Color rule = SystemInformation.HighContrast ? SystemColors.WindowText : Color.FromArgb(163, 180, 188);
        graphics.Clear(Color.FromArgb(8, surface));
        using (Brush brush = new SolidBrush(surface)) graphics.FillRectangle(brush, bounds);
        using (Pen pen = new Pen(rule)) graphics.DrawRectangle(pen, bounds.Left, bounds.Top, bounds.Width - 1, bounds.Height - 1);
        int padding = (int)Math.Round(12 * scale);
        var titleBounds = new RectangleF(bounds.Left + padding, bounds.Top + padding,
          bounds.Width - 2 * padding, 24 * scale);
        using (StringFormat format = new StringFormat(StringFormatFlags.NoWrap))
        {
          format.Trimming = StringTrimming.EllipsisCharacter;
          using (Brush brush = new SolidBrush(ink))
            graphics.DrawString(label1.Text, label1.Font, brush, titleBounds, format);
        }
        var messageBounds = new RectangleF(bounds.Left + padding, titleBounds.Bottom + 4 * scale,
          bounds.Width - 2 * padding, 40 * scale);
        using (StringFormat format = new StringFormat())
        {
          format.Trimming = StringTrimming.EllipsisCharacter;
          using (Brush brush = new SolidBrush(muted))
            graphics.DrawString(label2.Text, label2.Font, brush, messageBounds, format);
        }
        int footerY = bounds.Bottom - (int)Math.Round(38 * scale);
        using (Pen pen = new Pen(rule)) graphics.DrawLine(pen, bounds.Left, footerY, bounds.Right - 1, footerY);
        using (Brush brush = new SolidBrush(muted))
          graphics.DrawString("İşlem sürüyor…", label2.Font, brush, bounds.Left + padding, footerY + 9 * scale);
        if (showCancelButton)
        {
          cancelButtonBounds = new RectangleF(bounds.Right - padding - 82 * scale, footerY + 6 * scale, 82 * scale, 26 * scale);
          using (Pen pen = new Pen(rule))
            graphics.DrawRectangle(pen, cancelButtonBounds.X, cancelButtonBounds.Y, cancelButtonBounds.Width, cancelButtonBounds.Height);
          using (Brush brush = new SolidBrush(ink))
          using (StringFormat format = new StringFormat())
          {
            format.Alignment = StringAlignment.Center;
            format.LineAlignment = StringAlignment.Center;
            graphics.DrawString("Vazgeç", label2.Font, brush, cancelButtonBounds, format);
          }
        }
        else cancelButtonBounds = RectangleF.Empty;
      }
    }

    #endregion

    #region Properties

    protected override bool ShowWithoutActivation
    {
      get { return true; }
    }

    #endregion

    protected override CreateParams CreateParams	
		{
			get 
			{
				CreateParams cp = base.CreateParams;

        cp.ExStyle |= (int) WS.EX_LAYERED;
        cp.ExStyle |= (int) WS.EX_NOACTIVATE;
				return cp;
			}
		}

    #region Events and message handling

    protected override void WndProc(ref Message m)
    {
      switch ((WM) m.Msg)
      {
        case WM.LBUTTONDOWN:
          {
            int x = m.LParam.ToInt32() & 0xFFFF;
            int y = m.LParam.ToInt32() >> 16;
            if (cancelButtonBounds.Contains(x, y) && cancelDelegate != null && cancelDelegate())
            {
              Hide();
              DialogResult = DialogResult.Cancel;
              Close();
            }
            break;
          }
        case WM.SETCURSOR:
          {
            Point position = PointToClient(MousePosition);
            if (cancelButtonBounds.Contains(position))
            {
              // Check before set. Windows XP will otherwise cause an endless recursion.
              if (Cursor.Current != Cursors.Hand)
                Cursor.Current = Cursors.Hand;
            }
            else
            {
              if (Cursor.Current != Cursors.WaitCursor)
                Cursor.Current = Cursors.WaitCursor;
            }

            m.Result = new IntPtr(1);
            return;
          }
        case WM.MOUSEACTIVATE:
          m.Result = new IntPtr(Win32.MA_NOACTIVATE);
          return;
        case WM.ACTIVATEAPP:
          if (m.WParam.ToInt32() != 0)
            Win32.SetWindowPos(Handle, Win32.HWND_TOP, 0, 0, 0, 0,
              Win32.SWP_NOACTIVATE | Win32.SWP_NOMOVE | Win32.SWP_NOSIZE);
          break;
        case WM.WINDOWPOSCHANGED:
          Win32.SetWindowPos(Handle, Win32.HWND_TOP, 0, 0, 0, 0,
            Win32.SWP_NOACTIVATE | Win32.SWP_NOMOVE | Win32.SWP_NOSIZE);
          break;

      }
      base.WndProc(ref m);
    }

    #endregion

  }
}
