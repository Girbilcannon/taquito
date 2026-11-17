#pragma once
#include "../Core/MarkerPack.h"
#include "../mumble/Mumble.h"
#include "../nexus/Nexus.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <wrl/client.h>
#include <d3d11.h>

struct ImDrawList;
struct ImDrawCmd;

class Renderer {
public:
    struct FrameCamera {
        TacoPack::Vec3 position,front;
        float fov=0;
        const float* viewProjection=nullptr; // Column-major; borrowed for this Draw call only.
        TacoPack::Vec3 up{0,1,0};
    };
    struct WorldPass {
        using Quad = void (*)(const float[4][3],const float[4][2],void*,uint32_t);
        ImDrawList* drawList=nullptr;
        const FrameCamera* camera=nullptr;
        bool geometry=true;
        bool screenOverlays=true;
        bool cachedTexturesOnly=false;
        Quad worldQuad=nullptr;
    };
    bool Initialize(AddonAPI_t* api);
    bool IsReady() const { return device_.Get() != nullptr; }
    size_t WorldTrailSegments() const { return worldTrailSegments_; }
    size_t CachedTextures() const { return images_.size(); }
    size_t PendingTextures() const { return pendingTextures_.size(); }
    uint64_t TextureLoads() const { return textureLoads_; }
    uint64_t TextureFailures() const { return textureFailures_; }
    uint64_t TextureClears() const { return textureClears_; }
    const char* TextureClearReason() const { return textureClearReason_; }
    double LastTextureLoadMs() const { return lastTextureLoadMs_; }
    double MaxTextureLoadMs() const { return maxTextureLoadMs_; }
    float MinimapUIScale() const { return minimapUIScale_; }
    void Shutdown();
    // UI callbacks may run after Draw queues ImGui commands; release at next frame start.
    void RequestTextureClear();
    // world_quad textures must survive until the Good Render subscription drains.
    bool TextureResetPending(uint32_t map) const {
        return !images_.empty() && (clearTexturesNextFrame_ || map!=lastMap_ || !map);
    }
    void Draw(const std::vector<TacoPack::Pack>& packs, const Mumble::Data* mumble,
              const Mumble::Identity* identity, bool world, bool mini, bool big,
              float miniIconSize, float bigIconSize, float miniTrailSize, float bigTrailSize,
              float worldIconSize, float worldTrailSize,
              bool playerBubble, float bubbleRadius, float bubbleFeather, bool blishMapTrails = false,
              float worldMarkerDistance=1.0f,float worldTrailDistance=1.0f,
              const WorldPass* worldPass=nullptr,float verticalOffset=0);
private:
    static void BeginMapSampler(const ImDrawList*,const ImDrawCmd*);
    static void EndMapSampler(const ImDrawList*,const ImDrawCmd*);
    void ClearTextures(const char* reason);
    struct Image { Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view; int width = 0, height = 0; uint32_t mapColor = 0; };
    Image* Texture(const TacoPack::Pack& pack, const std::string& name);
    AddonAPI_t* api_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> mapSampler_,savedMapSampler_;
    std::unordered_map<std::string, Image> images_;
    struct TextureRequest { std::filesystem::path pack; std::string name,key; };
    std::deque<TextureRequest> pendingTextures_;
    std::unordered_set<std::string> pendingTextureKeys_;
    bool cachedTexturesOnly_=false;
    uint64_t textureLoads_=0,textureFailures_=0,textureClears_=0;
    const char* textureClearReason_="none";
    double lastTextureLoadMs_=0,maxTextureLoadMs_=0;
    float minimapUIScale_=1.0f;
    uint32_t lastMap_ = 0;
    unsigned newTexturesThisFrame_ = 0;
    bool clearTexturesNextFrame_ = false;
    size_t worldTrailSegments_ = 0;
};
