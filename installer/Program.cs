using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

namespace RE4CraftSetup {
 static class Program {
  public static byte[] Resource(string name) {
   using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("RE4Craft." + name)) {
    if (stream == null) throw new IOException("Не найден компонент пакета: " + name);
    using (var output = new MemoryStream()) { stream.CopyTo(output); return output.ToArray(); }
   }
  }
  static string Arg(string[] args, string key) { int i = Array.IndexOf(args, key);return i >= 0 && i + 1 < args.Length ? args[i + 1] : null; }
  [STAThread] static int Main(string[] args) {
   try {
    if (Array.IndexOf(args, "--self-test") >= 0) { InstallerEngine.SelfTest(Arg(args, "--test-root") ?? Path.GetTempPath());return 0; }
    var engine = new InstallerEngine(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RE4Craft", "backups"), Resource, s => Console.WriteLine(s));
    if (Array.IndexOf(args, "--install") >= 0) { engine.Install(Arg(args, "--game"), Arg(args, "--prism"), Arg(args, "--legacy-backup"));return 0; }
    if (Array.IndexOf(args, "--uninstall") >= 0) { engine.Uninstall(Arg(args, "--game"));return 0; }
    Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new SetupForm());return 0;
   } catch (Exception error) {
    if (args.Length == 0) MessageBox.Show(error.Message, "RE4Craft", MessageBoxButtons.OK, MessageBoxIcon.Error);
    else { Console.Error.WriteLine(error.Message);string report = Arg(args, "--report");if (!String.IsNullOrWhiteSpace(report)) File.WriteAllText(report, error.ToString()); }
    return 1;
   }
  }
 }
 sealed class SetupForm : Form {
  readonly TextBox game = new TextBox(), prism = new TextBox();
  readonly RichTextBox output = new RichTextBox();readonly Button install = new Button(), remove = new Button();
  readonly InstallerEngine engine;
  public SetupForm() {
   Text = "RE4Craft — установка и удаление " + InstallerEngine.Version;ClientSize = new Size(730, 435);MinimumSize = Size;Font = new Font("Segoe UI", 10);StartPosition = FormStartPosition.CenterScreen;
   engine = new InstallerEngine(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RE4Craft", "backups"), Program.Resource, Log);
   AddLabel("Resident Evil 4 (2005) × Minecraft 26.3", 20, 15, 690);
   AddLabel("Закройте обе игры. Установщик сохраняет прежний загрузчик; игровые сохранения остаются на месте.", 20, 43, 690);
   AddLabel("Папка Resident Evil 4 (с Bin32)", 20, 85, 660);game.SetBounds(20, 111, 620, 28);game.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam", "steamapps", "common", "Resident Evil 4");Controls.Add(game);Browse(game, 650, 111);
   AddLabel("Папка данных Prism Launcher (с instances)", 20, 149, 660);prism.SetBounds(20, 175, 620, 28);prism.Text = engine.FindPrism();Controls.Add(prism);Browse(prism, 650, 175);
   install.Text = "Установить / обновить";install.SetBounds(20, 221, 200, 37);install.Click += (s, e) => Run(() => engine.Install(game.Text, prism.Text, null));Controls.Add(install);
   remove.Text = "Удалить мод";remove.SetBounds(235, 221, 150, 37);remove.Click += (s, e) => {
    if (MessageBox.Show("Восстановить прежний загрузчик RE4? Мир Minecraft будет сохранён в архиве или оставлен в существующем профиле.", "Удаление RE4Craft", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes) Run(() => engine.Uninstall(game.Text));
   };Controls.Add(remove);
   var guide = new Button { Text = "Руководство" };guide.SetBounds(400, 221, 150, 37);guide.Click += (s, e) => {
    var form = new Form { Text = "RE4Craft — руководство", Size = new Size(790, 620), StartPosition = FormStartPosition.CenterParent };
    var text = new RichTextBox { Dock = DockStyle.Fill, ReadOnly = true, Font = new Font("Segoe UI", 11), Text = System.Text.Encoding.UTF8.GetString(Program.Resource("GUIDE_RU.md")) };form.Controls.Add(text);form.ShowDialog(this);
   };Controls.Add(guide);
   output.SetBounds(20, 276, 690, 137);output.ReadOnly = true;output.BackColor = Color.FromArgb(245, 245, 245);Controls.Add(output);
   Log("F10: Minecraft ↔ обычное прохождение RE4. Во время катсцен Minecraft HUD скрыт автоматически.");
  }
  void AddLabel(string text, int x, int y, int width) { Controls.Add(new Label { Text = text, Location = new Point(x, y), Size = new Size(width, 40) }); }
  void Browse(TextBox box, int x, int y) { var button = new Button { Text = "…" };button.SetBounds(x, y, 60, 28);button.Click += (s, e) => { using (var dialog = new FolderBrowserDialog { SelectedPath = box.Text, Description = "Выберите папку" }) if (dialog.ShowDialog(this) == DialogResult.OK) box.Text = dialog.SelectedPath; };Controls.Add(button); }
  void Log(string text) { output.AppendText(text + Environment.NewLine);output.ScrollToCaret(); }
  void Run(Action action) { install.Enabled = remove.Enabled = false;try { action(); } catch (Exception error) { Log("Ошибка: " + error.Message);MessageBox.Show(this, error.Message, "RE4Craft", MessageBoxButtons.OK, MessageBoxIcon.Error); } finally { install.Enabled = remove.Enabled = true; } }
 }
}
