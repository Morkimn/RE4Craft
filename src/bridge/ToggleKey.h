#pragma once
namespace re4craft {
// Used with one Windows keyboard event stream. Ignore held-key repeats.
struct ToggleKey {
 bool held=false;
 bool update(bool down){bool pressed=down&&!held;held=down;return pressed;}
 void reset(){held=false;}
};
}
