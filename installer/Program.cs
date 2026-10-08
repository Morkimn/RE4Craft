using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
using System.Runtime.Versioning;

[assembly: TargetFramework(".NETFramework,Version=v4.8")]

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
    Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
#if VISUAL_PREVIEW
    Application.Run(new SetupForm(true));
#else
    Application.Run(new SetupForm(Array.IndexOf(args, "--preview") >= 0));
#endif
    return 0;
   } catch (Exception error) {
    if (args.Length == 0) MessageBox.Show(error.Message, "RE4Craft", MessageBoxButtons.OK, MessageBoxIcon.Error);
    else { Console.Error.WriteLine(error.Message);string report = Arg(args, "--report");if (!String.IsNullOrWhiteSpace(report)) File.WriteAllText(report, error.ToString()); }
    return 1;
   }
  }
 }
}
