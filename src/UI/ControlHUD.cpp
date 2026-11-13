#include "ControlHUD.h"
#include "../Core/AppSettings.h"
#include "../Core/PieUIBridge.h"
#include "../imgui/imgui.h"
#include <algorithm>
#include <cmath>

namespace {
bool shown=false,dragging=false;
constexpr float Pi=3.14159265359f;
constexpr float DialWidth=86,DialSize=40,PanelGap=40;
ImU32 HudColor(const ImVec4& color,unsigned alpha) {
    return (ImGui::ColorConvertFloat4ToU32(color)&0x00ffffffu)|(alpha<<24);
}
struct Style {
    int vars=0;
    ImU32 dialFill=IM_COL32(33,33,33,200),dialBase=IM_COL32(86,86,86,230),
        dialActive=IM_COL32(189,189,189,255),dialHand=IM_COL32(235,235,235,255),
        divider=IM_COL32(170,170,170,70);
    ImVec4 heading{.9f,.9f,.9f,1};
    Style() {
        ImGuiStyle palette;ImGui::StyleColorsDark(&palette);
        for(auto& color:palette.Colors){const float gray=.2126f*color.x+.7152f*color.y+.0722f*color.z;color.x=color.y=color.z=gray;}
        palette.Colors[ImGuiCol_WindowBg]={.03f,.03f,.03f,.42f};
        palette.Colors[ImGuiCol_Border]={.65f,.65f,.65f,.55f};
        palette.Colors[ImGuiCol_FrameBg]={.19f,.19f,.19f,.72f};
        palette.Colors[ImGuiCol_FrameBgHovered]={.30f,.30f,.30f,.85f};
        palette.Colors[ImGuiCol_FrameBgActive]={.39f,.39f,.39f,.9f};
        palette.Colors[ImGuiCol_CheckMark]={.85f,.85f,.85f,1};
        palette.Colors[ImGuiCol_SliderGrab]={.68f,.68f,.68f,1};
        palette.Colors[ImGuiCol_SliderGrabActive]={.9f,.9f,.9f,1};
        palette.Colors[ImGuiCol_Text]={.96f,.96f,.96f,1};
        // Take only RGB from the published palette. HUD geometry and every
        // opacity remain local, independent of Pie UI and the Nexus style.
        if(AppSettings::Get().pieUIThemes&&PieUIBridge::Present()) {
            const auto theme=PieUIBridge::GetTheme();
            const auto count=std::min(theme.count,static_cast<uint32_t>(ImGuiCol_COUNT));
            for(uint32_t i=0;i<count;++i) {
                const auto color=ImGui::ColorConvertU32ToFloat4(theme.colors[i]);
                auto& hud=palette.Colors[i];hud.x=color.x;hud.y=color.y;hud.z=color.z;
            }
            if(count&&theme.accent) {
                const auto accent=ImGui::ColorConvertU32ToFloat4(theme.accent);
                auto& active=palette.Colors[ImGuiCol_SliderGrabActive];
                active.x=accent.x;active.y=accent.y;active.z=accent.z;
            }
            if(count>ImGuiCol_FrameBg)dialFill=HudColor(palette.Colors[ImGuiCol_FrameBg],200);
            if(count>ImGuiCol_Border){dialBase=HudColor(palette.Colors[ImGuiCol_Border],230);divider=HudColor(palette.Colors[ImGuiCol_Border],70);}
            if(count>ImGuiCol_SliderGrabActive||(count&&theme.accent))dialActive=HudColor(palette.Colors[ImGuiCol_SliderGrabActive],255);
            if(count>ImGuiCol_Text){dialHand=HudColor(palette.Colors[ImGuiCol_Text],255);heading=palette.Colors[ImGuiCol_Text];}
        }
        for(int i=0;i<ImGuiCol_COUNT;++i)ImGui::PushStyleColor(i,palette.Colors[i]);
        V(ImGuiStyleVar_Alpha,1);V(ImGuiStyleVar_WindowPadding,ImVec2(14,12));
        V(ImGuiStyleVar_ItemSpacing,ImVec2(10,7));V(ImGuiStyleVar_ItemInnerSpacing,ImVec2(8,5));
        V(ImGuiStyleVar_FramePadding,ImVec2(6,4));V(ImGuiStyleVar_WindowRounding,12);
        V(ImGuiStyleVar_ChildRounding,8);V(ImGuiStyleVar_FrameRounding,5);
        V(ImGuiStyleVar_GrabRounding,5);V(ImGuiStyleVar_ScrollbarRounding,6);
        V(ImGuiStyleVar_WindowBorderSize,1);V(ImGuiStyleVar_FrameBorderSize,0);
        V(ImGuiStyleVar_IndentSpacing,16);V(ImGuiStyleVar_GrabMinSize,10);
        V(ImGuiStyleVar_ScrollbarSize,12);V(ImGuiStyleVar_WindowMinSize,ImVec2(32,32));
        V(ImGuiStyleVar_ButtonTextAlign,ImVec2(.5f,.5f));
    }
    void V(ImGuiStyleVar key,float value){ImGui::PushStyleVar(key,value);++vars;}
    void V(ImGuiStyleVar key,ImVec2 value){ImGui::PushStyleVar(key,value);++vars;}
    ~Style(){ImGui::PopStyleVar(vars);ImGui::PopStyleColor(ImGuiCol_COUNT);}
};
bool Number(float& value,float maximum,float width=72) {
    ImGui::SetNextItemWidth(width);
    const bool changed=ImGui::InputFloat("##number",&value,0,0,"%.2fx");
    if(changed)value=std::isfinite(value)?std::clamp(value,.25f,maximum):1;
    return changed;
}
void DistanceTip(){if(ImGui::IsItemHovered())ImGui::SetTooltip("Moves the fade start and keeps its width. Missing fade values use 3000-3500 game inches. 1x uses the base range.");}
bool Slider(const char* label,float& value,float maximum,bool distance=false) {
    ImGui::PushID(label);ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(174);
    bool changed=ImGui::SliderFloat("##slider",&value,.25f,maximum,"");
    if(distance)DistanceTip();
    ImGui::SameLine();changed|=Number(value,maximum,76);
    ImGui::PopID();return changed;
}
bool Dial(const Style& skin,const char* label,float& value,float maximum,bool distance=false) {
    ImGui::PushID(label);ImGui::BeginGroup();
    const float x=ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x+(DialWidth-DialSize)*.5f);
    const ImVec2 pos=ImGui::GetCursorScreenPos();
    const ImVec2 center{pos.x+DialSize*.5f,pos.y+DialSize*.5f};
    ImGui::InvisibleButton("##dial",{DialSize,DialSize});
    bool changed=false;
    if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0,0)) {
        const auto delta=ImGui::GetIO().MouseDelta;
        const float next=std::clamp(value+(delta.x-delta.y)*(maximum-.25f)/200,.25f,maximum);
        if(next!=value){value=next;changed=true;}
    }
    if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){value=1;changed=true;}
    if(distance)DistanceTip();
    auto* draw=ImGui::GetWindowDrawList();
    const float angle=.75f*Pi+std::clamp((value-.25f)/(maximum-.25f),0.0f,1.0f)*1.5f*Pi;
    draw->AddCircleFilled(center,19,skin.dialFill,32);
    draw->PathArcTo(center,17,.75f*Pi,2.25f*Pi,32);draw->PathStroke(skin.dialBase,false,2);
    draw->PathArcTo(center,17,.75f*Pi,angle,32);draw->PathStroke(skin.dialActive,false,2);
    draw->AddLine(center,{center.x+std::cos(angle)*12,center.y+std::sin(angle)*12},skin.dialHand,1.5f);
    // Center each label line independently (the two-line labels have unequal lengths).
    const char* line=label;
    for(const char* end=label;;++end)if(*end=='\n'||!*end){
        const float textWidth=ImGui::CalcTextSize(line,end).x;
        ImGui::SetCursorPosX(x+(DialWidth-textWidth)*.5f);ImGui::TextUnformatted(line,end);
        if(!*end)break;line=end+1;
    }
    ImGui::SetCursorPosX(x);changed|=Number(value,maximum,DialWidth);
    ImGui::EndGroup();ImGui::PopID();return changed;
}
void Heading(const Style& skin,const char* label) {ImGui::TextColored(skin.heading,"%s",label);}
bool Panel(const Style& skin,const char* name,bool& visible,float& scale,bool world,bool horizontal) {
    auto& settings=AppSettings::Get();
    ImGui::PushID(name);ImGui::BeginGroup();
    const float x=ImGui::GetCursorPosX(),top=ImGui::GetCursorPosY();
    const float checkboxWidth=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::CalcTextSize("Visibility").x;
    const float width=world?3*DialWidth+2*ImGui::GetStyle().ItemSpacing.x:std::max(DialWidth,checkboxWidth);
    if(horizontal){ImGui::Dummy({width,0});ImGui::SetCursorPosY(top);ImGui::SetCursorPosX(x+(width-ImGui::CalcTextSize(name).x)*.5f);}
    Heading(skin,name);
    if(horizontal)ImGui::SetCursorPosX(x+(width-checkboxWidth)*.5f);
    bool changed=ImGui::Checkbox("Visibility",&visible);
    if(horizontal) {
        ImGui::SetCursorPosX(x+(world?0:(width-DialWidth)*.5f));
        if(world){changed|=Dial(skin,"Marker\nDistance",settings.worldMarkerDistance,4,true);ImGui::SameLine();
            changed|=Dial(skin,"Trail\nDistance",settings.worldTrailDistance,4,true);ImGui::SameLine();}
        changed|=Dial(skin,"Marker\nScale",scale,3);
    } else {
        if(world){changed|=Slider("Marker Distance",settings.worldMarkerDistance,4,true);changed|=Slider("Trail Distance",settings.worldTrailDistance,4,true);}
        changed|=Slider("Marker Scale",scale,3);
    }
    ImGui::EndGroup();ImGui::PopID();return changed;
}
}
void ControlHUD::Toggle(){if(AppSettings::Get().controlHudEnabled){shown=!shown;dragging=false;}}
void ControlHUD::Close(){shown=false;dragging=false;}
bool ControlHUD::IsOpen(){return shown;}
void ControlHUD::Render(void (*save)()) {
    auto& settings=AppSettings::Get();if(!settings.controlHudEnabled){Close();return;}if(!shown)return;
    Style style;
    const auto screen=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({30,120},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({0,0},{std::max(1.0f,screen.x),std::max(1.0f,screen.y)});
    const bool visible=ImGui::Begin("Taquito Control HUD##Taquito",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove);
    // Keep this HUD's typography/layout independent of Nexus global font scaling.
    const float fontScale=ImGui::GetFont()->FontSize*ImGui::GetIO().FontGlobalScale;
    ImGui::SetWindowFontScale(fontScale>0?14.0f/fontScale:1);
    if(visible) {
        const bool horizontal=settings.controlHudHorizontal&&screen.x>=620;
        if(horizontal)ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{10,3});
        const float top=ImGui::GetCursorScreenPos().y;
        auto* draw=ImGui::GetWindowDrawList();
        bool changed=Panel(style,"World",settings.world,settings.worldIconSize,true,horizontal);
        const float firstDivider=ImGui::GetItemRectMax().x+PanelGap*.5f;
        if(horizontal)ImGui::SameLine(0,PanelGap);else {ImGui::Spacing();ImGui::Separator();ImGui::Spacing();}
        changed|=Panel(style,"Full Map",settings.big,settings.bigIconSize,false,horizontal);
        const float secondDivider=ImGui::GetItemRectMax().x+PanelGap*.5f;
        if(horizontal)ImGui::SameLine(0,PanelGap);else {ImGui::Spacing();ImGui::Separator();ImGui::Spacing();}
        changed|=Panel(style,"Minimap",settings.mini,settings.miniIconSize,false,horizontal);
        if(horizontal){
            const float bottom=ImGui::GetItemRectMax().y;
            for(float x:{firstDivider,secondDivider})draw->AddLine({x,top+2},{x,bottom-2},style.divider);
            ImGui::PopStyleVar();
        }
        if(changed&&save)save();
        // Drag unused background/heading space; widgets keep their normal input.
        if(ImGui::IsWindowHovered()&&!ImGui::IsAnyItemHovered()&&!ImGui::IsAnyItemActive()&&ImGui::IsMouseClicked(0))dragging=true;
        if(!ImGui::IsMouseDown(0))dragging=false;
        if(dragging){const auto p=ImGui::GetWindowPos(),d=ImGui::GetIO().MouseDelta;ImGui::SetWindowPos({p.x+d.x,p.y+d.y});}
        if(ImGui::BeginPopupContextWindow("##ControlHUDMenu",ImGuiPopupFlags_MouseButtonRight)){
            if(ImGui::MenuItem("Close"))Close();
            ImGui::EndPopup();
        }
    }
    const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    ImGui::SetWindowPos({std::clamp(p.x,0.0f,std::max(0.0f,screen.x-size.x)),std::clamp(p.y,0.0f,std::max(0.0f,screen.y-size.y))});
    ImGui::End();
}
