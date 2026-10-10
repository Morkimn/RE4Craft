using System;
using System.IO;
using System.Linq;
using System.Text;
using RE4CraftSetup;

// Compile against a built installer; keep the test EXE and installer together.
// Reads an owned original EXE, but writes only to a fresh private fixture folder.
static class InstallerCompatibility {
 const string OriginalHash = "19AED4AF0AB06A748FF8744D45AC5580FCD6BE6B6B7E944B1AB8822A00C8EE4A";
 const string LaaHash = "DD227815A2747C2A81321F458308B5CF10C7F5A661A64BB33ABA006FE0BBD9E0";
 static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
 static void Write(string path, byte[] bytes) { Directory.CreateDirectory(Path.GetDirectoryName(path)); File.WriteAllBytes(path, bytes); }
 static byte[] Resource(string name) {
  using (var input = typeof(InstallerEngine).Assembly.GetManifestResourceStream("RE4Craft." + name)) {
   Require(input != null, "Missing production resource: " + name);
   using (var output = new MemoryStream()) { input.CopyTo(output); return output.ToArray(); }
  }
 }
 static byte[] LaaPatch(byte[] original) {
  var patched = (byte[])original.Clone();
  int pe = BitConverter.ToInt32(original, 0x3c), flags = pe + 22, checksum = pe + 24 + 64;
  patched[flags] |= 0x20; Array.Clear(patched, checksum, 4);
  uint sum = 0;
  for (int i = 0; i < patched.Length; i += 2) {
   sum += (uint)(patched[i] | (i + 1 < patched.Length ? patched[i + 1] << 8 : 0));
   sum = (sum & 0xffff) + (sum >> 16);
  }
  sum = (sum & 0xffff) + (sum >> 16); sum += (uint)patched.Length;
  Array.Copy(BitConverter.GetBytes(sum), 0, patched, checksum, 4);
  Require(InstallerEngine.Hash(patched) == LaaHash, "Generated patch does not match the reported 4GB executable");
  for (int i = 0; i < original.Length; ++i)
   Require(original[i] == patched[i] || i == flags || (i >= checksum && i < checksum + 4), "Patch changed bytes outside LAA/checksum");
  return patched;
 }
 static void Verify(string root, string name, byte[] exe, bool accepted) {
  string fixture = Path.Combine(root, name), game = Path.Combine(fixture, "game"), prism = Path.Combine(fixture, "prism");
  string state = Path.Combine(fixture, "state"), bin = Path.Combine(game, "Bin32"), exePath = Path.Combine(bin, "bio4.exe");
  byte[] oldLoader = Encoding.UTF8.GetBytes("previous-loader"), world = Encoding.UTF8.GetBytes("private-world-fixture");
  Write(exePath, exe); Write(Path.Combine(bin, "dinput8.dll"), oldLoader);
  Directory.CreateDirectory(Path.Combine(prism, "instances"));
  var engine = new InstallerEngine(state, Resource, s => {});
  if (!accepted) {
   bool refused = false;
   try { engine.Install(game, prism, null); } catch (IOException) { refused = true; }
   Require(refused, name + ": unsupported executable accepted");
   Require(!Directory.Exists(state) && !Directory.Exists(Path.Combine(prism, "instances", "RE4Craft")), name + ": refusal mutated installation state");
   Require(Directory.GetFiles(game, "*", SearchOption.AllDirectories).Length == 2, name + ": refusal wrote game files");
  } else {
   engine.Install(game, prism, null);
   Require(File.ReadAllBytes(Path.Combine(bin, "dinput8.dll")).SequenceEqual(Resource("native.dll")), name + ": incorrect native payload");
   string profile = Path.Combine(prism, "instances", "RE4Craft");
   Require(File.ReadAllBytes(Path.Combine(profile, ".minecraft", "mods", "skycraft-0.1.2.jar")).SequenceEqual(Resource("guest.jar")), name + ": incorrect guest payload");
   Write(Path.Combine(profile, ".minecraft", "saves", "test", "level.dat"), world);
   engine.Install(game, prism, null); engine.Install(game, prism, null);
   engine.Uninstall(game);
   Require(!Directory.Exists(profile), name + ": new profile not archived");
   string[] worlds = Directory.GetFiles(Path.Combine(prism, "instances"), "level.dat", SearchOption.AllDirectories);
   Require(worlds.Length == 1 && File.ReadAllBytes(worlds[0]).SequenceEqual(world), name + ": world lost");
  }
  Require(File.ReadAllBytes(exePath).SequenceEqual(exe), name + ": executable modified");
  Require(File.ReadAllBytes(Path.Combine(bin, "dinput8.dll")).SequenceEqual(oldLoader), name + ": first loader not preserved/restored");
  Console.WriteLine("PASS: " + name + (accepted ? " install/update/uninstall" : " rejected before edits"));
 }
 static int Main(string[] args) {
  try {
   if (args.Length != 2) throw new ArgumentException("Usage: InstallerCompatibility.exe <owned-original-bio4.exe> <private-test-root>");
   byte[] original = File.ReadAllBytes(args[0]);
   Require(InstallerEngine.Hash(original) == OriginalHash, "Input must be the supported original; input is read-only");
   string root = Path.Combine(Path.GetFullPath(args[1]), "compatibility-" + Guid.NewGuid().ToString("N"));
   Directory.CreateDirectory(root);
   byte[] laa = LaaPatch(original), flagOnly = (byte[])original.Clone(), checksumOnly = (byte[])original.Clone();
   int pe = BitConverter.ToInt32(original, 0x3c), checksum = pe + 24 + 64;
   flagOnly[pe + 22] |= 0x20;
   for (int i = 0; i < 4; ++i) checksumOnly[checksum + i] ^= (byte)(0x31 + i);
   Verify(root, "original", original, true);
   Verify(root, "4gb-matching-friend", laa, true);
   Verify(root, "4gb-without-checksum-update", flagOnly, true);
   Verify(root, "checksum-only", checksumOnly, true);
   var changedCode = (byte[])laa.Clone();
   int section = pe + 24 + BitConverter.ToUInt16(original, pe + 20);
   int sections = BitConverter.ToUInt16(original, pe + 6), codeOffset = -1;
   for (int i = 0; i < sections; ++i, section += 40)
    if ((BitConverter.ToUInt32(original, section + 36) & 0x20) != 0 && BitConverter.ToUInt32(original, section + 16) > 16) { codeOffset = BitConverter.ToInt32(original, section + 20) + 16; break; }
   Require(codeOffset > 0 && codeOffset < original.Length, "No code section found");
   changedCode[codeOffset] ^= 1;
   var changedHeader = (byte[])laa.Clone(); changedHeader[pe + 22] ^= 0x02;
   Verify(root, "modified-code", changedCode, false);
   Verify(root, "other-header-flag", changedHeader, false);
   Verify(root, "truncated", laa.Take(1024).ToArray(), false);
   Verify(root, "empty", new byte[0], false);
   // Keep legacy .NET Framework enumeration below MAX_PATH. Nesting another
   // GUID fixture in root made the archived level.dat path exactly 260 chars;
   // it existed, but this test process's Directory.GetFiles omitted it.
   InstallerEngine.SelfTest(Path.GetFullPath(args[1]));
   Require(File.ReadAllBytes(args[0]).SequenceEqual(original), "Owned source executable changed");
   File.WriteAllText(Path.Combine(root, "compatibility-result.txt"), "PASS: original, exact friend 4GB hash, LAA-only, checksum-only; code/header changes, truncation and empty input refused; install/update/uninstall and existing self-tests passed.\n");
   Console.WriteLine("PASS: source executable untouched; synthetic rollback/backups/world tests passed");
   return 0;
  } catch (Exception error) { Console.Error.WriteLine(error); return 1; }
 }
}
