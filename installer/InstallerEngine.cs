using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Text;

namespace RE4CraftSetup {
 [DataContract] public sealed class BackupEntry {
  [DataMember] public string Key;
  [DataMember] public bool Existed;
  [DataMember] public string Hash;
 }
 [DataContract] public sealed class Installation {
  [DataMember] public int Schema = 1;
  [DataMember] public string GameRoot;
  [DataMember] public string PrismRoot;
  [DataMember] public bool CreatedProfile;
  [DataMember] public string Version;
  [DataMember] public List<BackupEntry> Files = new List<BackupEntry>();
 }
 public sealed class InstallerEngine {
  public const string SupportedExe = "19AED4AF0AB06A748FF8744D45AC5580FCD6BE6B6B7E944B1AB8822A00C8EE4A";
  // Fingerprint of the supported image with only its LAA bit and PE checksum cleared.
  const string SupportedNormalizedExe = "1C121D4CC199616A86375F3AB157BEE2DF1C7D83CB980021778D5A6DD45BF53D";
  const int SupportedLaaOffset = 0x14e, SupportedChecksumOffset = 0x190;
  public const string Version = "0.3.2";
  readonly string storage, expectedExe;
  readonly Func<string, byte[]> payload;
  readonly Action<string> log;
  public InstallerEngine(string storageRoot, Func<string, byte[]> readResource, Action<string> logger) : this(storageRoot, readResource, logger, SupportedExe) {}
  InstallerEngine(string storageRoot, Func<string, byte[]> readResource, Action<string> logger, string expected) {
   storage = Full(storageRoot); payload = readResource; log = logger; expectedExe = expected;
  }
  public static string Full(string path) { return Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar); }
  static bool Equal(string a, string b) { return String.Equals(Full(a), Full(b), StringComparison.OrdinalIgnoreCase); }
  public static string Hash(byte[] bytes) { using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", ""); }
  static string FileHash(string file) { return Hash(File.ReadAllBytes(file)); }
  bool SupportsExecutable(byte[] bytes) {
   if (Hash(bytes) == expectedExe) return true;
   if (expectedExe != SupportedExe || bytes.Length < SupportedChecksumOffset + 4) return false;
   // These offsets belong to the supported PE layout. Every other byte, including
   // all code, data and remaining headers, must still match its fingerprint.
   var normalized = (byte[])bytes.Clone();
   normalized[SupportedLaaOffset] &= 0xdf;
   Array.Clear(normalized, SupportedChecksumOffset, 4);
   return Hash(normalized) == SupportedNormalizedExe;
  }
  string Home(string game) { return Path.Combine(storage, Hash(Encoding.UTF8.GetBytes(Full(game).ToUpperInvariant())).Substring(0, 16)); }
  static string Profile(string prism) { return Path.Combine(prism, "instances", "RE4Craft"); }
  static Dictionary<string, string> Targets(Installation m) {
   string bin = Path.Combine(m.GameRoot, "Bin32"), profile = Profile(m.PrismRoot), mods = Path.Combine(profile, ".minecraft", "mods");
   return new Dictionary<string, string> {
    {"loader", Path.Combine(bin, "dinput8.dll")},
    {"settings", Path.Combine(bin, "re4_tweaks", "default_settings", "settings.ini")},
    {"trainer", Path.Combine(bin, "re4_tweaks", "default_settings", "trainer_settings.ini")},
    {"guest", Path.Combine(mods, "skycraft-0.1.2.jar")},
    {"fabric", Path.Combine(mods, "fabric-api-0.161.0+26.3.jar")},
    {"pack", Path.Combine(profile, "mmc-pack.json")},
    {"profile", Path.Combine(profile, "instance.cfg")}
   };
  }
  static void EnsureClosed(string game, string prism) {
   foreach (var name in new[] { "bio4", "javaw", "java" }) foreach (var p in Process.GetProcessesByName(name)) using (p) {
    string path;
    try { path = p.MainModule.FileName; } catch { throw new IOException("Закройте RE4 и Minecraft перед установкой/удалением."); }
    if (name == "bio4" && Equal(path, Path.Combine(game, "Bin32", "bio4.exe"))) throw new IOException("Сначала закройте Resident Evil 4.");
    if (name != "bio4" && Full(path).StartsWith(Full(prism) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new IOException("Сначала закройте Minecraft, запущенный через этот Prism.");
   }
  }
  static void Write(string path, byte[] data) { Directory.CreateDirectory(Path.GetDirectoryName(path)); File.WriteAllBytes(path, data); }
  static T ReadJson<T>(string file) { using (var s = File.OpenRead(file)) return (T)new DataContractJsonSerializer(typeof(T)).ReadObject(s); }
  static void Save(Installation m, string path) {
   Directory.CreateDirectory(Path.GetDirectoryName(path)); string temp = path + ".new";
   using (var s = File.Create(temp)) new DataContractJsonSerializer(typeof(Installation)).WriteObject(s, m);
   if (File.Exists(path)) File.Replace(temp, path, path + ".previous"); else File.Move(temp, path);
  }
  void Validate(Installation m, string game) {
   if (m == null || m.Schema != 1 || !Equal(m.GameRoot, game) || String.IsNullOrWhiteSpace(m.PrismRoot)) throw new IOException("Неверный манифест установки.");
   var targets = Targets(m); var keys = new HashSet<string>();
   foreach (var e in m.Files) {
    if (!targets.ContainsKey(e.Key) || !keys.Add(e.Key)) throw new IOException("Неизвестный файл в манифесте.");
    if (e.Existed && (e.Hash == null || e.Hash.Length != 64)) throw new IOException("Повреждённый манифест резервной копии.");
   }
  }
  public string FindPrism() {
   string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
   foreach (var p in new[] { Path.Combine(local, "PeakCraft", "Prism"), Path.Combine(local, "Programs", "PrismLauncher"), Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "PrismLauncher") })
    if (Directory.Exists(Path.Combine(p, "instances"))) return p;
   return "";
  }
  public void Install(string gameRoot, string prismRoot, string legacyBackup) {
   string game = Full(gameRoot), prism = Full(prismRoot);
   EnsureClosed(game, prism);
   string exe = Path.Combine(game, "Bin32", "bio4.exe");
   if (!File.Exists(exe) || !SupportsExecutable(File.ReadAllBytes(exe))) throw new IOException("Нужна оригинальная Steam RE4 UHD 1.1.0. Файлы игры не изменены.");
   if (!Directory.Exists(Path.Combine(prism, "instances"))) throw new IOException("Укажите папку данных Prism Launcher (с папкой instances). Сначала запустите Prism хотя бы один раз.");
   string home = Home(game), manifest = Path.Combine(home, "installation.json");
   bool updating = File.Exists(manifest);
   Installation m = updating ? ReadJson<Installation>(manifest) : new Installation { GameRoot = game, PrismRoot = prism, CreatedProfile = !Directory.Exists(Profile(prism)), Version = Version };
   Validate(m, game);
   if (!Equal(m.PrismRoot, prism)) throw new IOException("Обновление должно использовать тот же Prism. Сначала удалите предыдущую установку.");
   var targets = Targets(m);
   var files = new Dictionary<string, byte[]> {
    {"loader", payload("native.dll")}, {"settings", payload("settings.ini")}, {"trainer", payload("trainer.ini")},
    {"guest", payload("guest.jar")}, {"fabric", payload("fabric.jar")}
   };
   if (m.CreatedProfile && (!updating || !File.Exists(targets["profile"]))) {
    files["pack"] = payload("mmc-pack.json");
    string java = Path.Combine(prism, "java", "java-runtime-epsilon", "bin", "javaw.exe");
    string cfg = "[General]\r\nInstanceType=OneSix\r\nname=RE4Craft\r\niconKey=default\r\nOverrideJavaArgs=true\r\nJvmArgs=\"--enable-native-access=ALL-UNNAMED -Dskycraft.link=RE4Craft_v1 -Dskycraft.firstPersonAvatar=false\"\r\nOverrideMemory=true\r\nMinMemAlloc=512\r\nMaxMemAlloc=4096\r\nOverrideConsole=true\r\nShowConsole=false\r\nShowConsoleOnError=true\r\nConfigVersion=1.3\r\n";
    if (File.Exists(java)) cfg += "OverrideJavaLocation=true\r\nJavaPath=" + java.Replace('\\', '/') + "\r\n";
    files["profile"] = Encoding.UTF8.GetBytes(cfg);
   } else {
    string cfgFile = targets["profile"];
    if (!File.Exists(cfgFile) || !File.ReadAllText(cfgFile).Contains("skycraft.link=RE4Craft_v1")) throw new IOException("Существующий профиль RE4Craft не настроен для этого моста. Переименуйте его в Prism или укажите другой Prism.");
   }
   // Read and validate all resources before mutating a user's installation.
   foreach (var data in files.Values) if (data == null || data.Length == 0) throw new IOException("Пакет установщика повреждён.");
   // A standard Prism may use Java outside its data directory. Check the
   // affected files for live JAR/DLL locks without inspecting account or JVM
   // command-line data. This happens before the first target is replaced.
   foreach (var key in files.Keys) if (File.Exists(targets[key])) using (var probe = new FileStream(targets[key], FileMode.Open, FileAccess.ReadWrite, FileShare.None)) {}
   Directory.CreateDirectory(home);
   var prior = new Dictionary<string, byte[]>();
   foreach (var key in files.Keys) {
    string target = targets[key]; prior[key] = File.Exists(target) ? File.ReadAllBytes(target) : null;
    if (m.Files.Any(e => e.Key == key)) continue;
    string oldLoader = target;
    if (!String.IsNullOrWhiteSpace(legacyBackup) && (key == "loader" || key == "settings" || key == "trainer")) {
     if (!Directory.Exists(Full(legacyBackup))) throw new IOException("Не найдена исходная резервная копия.");
     oldLoader = key == "loader" ? Path.Combine(Full(legacyBackup), "dinput8.dll") : Path.Combine(Full(legacyBackup), "re4_tweaks", "default_settings", key == "settings" ? "settings.ini" : "trainer_settings.ini");
    }
    byte[] original = File.Exists(oldLoader) ? File.ReadAllBytes(oldLoader) : null;
    var entry = new BackupEntry { Key = key, Existed = original != null, Hash = original == null ? null : Hash(original) };
    if (original != null) Write(Path.Combine(home, key + ".backup"), original);
    m.Files.Add(entry);
   }
   Save(m, manifest); // recoverable even if this process is interrupted while copying
   try {
    foreach (var pair in files) { Write(targets[pair.Key], pair.Value); if (FileHash(targets[pair.Key]) != Hash(pair.Value)) throw new IOException("Ошибка проверки записанного файла."); log("Установлен: " + Path.GetFileName(targets[pair.Key])); }
    m.Version = Version; Save(m, manifest);
   } catch {
    foreach (var pair in prior) { if (pair.Value == null) { if (File.Exists(targets[pair.Key])) File.Delete(targets[pair.Key]); } else Write(targets[pair.Key], pair.Value); }
    throw;
   }
   log("Готово. Миры и аккаунты не копировались. Запустите RE4 через Steam и профиль RE4Craft в Prism.");
  }
  public void Uninstall(string gameRoot) {
   string game = Full(gameRoot), home = Home(game), manifest = Path.Combine(home, "installation.json");
   if (!File.Exists(manifest)) throw new IOException("Установка этого пакета не найдена для выбранной игры.");
   var m = ReadJson<Installation>(manifest); Validate(m, game); EnsureClosed(game, m.PrismRoot);
   var targets = Targets(m);
   // Verify every original before touching installed files.
   foreach (var e in m.Files) if (e.Existed && (!File.Exists(Path.Combine(home, e.Key + ".backup")) || FileHash(Path.Combine(home, e.Key + ".backup")) != e.Hash)) throw new IOException("Повреждена резервная копия: " + e.Key);
   string archive = Path.Combine(home, "removed-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 6));
   Directory.CreateDirectory(archive);
   foreach (var e in m.Files) {
    string target = targets[e.Key]; if (File.Exists(target)) File.Copy(target, Path.Combine(archive, e.Key + ".installed"));
    if (e.Existed) Write(target, File.ReadAllBytes(Path.Combine(home, e.Key + ".backup")));
    else if (File.Exists(target)) File.Delete(target);
   }
   // Keep the whole dedicated Minecraft profile, including worlds, in an archive.
   if (m.CreatedProfile && Directory.Exists(Profile(m.PrismRoot))) {
    string profile = Full(Profile(m.PrismRoot)), instances = Full(Path.Combine(m.PrismRoot, "instances"));
    if (!Equal(Path.GetDirectoryName(profile), instances) || Path.GetFileName(profile) != "RE4Craft") throw new IOException("Неверный путь профиля. Миры сохранены на месте.");
    string archivedProfile = Path.Combine(instances, ".RE4Craft-removed-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 6));
    Directory.Move(profile, archivedProfile); log("Миры сохранены: " + archivedProfile);
   }
   File.Move(manifest, Path.Combine(archive, "installation.json"));
   log("Мод удалён. Прежний загрузчик восстановлен; сохранения RE4 не изменялись.");
  }
  static void Require(bool condition, string message) { if (!condition) throw new Exception("Self-test: " + message); }
  public static void SelfTest(string root) {
   string fixture = Path.Combine(Full(root), "installer-test-" + Guid.NewGuid().ToString("N"));
   string game = Path.Combine(fixture, "game"), prism = Path.Combine(fixture, "prism"), store = Path.Combine(fixture, "state");
   byte[] exe = Encoding.UTF8.GetBytes("fixture-exe"), loader = Encoding.UTF8.GetBytes("previous-loader");
   Write(Path.Combine(game, "Bin32", "bio4.exe"), exe); Write(Path.Combine(game, "Bin32", "dinput8.dll"), loader);
   Write(Path.Combine(prism, "prismlauncher.exe"), new byte[] { 1 }); Directory.CreateDirectory(Path.Combine(prism, "instances"));
   var files = new Dictionary<string, byte[]>();foreach (var key in new[] { "native.dll", "settings.ini", "trainer.ini", "guest.jar", "fabric.jar", "mmc-pack.json" }) files[key] = Encoding.UTF8.GetBytes("payload-" + key);
   var engine = new InstallerEngine(store, k => files[k], s => {}, Hash(exe));
   engine.Install(game, prism, null); engine.Install(game, prism, null); engine.Install(game, prism, null);
   string save = Path.Combine(Profile(prism), ".minecraft", "saves", "SkyCraft", "level.dat"); Write(save, new byte[] { 42 });
   engine.Uninstall(game);
   Require(FileHash(Path.Combine(game, "Bin32", "dinput8.dll")) == Hash(loader), "update/uninstall must restore the first loader");
   Require(FileHash(Path.Combine(game, "Bin32", "bio4.exe")) == Hash(exe), "game executable changed");
   Require(Directory.GetFiles(Path.Combine(prism, "instances"), "level.dat", SearchOption.AllDirectories).Any(p => File.ReadAllBytes(p)[0] == 42), "Minecraft world lost");
   Require(!Directory.Exists(Profile(prism)), "profile was not archived");
   var wrong = new InstallerEngine(Path.Combine(fixture, "wrong"), k => files[k], s => {});
   bool refused = false;try { wrong.Install(game, prism, null); } catch (IOException) { refused = true; }
   Require(refused && FileHash(Path.Combine(game, "Bin32", "dinput8.dll")) == Hash(loader), "unsupported EXE must refuse before edits");
   // Force a failure after the native DLL has already been replaced.
   Directory.CreateDirectory(Path.Combine(Profile(prism), ".minecraft", "mods", "skycraft-0.1.2.jar"));
   var failing = new InstallerEngine(Path.Combine(fixture, "failure-state"), k => files[k], s => {}, Hash(exe));
   bool failed = false; try { failing.Install(game, prism, null); } catch (IOException) { failed = true; } catch (UnauthorizedAccessException) { failed = true; }
   Require(failed && FileHash(Path.Combine(game, "Bin32", "dinput8.dll")) == Hash(loader), "partial copy must roll back native DLL");
   // Existing compatible profiles, their configuration and worlds must survive.
   Directory.Delete(Path.Combine(Profile(prism), ".minecraft", "mods", "skycraft-0.1.2.jar"));
   Write(Path.Combine(Profile(prism), "instance.cfg"), Encoding.UTF8.GetBytes("skycraft.link=RE4Craft_v1\nuser-setting=keep"));
   Write(Path.Combine(Profile(prism), "mmc-pack.json"), Encoding.UTF8.GetBytes("user-pack")); Write(save, new byte[] { 43 });
   var existing = new InstallerEngine(Path.Combine(fixture, "existing-state"), k => files[k], s => {}, Hash(exe));
   existing.Install(game, prism, null);
   var state = ReadJson<Installation>(Path.Combine(existing.Home(game), "installation.json"));
   string original = Path.Combine(existing.Home(game), "loader.backup"); Write(original, new byte[] { 99 });
   bool damaged = false; try { existing.Uninstall(game); } catch (IOException) { damaged = true; }
   Require(damaged && FileHash(Path.Combine(game, "Bin32", "dinput8.dll")) == Hash(files["native.dll"]), "damaged backup must refuse before edits");
   Write(original, loader); existing.Uninstall(game);
   Require(Directory.Exists(Profile(prism)) && File.ReadAllBytes(save)[0] == 43, "existing profile/world lost");
   Require(File.ReadAllText(Path.Combine(Profile(prism), "instance.cfg")).Contains("user-setting=keep") && File.ReadAllText(Path.Combine(Profile(prism), "mmc-pack.json")) == "user-pack", "existing profile settings changed");
   string legacy = Path.Combine(fixture, "legacy-empty"); Directory.CreateDirectory(legacy);
   var adopted = new InstallerEngine(Path.Combine(fixture, "adopted-state"), k => files[k], s => {}, Hash(exe));
   adopted.Install(game, prism, legacy); adopted.Uninstall(game);
   Require(!File.Exists(Path.Combine(game, "Bin32", "dinput8.dll")) && File.ReadAllBytes(save)[0] == 43, "legacy adoption must restore absent loader and preserve world");
   File.WriteAllText(Path.Combine(root, "installer-test-result.txt"), "PASS: install, repeated update, partial-copy rollback, new/existing world preservation, unsupported EXE, damaged backup refusal\n", Encoding.UTF8);
  }
 }
}
