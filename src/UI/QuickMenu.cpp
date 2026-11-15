#include "PieUITheme.h"
#include "ControlHUD.h"
#include "../Core/AddonLifetime.h"
#include "QuickMenu.h"
#include "CategoryMenu.h"
#include "OverlayRenderer.h"
#include "MarkerInteractions.h"
#include "PackBrowserWindow.h"
#include "SettingsWindow.h"
#include "ShortcutSettings.h"
#include "PresetsWindow.h"
#include "../Core/Presets.h"
#include "../Core/AppRuntime.h"
#include "../Core/AppSettings.h"
#include "../Core/CategorySearch.h"
#include "../Core/MapNames.h"
#include "../Core/MarkerPack.h"
#include "../Core/OnlinePacks.h"
#include "../Core/PathingState.h"
#include "../Core/PieUIBridge.h"
#include "../Core/AttributeRuntime.h"
#include "../Core/CheckpointRuntime.h"
#include "../third_party/jsonxx.h"
#include "../third_party/good_render/good_render.h"
#include <chrono>
#include <algorithm>
#include "../imgui/imgui.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <cmath>

namespace {
struct RuntimeState {
    Renderer renderer;
    std::vector<TacoPack::Pack> packs;
    std::atomic_bool toggleRequested{false},iconToggleRequested{false};
    bool leftCycleSeen=false,menuAtLeftPress=false;int lastLeftReleaseFrame=-100;
    AppSettings::State lastSharedSettings;
    bool showMenu = false;
    std::thread loader;
    std::mutex loadMutex;
    std::optional<std::vector<TacoPack::Pack>> loadedPacks;
    std::atomic_bool loading{false};
    bool packsReady=false;
    std::vector<InstalledPacks::Record> packCatalog;
    std::wstring lastIdentity;std::string character;
};
AddonLifetime::Storage<RuntimeState> storage;
auto& [renderer,packs,toggleRequested,iconToggleRequested,leftCycleSeen,menuAtLeftPress,lastLeftReleaseFrame,lastSharedSettings,showMenu,loader,loadMutex,loadedPacks,loading,packsReady,packCatalog,lastIdentity,character]=storage.Get();

struct GoodRenderState {
    std::mutex drawMutex;
    good_render::Link link;
    good_render::Slot* slot=nullptr;
    Mumble::Data mumble{};
    Mumble::Identity identity{};
    AppSettings::State settings;
    bool ready=false,hasIdentity=false;
    std::atomic_bool drewWorld{false},lastFrame{false};
    std::atomic_bool drewDepth{false},lastDepth{false};
    std::atomic<uint64_t> worldPasses{0};
};
AddonLifetime::Storage<GoodRenderState> goodStorage;
auto& good=goodStorage.Get();
struct SearchState {
    std::array<char,256> text{};
    bool enabled=false,dirty=true;
    bool filterByMap=false;
    uint32_t map=0;
    uint64_t namesRevision=0;
    CategorySearch::MapMatches mapMatches;
    bool mapIndexDirty=true;
};
AddonLifetime::Storage<SearchState> searchStorage;
auto& search=searchStorage.Get();

void OnGoodRenderMoment(const good_render::Moment* moment) {
    AddonLifetime::Callback callback;
    if(!callback || !moment || moment->moment!=good_render::MOMENT_WORLD_FINISHED ||
       !moment->draw_list || !moment->camera || !ImGui::GetCurrentContext())return;
    // Never block the game's render thread on UI/packs work. A skipped moment
    // is rendered by the normal Nexus pass instead.
    std::unique_lock lock(good.drawMutex,std::try_to_lock);
    if(!lock || !good.ready || !renderer.IsReady())return;
    const auto* live=AppRuntime::GetPlayableMumble();
    if(!live || live->Context.IsMapOpen || live->Context.MapID!=good.mumble.Context.MapID)return;
    const auto& camera=*moment->camera;
    const auto finite=[](float value){return std::isfinite(value);};
    if(!std::all_of(camera.position,camera.position+3,finite) ||
       !std::all_of(camera.front,camera.front+3,finite) ||
       !std::all_of(camera.up,camera.up+3,finite) ||
       !std::all_of(camera.view_projection,camera.view_projection+16,finite) ||
       !finite(camera.fov) || camera.fov<=.1f || camera.fov>=3.04f)return;
    const float frontLength=camera.front[0]*camera.front[0]+camera.front[1]*camera.front[1]+camera.front[2]*camera.front[2];
    if(frontLength<.5f)return;
    Renderer::FrameCamera frame{{camera.position[0],camera.position[1],camera.position[2]},
        {camera.front[0],camera.front[1],camera.front[2]},camera.fov,camera.view_projection};
    frame.up={camera.up[0],camera.up[1],camera.up[2]};
    Renderer::WorldPass pass{moment->draw_list,&frame,true,false,true};
    const auto& s=good.settings;
    if(s.goodRenderDepth && moment->linear_depth)pass.worldQuad=moment->world_quad;
    renderer.Draw(packs,&good.mumble,good.hasIdentity ? &good.identity : nullptr,
        s.renderingEnabled&&s.world,false,false,s.miniIconSize,s.bigIconSize,s.miniTrailSize,s.bigTrailSize,
        s.worldIconSize,s.worldTrailSize,s.playerBubble,s.bubbleRadius,s.bubbleFeather,
        s.mapTrailStyle==AppSettings::MapTrailStyle::BlishHUD,s.worldMarkerDistance,s.worldTrailDistance,&pass);
    good.worldPasses.fetch_add(1,std::memory_order_relaxed);
    good.drewWorld.store(true,std::memory_order_release);
    good.drewDepth.store(pass.worldQuad!=nullptr,std::memory_order_release);
}
void DrainGoodRenderSlot() {
    if(good.slot) {
        auto* slot=good.slot;
        good_render::unsubscribe(*slot);
        // The upstream helper's wait is bounded. Keep our mapping/resources
        // alive until the service has also consumed the submitted draw list.
        // Shutdown never holds drawMutex while waiting.
        while(slot->calls.load(std::memory_order_acquire)!=0)Sleep(1);
        good.slot=nullptr;
    }
}
void StopGoodRender() {
    DrainGoodRenderSlot();
    good_render::close(good.link);
    good.ready=false;good.drewWorld=false;good.lastFrame=false;good.drewDepth=false;good.lastDepth=false;
}

bool UpdateSearchFilter();
void PublishPathingState() {
    UpdateSearchFilter();
    PathingState::Update(packs,AppSettings::Get(),AppRuntime::GetPacksDirectory(),packCatalog,packsReady);
    lastSharedSettings=AppSettings::Get();
}
void ResetSelectionContact() {
    MarkerInteractions::Reset();AttributeRuntime::ResetFocus();CheckpointRuntime::UpdateContact(0,{},false);
}
bool UpdateSearchFilter() {
    const bool enabled=AppSettings::Get().searchEnabled;
    const bool byMap=AppSettings::Get().filterByMap;
    const auto* mumble=byMap ? AppRuntime::GetPlayableMumble() : nullptr;
    const uint32_t map=mumble ? mumble->Context.MapID : 0;
    const auto revision=byMap ? MapNames::Revision() : 0;
    if(!search.dirty && enabled==search.enabled && byMap==search.filterByMap &&
       map==search.map && revision==search.namesRevision)return false;
    if(!enabled)search.text.fill(0);
    if(byMap && map)MapNames::Request(map);
    const auto mapName=byMap && map ? MapNames::Name(map) : std::string{};
    if(search.mapIndexDirty || map!=search.map || byMap!=search.filterByMap) {
        search.mapMatches=CategorySearch::FindMapMatches(packs,byMap ? map : 0);
        search.mapIndexDirty=false;
    }
    const bool changed=CategorySearch::Apply(packs,search.text.data(),enabled,byMap,map,mapName,&search.mapMatches);
    search.enabled=enabled;search.dirty=false;
    search.filterByMap=byMap;search.map=map;search.namesRevision=revision;
    if(changed)ResetSelectionContact();
    return changed;
}
void CategoryChanged();
void Save() {
    // Local preferences do not capture presets, touch categories, or rebuild the bridge snapshot.
    if(packsReady&&Presets::Enabled()!=AppSettings::Get().presetsEnabled){
        if(!Presets::SetEnabled(AppSettings::Get().presetsEnabled,packs))AppSettings::Get().presetsEnabled=false;
        ResetSelectionContact();Presets::Save();CategoryChanged();return;
    }
    AppSettings::Save(AppRuntime::GetAddonDirectory(),{});
    const auto&s=AppSettings::Get();const auto&old=lastSharedSettings;
    if(s.renderingEnabled!=old.renderingEnabled||s.mini!=old.mini||s.big!=old.big||s.miniIconSize!=old.miniIconSize||s.miniTrailSize!=old.miniTrailSize||
       s.bigIconSize!=old.bigIconSize||s.bigTrailSize!=old.bigTrailSize||s.searchEnabled!=old.searchEnabled||s.filterByMap!=old.filterByMap)PublishPathingState();
}
void PresetChanged(){ResetSelectionContact();CategoryChanged();}
// Only the loader thread parses archives; ReceivePacks swaps them onto the render thread.
void Refresh() {
    if (loader.joinable()) loader.join();
    renderer.RequestTextureClear();
    { std::lock_guard lock(loadMutex); loadedPacks.reset(); }
    loading.store(true, std::memory_order_release);
    packsReady=false;
    PublishPathingState();
    auto directory = AppRuntime::GetPacksDirectory();
    loader = std::thread([directory] {
        auto result = TacoPack::LoadFolder(directory);
        { std::lock_guard lock(loadMutex); loadedPacks.emplace(std::move(result)); }
        loading.store(false, std::memory_order_release);
    });
}
void ReceivePacks() {
    if (loading.load(std::memory_order_acquire)) return;
    std::optional<std::vector<TacoPack::Pack>> ready;
    { std::lock_guard lock(loadMutex); ready.swap(loadedPacks); }
    if (!ready) return;
    if (loader.joinable()) loader.join();
    MarkerInteractions::Reset();
    AttributeRuntime::ResetFocus();
    CheckpointRuntime::PrepareReload();
    packs = std::move(*ready);
    search.dirty=true;
    search.mapIndexDirty=true;
    AppSettings::Load(AppRuntime::GetAddonDirectory(), packs);
    if(!Presets::SetEnabled(AppSettings::Get().presetsEnabled,packs))AppSettings::Get().presetsEnabled=false;
    Presets::Apply(packs);Presets::Save();
    CheckpointRuntime::Load(packs);
    packCatalog=InstalledPacks::Load(AppRuntime::GetAddonDirectory()/"installed-packs.json");
    packsReady=true;
    PublishPathingState();
    if (auto* api = AppRuntime::GetApi()) {
        const auto message = "Loaded " + std::to_string(packs.size()) + " marker pack(s).";
        api->Log(LOGL_INFO, "Taquito", message.c_str());
    }
}
// Show XML categories directly; Save stores visibility for the next launch.
void CategoryChanged() {
    if(packsReady&&Presets::Enabled()){Presets::Capture(packs);Presets::Save();}
    AppSettings::Save(AppRuntime::GetAddonDirectory(),packs);PublishPathingState();
}
void DrawMenu() {
    bool justOpened=false;
    const bool normalToggle=toggleRequested.exchange(false,std::memory_order_acq_rel);
    const bool iconToggle=iconToggleRequested.exchange(false,std::memory_order_acq_rel);
    if (normalToggle||iconToggle) {
        const bool sameMouseCycle=leftCycleSeen&&((ShortcutSettings::MousePressed()&1u)||ImGui::GetFrameCount()-lastLeftReleaseFrame<=2);
        showMenu=iconToggle&&sameMouseCycle ? !menuAtLeftPress : !showMenu;
        if(iconToggle)leftCycleSeen=false;
        justOpened=showMenu;
    }
    if (!showMenu) return;
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_FirstUseEver);
    const auto screen=ImGui::GetIO().DisplaySize;
    const ImVec2 limit{std::max(1.0f,screen.x-16),std::max(1.0f,screen.y-16)};
    ImGui::SetNextWindowSizeConstraints({std::min(250.0f,limit.x),0},limit);
    if (ImGui::Begin("Taquito##menu", &showMenu,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(AppSettings::Get().searchEnabled) {
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputTextWithHint("##TaquitoCategorySearch","Search...",search.text.data(),search.text.size())) {
                search.dirty=true;
                if(UpdateSearchFilter())PublishPathingState();
            }
        }
        if (loading.load()) ImGui::TextUnformatted("Reading TacoPacks...");
        else if (packs.empty()) ImGui::TextUnformatted("Place .taco or .zip files in TacoPacks beside the DLL.");
        if(!AppSettings::Get().renderingEnabled){
            ImGui::TextColored({1,.2f,.2f,1},"Rendering Disabled");
            if(ImGui::BeginPopupContextItem("##RenderingState")){
                if(ImGui::MenuItem("Enable Rendering")){AppSettings::Get().renderingEnabled=true;Save();}
                ImGui::EndPopup();
            }
        }
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*(AppSettings::Get().renderingEnabled?1.0f:.35f));
        for (auto& pack : packs) {
            const bool tree=AppSettings::Get().nestingStyle==AppSettings::NestingStyle::Taquito;
            if(tree) ImGui::PushID(pack.path.string().c_str());
            for (auto& category : pack.root.children)
                CategoryMenu::Draw(*category, AppSettings::Get().nestingStyle, CategoryChanged,MarkerInteractions::CopyCategory);
            if(tree) ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::Separator();
        if (ImGui::MenuItem("Download Marker Packs...")) PackBrowserWindow::Open();
        if (ImGui::MenuItem("Check for pack updates")) OnlinePacks::CheckForUpdates(AppRuntime::GetPacksDirectory());
        const auto updateStatus=OnlinePacks::GetUpdateStatus();
        if(!updateStatus.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s",updateStatus.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::MenuItem("Reload packs")) Refresh();
        ImGui::Separator();
        if(AppSettings::Get().presetsEnabled&&Presets::Enabled())if(ImGui::MenuItem("Presets"))PresetsWindow::Open();
        auto* api=AppRuntime::GetApi();
        auto* icon=api && api->Textures_Get ? api->Textures_Get("ICON_OPTIONS") : nullptr;
        if(icon && icon->Resource) {
            ImGui::PushID("TaquitoSettingsIcon");
            if(ImGui::ImageButton(icon->Resource,{18,18},{0,0},{1,1},0)) SettingsWindow::Open();
            ImGui::PopID();
            ImGui::SameLine();
        }
        if(ImGui::MenuItem("Settings")) SettingsWindow::Open();
    }
    if(AppSettings::Get().quickCloseMenu&&!justOpened&&
       ShortcutSettings::MouseReleased()&&
       !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows|ImGuiHoveredFlags_AllowWhenBlockedByPopup|ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))showMenu=false;
    const auto size=ImGui::GetWindowSize(),pos=ImGui::GetWindowPos();
    ImGui::SetWindowPos({std::clamp(pos.x,0.0f,std::max(0.0f,screen.x-size.x)),std::clamp(pos.y,0.0f,std::max(0.0f,screen.y-size.y))});
    ImGui::End();
}
}

