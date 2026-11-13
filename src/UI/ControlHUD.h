#pragma once
namespace ControlHUD {
void Toggle();
void Close();
bool IsOpen();
void Render(void (*save)());
}
