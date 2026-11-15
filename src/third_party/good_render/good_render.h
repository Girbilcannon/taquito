#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cwchar>

// GW2 Good Render: one addon follows the game's frame and calls yours at each of its moments, so you draw into the
// game's frame rather than over all of it: under the game's windows, or into the world. **Take this header as it is**,
// from https://github.com/HasKha/gw2-good-render. An addon built against an older header can still be called, but
// consider that header deprecated and update to the latest.
//
// Your callback sits in a table in shared memory, a named file mapping every addon opens for itself. The service calls
// it straight from the game's render thread, with no lock in between, and counts each call, so unsubscribe() can wait
// for one in progress.

struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ImDrawList;

namespace good_render {

// In frame order.
enum MomentId : uint32_t {
    // Opaque geometry (terrain, gadgets, characters) is done. The game's ground telegraphs are next.
    MOMENT_AFTER_OPAQUE = 0,
    // The game's ground telegraphs are done. Water, particles and effects are next, and blend over what is drawn here.
    MOMENT_AFTER_DECALS = 1,
    // The surface pass is done. What is drawn here still goes through post-processing.
    MOMENT_SURFACE_END = 2,
    // The world is finished. The game's UI is next.
    MOMENT_WORLD_FINISHED = 3,
    // Inside the game's UI: over its names, nameplates, damage numbers and HUD. Under the NPC dialog, windows and tooltips.
    MOMENT_BEFORE_WINDOWS = 4,
    // The game's UI is done. Addon overlays are next.
    MOMENT_UI_FINISHED = 5,
};

// The camera the frame is drawn with, in MumbleLink's space: metres, x right, y up, z forward, left-handed.
//
// The matrices are column-major, element [column * 4 + row], so clip = matrix * (x, y, z, 1). Both glm::make_mat4() and
// DirectXMath's XMLoadFloat4x4() take them as they are: DirectXMath's row vectors, v * M, read the same memory the same way.
struct Camera {
    float position[3];
    float front[3];  // unit length
    float up[3];     // unit length
    float fov;       // vertical, radians
    // The scene's width over its height, which the projection uses. Not the window's when the game renders below the
    // monitor's resolution.
    float aspect;
    // World to view, lookAtLH: view z is the view depth, in metres.
    float view[16];
    // View to clip, perspectiveLH with D3D's 0..1 depth, from 0.1 m to 10 km: clip w is the view depth. These planes are
    // not the game's, so to compare with the game's depth, compare view depths: clip w against linear_depth * z_far.
    float projection[16];
    float view_projection[16];
};

// What a moment hands you. Valid only during your callback.
struct Moment {
    uint32_t moment;  // MomentId
    // The size of what is bound, in its own pixels: the scene's up to WORLD_FINISHED, smaller than the window with
    // resolution scaling, and the window's after.
    uint32_t width;
    uint32_t height;
    // The far plane linear_depth is divided by, in metres: view depth is linear_depth * z_far.
    float z_far;
    // Camera can be null: before MumbleLink is written, and while the full map is open.
    const Camera* camera;
    // How many of the bound target's pixels make one window pixel: 1 in the game's UI.
    float pixels_per_window_pixel;

    // The game's immediate context, with the game's targets bound.
    // IF YOU CHANGE ANYTHING IN IT, RESTORE IT TO WHAT IT WAS BEFORE RETURNING.
    // The game relies on this state to render correctly. Changing it and not restoring it will break it.
    // Never write a target's alpha: in the world, the engine uses it as a per-pixel tag.
    ID3D11DeviceContext* context;

    // The scene's prepass colour. Its alpha * 255 is 0 on most of the world, 1 where the game puts no decals, 3 on
    // foliage and 5 on characters. In the game's UI it is a different size from what is bound: sample it by screen
    // fraction there, not by pixel. Read it only, never bind it as a target.
    ID3D11ShaderResourceView* mask;

    // Opaque geometry's view depth divided by z_far, an R32_FLOAT. The sky reads 1. Sized like mask.
    ID3D11ShaderResourceView* linear_depth;

    // An empty ImGui list to draw into, like Nexus's background list: in window pixels, with the font atlas bound and a
    // full-screen clip rectangle. It is drawn into the game's frame after your callback returns. Each command's
    // TextureId is an ID3D11ShaderResourceView*; clip rectangles and user callbacks are ignored. It is Nexus's ImGui
    // 1.80, the one every Nexus addon shares.
    ImDrawList* draw_list;

