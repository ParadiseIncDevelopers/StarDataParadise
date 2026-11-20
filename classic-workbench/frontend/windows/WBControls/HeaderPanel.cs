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
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

using MySQL.Utilities;

namespace MySQL.Controls
{
  public interface IConnectionWaitHost
  {
    Action PendingConnectionCancel { get; set; }
  }

  public class HeaderPanel : System.Windows.Forms.Panel
  {
    /// <summary> 
	  /// Required designer variable.
	  /// </summary>
	  private System.ComponentModel.Container components = null;

    #region Construction and destruction

    public HeaderPanel()
	  {
		  // This call is required by the Windows.Forms Form Designer.
		  InitializeComponent();

		  // Some special styles for the control.
		  SetStyle(ControlStyles.UserPaint, true);
		  SetStyle(ControlStyles.AllPaintingInWmPaint, true);
		  SetStyle(ControlStyles.DoubleBuffer, true);
		  SetStyle(ControlStyles.SupportsTransparentBackColor, true);
		  SetStyle(ControlStyles.OptimizedDoubleBuffer, true);
      UpdateStyles();

      Padding = new Padding(0, 24, 0, 0);
      BackColor = Color.FromArgb(231, 238, 239);
      ForeColor = Color.FromArgb(16, 38, 41);
    }

    #endregion

    #region Drawing the control and its parts.

    override protected void OnPaintBackground(PaintEventArgs e)
    {
      using (Brush brush = new SolidBrush(SystemInformation.HighContrast ? SystemColors.Control : Color.FromArgb(231, 238, 239)))
        e.Graphics.FillRectangle(brush, e.ClipRectangle);
    }
    
    override protected void OnPaint(PaintEventArgs e)
    {
      base.OnPaint(e);
      DrawControl(e.Graphics);
    }

	  internal void DrawControl(Graphics g)
	  {
      RectangleF bounds = ClientRectangle;
      bounds.Height = Padding.Top;
      bounds.X = headerPadding.Left;
      bounds.Y = headerPadding.Top;
      bounds.Width -= headerPadding.Horizontal;
      bounds.Height -= headerPadding.Vertical;

      if (Text != null && Text.Length > 0)
      {
        using (StringFormat stringFormat = new StringFormat())
        {
          stringFormat.Alignment = StringAlignment.Near;
          stringFormat.LineAlignment = StringAlignment.Center;

          using (Brush brush = new SolidBrush(SystemInformation.HighContrast ? SystemColors.ControlText : Color.FromArgb(16, 38, 41)))
            g.DrawString(Text, Font, brush, bounds, stringFormat);
        }

      }
      using (Pen divider = new Pen(SystemInformation.HighContrast ? SystemColors.ControlText : Color.FromArgb(163, 180, 188)))
        g.DrawLine(divider, 0, Padding.Top - 1, Width, Padding.Top - 1);
    }

    #endregion

    #region Event handling

    protected override void OnEnter(EventArgs e)
    {
      base.OnEnter(e);
      Invalidate();
    }

    protected override void OnLeave(EventArgs e)
    {
      base.OnLeave(e);
      Invalidate();
    }

    protected override void OnHandleCreated(EventArgs e)
    {
      base.OnHandleCreated(e);

      Form container = Application.OpenForms[0].TopLevelControl as Form;
      if (container != null)
      {
        container.Deactivate += new EventHandler(ActivationChanged);
        container.Activated += new EventHandler(ActivationChanged);
      }
    }

    protected override void OnHandleDestroyed(EventArgs e)
    {
      base.OnHandleDestroyed(e);

      // Unregister our activation listener from the application's main form.
      // This assumes the main form hasn't changed in the meantime (which should be the case in 99.99% of all cases).
      if (Application.OpenForms.Count > 0)
      {
        Form container = Application.OpenForms[0].TopLevelControl as Form;
        if (container != null)
        {
          container.Deactivate -= ActivationChanged;
          container.Activated -= ActivationChanged;
        }
      }
    }

    private void ActivationChanged(object sender, EventArgs e)
    {
      Invalidate();
    }

    #endregion

    #region Component Designer generated code

    /// <summary>
	  /// Required method for Designer support - do not modify 
	  /// the contents of this method with the code editor.
	  /// </summary>
	  private void InitializeComponent()
	  {
		  components = new System.ComponentModel.Container();
	  }

	  #endregion

    #region Properties

    private Padding headerPadding = new Padding(5, 0, 0, 0);
    public Padding HeaderPadding
    {
      get { return headerPadding; }
      set
      {
        headerPadding = value;
        Invalidate();
      }
    }

    private Color headerColor = Color.FromArgb(231, 238, 239);
    public Color HeaderColor
    {
      get { return headerColor; }
      set
      {
        if (headerColor != value)
        {
          headerColor = value;
          Invalidate();
        }
      }
    }

    private Color headerColorFocused = Color.FromArgb(231, 238, 239);
    public Color HeaderColorFocused
    {
      get { return headerColorFocused; }
      set
      {
        if (headerColorFocused != value)
        {
          headerColorFocused = value;
          Invalidate();
        }
      }
    }

    private Color foreColorFocused = Color.FromArgb(16, 38, 41);
    public Color ForeColorFocused
    {
      get { return foreColorFocused; }
      set
      {
        if (foreColorFocused != value)
        {
          foreColorFocused = value;
          Invalidate();
        }
      }
    }

    [EditorBrowsable(EditorBrowsableState.Always)]
    [Bindable(true)]
    [Browsable(true)]
    public override string Text { get; set; }

    #endregion
    
    #region Accessibility support

    protected override AccessibleObject CreateAccessibilityInstance()
    {
      return new HeaderPanelAccessibleObject();
    }

    private class HeaderPanelAccessibleObject : AccessibleObject
    {
      public override AccessibleRole Role
      {
        get { return AccessibleRole.Pane; }
      }

      public override String Name
      {
        get { return "HaderPanel"; }
      }
    }

    #endregion
  }
}
