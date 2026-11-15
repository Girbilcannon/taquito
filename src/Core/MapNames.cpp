#include "MapNames.h"
#include "AddonLifetime.h"
#include "AtomicFile.h"
#include "AttributeNumbers.h"
#include "../third_party/jsonxx.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib,"winhttp.lib")
#endif

namespace {
struct State {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::atomic_bool stopping{false};
    std::atomic<uint64_t> revision{0};
    std::filesystem::path cache;
    std::unordered_map<uint32_t,std::string> names;
    std::unordered_set<uint32_t> attempted;
    uint32_t pending=0;
    std::string status="idle";
};
AddonLifetime::Storage<State> storage;
auto& state=storage.Get();
bool ValidName(const std::string& name) {
    return !name.empty() && name.size()<=256 &&
        std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32 && c!=127;});
}
bool Fetch(uint32_t map,std::string& body) {
#ifdef _WIN32
    struct Handle {
        HINTERNET value=nullptr;
        explicit Handle(HINTERNET h):value(h){}
        ~Handle(){if(value)WinHttpCloseHandle(value);}
        Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    };
    Handle session(WinHttpOpen(L"Taquito map metadata",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session.value)return false;
    WinHttpSetTimeouts(session.value,1000,1000,2000,2000);
    Handle connection(WinHttpConnect(session.value,L"api.guildwars2.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    const auto path=L"/v2/maps/"+std::to_wstring(map)+L"?lang=en";
    Handle request(connection.value ? WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE) : nullptr);
    if(!request.value || state.stopping.load())return false;
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    if(!WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) ||
       state.stopping.load() || !WinHttpReceiveResponse(request.value,nullptr))return false;
    DWORD code=0,size=sizeof(code);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX) || code!=200)return false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    char bytes[4096];
    while(!state.stopping.load() && std::chrono::steady_clock::now()<deadline) {
        DWORD count=0;
        if(!WinHttpReadData(request.value,bytes,sizeof(bytes),&count))return false;
        if(!count)return !body.empty();
        if(body.size()+count>32768)return false;
        body.append(bytes,count);
    }
#endif
    return false;
}
void Run() {
    for(;;) {
        uint32_t map=0;
        {
            std::unique_lock lock(state.mutex);
            state.wake.wait(lock,[]{return state.stopping.load() || state.pending!=0;});
            if(state.stopping.load())return;
            map=state.pending;state.pending=0;state.attempted.insert(map);
            state.status="looking up map "+std::to_string(map);
        }
        std::string body,name;
        jsonxx::Object result;
        if(Fetch(map,body) && result.parse(body) && result.has<jsonxx::Number>("id") &&
           result.get<jsonxx::Number>("id")==static_cast<double>(map) && result.has<jsonxx::String>("name"))
            name=result.get<jsonxx::String>("name");
        std::unordered_map<uint32_t,std::string> snapshot;
        {
            std::lock_guard lock(state.mutex);
            if(state.stopping.load())return;
            if(!ValidName(name)) {state.status="map "+std::to_string(map)+": name unavailable (MapID matching only)";continue;}
            state.names[map]=name;snapshot=state.names;
            state.status="map "+std::to_string(map)+": "+name;
            state.revision.fetch_add(1,std::memory_order_release);
        }
        jsonxx::Object names,cache;
        for(const auto& [id,value]:snapshot)names.import(std::to_string(id),jsonxx::Value(value));
        cache.import("schema",jsonxx::Value(1));cache.import("names",jsonxx::Value(names));
        // Worker-only disk IO; no settings/preset/category writes.
        AtomicFile::Write(state.cache,cache.json());
    }
}
}
void MapNames::Initialize(const std::filesystem::path& directory) {
    Shutdown();
    std::lock_guard lock(state.mutex);
    state.cache=directory/"map-names.json";state.names.clear();state.attempted.clear();state.pending=0;
    state.stopping=false;state.status="idle";state.revision=0;
    std::error_code error;const auto bytes=std::filesystem::file_size(state.cache,error);
    if(error || bytes>1024*1024)return;
    std::ifstream input(state.cache,std::ios::binary);std::string body(static_cast<size_t>(bytes),'\0');
    jsonxx::Object cache;
    if(!input.read(body.data(),static_cast<std::streamsize>(body.size())) || !cache.parse(body) ||
       !cache.has<jsonxx::Number>("schema") || cache.get<jsonxx::Number>("schema")!=1 || !cache.has<jsonxx::Object>("names"))return;
    const auto& names=cache.get<jsonxx::Object>("names");
    for(const auto& [key,value]:names.kv_map()) {
        uint32_t id=0;
        if(AttributeNumbers::Unsigned(key,id) && id && names.has<jsonxx::String>(key)) {
            const auto& name=names.get<jsonxx::String>(key);if(ValidName(name))state.names[id]=name;
        }
    }
}
void MapNames::Shutdown() {
    {std::lock_guard lock(state.mutex);state.stopping=true;state.pending=0;}
    state.wake.notify_all();if(state.worker.joinable())state.worker.join();
}
void MapNames::Request(uint32_t map) {
    if(!map)return;
    std::lock_guard lock(state.mutex);
    if(state.stopping.load() || state.cache.empty())return;
    if(const auto cached=state.names.find(map);cached!=state.names.end()) {
        state.status="map "+std::to_string(map)+": "+cached->second+" (cached)";return;
    }
    if(state.attempted.contains(map) || state.pending==map)return;
    state.pending=map;
    if(!state.worker.joinable())state.worker=std::thread(Run);
    state.wake.notify_one();
}
std::string MapNames::Name(uint32_t map) {
    std::lock_guard lock(state.mutex);const auto found=state.names.find(map);
    return found==state.names.end() ? std::string{} : found->second;
}
uint64_t MapNames::Revision(){return state.revision.load(std::memory_order_acquire);}
std::string MapNames::Status(){std::lock_guard lock(state.mutex);return state.status;}
