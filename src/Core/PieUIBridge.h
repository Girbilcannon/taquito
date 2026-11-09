#pragma once
#include <string>
#include <array>
#include <cstdint>
#include <cstddef>
namespace PieUIBridge {
inline constexpr const char* PingEvent="EV_PIE_UI_PING";
inline constexpr const char* ReadyEvent="EV_PIE_UI_READY";
inline constexpr const char* OpenEvent="EV_PIEUI_OPEN_CHATLINK";
inline constexpr const char* NativeCompassEvent="EV_PIEUI_NATIVE_COMPASS";
inline constexpr const char* NativeCompassRequestEvent="EV_PIEUI_NATIVE_COMPASS_REQUEST";
inline constexpr const char* ThemeEvent="EV_PIEUI_THEME";
inline constexpr const char* ThemeRequestEvent="EV_PIEUI_REQUEST_THEME";
// PieUiAPI.h v1 wire layout: borrowed only during synchronous event delivery.
struct Theme { uint32_t version=1,accent=0,count=0; std::array<uint32_t,96> colors{}; };
static_assert(offsetof(Theme,colors)==12 && sizeof(Theme)==396);
void OnTheme(void*);
Theme GetTheme();
// Queue a manual refresh for the next frame, before any scoped colors are pushed.
void RequestThemeRefresh();
using RaiseEvent=void (*)(const char*,void*);
// Subscribe to ReadyEvent and NativeCompassEvent before probing.
// Poll/requests run on the UI thread; incoming handlers only commit atomic state.
void Initialize(RaiseEvent raise);
void Shutdown();
void OnReady(void*);
// Complete JSON state from Pie UI; copied/parsed during synchronous dispatch.
void OnNativeCompass(void*);
struct MapOwnership { bool minimap=false,worldMap=false; };
MapOwnership GetMapOwnership();
void Probe(bool refreshTheme=false);
void Poll(double frameSeconds,bool refreshTheme=false);
bool Present();
// A single complete base64 type-0x04 map link; whitespace outside the link is allowed.
// This verifies format, not whether a particular map point exists or is unlocked.
std::string MapLink(const std::string& text);
bool CanOpenMap(const std::string& text);
// Rechecks presence immediately; the borrowed string is valid throughout synchronous dispatch.
bool OpenMap(const std::string& text);
}
