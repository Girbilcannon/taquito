#pragma once
namespace ShortcutSettings {
enum Action : unsigned { ToggleRendering=1,NextPreset=2,PreviousPreset=4,ToggleMenu=8 };
struct Result {unsigned actions=0;};
// Nexus callbacks may run on other threads; execution stays on the render thread.
void Request(unsigned action,bool released=false);
// Observe mouse messages delivered to GW2; never consumes or synthesizes input.
void MouseButton(unsigned button,bool down);
Result Poll(bool gameFocused,bool allowActions);
void Reset();
unsigned MouseReleased();
unsigned MousePressed();
}
