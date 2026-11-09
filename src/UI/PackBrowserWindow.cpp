#include "../Core/AddonLifetime.h"
#include "PackBrowserWindow.h"
#include "../Core/AppRuntime.h"
#include "../Core/OnlinePacks.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace {
struct RuntimeState {
    bool shown=false;
    std::string selected;
    char search[128]{};
    std::string removeId,removeName,removeFile;
};
AddonLifetime::Storage<RuntimeState> storage;
auto& [shown,selected,search,removeId,removeName,removeFile]=storage.Get();
void OpenWebPage(const std::string& url) {
    if(!url.starts_with("https://")) return;
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),static_cast<int>(url.size()),nullptr,0);
    if(n<=0) return;
    std::wstring wide(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),static_cast<int>(url.size()),wide.data(),n);
    ShellExecuteW(nullptr,L"open",wide.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
bool Matches(const std::string& value, const std::string& term) {
    std::string lower=value;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return lower.find(term)!=std::string::npos;
}
void Gap() { ImGui::Dummy({0,ImGui::GetFontSize()*0.45f}); }
void Muted(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s",text.c_str());
    ImGui::PopStyleColor();
}
bool Button(const char* label,bool enabled=true) {
    if(!enabled) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*0.5f);
    }
    const bool clicked=ImGui::Button(label);
    if(!enabled) { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
    return clicked;
}
// Keep complete buttons on the next row when the pane is too narrow.
void NextAction(const char* label) {
    const float width=ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;
    const float right=ImGui::GetWindowContentRegionMax().x;
    if(ImGui::GetItemRectMax().x-ImGui::GetWindowPos().x+ImGui::GetStyle().ItemSpacing.x+width<=right)
        ImGui::SameLine();
}
void DesignButton() {
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.10f,0.34f,0.62f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.15f,0.44f,0.77f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0.08f,0.28f,0.54f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,1,1,1));
    if(ImGui::Button("Design Your Own Packs"))
        OpenWebPage("https://github.com/Girbilcannon/TacoStuffer/releases/latest");
    ImGui::PopStyleColor(4);
}
}
void PackBrowserWindow::Open() {
    shown=true;
    OnlinePacks::Start(AppRuntime::GetPacksDirectory());
}
void PackBrowserWindow::Close() { shown=false; selected.clear(); }
void PackBrowserWindow::Render() {
    if(!shown) return;
    OnlinePacks::RefreshInstalled();
    const float font=ImGui::GetFontSize();
    const auto& style=ImGui::GetStyle();
    // Add readable minimum padding without changing Nexus colors, borders or rounding.
    const ImVec2 padding{std::max(style.WindowPadding.x,font*0.85f),std::max(style.WindowPadding.y,font*0.7f)};
    const ImVec2 spacing{std::max(style.ItemSpacing.x,font*0.6f),std::max(style.ItemSpacing.y,font*0.35f)};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,padding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,spacing);
    ImGui::SetNextWindowSize({900,620},ImGuiCond_FirstUseEver);
    const auto screen=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSizeConstraints({std::min(font*36,screen.x),std::min(font*24,screen.y)},
                                       {std::max(1.0f,screen.x),std::max(1.0f,screen.y)});
    if(ImGui::Begin("Download Marker Packs##Taquito",&shown)) {
        const auto snapshot=OnlinePacks::GetSnapshot();
        const float designWidth=ImGui::CalcTextSize("Design Your Own Packs").x+style.FramePadding.x*2;
        const float right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Marker packs");
        if(ImGui::GetItemRectMax().x-ImGui::GetWindowPos().x+spacing.x+designWidth<=right)
            ImGui::SameLine(right-designWidth);
        DesignButton();
        Muted(snapshot.status);
        Gap();
        const float refreshWidth=ImGui::CalcTextSize("Refresh list").x+style.FramePadding.x*2;
        ImGui::SetNextItemWidth(std::max(font*8,ImGui::GetContentRegionAvail().x-refreshWidth-spacing.x));
        ImGui::InputTextWithHint("##Pack search","Search packs",search,sizeof(search));
        ImGui::SameLine();
        const bool busy=std::any_of(snapshot.entries.begin(),snapshot.entries.end(),[](const auto&e){return e.downloading||e.removing;});
        if(Button("Refresh list",!snapshot.loading&&!busy)) OnlinePacks::Refresh();
        Gap();
        const char* source="Downloads are linked directly from Blish HUD's marker pack catalog.";
        const float width=ImGui::GetContentRegionAvail().x;
        const float footer=ImGui::CalcTextSize(source,nullptr,false,width).y+spacing.y;
        const float height=std::max(font*8,ImGui::GetContentRegionAvail().y-footer);
        const float listWidth=std::clamp(width*0.34f,font*12,std::max(font*12,width*0.46f));
        ImGui::BeginChild("Pack list",{listWidth,height},true,ImGuiWindowFlags_AlwaysUseWindowPadding);
        std::string term=search;
        std::transform(term.begin(),term.end(),term.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        for(const auto& entry:snapshot.entries) {
            if(!term.empty()&&!Matches(entry.name,term)&&!Matches(entry.author,term)&&!Matches(entry.categories,term)) continue;
            ImGui::PushID(entry.id.c_str());
            if(ImGui::Selectable(entry.name.c_str(),selected==entry.id)) {
                selected=entry.id; OnlinePacks::Describe(entry.id);
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",entry.name.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Pack details",{0,height},true,ImGuiWindowFlags_AlwaysUseWindowPadding);
        const OnlinePacks::Entry* current=nullptr;
        for(const auto& entry:snapshot.entries) if(entry.id==selected) { current=&entry; break; }
        if(current) {
            const bool installed=!current->installedFilename.empty();
            ImGui::TextWrapped("%s",current->name.c_str());
            if(!current->author.empty()) Muted("By "+current->author);
            Gap();
            if(!current->version.empty()) Muted("Latest version: "+current->version);
            if(installed) Muted(current->installedVersion.empty()?"Installed version: unknown":"Installed version: "+current->installedVersion);
            else Muted("Not installed");
            Gap(); ImGui::Separator(); Gap();
            if(current->descriptionPending) Muted("Loading description...");
            else if(current->descriptionReady) ImGui::TextWrapped("%s",current->description.c_str());
            else Muted("No description available.");
            if(!current->categories.empty()) {
                Gap(); Muted("Categories");
                ImGui::TextWrapped("%s",current->categories.c_str());
            }
            Gap(); Muted(installed?current->installedFilename:current->filename);
            if(!current->downloadStatus.empty()) { Gap(); ImGui::TextWrapped("%s",current->downloadStatus.c_str()); }
            Gap(); ImGui::Separator(); Gap();
            const bool enabled=!snapshot.loading&&!current->downloading&&!current->removing;
            if(Button(current->downloading?"Downloading...":installed?"Download / replace":"Download pack",enabled)) OnlinePacks::Download(current->id);
            if(!current->repository.empty()||current->pageUrl.starts_with("https://")) {
                const char* label=current->repository.empty()?"Website":"GitHub";NextAction(label);
                if(Button(label)) OpenWebPage(current->repository.empty()?current->pageUrl:current->repository);
            }
            if(!current->repository.empty()&&current->pageUrl.starts_with("https://")&&current->pageUrl!=current->repository) {
                NextAction("More info");if(Button("More info")) OpenWebPage(current->pageUrl);
            }
            if(installed) {
                Gap();
                if(Button(current->removing?"Removing...":"Remove From Taquito",enabled)) {
                    removeId=current->id; removeName=current->name; removeFile=current->installedFilename;
                    ImGui::OpenPopup("Remove pack?##Taquito");
                }
            }
        } else Muted("Select a marker pack to see its details.");
        if(ImGui::BeginPopupModal("Remove pack?##Taquito",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(removeName.c_str());
            Muted(removeFile);
            ImGui::TextUnformatted("Delete this pack from TacoPacks?");
            if(ImGui::Button("Remove")) { OnlinePacks::Remove(removeId,removeFile); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        Muted(source);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
