#pragma once
#include <atomic>

namespace AddonLifetime {
// DllMain sets this before the CRT runs static destructors on process detach.
// Windows may already have killed workers while they held locks or changed a
// container. Leave their storage to the OS instead of traversing it at exit.
inline constinit std::atomic_bool terminating{false}, acceptingCallbacks{false};
inline constinit std::atomic_uint callbacks{0};
inline bool ProcessTerminating() noexcept { return terminating.load(std::memory_order_acquire); }
inline void BeginProcessTermination() noexcept {
    terminating.store(true,std::memory_order_release);
    acceptingCallbacks.store(false,std::memory_order_release);
}
inline void StartCallbacks() noexcept { acceptingCallbacks.store(true,std::memory_order_release); }
inline void StopCallbacks() noexcept { acceptingCallbacks.store(false,std::memory_order_release); }
inline void WaitForCallbacks() noexcept {
    for(auto count=callbacks.load(std::memory_order_acquire);count;count=callbacks.load(std::memory_order_acquire))
        callbacks.wait(count,std::memory_order_acquire);
}
class Callback {
    bool active_;
    static void Leave() noexcept {
        if(callbacks.fetch_sub(1,std::memory_order_acq_rel)==1)callbacks.notify_all();
    }
public:
    Callback() noexcept {
        callbacks.fetch_add(1,std::memory_order_acq_rel);
        active_=acceptingCallbacks.load(std::memory_order_acquire);
        if(!active_)Leave();
    }
    ~Callback(){if(active_)Leave();}
    explicit operator bool() const noexcept { return active_; }
    Callback(const Callback&)=delete;
    Callback& operator=(const Callback&)=delete;
};
// No allocation, leak or special treatment on a normal Nexus unload. Explicit
// unload stops callbacks/workers and flushes saves before ordinary destruction.
// The union prevents an implicit member destructor on the process-exit path.
template<class T> class Storage {
    union { T value_; };
public:
    Storage():value_{}{}
    ~Storage(){if(!ProcessTerminating())value_.~T();}
    T& Get() noexcept {return value_;}
    Storage(const Storage&)=delete;
    Storage& operator=(const Storage&)=delete;
};
}
