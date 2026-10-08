using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace RE4CraftSetup {
 static class Theme {
  public static readonly Color Background = Color.FromArgb(16, 21, 18);
  public static readonly Color Surface = Color.FromArgb(26, 34, 29);
  public static readonly Color Border = Color.FromArgb(49, 62, 53);
  public static readonly Color Text = Color.FromArgb(239, 243, 233);
  public static readonly Color Muted = Color.FromArgb(159, 174, 161);
  public static readonly Color Green = Color.FromArgb(156, 199, 120);
  public static readonly Color Gold = Color.FromArgb(211, 179, 126);
  public static Font Font(float size, FontStyle style) { return new Font("Segoe UI", size, style); }
  public static Label Label(string text, float size, Color color, FontStyle style) {
   return new Label { Text = text, AutoSize = true, Dock = DockStyle.Fill, Font = Font(size, style), ForeColor = color, BackColor = Color.Transparent, Margin = new Padding(0) };
  }
  public static TableLayoutPanel Stack() {
   var table = new TableLayoutPanel { ColumnCount = 1, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Top, BackColor = Color.Transparent, Margin = new Padding(0), Padding = new Padding(0) };
   table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
   return table;
  }
  public static void Row(TableLayoutPanel table, Control control, int bottom) {
   int row = table.RowCount++;
   table.RowStyles.Add(new RowStyle(SizeType.AutoSize));
   control.Margin = new Padding(0, 0, 0, bottom);
   table.Controls.Add(control, 0, row);
  }
  public static GraphicsPath Rounded(RectangleF bounds, float radius) {
   var path = new GraphicsPath(); float d = radius * 2;
   path.AddArc(bounds.X, bounds.Y, d, d, 180, 90); path.AddArc(bounds.Right - d, bounds.Y, d, d, 270, 90);
   path.AddArc(bounds.Right - d, bounds.Bottom - d, d, d, 0, 90); path.AddArc(bounds.X, bounds.Bottom - d, d, d, 90, 90); path.CloseFigure();
   return path;
  }
 }

 class DarkForm : Form {
  [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);
  public DarkForm() {
   AutoScaleDimensions = new SizeF(96, 96); AutoScaleMode = AutoScaleMode.Dpi;
   Font = Theme.Font(10, FontStyle.Regular); BackColor = Theme.Background; ForeColor = Theme.Text;
   StartPosition = FormStartPosition.CenterParent;
  }
  protected override void OnHandleCreated(EventArgs e) {
   base.OnHandleCreated(e);
   try { int value = 1; if (DwmSetWindowAttribute(Handle, 20, ref value, 4) != 0) DwmSetWindowAttribute(Handle, 19, ref value, 4); } catch { }
  }
  protected override void OnLoad(EventArgs e) {
   base.OnLoad(e);
   var area = Screen.FromControl(this).WorkingArea;
   MinimumSize = new Size(Math.Min(MinimumSize.Width, area.Width - 40), Math.Min(MinimumSize.Height, area.Height - 40));
   Size = new Size(Math.Min(Width, area.Width - 40), Math.Min(Height, area.Height - 40));
  }
 }

 sealed class Card : Panel {
  public Card() {
   SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
   BackColor = Theme.Surface; ForeColor = Theme.Text; Padding = new Padding(16); Margin = new Padding(0); Dock = DockStyle.Fill;
  }
  protected override void OnPaintBackground(PaintEventArgs e) {
   e.Graphics.Clear(Parent == null ? Theme.Background : Parent.BackColor);
   e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
   using (var path = Theme.Rounded(new RectangleF(.5f, .5f, Width - 1, Height - 1), 8 * e.Graphics.DpiX / 96))
   using (var brush = new SolidBrush(BackColor))
   using (var pen = new Pen(Theme.Border)) { e.Graphics.FillPath(brush, path); e.Graphics.DrawPath(pen, path); }
  }
 }

 sealed class ActionButton : Button {
  readonly bool primary;
  bool hovered;
  public ActionButton(string text, int width, bool primary) {
   this.primary = primary; Text = text; Size = new Size(width, 44); Font = Theme.Font(10, FontStyle.Bold);
   FlatStyle = FlatStyle.Flat; FlatAppearance.BorderSize = 0; Cursor = Cursors.Hand; UseVisualStyleBackColor = false;
   SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
   Margin = new Padding(0, 0, 10, 0); AccessibleName = text;
  }
  protected override void OnMouseEnter(EventArgs e) { hovered = true; Invalidate(); base.OnMouseEnter(e); }
  protected override void OnMouseLeave(EventArgs e) { hovered = false; Invalidate(); base.OnMouseLeave(e); }
  protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
  protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
  protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }
  protected override void OnPaint(PaintEventArgs e) {
   e.Graphics.Clear(Parent == null ? Theme.Background : Parent.BackColor); e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
   Color fill = primary ? (hovered ? Color.FromArgb(179, 220, 145) : Theme.Green) : (hovered ? Color.FromArgb(39, 51, 43) : Theme.Surface);
   if (!Enabled) fill = Theme.Border;
   using (var path = Theme.Rounded(new RectangleF(.5f, .5f, Width - 1, Height - 1), 7 * e.Graphics.DpiX / 96))
   using (var brush = new SolidBrush(fill))
   using (var pen = new Pen(Focused && ShowFocusCues ? Theme.Green : (primary ? fill : Theme.Border))) { e.Graphics.FillPath(brush, path); e.Graphics.DrawPath(pen, path); }
   Color text = !Enabled ? Theme.Muted : (primary ? Theme.Background : Theme.Text);
   TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, text, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
  }
 }

 sealed class WorldArtwork : Control {
  readonly bool preview;
  public WorldArtwork(bool preview) {
   this.preview = preview; Dock = DockStyle.Fill; TabStop = false;
   AccessibleName = "RE4Craft. Resident Evil 4 и Minecraft";
   SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
  }
  static void ArtworkText(Graphics g, string text, float size, FontStyle style, Color color, RectangleF bounds) {
   using (var font = new Font("Segoe UI", size, style, GraphicsUnit.Pixel))
   using (var brush = new SolidBrush(color)) g.DrawString(text, font, brush, bounds);
  }
  static void Polygon(Graphics g, Color color, params PointF[] points) { using (var brush = new SolidBrush(color)) g.FillPolygon(brush, points); }
  static void Cube(Graphics g, float x, float y, float size, bool grass) {
   PointF a = new PointF(x, y), b = new PointF(x + size, y - size / 2), c = new PointF(x + size * 2, y), d = new PointF(x + size, y + size / 2);
   Polygon(g, grass ? Color.FromArgb(139, 174, 103) : Color.FromArgb(107, 121, 106), a, b, c, d);
   Polygon(g, grass ? Color.FromArgb(77, 85, 58) : Color.FromArgb(60, 74, 66), a, d, new PointF(d.X, d.Y + size), new PointF(a.X, a.Y + size));
   Polygon(g, grass ? Color.FromArgb(54, 66, 45) : Color.FromArgb(42, 56, 47), d, c, new PointF(c.X, c.Y + size), new PointF(d.X, d.Y + size));
   if (grass) {
    Polygon(g, Color.FromArgb(92, 127, 66), a, d, new PointF(d.X, d.Y + size * .22f), new PointF(a.X, a.Y + size * .22f));
    Polygon(g, Color.FromArgb(71, 104, 51), d, c, new PointF(c.X, c.Y + size * .22f), new PointF(d.X, d.Y + size * .22f));
    using (var brush = new SolidBrush(Color.FromArgb(89, 92, 66))) { g.FillRectangle(brush, x + size * .24f, y + size * .48f, size * .15f, size * .15f); g.FillRectangle(brush, x + size * .62f, y + size * .74f, size * .17f, size * .17f); }
   }
  }
  protected override void OnPaint(PaintEventArgs e) {
   float scale = e.Graphics.DpiX / 96; var g = e.Graphics; g.ScaleTransform(scale, scale);
   float w = Width / scale, h = Height / scale;
   using (var gradient = new LinearGradientBrush(new RectangleF(0, 0, w, h), Color.FromArgb(31, 45, 32), Color.FromArgb(15, 24, 19), 90)) g.FillRectangle(gradient, 0, 0, w, h);
   using (var line = new Pen(Color.FromArgb(33, 49, 36))) {
    for (int y = 0; y < h; y += 32) g.DrawLine(line, 0, y, w, y);
    for (int x = 0; x < w; x += 32) g.DrawLine(line, x, 0, x, h);
   }
   ArtworkText(g, "RE4CRAFT", 29, FontStyle.Bold, Theme.Text, new RectangleF(26, 28, w - 45, 42));
   ArtworkText(g, preview ? "ПРЕДПРОСМОТР / " + InstallerEngine.Version : InstallerEngine.Version, 11, FontStyle.Bold, Theme.Green, new RectangleF(28, 75, w - 45, 24));
   ArtworkText(g, "Два мира.\nОдно приключение.", 22, FontStyle.Bold, Theme.Text, new RectangleF(27, 126, w - 48, 88));
   float scene = Math.Max(270, h * .52f);
   // The artwork is drawn in the installer; no assets from either game are used.
   for (int i = 0; i < 8; ++i) {
    float x = i * 39 - 12, y = scene - 62 + (i % 3) * 17;
    using (var trunk = new Pen(Color.FromArgb(41, 62, 44), 6)) g.DrawLine(trunk, x, y, x, y + 120);
    for (int j = 0; j < 4; ++j) Polygon(g, Color.FromArgb(34 + j * 2, 53 + j * 2, 37 + j * 2), new PointF(x, y + j * 20 - 42), new PointF(x - 27 - j * 5, y + j * 20 + 16), new PointF(x + 27 + j * 5, y + j * 20 + 16));
   }
   using (var glow = new SolidBrush(Color.FromArgb(18, Theme.Green))) { g.FillEllipse(glow, w / 2 - 92, scene - 25, 184, 113); }
   Cube(g, w / 2 - 66, scene + 25, 42, false);
   Cube(g, w / 2 - 8, scene + 53, 42, false);
   Cube(g, w / 2 - 38, scene - 19, 48, true);
   ArtworkText(g, "RESIDENT EVIL 4 (2005)\n× MINECRAFT JAVA 26.3", 12, FontStyle.Bold, Theme.Muted, new RectangleF(28, scene + 136, w - 45, 46));
   using (var line = new Pen(Color.FromArgb(49, 66, 50))) g.DrawLine(line, 28, h - 114, w - 28, h - 114);
   ArtworkText(g, "F10", 24, FontStyle.Bold, Theme.Green, new RectangleF(27, h - 96, 72, 38));
   ArtworkText(g, "Minecraft ↔ RE4", 12, FontStyle.Regular, Theme.Text, new RectangleF(94, h - 85, w - 114, 27));
   ArtworkText(g, "HUD скрывается в катсценах", 11, FontStyle.Regular, Theme.Muted, new RectangleF(28, h - 49, w - 45, 30));
  }
 }

 sealed class SetupForm : DarkForm {
  readonly TextBox game = new TextBox(), prism = new TextBox();
  readonly RichTextBox output = new RichTextBox();
  readonly ActionButton install = new ActionButton("Установить / обновить", 226, true), remove = new ActionButton("Удалить мод", 137, false);
  readonly ActionButton musicButton = new ActionButton("Выключить трек", 161, false);
  readonly Label status = Theme.Label("Готов к установке", 10, Theme.Green, FontStyle.Bold);
  readonly Label musicStatus = Theme.Label("Загрузка трека…", 9, Theme.Muted, FontStyle.Regular);
  readonly SetupMusic music = new SetupMusic();
  readonly ToolTip toolTip = new ToolTip();
  readonly InstallerEngine engine;
  readonly bool preview;
  public SetupForm(bool preview) {
   this.preview = preview;
   Text = "RE4Craft — " + (preview ? "предпросмотр установщика" : "установка и удаление") + " · " + InstallerEngine.Version;
   StartPosition = FormStartPosition.CenterScreen; ClientSize = new Size(1060, 708); MinimumSize = new Size(930, 640);
   engine = new InstallerEngine(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RE4Craft", "backups"), Program.Resource, Log);
   SuspendLayout();
   var shell = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0), BackColor = Theme.Background };
   shell.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 256)); shell.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
   shell.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
   shell.Controls.Add(new WorldArtwork(preview) { Margin = new Padding(0) }, 0, 0);
   var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true, Padding = new Padding(32, 26, 32, 20), Margin = new Padding(0), BackColor = Theme.Background };
   var content = Theme.Stack(); scroll.Controls.Add(content); shell.Controls.Add(scroll, 1, 0); Controls.Add(shell);
   Theme.Row(content, Theme.Label("Установка мода", 25, Theme.Text, FontStyle.Bold), 6);
   Theme.Row(content, Theme.Label("Твой RE4. Теперь с механиками Minecraft.", 10, Theme.Muted, FontStyle.Regular), 24);
   game.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam", "steamapps", "common", "Resident Evil 4");
   prism.Text = engine.FindPrism();
   Theme.Row(content, FolderField("Resident Evil 4", "Папка игры, внутри которой находится Bin32", game, 0), 18);
   Theme.Row(content, FolderField("Prism Launcher", "Папка данных лаунчера, внутри которой находится instances", prism, 2), 18);
   var notice = new Card { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(14, 12, 14, 12) };
   var noticeText = Theme.Stack();
   Theme.Row(noticeText, Theme.Label("Перед установкой закрой RE4 и Minecraft.", 10, Theme.Text, FontStyle.Regular), 3);
   Theme.Row(noticeText, Theme.Label("Резервные копии создаются автоматически. Сохранения остаются на месте.", 9, Theme.Muted, FontStyle.Regular), 0);
   notice.Controls.Add(noticeText); Theme.Row(content, notice, 20);
   var actions = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill, Margin = new Padding(0), BackColor = Theme.Background, WrapContents = true };
   install.TabIndex = 4; install.Click += (s, e) => { if (preview) PreviewNotice(); else Run(() => engine.Install(game.Text, prism.Text, null)); };
   remove.TabIndex = 5; remove.Click += (s, e) => {
    if (preview) { PreviewNotice(); return; }
    if (Message("Удаление RE4Craft", "Восстановить прежний загрузчик RE4? Мир Minecraft будет сохранён в архиве или оставлен в существующем профиле.", true) == DialogResult.Yes) Run(() => engine.Uninstall(game.Text));
   };
   var guide = new ActionButton("Руководство", 137, false) { TabIndex = 6, Margin = new Padding(0) };
   guide.Click += (s, e) => Guide(); actions.Controls.Add(install); actions.Controls.Add(remove); actions.Controls.Add(guide);
   Theme.Row(content, actions, 20);
   var log = new Card { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(15, 12, 15, 12) };
   var logContent = Theme.Stack(); Theme.Row(logContent, status, 8);
   output.Dock = DockStyle.Fill; output.Height = 58; output.ReadOnly = true; output.BorderStyle = BorderStyle.None;
   output.BackColor = Theme.Surface; output.ForeColor = Theme.Muted; output.Font = Theme.Font(9, FontStyle.Regular);
   output.ScrollBars = RichTextBoxScrollBars.Vertical; output.TabStop = false; output.AccessibleName = "Журнал установки";
   Theme.Row(logContent, output, 0); log.Controls.Add(logContent); Theme.Row(content, log, 18);
   var audioCard = new Card { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(15, 12, 15, 12) };
   var audioLayout = new TableLayoutPanel { ColumnCount = 2, RowCount = 1, Dock = DockStyle.Top, AutoSize = true, Margin = new Padding(0), BackColor = Theme.Surface };
   audioLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); audioLayout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
   var track = Theme.Stack(); Theme.Row(track, Theme.Label("♫  RIVER SOLO", 10, Theme.Text, FontStyle.Bold), 3); Theme.Row(track, musicStatus, 0);
   musicButton.TabIndex = 7; musicButton.Anchor = AnchorStyles.Right; musicButton.Margin = new Padding(12, 0, 0, 0);
   musicButton.Click += (s, e) => { if (music.Playing) music.Stop(); else music.Start(); UpdateMusic(); };
   audioLayout.Controls.Add(track, 0, 0); audioLayout.Controls.Add(musicButton, 1, 0); audioCard.Controls.Add(audioLayout); Theme.Row(content, audioCard, 0);
   Log(preview ? "Предпросмотр интерфейса: установка и удаление отключены." : "Выбери папки игр и нажми «Установить / обновить».");
   ResumeLayout(true);
  }
  Control FolderField(string title, string help, TextBox box, int tabIndex) {
   var group = Theme.Stack(); Theme.Row(group, Theme.Label(title, 11, Theme.Text, FontStyle.Bold), 3);
   Theme.Row(group, Theme.Label(help, 9, Theme.Muted, FontStyle.Regular), 8);
   var field = new Card { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(12, 8, 8, 8) };
   var entry = new TableLayoutPanel { ColumnCount = 2, RowCount = 1, AutoSize = true, Dock = DockStyle.Top, Margin = new Padding(0), BackColor = Theme.Surface };
   entry.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); entry.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
   box.BorderStyle = BorderStyle.None; box.BackColor = Theme.Surface; box.ForeColor = Theme.Text; box.Font = Theme.Font(10, FontStyle.Regular);
   box.Anchor = AnchorStyles.Left | AnchorStyles.Right; box.Margin = new Padding(0, 0, 12, 0); box.TabIndex = tabIndex; box.AccessibleName = title + " — путь к папке";
   toolTip.SetToolTip(box, box.Text); box.TextChanged += (s, e) => toolTip.SetToolTip(box, box.Text);
   var browse = new ActionButton("Выбрать…", 107, false) { Height = 32, TabIndex = tabIndex + 1, Margin = new Padding(0) };
   browse.Click += (s, e) => { using (var dialog = new FolderBrowserDialog { SelectedPath = box.Text, Description = help }) if (dialog.ShowDialog(this) == DialogResult.OK) box.Text = dialog.SelectedPath; };
   entry.Controls.Add(box, 0, 0); entry.Controls.Add(browse, 1, 0); field.Controls.Add(entry); Theme.Row(group, field, 0); return group;
  }
  protected override void OnShown(EventArgs e) { base.OnShown(e); music.Start(); UpdateMusic(); ActiveControl = null; }
  void UpdateMusic() {
   bool playing = music.Playing;
   musicButton.Text = playing ? "Выключить трек" : "Включить трек";
   musicButton.AccessibleName = musicButton.Text; musicButton.Enabled = music.Available;
   musicStatus.Text = !music.Available ? "Не удалось воспроизвести трек" : (playing ? "Сейчас играет · громкость 35%" : "Музыка выключена");
   musicButton.Invalidate();
  }
  void PreviewNotice() { Message("Предпросмотр интерфейса", "Здесь можно посмотреть оформление и включить или выключить музыку. Установка и удаление мода в этой сборке отключены.", false); }
  void Guide() {
   using (var form = new DarkForm { Text = "RE4Craft — руководство", ClientSize = new Size(790, 640), MinimumSize = new Size(560, 400), Padding = new Padding(24) })
   using (var text = new RichTextBox { Dock = DockStyle.Fill, ReadOnly = true, BorderStyle = BorderStyle.None, BackColor = Theme.Background, ForeColor = Theme.Text, Font = Theme.Font(11, FontStyle.Regular), Text = Encoding.UTF8.GetString(Program.Resource("GUIDE_RU.md")) }) {
    form.Controls.Add(text); form.ShowDialog(this);
   }
  }
  DialogResult Message(string title, string text, bool yesNo) {
   using (var form = new DarkForm { Text = "RE4Craft", ClientSize = new Size(570, 270), MinimumSize = new Size(510, 260), MaximizeBox = false, MinimizeBox = false, Padding = new Padding(26) }) {
    var stack = Theme.Stack(); Theme.Row(stack, Theme.Label(title, 17, Theme.Text, FontStyle.Bold), 14);
    Theme.Row(stack, Theme.Label(text, 10, Theme.Muted, FontStyle.Regular), 24);
    var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, Margin = new Padding(0) };
    var ok = new ActionButton(yesNo ? "Восстановить" : "Понятно", 140, true) { DialogResult = yesNo ? DialogResult.Yes : DialogResult.OK, Margin = new Padding(10, 0, 0, 0) };
    buttons.Controls.Add(ok); form.AcceptButton = ok;
    if (yesNo) { var cancel = new ActionButton("Отмена", 120, false) { DialogResult = DialogResult.No }; buttons.Controls.Add(cancel); form.CancelButton = cancel; }
    else form.CancelButton = ok;
    Theme.Row(stack, buttons, 0); form.Controls.Add(stack); return form.ShowDialog(this);
   }
  }
  string DisplayError(string text) {
   if (text != "Нужна оригинальная Steam RE4 UHD 1.1.0. Файлы игры не изменены.") return text;
   string exe;
   try { exe = Path.Combine(game.Text, "Bin32", "bio4.exe"); } catch { exe = ""; }
   return File.Exists(exe)
    ? "Эта сборка bio4.exe не совпадает с поддерживаемой RE4 UHD 1.1.0. Мост использует адреса этой версии игры. Проверка аккаунта или лицензии Steam не выполняется."
    : "В выбранной папке не найден Bin32\\bio4.exe. Выбери корневую папку Resident Evil 4, внутри которой находится Bin32. Проверка аккаунта или лицензии Steam не выполняется.";
  }
  void Log(string text) { output.AppendText(text + Environment.NewLine); output.ScrollToCaret(); output.Refresh(); }
  void Run(Action action) {
   install.Enabled = remove.Enabled = false; status.Text = "Выполняется…"; status.ForeColor = Theme.Gold; status.Refresh();
   try { action(); status.Text = "Готово"; status.ForeColor = Theme.Green; }
   catch (Exception error) { string text = DisplayError(error.Message); status.Text = "Не удалось завершить"; status.ForeColor = Theme.Gold; Log("Ошибка: " + text); Message("Не удалось завершить", text, false); }
   finally { install.Enabled = remove.Enabled = true; }
  }
  protected override void Dispose(bool disposing) {
   if (disposing) { music.Dispose(); toolTip.Dispose(); }
   base.Dispose(disposing);
  }
 }
}
