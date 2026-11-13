#include "PieUIBridge.h"
#include "AddonLifetime.h"
#include "../third_party/jsonxx.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <algorithm>
#include <cstring>

namespace {
std::atomic<PieUIBridge::RaiseEvent> sender{nullptr};
std::atomic_bool present{false};
std::atomic_uint mapOwnership{0}; // bit 0: hidden native compass; bit 1: Pie UI world map.
std::atomic_bool themeRefreshRequested{false};
double lastProbe=-1;
struct ThemeState { std::mutex mutex; PieUIBridge::Theme palette; };
AddonLifetime::Storage<ThemeState> themeStorage;
void ClearTheme(){auto& s=themeStorage.Get();std::lock_guard lock(s.mutex);s.palette={};}
int Base64(char c) {
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
}
void PieUIBridge::Initialize(RaiseEvent raise){sender.store(raise);present.store(false);mapOwnership.store(0);themeRefreshRequested.store(false);lastProbe=-1;ClearTheme();}
void PieUIBridge::Shutdown(){sender.store(nullptr);present.store(false);mapOwnership.store(0);themeRefreshRequested.store(false);lastProbe=-1;ClearTheme();}
void PieUIBridge::RequestThemeRefresh(){if(Present())themeRefreshRequested.store(true);}
void PieUIBridge::OnTheme(void* value){
    if(!sender.load()||!value)return;
    // Read and validate the header before touching the fixed-capacity color array.
    uint32_t header[3];std::memcpy(header,value,sizeof(header));
    if(header[0]!=1 || !header[2] || header[2]>96)return;
    Theme copy;copy.accent=header[1];copy.count=header[2];
    std::memcpy(copy.colors.data(),static_cast<const unsigned char*>(value)+sizeof(header),copy.count*sizeof(uint32_t));
    auto& s=themeStorage.Get();std::lock_guard lock(s.mutex);s.palette=copy;
}
PieUIBridge::Theme PieUIBridge::GetTheme(){
    auto& s=themeStorage.Get();std::lock_guard lock(s.mutex);return s.palette;
}
void PieUIBridge::OnReady(void*){if(sender.load())present.store(true);}
void PieUIBridge::OnNativeCompass(void* value){
    if(!sender.load()||!value)return;
    const auto* text=static_cast<const char*>(value);
    size_t size=0;while(size<4096&&text[size])++size;
    if(!size||size==4096)return;
    jsonxx::Object state;
    if(!state.parse(std::string(text,size))||!state.has<jsonxx::Number>("schema")||state.get<jsonxx::Number>("schema")!=1||
       !state.has<jsonxx::String>("source")||state.get<jsonxx::String>("source")!="Pie UI"||!state.has<jsonxx::Boolean>("active"))return;
    if(!state.get<jsonxx::Boolean>("active")){mapOwnership.store(0);present.store(false);ClearTheme();return;}
    if(!state.has<jsonxx::Boolean>("nativeCompassHidden"))return;
    unsigned owned=state.get<jsonxx::Boolean>("nativeCompassHidden")?1u:0u;
    // Older compass-only providers retain local full-map rendering. Unknown
    // or missing selections likewise default to native on each complete state.
    if(state.has<jsonxx::String>("worldMap")&&state.get<jsonxx::String>("worldMap")=="pieui")owned|=2u;
    mapOwnership.store(owned);present.store(true);
}
PieUIBridge::MapOwnership PieUIBridge::GetMapOwnership(){
    const auto owned=mapOwnership.load();return {(owned&1u)!=0,(owned&2u)!=0};
}
void PieUIBridge::Probe(bool refreshTheme){
    const bool wasPresent=present.load();
    present.store(false);
    // Both documented requests reply synchronously. Clear stale ownership first
    // so a missing/unloaded provider cannot leave native rendering suppressed.
    mapOwnership.store(0);
    if(auto send=sender.load()){
        send(PingEvent,nullptr);
        send(NativeCompassRequestEvent,nullptr);
        // Refresh an existing cache when requested, before Taquito pushes any colors.
        if(present.load()&&(refreshTheme||!wasPresent))send(ThemeRequestEvent,nullptr);
    }
    if(!present.load())ClearTheme();
}
void PieUIBridge::Poll(double now){
    if(!std::isfinite(now))return;
    const bool manual=themeRefreshRequested.exchange(false);
    if(lastProbe<0||now<lastProbe||now-lastProbe>=1){
        lastProbe=now;
        // Only discovery/manual requests pull a theme. Later changes normally
        // arrive as broadcasts; no automatic palette refresh runs here.
        Probe(manual);
    }
    else if(manual&&Present())if(auto send=sender.load())send(ThemeRequestEvent,nullptr);
}
bool PieUIBridge::Present(){return sender.load()!=nullptr&&present.load();}
std::string PieUIBridge::MapLink(const std::string&text){
    const auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    if(first==std::string::npos||last-first+1!=11)return {};
    const auto link=text.substr(first,11);
    if(link[0]!='['||link[1]!='&'||link[9]!='='||link[10]!=']')return {};
    uint8_t bytes[5]{};unsigned accumulator=0,bits=0,count=0;
    for(size_t i=2;i<9;++i){
        const int value=Base64(link[i]);if(value<0)return {};
        accumulator=(accumulator<<6)|static_cast<unsigned>(value);bits+=6;
        if(bits>=8){bits-=8;bytes[count++]=static_cast<uint8_t>((accumulator>>bits)&255);}
    }
    // Canonical padding, exact 5-byte map format, nonzero map-point ID.
    if(count!=5||(accumulator&3u)!=0||bytes[0]!=4||!(bytes[1]|bytes[2]|bytes[3]|bytes[4]))return {};
    return link;
}
bool PieUIBridge::CanOpenMap(const std::string&text){return Present()&&!MapLink(text).empty();}
bool PieUIBridge::OpenMap(const std::string&text){
    const auto link=MapLink(text);if(link.empty()||!Present())return false;
    // PieUI's documented PING handler replies with READY synchronously. Never hold a lock across events.
    Probe();if(!Present())return false;
    if(auto send=sender.load()){send(OpenEvent,const_cast<char*>(link.c_str()));return true;}
    return false;
}
