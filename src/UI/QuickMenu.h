#pragma once
#include <cstdint>
namespace QuickMenu {
    void Initialize();
    void Shutdown();
    void RequestToggle();
    void RequestIconToggle();
    void Render();
    bool GoodRenderAvailable();
    bool GoodRenderUsedLastFrame();
    bool GoodRenderDepthLastFrame();
    uint64_t GoodRenderWorldPasses();
}
