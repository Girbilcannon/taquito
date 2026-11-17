// Nexus registration and lifecycle. Runtime, pack parsing, settings and UI live in their respective folders.
#include <Windows.h>
#include <cstddef>
#include "nexus/Nexus.h"
#include "imgui/imgui.h"
#include "Core/AppRuntime.h"
#include "Core/AddonLifetime.h"
#include "Core/AppSettings.h"
#include "Core/Presets.h"
#include "Core/PathingState.h"
#include "Core/PieUIBridge.h"
#include "TaquitoAPI.h"
#include "UI/QuickMenu.h"
#include "UI/PresetsWindow.h"
#include "UI/ControlHUD.h"
#include "UI/ShortcutSettings.h"
#include "UI/MarkerInteractions.h"
#include "resource.h"

namespace {
constexpr const char* Name = "Taquito";
constexpr const char* QuickID = "TAQUITO_QUICKACCESS_78FD302C";
constexpr const char* PresetsContextID = "TAQUITO_PRESETS_CONTEXT_78FD302C";
constexpr const char* IconID = "TAQUITO_ICON_78FD302C_V2";
constexpr const char* BindID = "KB_TAQUITO_MENU_78FD302C_V2";
constexpr const char* RenderBindID = "KB_TAQUITO_RENDERING_78FD302C";
constexpr const char* NextBindID = "KB_TAQUITO_NEXT_PRESET_78FD302C";
constexpr const char* PreviousBindID = "KB_TAQUITO_PREVIOUS_PRESET_78FD302C";
constexpr const char* IconBindID = "KB_TAQUITO_ICON_ACTION_78FD302C";
constexpr const char* InteractBindID = "KB_TAQUITO_MARKER_INTERACT_78FD302C";
constexpr unsigned IconResource = IDR_TAQUITO_QUICKACCESS_ICON;
constexpr const char* WorldBindID="KB_TAQUITO_WORLD_78FD302C";
constexpr const char* FullBindID="KB_TAQUITO_FULL_MAP_78FD302C";
constexpr const char* MiniBindID="KB_TAQUITO_MINIMAP_78FD302C";
constexpr const char* HudBindID="KB_TAQUITO_CONTROL_HUD_78FD302C";
bool contextRegistered=false;
AddonDefinition_t definition{};
AddonAPI_t* api = nullptr;
HMODULE module = nullptr;

void OnToggle(const char*, bool released) { AddonLifetime::Callback callback;if(!callback)return; if(!released)QuickMenu::RequestToggle(); }
void OnRendering(const char*,bool released){ AddonLifetime::Callback callback;if(!callback)return;ShortcutSettings::Request(ShortcutSettings::ToggleRendering,released);}
void OnNextPreset(const char*,bool released){ AddonLifetime::Callback callback;if(!callback)return;ShortcutSettings::Request(ShortcutSettings::NextPreset,released);}
void OnPreviousPreset(const char*,bool released){ AddonLifetime::Callback callback;if(!callback)return;ShortcutSettings::Request(ShortcutSettings::PreviousPreset,released);}
void OnWorld(const char*,bool released){AddonLifetime::Callback callback;if(callback)ShortcutSettings::Request(ShortcutSettings::ToggleWorld,released);}
void OnFull(const char*,bool released){AddonLifetime::Callback callback;if(callback)ShortcutSettings::Request(ShortcutSettings::ToggleFullMap,released);}
void OnMini(const char*,bool released){AddonLifetime::Callback callback;if(callback)ShortcutSettings::Request(ShortcutSettings::ToggleMinimap,released);}
void OnHUD(const char*,bool released){AddonLifetime::Callback callback;if(callback)ShortcutSettings::Request(ShortcutSettings::ToggleControlHUD,released);}
void OnIconToggle(const char*,bool);
void OnMarkerInteract(const char*,bool);
Keybind_t LegacyDefault(const AppSettings::Shortcut&old){
    Keybind_t key{};
    if(old.key>0&&old.key<256){key.Key=static_cast<uint16_t>(MapVirtualKeyA(static_cast<UINT>(old.key),MAPVK_VK_TO_VSC_EX));key.Alt=old.alt;key.Ctrl=old.ctrl;key.Shift=old.shift;}
    return key;
}
void RegisterShortcuts(){
    // Defaults seed only previously unknown Nexus IDs; existing Nexus choices always win.
    api->InputBinds_RegisterWithString(BindID,OnToggle,"ALT+SHIFT+T");
    api->InputBinds_RegisterWithStruct(RenderBindID,OnRendering,LegacyDefault(AppSettings::Get().shortcuts[0]));
    api->InputBinds_RegisterWithStruct(NextBindID,OnNextPreset,LegacyDefault(AppSettings::Get().shortcuts[1]));
    api->InputBinds_RegisterWithStruct(PreviousBindID,OnPreviousPreset,LegacyDefault(AppSettings::Get().shortcuts[2]));
    api->InputBinds_RegisterWithStruct(IconBindID,OnIconToggle,{});
    api->InputBinds_RegisterWithString(InteractBindID,OnMarkerInteract,"ALT+F");
    api->InputBinds_RegisterWithStruct(WorldBindID,OnWorld,{});
    api->InputBinds_RegisterWithStruct(FullBindID,OnFull,{});
    api->InputBinds_RegisterWithStruct(MiniBindID,OnMini,{});
    api->InputBinds_RegisterWithStruct(HudBindID,OnHUD,{});
    api->Localization_Set(WorldBindID,"en","Toggle World Markers and Trails");
    api->Localization_Set(FullBindID,"en","Toggle Full Map Markers and Trails");
    api->Localization_Set(MiniBindID,"en","Toggle Minimap Markers and Trails");
    api->Localization_Set(HudBindID,"en","Open/Close Control HUD");
    api->Localization_Set(BindID,"en","Open/Close Quick Menu");
    api->Localization_Set(RenderBindID,"en","Disable Taquito Rendering");
    api->Localization_Set(NextBindID,"en","Next Preset");
    api->Localization_Set(PreviousBindID,"en","Previous Preset");
    api->Localization_Set(InteractBindID,"en","Interact with Marker");
    api->Localization_Set(IconBindID,"en","Quick-access Icon");
}
void OnIconToggle(const char*,bool released){ AddonLifetime::Callback callback;if(!callback)return;if(!released)QuickMenu::RequestIconToggle();}
void OnMarkerInteract(const char*, bool released) { AddonLifetime::Callback callback;if(!callback)return; if(!released) MarkerInteractions::RequestInteract(); }
void OnMumbleIdentityUpdated(void* value) { AddonLifetime::Callback callback;if(!callback)return; AppRuntime::SetMumbleIdentity(static_cast<Mumble::Identity*>(value)); }
void OnPathingRequest(void*) { AddonLifetime::Callback callback;if(!callback)return; PathingState::Reply(); }
void OnPieUIReady(void* value) { AddonLifetime::Callback callback;if(!callback)return;PieUIBridge::OnReady(value); }
void OnPieUICompass(void* value) { AddonLifetime::Callback callback;if(!callback)return;PieUIBridge::OnNativeCompass(value); }
void OnPieUITheme(void* value) { AddonLifetime::Callback callback;if(!callback)return;PieUIBridge::OnTheme(value); }
void SyncQuickAccessContext();
void OnRender() { AddonLifetime::Callback callback;if(!callback)return; QuickMenu::Render();SyncQuickAccessContext(); }
UINT OnMouseMessage(HWND,UINT message,WPARAM,LPARAM) {
    AddonLifetime::Callback callback;
    if(callback)switch(message) {
        case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:ShortcutSettings::MouseButton(0,true);break;
        case WM_LBUTTONUP:ShortcutSettings::MouseButton(0,false);break;
        case WM_RBUTTONDOWN:case WM_RBUTTONDBLCLK:ShortcutSettings::MouseButton(1,true);break;
        case WM_RBUTTONUP:ShortcutSettings::MouseButton(1,false);break;
        case WM_MBUTTONDOWN:case WM_MBUTTONDBLCLK:ShortcutSettings::MouseButton(2,true);break;
        case WM_MBUTTONUP:ShortcutSettings::MouseButton(2,false);break;
    }
    return message; // Observe only: let Nexus, other addons and the game handle it.
}
void OnQuickAccessMenu() { AddonLifetime::Callback callback;if(!callback)return;
    if(AppSettings::Get().controlHudEnabled)
        if(ImGui::MenuItem("Control HUD",nullptr,ControlHUD::IsOpen()))ControlHUD::Toggle();
    if(AppSettings::Get().presetsEnabled&&Presets::Enabled())
        if(ImGui::MenuItem("Open Presets"))PresetsWindow::Open();
}
void SyncQuickAccessContext(){
    const bool available=AppSettings::Get().controlHudEnabled||(AppSettings::Get().presetsEnabled&&Presets::Enabled());
    if(available==contextRegistered)return;
    if(available)api->QuickAccess_AddContextMenu(PresetsContextID,QuickID,OnQuickAccessMenu);
    else api->QuickAccess_RemoveContextMenu(PresetsContextID);
    contextRegistered=available;
}
// Keep registrations below paired with OnUnload before changing the addon lifecycle.
void OnLoad(AddonAPI_t* value) {
    api = value;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void* (*)(std::size_t, void*)>(api->ImguiMalloc),
        reinterpret_cast<void (*)(void*, void*)>(api->ImguiFree));
    AppRuntime::Initialize(api, module);
    AddonLifetime::StartCallbacks();
    PieUIBridge::Initialize(api->Events_Raise);
    api->Events_Subscribe(PieUIBridge::ReadyEvent,OnPieUIReady);
    api->Events_Subscribe(PieUIBridge::NativeCompassEvent,OnPieUICompass);
    api->Events_Subscribe(PieUIBridge::ThemeEvent,OnPieUITheme);
    PieUIBridge::Probe();
    QuickMenu::Initialize();
    api->Events_Subscribe(EV_TAQUITO_PATHING_REQUEST,OnPathingRequest);
    api->Textures_GetOrCreateFromResource(IconID, IconResource, module);
    RegisterShortcuts();
    api->Events_Subscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    api->QuickAccess_Add(QuickID, IconID, IconID, IconBindID, Name);
    SyncQuickAccessContext();
    api->GUI_Register(RT_Render, OnRender);
    api->WndProc_Register(OnMouseMessage);
    api->Log(LOGL_INFO, Name, "Taquito " TAQUITO_VERSION " loaded.");
}
void OnUnload() {
    if (!api || AddonLifetime::ProcessTerminating()) return;
    AddonLifetime::StopCallbacks();
    api->GUI_Deregister(OnRender);
    api->WndProc_Deregister(OnMouseMessage);
    api->InputBinds_Deregister(BindID);
    api->InputBinds_Deregister(RenderBindID);
    api->InputBinds_Deregister(NextBindID);
    api->InputBinds_Deregister(PreviousBindID);
    api->InputBinds_Deregister(IconBindID);
    api->InputBinds_Deregister(InteractBindID);
    api->InputBinds_Deregister(WorldBindID);
    api->InputBinds_Deregister(FullBindID);
    api->InputBinds_Deregister(MiniBindID);
    api->InputBinds_Deregister(HudBindID);
    api->Events_Unsubscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    api->Events_Unsubscribe(PieUIBridge::ReadyEvent,OnPieUIReady);
    api->Events_Unsubscribe(PieUIBridge::NativeCompassEvent,OnPieUICompass);
    api->Events_Unsubscribe(PieUIBridge::ThemeEvent,OnPieUITheme);
    api->Events_Unsubscribe(EV_TAQUITO_PATHING_REQUEST,OnPathingRequest);
    AddonLifetime::WaitForCallbacks();
    if(contextRegistered)api->QuickAccess_RemoveContextMenu(PresetsContextID);
    contextRegistered=false;
    api->QuickAccess_Remove(QuickID);

    PieUIBridge::Shutdown();
    PathingState::Shutdown();
    QuickMenu::Shutdown();
    AppRuntime::Shutdown();
    api->Log(LOGL_INFO,Name,"Taquito shutdown complete; workers stopped and pending saves flushed.");
    api = nullptr;
}
}
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) { module = h; DisableThreadLibraryCalls(h); }
    // Only an atomic flag here: no joins, locks, saves, events or COM teardown.
    if(reason==DLL_PROCESS_DETACH && reserved)AddonLifetime::BeginProcessTermination();
    return TRUE;
}
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    definition.Signature = 0x78FD302C; // Preserve this unique Nexus identity across releases.
    definition.APIVersion = NEXUS_API_VERSION;
    definition.Name = Name;
    // Bump this version for releases; never regenerate the signature for an update.
    definition.Version.Major = 1;
    definition.Version.Minor = 0;
    definition.Version.Build = 2;
    definition.Version.Revision = 2;
    definition.Author = "Girbilcannon.8259";
    definition.Description = "Lightweight TacO marker and trail pack viewer.";
    definition.Load = OnLoad;
    definition.Unload = OnUnload;
    definition.Flags = AF_None;
    definition.Provider = UP_GitHub;
    definition.UpdateLink = "https://github.com/Girbilcannon/taquito";
    return &definition;
}
