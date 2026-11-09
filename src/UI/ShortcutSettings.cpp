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
std::atomic_uint mouseEvents{0}; // Low 3 bits: down; bits 8..10: up.
std::array<bool,3> previousMouse{};
unsigned releasedMouse=0,pressedMouse=0;
}
void ShortcutSettings::Request(unsigned action,bool released){if(!released)pending.fetch_or(action,std::memory_order_release);}
void ShortcutSettings::Reset(){pending.store(0,std::memory_order_release);mouseEvents.store(0,std::memory_order_release);previousMouse.fill(false);releasedMouse=pressedMouse=0;}
void ShortcutSettings::MouseButton(unsigned button,bool down){
    if(button<3)mouseEvents.fetch_or(1u<<(button+(down?0:8)),std::memory_order_release);
}
unsigned ShortcutSettings::MouseReleased(){return releasedMouse;}
unsigned ShortcutSettings::MousePressed(){return pressedMouse;}
ShortcutSettings::Result ShortcutSettings::Poll(bool gameFocused,bool allowActions){
    const auto actions=pending.exchange(0,std::memory_order_acq_rel);
    const auto observed=mouseEvents.exchange(0,std::memory_order_acq_rel);
    pressedMouse=observed&7u;releasedMouse=(observed>>8)&7u;
#if defined(_WIN32) || defined(TAQUITO_TEST_NATIVE_INPUT)
    const int mouseKeys[]={1,2,4};
    for(int i=0;i<3;++i){const bool down=(GetAsyncKeyState(mouseKeys[i])&0x8000)!=0;
        if(gameFocused&&previousMouse[i]&&!down)releasedMouse|=1u<<i;
        if(gameFocused&&!previousMouse[i]&&down)pressedMouse|=1u<<i;
        previousMouse[i]=down;
    }
#endif
    // Nexus/ImGui may know about a click that native polling missed, particularly
    // a short click between render frames or under Wine. Merge both sources.
    for(int i=0;i<3;++i){if(ImGui::IsMouseReleased(i))releasedMouse|=1u<<i;if(ImGui::IsMouseClicked(i))pressedMouse|=1u<<i;}
    // Mumble game focus gates keyboard actions, not actual clicks on addon UI.
    if(!gameFocused)return {};
    if(!allowActions||ImGui::GetIO().WantTextInput)return {};
    return {actions};
}
