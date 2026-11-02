#pragma once
namespace ShortcutSettings {
enum Action : unsigned { ToggleRendering=1,NextPreset=2,PreviousPreset=4,ToggleMenu=8 };
struct Result {unsigned actions=0;};
// Nexus callbacks may run on other threads; execution stays on the render thread.
void Request(unsigned action,bool released=false);
Result Poll(bool gameFocused,bool allowActions);
void Reset();
unsigned MouseReleased();
unsigned MousePressed();
}
