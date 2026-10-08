using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace RE4CraftSetup {
 sealed class SetupMusic : IDisposable {
  [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
  static extern uint mciSendString(string command, StringBuilder result, int length, IntPtr callback);
  readonly string alias = "re4craft" + Guid.NewGuid().ToString("N");
  string folder, file;
  bool opened, disposed;
  public bool Available { get; private set; }
  public bool Playing {
   get {
    if (!opened) return false;
    var result = new StringBuilder(64);
    return mciSendString("status " + alias + " mode", result, result.Capacity, IntPtr.Zero) == 0 && result.ToString() == "playing";
   }
  }
  uint Command(string text) { return mciSendString(text, null, 0, IntPtr.Zero); }
  public void Start() {
   if (disposed) return;
   try {
    if (!opened) {
     folder = Path.Combine(Path.GetTempPath(), "RE4Craft-music-" + Guid.NewGuid().ToString("N"));
     Directory.CreateDirectory(folder);
     file = Path.Combine(folder, "theme.mp3");
     File.WriteAllBytes(file, Program.Resource("theme.mp3"));
     opened = Command("open \"" + file + "\" type mpegvideo alias " + alias) == 0;
     if (!opened) { Available = false; return; }
     Command("setaudio " + alias + " volume to 350");
    }
    Available = Command("play " + alias + " from 0 repeat") == 0;
   } catch { Available = false; }
  }
  public void Stop() { if (opened) Command("stop " + alias); }
  public void Dispose() {
   if (disposed) return;
   disposed = true;
   if (opened) { Command("stop " + alias); Command("close " + alias); opened = false; }
   // Delete only this player's own temporary file and its now-empty directory.
   try { if (file != null && File.Exists(file)) File.Delete(file); if (folder != null && Directory.Exists(folder)) Directory.Delete(folder, false); } catch { }
  }
 }
}
