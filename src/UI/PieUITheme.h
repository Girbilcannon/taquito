#pragma once
#include "../Core/PieUIBridge.h"
#include "../imgui/imgui.h"
#include <algorithm>

namespace PieUITheme {
// Scope only Taquito's drawing. Never assign the shared Nexus ImGuiStyle.
class Scope {
    int count=0;
public:
    explicit Scope(bool enabled) {
        if(!enabled || !PieUIBridge::Present())return;
        const auto theme=PieUIBridge::GetTheme();
        count=static_cast<int>(std::min(theme.count,static_cast<uint32_t>(ImGuiCol_COUNT)));
        for(int i=0;i<count;++i)ImGui::PushStyleColor(i,ImGui::ColorConvertU32ToFloat4(theme.colors[i]));
    }
    ~Scope(){if(count)ImGui::PopStyleColor(count);}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
}