void QuickMenu::Initialize() {
    search.text.fill(0);search.dirty=true;search.enabled=false;
    search.filterByMap=false;search.map=0;search.namesRevision=0;
    search.mapMatches.clear();search.mapIndexDirty=true;
    AppSettings::Load(AppRuntime::GetAddonDirectory(),packs);
    MapNames::Initialize(AppRuntime::GetAddonDirectory());
    AppSettings::Get().renderingEnabled=true;
    Presets::Initialize(AppRuntime::GetAddonDirectory(),AppSettings::Get().presetsEnabled);
    ShortcutSettings::Reset();
    auto* api=AppRuntime::GetApi();
    PathingState::Initialize(api ? api->Events_Raise : nullptr);
    AttributeRuntime::Initialize(AppRuntime::GetAddonDirectory());
    CheckpointRuntime::Clear();
    renderer.Initialize(api); Refresh();
    good.ready=false;good.drewWorld=false;good.lastFrame=false;good.worldPasses=0;
    good.link=good_render::open();
    if(good.link.block)good.slot=good_render::subscribe(*good.link.block,OnGoodRenderMoment,0);
}
void QuickMenu::Shutdown() {
    StopGoodRender();
    MapNames::Shutdown();
    // Nexus callbacks have been removed and drained before any state is freed.
    OnlinePacks::Stop();
    if(loader.joinable())loader.join();
    if(packsReady&&Presets::Enabled()){Presets::Capture(packs);Presets::Save();}
    AppSettings::Save(AppRuntime::GetAddonDirectory(),packs);
    AppSettings::Shutdown();
    // Do not use CategoryChanged here: unloading must not publish an active
    // snapshot again or change presets after the final save has been queued.
    MarkerInteractions::Reset();CheckpointRuntime::Clear();AttributeRuntime::ResetFocus();
    renderer.Shutdown();packs.clear();packCatalog.clear();packsReady=false;loading=false;
    showMenu=false;toggleRequested=false;iconToggleRequested=false;
    lastIdentity.clear();character.clear();
    PackBrowserWindow::Close();SettingsWindow::Close();PresetsWindow::Close();ControlHUD::Close();
    Presets::Clear();ShortcutSettings::Reset();
    {std::lock_guard lock(loadMutex);loadedPacks.reset();}
}
void QuickMenu::RequestToggle() { toggleRequested.store(true, std::memory_order_release); }
void QuickMenu::RequestIconToggle(){iconToggleRequested.store(true,std::memory_order_release);}
void QuickMenu::Render() {
    std::lock_guard frameLock(good.drawMutex);
    PieUIBridge::Poll(ImGui::GetTime());
    PieUITheme::Scope theme(AppSettings::Get().pieUIThemes);
    if(OnlinePacks::ConsumeReloadRequest()) Refresh();
    ReceivePacks();
    auto& settings = AppSettings::Get();
    auto* rawMumble=AppRuntime::GetMumble();
    const bool focused=rawMumble&&rawMumble->UITick&&rawMumble->Context.IsGameFocused;
    const auto shortcut=ShortcutSettings::Poll(focused,focused&&!rawMumble->Context.IsTextboxFocused);
    if(ShortcutSettings::MousePressed()&1u){leftCycleSeen=true;menuAtLeftPress=showMenu;}
    if(ShortcutSettings::MouseReleased()&1u)lastLeftReleaseFrame=ImGui::GetFrameCount();
    bool shortcutsChanged=false;
    if(shortcut.actions&1u){settings.renderingEnabled=!settings.renderingEnabled;shortcutsChanged=true;}
    if(shortcut.actions&ShortcutSettings::ToggleWorld){settings.world=!settings.world;shortcutsChanged=true;}
    if(shortcut.actions&ShortcutSettings::ToggleFullMap){settings.big=!settings.big;shortcutsChanged=true;}
    if(shortcut.actions&ShortcutSettings::ToggleMinimap){settings.mini=!settings.mini;shortcutsChanged=true;}
    if(shortcut.actions&ShortcutSettings::ToggleControlHUD)if(settings.controlHudEnabled)ControlHUD::Toggle();
    if(shortcut.actions&8u)QuickMenu::RequestToggle();
    if(packsReady&&((shortcut.actions&2u&&Presets::Step(1,packs))||(shortcut.actions&4u&&Presets::Step(-1,packs)))){PresetChanged();}
    if(shortcutsChanged)Save();
    auto* playableMumble=AppRuntime::GetPlayableMumble();
    if(UpdateSearchFilter())PublishPathingState();

    if(playableMumble) {
        const auto* raw=playableMumble->Identity;size_t length=0;while(length<256 && raw[length])++length;
        std::wstring identity(raw,length);
        if(identity!=lastIdentity) {
            lastIdentity=identity;character.clear();
            const int bytes=WideCharToMultiByte(CP_UTF8,0,identity.data(),static_cast<int>(identity.size()),nullptr,0,nullptr,nullptr);
            std::string json(static_cast<size_t>(std::max(0,bytes)),0);
            if(bytes>0)WideCharToMultiByte(CP_UTF8,0,identity.data(),static_cast<int>(identity.size()),json.data(),bytes,nullptr,nullptr);
            jsonxx::Object parsed;if(parsed.parse(json)&&parsed.has<jsonxx::String>("name"))character=parsed.get<jsonxx::String>("name");
        }
    }
    const auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    AttributeRuntime::Tick(playableMumble ? playableMumble->Context.MapID : 0,
        playableMumble ? playableMumble->Context.ShardID : 0,playableMumble ? playableMumble->Context.InstanceID : 0,
        playableMumble ? character : std::string{},playableMumble ? static_cast<int>(playableMumble->Context.MountIndex) : -1,
        now,ImGui::GetTime());
    const TacoPack::Vec3 player=playableMumble ? TacoPack::Vec3{playableMumble->AvatarPosition.X,playableMumble->AvatarPosition.Y,playableMumble->AvatarPosition.Z} : TacoPack::Vec3{};
    AttributeRuntime::UpdateFocus(packs,player,playableMumble && settings.renderingEnabled && settings.world && !playableMumble->Context.IsMapOpen);
    MarkerInteractions::Render(packs,playableMumble,settings.renderingEnabled&&settings.world,Save,CategoryChanged);
    const bool checkpointContact=playableMumble && settings.renderingEnabled && settings.world && playableMumble->Context.IsGameFocused &&
        !playableMumble->Context.IsMapOpen && !playableMumble->Context.IsTextboxFocused && !ImGui::GetIO().WantTextInput;
    CheckpointRuntime::UpdateContact(playableMumble ? playableMumble->Context.MapID : 0,player,checkpointContact);
    const auto mapOwner=PieUIBridge::GetMapOwnership();
    const bool drawnInMoment=good.drewWorld.exchange(false,std::memory_order_acq_rel);
    good.lastFrame.store(drawnInMoment,std::memory_order_release);
    good.lastDepth.store(good.drewDepth.exchange(false,std::memory_order_acq_rel),std::memory_order_release);
    // world_quad textures must live until the service's subscription drains.
    const bool resetSubscription=renderer.TextureResetPending(playableMumble ? playableMumble->Context.MapID : 0);
    if(resetSubscription)DrainGoodRenderSlot();
    Renderer::WorldPass nexusPass;
    nexusPass.geometry=!drawnInMoment;
    renderer.Draw(packs, playableMumble, AppRuntime::GetMumbleIdentity(),
                  settings.renderingEnabled&&settings.world, settings.renderingEnabled&&settings.mini&&!mapOwner.minimap,
                  settings.renderingEnabled&&settings.big&&!mapOwner.worldMap, settings.miniIconSize, settings.bigIconSize,
                  settings.miniTrailSize, settings.bigTrailSize,
                  settings.worldIconSize, settings.worldTrailSize,
                  settings.playerBubble, settings.bubbleRadius, settings.bubbleFeather,
                  settings.mapTrailStyle==AppSettings::MapTrailStyle::BlishHUD,
                  settings.worldMarkerDistance,settings.worldTrailDistance,&nexusPass);
    if(resetSubscription && good.link.block)good.slot=good_render::subscribe(*good.link.block,OnGoodRenderMoment,0);
    DrawMenu();
    PackBrowserWindow::Render();
    if(!settings.presetsEnabled)PresetsWindow::Close();
    PresetsWindow::Render(packs,PresetChanged);
    SettingsWindow::Render(packs,renderer,loading.load(),Refresh,Save);
    ControlHUD::Render(Save);
    // Prepare only on Nexus's normal UI pass. The earlier game moment uses
    // this stable snapshot plus its own frame-matched camera; it never runs
    // interactions, menus, API work, pack adoption or settings saves.
    good.ready=packsReady && playableMumble && !playableMumble->Context.IsMapOpen &&
        settings.renderingEnabled && settings.world;
    if(good.ready) {
        good.mumble=*playableMumble;good.settings=settings;
        const auto* identity=AppRuntime::GetMumbleIdentity();
        good.hasIdentity=identity!=nullptr;
        if(identity)good.identity=*identity;
    }
}
bool QuickMenu::GoodRenderAvailable(){return good.slot && good.link.block && good_render::served(*good.link.block);}
bool QuickMenu::GoodRenderUsedLastFrame(){return good.lastFrame.load(std::memory_order_acquire);}
uint64_t QuickMenu::GoodRenderWorldPasses(){return good.worldPasses.load(std::memory_order_relaxed);}
bool QuickMenu::GoodRenderDepthLastFrame(){return good.lastDepth.load(std::memory_order_acquire);}
