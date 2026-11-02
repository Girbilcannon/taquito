#include "ShortcutSettings.h"
#include "../imgui/imgui.h"
#include <array>
#include <atomic>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
namespace {
std::atomic_uint pending{0};
std::array<bool,3> previousMouse{};
unsigned releasedMouse=0,pressedMouse=0;
}
void ShortcutSettings::Request(unsigned action,bool released){if(!released)pending.fetch_or(action,std::memory_order_release);}
void ShortcutSettings::Reset(){pending.store(0,std::memory_order_release);releasedMouse=pressedMouse=0;}
unsigned ShortcutSettings::MouseReleased(){return releasedMouse;}
unsigned ShortcutSettings::MousePressed(){return pressedMouse;}
ShortcutSettings::Result ShortcutSettings::Poll(bool gameFocused,bool allowActions){
    const auto actions=pending.exchange(0,std::memory_order_acq_rel);
    releasedMouse=pressedMouse=0;
#if defined(_WIN32) || defined(TAQUITO_TEST_NATIVE_INPUT)
    const int mouseKeys[]={1,2,4};
    for(int i=0;i<3;++i){const bool down=(GetAsyncKeyState(mouseKeys[i])&0x8000)!=0;
        if(previousMouse[i]&&!down)releasedMouse|=1u<<i;
        if(!previousMouse[i]&&down)pressedMouse|=1u<<i;
        previousMouse[i]=down;
    }
#else
    for(int i=0;i<3;++i){if(ImGui::IsMouseReleased(i))releasedMouse|=1u<<i;if(ImGui::IsMouseClicked(i))pressedMouse|=1u<<i;}
#endif
    if(!gameFocused){releasedMouse=pressedMouse=0;return {};}
    if(!allowActions||ImGui::GetIO().WantTextInput)return {};
    return {actions};
}
