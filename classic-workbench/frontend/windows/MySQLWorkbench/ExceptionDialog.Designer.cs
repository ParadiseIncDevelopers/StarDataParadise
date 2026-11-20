namespace MySQL.GUI.Workbench
{
  partial class ExceptionDialog
  {
    private System.ComponentModel.IContainer components = null;

    protected override void Dispose(bool disposing)
    {
      if (disposing)
      {
        if (pictureBox1 != null && pictureBox1.Image != null)
          pictureBox1.Image.Dispose();
        if (components != null)
          components.Dispose();
      }
      base.Dispose(disposing);
    }

    private void InitializeComponent()
    {
      this.components = new System.ComponentModel.Container();
      this.layout = new System.Windows.Forms.TableLayoutPanel();
      this.headerLayout = new System.Windows.Forms.TableLayoutPanel();
      this.pictureBox1 = new System.Windows.Forms.PictureBox();
      this.titleLabel = new System.Windows.Forms.Label();
      this.messagePanel = new System.Windows.Forms.Panel();
      this.messageLabel = new System.Windows.Forms.Label();
      this.detailsButton = new System.Windows.Forms.Button();
      this.richTextBox1 = new System.Windows.Forms.RichTextBox();
      this.separator = new System.Windows.Forms.Panel();
      this.footerLayout = new System.Windows.Forms.FlowLayoutPanel();
      this.copyInfoButton = new System.Windows.Forms.Button();
      this.closeButton = new System.Windows.Forms.Button();
      this.copyStatusLabel = new System.Windows.Forms.Label();
      this.contextMenuStrip1 = new System.Windows.Forms.ContextMenuStrip(this.components);
      this.copyStackTraceToClipboardToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
      this.SuspendLayout();
      this.layout.SuspendLayout();
      this.headerLayout.SuspendLayout();
      this.footerLayout.SuspendLayout();
      this.layout.Dock = System.Windows.Forms.DockStyle.Fill;
      this.layout.Padding = new System.Windows.Forms.Padding(12);
      this.layout.ColumnCount = 1;
      this.layout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
      this.layout.RowCount = 6;
      this.layout.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.AutoSize));
      this.layout.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 64F));
      this.layout.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.AutoSize));
      this.layout.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 0F));
      this.layout.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 17F));
      this.layout.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.AutoSize));
      this.headerLayout.AutoSize = true;
      this.headerLayout.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
      this.headerLayout.Dock = System.Windows.Forms.DockStyle.Fill;
      this.headerLayout.Margin = new System.Windows.Forms.Padding(0, 0, 0, 8);
      this.headerLayout.ColumnCount = 2;
      this.headerLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Absolute, 40F));
      this.headerLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
      this.headerLayout.RowCount = 1;
      this.headerLayout.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.AutoSize));
      this.pictureBox1.Name = "productMark";
      this.pictureBox1.Size = new System.Drawing.Size(28, 28);
      this.pictureBox1.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom;
      this.pictureBox1.Margin = System.Windows.Forms.Padding.Empty;
      this.pictureBox1.TabStop = false;
      this.titleLabel.Text = "İşlem tamamlanamadı";
      this.titleLabel.AutoSize = true;
      this.titleLabel.Dock = System.Windows.Forms.DockStyle.Fill;
      this.titleLabel.TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
      this.titleLabel.Font = new System.Drawing.Font("Segoe UI", 12F, System.Drawing.FontStyle.Bold);
      this.titleLabel.Margin = System.Windows.Forms.Padding.Empty;
      this.headerLayout.Controls.Add(this.pictureBox1, 0, 0);
      this.headerLayout.Controls.Add(this.titleLabel, 1, 0);
      this.messagePanel.Dock = System.Windows.Forms.DockStyle.Fill;
      this.messagePanel.AutoScroll = true;
      this.messagePanel.Margin = new System.Windows.Forms.Padding(0, 0, 0, 8);
      this.messageLabel.Name = "errorMessage";
      this.messageLabel.AutoSize = true;
      this.messageLabel.Margin = System.Windows.Forms.Padding.Empty;
      this.messageLabel.Location = System.Drawing.Point.Empty;
      this.messagePanel.Controls.Add(this.messageLabel);
      this.detailsButton.Name = "toggleErrorDetails";
      this.detailsButton.Text = "Teknik ayrıntıları göster";
      this.detailsButton.AutoSize = true;
      this.detailsButton.MinimumSize = new System.Drawing.Size(0, 28);
      this.detailsButton.Anchor = System.Windows.Forms.AnchorStyles.Left;
      this.detailsButton.FlatStyle = System.Windows.Forms.FlatStyle.Flat;
      this.detailsButton.FlatAppearance.BorderSize = 0;
      this.detailsButton.Margin = new System.Windows.Forms.Padding(0, 0, 0, 8);
      this.detailsButton.Padding = new System.Windows.Forms.Padding(2);
      this.detailsButton.TabIndex = 0;
      this.detailsButton.Click += new System.EventHandler(this.detailsButton_Click);
      this.richTextBox1.Name = "errorDetails";
      this.richTextBox1.AccessibleName = "Teknik hata ayrıntıları";
      this.richTextBox1.Dock = System.Windows.Forms.DockStyle.Fill;
      this.richTextBox1.ReadOnly = true;
      this.richTextBox1.WordWrap = false;
      this.richTextBox1.DetectUrls = false;
      this.richTextBox1.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
      this.richTextBox1.BackColor = System.Drawing.Color.White;
      this.richTextBox1.ForeColor = System.Drawing.Color.FromArgb(16, 38, 41);
      this.richTextBox1.Font = new System.Drawing.Font("Consolas", 9.75F);
      this.richTextBox1.ScrollBars = System.Windows.Forms.RichTextBoxScrollBars.Both;
      this.richTextBox1.Margin = System.Windows.Forms.Padding.Empty;
      this.richTextBox1.Visible = false;
      this.richTextBox1.TabIndex = 1;
      this.richTextBox1.ContextMenuStrip = this.contextMenuStrip1;
      this.separator.BackColor = System.Drawing.Color.FromArgb(163, 180, 188);
      this.separator.Dock = System.Windows.Forms.DockStyle.Fill;
      this.separator.MaximumSize = new System.Drawing.Size(0, 1);
      this.separator.Margin = new System.Windows.Forms.Padding(0, 8, 0, 8);
      this.footerLayout.Dock = System.Windows.Forms.DockStyle.Fill;
      this.footerLayout.AutoSize = true;
      this.footerLayout.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
      this.footerLayout.FlowDirection = System.Windows.Forms.FlowDirection.RightToLeft;
      this.footerLayout.WrapContents = false;
      this.footerLayout.Margin = System.Windows.Forms.Padding.Empty;
      this.closeButton.Name = "closeError";
      this.closeButton.Text = "Kapat";
      this.closeButton.AutoSize = true;
      this.closeButton.MinimumSize = new System.Drawing.Size(88, 28);
      this.closeButton.FlatStyle = System.Windows.Forms.FlatStyle.Flat;
      this.closeButton.FlatAppearance.BorderSize = 0;
      this.closeButton.BackColor = System.Drawing.Color.FromArgb(4, 49, 51);
      this.closeButton.ForeColor = System.Drawing.Color.White;
      this.closeButton.Padding = new System.Windows.Forms.Padding(10, 2, 10, 2);
      this.closeButton.Margin = new System.Windows.Forms.Padding(8, 0, 0, 0);
      this.closeButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
      this.closeButton.TabIndex = 3;
      this.copyInfoButton.Name = "copyErrorDetails";
      this.copyInfoButton.Text = "Ayrıntıları kopyala";
      this.copyInfoButton.AutoSize = true;
      this.copyInfoButton.MinimumSize = new System.Drawing.Size(0, 28);
      this.copyInfoButton.FlatStyle = System.Windows.Forms.FlatStyle.Flat;
      this.copyInfoButton.FlatAppearance.BorderSize = 1;
      this.copyInfoButton.FlatAppearance.BorderColor = System.Drawing.Color.FromArgb(163, 180, 188);
      this.copyInfoButton.BackColor = System.Drawing.Color.White;
      this.copyInfoButton.Padding = new System.Windows.Forms.Padding(10, 2, 10, 2);
      this.copyInfoButton.Margin = new System.Windows.Forms.Padding(8, 0, 0, 0);
      this.copyInfoButton.TabIndex = 2;
      this.copyInfoButton.Click += new System.EventHandler(this.copyInfoButton_Click);
      this.copyStatusLabel.AutoSize = true;
      this.copyStatusLabel.Anchor = System.Windows.Forms.AnchorStyles.None;
      this.copyStatusLabel.Margin = new System.Windows.Forms.Padding(0, 6, 0, 0);
      this.copyStatusLabel.ForeColor = System.Drawing.Color.FromArgb(79, 87, 89);
      this.footerLayout.Controls.Add(this.closeButton);
      this.footerLayout.Controls.Add(this.copyInfoButton);
      this.footerLayout.Controls.Add(this.copyStatusLabel);
      this.contextMenuStrip1.Items.Add(this.copyStackTraceToClipboardToolStripMenuItem);
      this.copyStackTraceToClipboardToolStripMenuItem.Text = "Ayrıntıları kopyala";
      this.copyStackTraceToClipboardToolStripMenuItem.Click += new System.EventHandler(this.copyStackTraceToClipboardToolStripMenuItem_Click);
      this.layout.Controls.Add(this.headerLayout, 0, 0);
      this.layout.Controls.Add(this.messagePanel, 0, 1);
      this.layout.Controls.Add(this.detailsButton, 0, 2);
      this.layout.Controls.Add(this.richTextBox1, 0, 3);
      this.layout.Controls.Add(this.separator, 0, 4);
      this.layout.Controls.Add(this.footerLayout, 0, 5);
      this.Controls.Add(this.layout);
      this.AutoScaleDimensions = new System.Drawing.SizeF(7F, 17F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.Font = new System.Drawing.Font("Segoe UI", 9.75F);
      this.BackColor = System.Drawing.Color.FromArgb(237, 242, 244);
      this.ForeColor = System.Drawing.Color.FromArgb(16, 38, 41);
      this.ClientSize = new System.Drawing.Size(640, 220);
      this.MinimizeBox = false;
      this.MaximizeBox = false;
      this.ShowInTaskbar = false;
      this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
      this.Text = "StarDataParadise - Hata";
      this.Name = "ExceptionDialog";
      this.AcceptButton = this.closeButton;
      this.CancelButton = this.closeButton;
      this.headerLayout.ResumeLayout(false);
      this.headerLayout.PerformLayout();
      this.footerLayout.ResumeLayout(false);
      this.footerLayout.PerformLayout();
      this.layout.ResumeLayout(false);
      this.layout.PerformLayout();
      this.ResumeLayout(false);
    }

    private System.Windows.Forms.TableLayoutPanel layout;
    private System.Windows.Forms.TableLayoutPanel headerLayout;
    private System.Windows.Forms.PictureBox pictureBox1;
    private System.Windows.Forms.Label titleLabel;
    private System.Windows.Forms.Panel messagePanel;
    private System.Windows.Forms.Label messageLabel;
    private System.Windows.Forms.Button detailsButton;
    private System.Windows.Forms.RichTextBox richTextBox1;
    private System.Windows.Forms.Panel separator;
    private System.Windows.Forms.FlowLayoutPanel footerLayout;
    private System.Windows.Forms.Button copyInfoButton;
    private System.Windows.Forms.Button closeButton;
    private System.Windows.Forms.Label copyStatusLabel;
    private System.Windows.Forms.ContextMenuStrip contextMenuStrip1;
    private System.Windows.Forms.ToolStripMenuItem copyStackTraceToClipboardToolStripMenuItem;
  }
}