    // Like ImGui's AddImageQuad(), but at points in the world, in MumbleLink metres. Each pixel is hidden behind whatever
    // the game drew in front of it (a wall, a hill, a character), which draw_list never is. Drawn after your callback
    // returns, before draw_list, in the order you call it.
    //
    // `texture` is what AddImageQuad() takes, an ID3D11ShaderResourceView*, or null for a plain colour. `colour` is an
    // IM_COL32. The corners go round the quad in either direction, with uvs[i] at corners[i]; null uvs map the whole
    // texture, (0,0) (1,0) (1,1) (0,1). A quad with a point that is not finite is dropped. Keep the texture alive until
    // unsubscribe() returns.
    //
    // Null outside the world (BEFORE_WINDOWS on) and when there is no camera: use draw_list then.
    void (*world_quad)(const float corners[4][3], const float uvs[4][2], void* texture, uint32_t colour);
    // A world_quad facing the camera, like an icon: the whole texture, `half_width` and `half_height` metres from
    // `centre` each way. Null when world_quad is.
    void (*world_billboard)(const float centre[3], float half_width, float half_height, void* texture, uint32_t colour);
};

using Callback = void (*)(const Moment* moment);

// This header's Moment. Moment only grows at its end, and a field never changes meaning, so a newer service can still
// call you: you read only the fields you know. Raised whenever Moment changes.
constexpr uint32_t MOMENT_VERSION = 1;

// More slots than there will be addons.
constexpr int MOST_SUBSCRIBERS = 64;

// Never write a slot yourself: subscribe() and unsubscribe() do.
struct Slot {
    // Taken by subscribe() before anything else; freed by unsubscribe() last.
    std::atomic<uint32_t> claimed;
    // Callbacks run in ascending order, ties in slot order: the highest draws over the others.
    std::atomic<int32_t> order;
    // The MOMENT_VERSION you were built against. The service calls only the versions it serves.
    std::atomic<uint32_t> version;
    std::atomic<Callback> callback;
    // Calls in progress, counted by the service around your callback and whatever it draws for you, so unsubscribe()
    // returns once nothing of yours is in use.
    std::atomic<uint32_t> calls;
};

// Never write directly. Use the block only through subscribe(), unsubscribe() and served().
struct Block {
    // The Moment versions the service calls, written by the service only. Both zero while no service is loaded, or while
    // it cannot follow the frame.
    std::atomic<uint32_t> newest_served;
    std::atomic<uint32_t> oldest_served;
    Slot slots[MOST_SUBSCRIBERS];
};

static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<int32_t>::is_always_lock_free &&
                  std::atomic<Callback>::is_always_lock_free,
              "the block is shared between modules, so every atomic in it must be a plain word");

// ---- The block

struct Link {
    HANDLE mapping = nullptr;
    Block* block = nullptr;
};

// Opens this process's block, creating it if you are first: zeroed, so no subscribers and no service. Empty only if
// Windows could not create the mapping: treat that as no service.
inline Link open() {
    wchar_t name[64]{};
    // v1 is the block's layout, not Moment's version.
    swprintf_s(name, L"GW2GoodRender.v1.%lu", GetCurrentProcessId());
    Link link;
    link.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Block), name);
    if (link.mapping != nullptr) {
        link.block = (Block*)MapViewOfFile(link.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block));
        if (link.block == nullptr) {
            CloseHandle(link.mapping);
            link.mapping = nullptr;
        }
    }
    return link;
}

// Closes your link. The block lives on while any addon has it open.
inline void close(Link& link) {
    if (link.block != nullptr) {
        UnmapViewOfFile(link.block);
    }
    if (link.mapping != nullptr) {
        CloseHandle(link.mapping);
    }
    link = Link{};
}

// ---- Subscribing

// Takes a free slot and calls `callback` at every moment from now on. Null when every slot is taken.
inline Slot* subscribe(Block& block, Callback callback, int32_t order) {
    for (Slot& slot : block.slots) {
        uint32_t free = 0;
        if (slot.claimed.compare_exchange_strong(free, 1)) {
            slot.order.store(order);
            slot.version.store(MOMENT_VERSION);
            // Last: the slot is live once its callback is in.
            slot.callback.store(callback);
            return &slot;
        }
    }
    return nullptr;
}

// Stops calls to your callback, then waits up to a second for one in progress to return. Once this returns, your
// callback is never called again and you may unload. Never call it from inside your callback: it would wait for itself.
inline void unsubscribe(Slot& slot) {
    slot.callback.store(nullptr);
    for (int waited_ms = 0; waited_ms < 1000 && slot.calls.load() != 0; waited_ms += 5) {
        Sleep(5);
    }
    slot.claimed.store(0);
}

// Whether a service is loaded that calls your version right now. The service can load and unload while you run, so
// check when you need to know, not once. False while a service is loaded means it does not serve your version: your
// header or the service is out of date.
inline bool served(const Block& block, uint32_t version = MOMENT_VERSION) {
    const uint32_t newest = block.newest_served.load();
    return newest != 0 && block.oldest_served.load() <= version && version <= newest;
}

} // namespace good_render
