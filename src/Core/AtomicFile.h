#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <atomic>
#include <cstdint>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#include <Windows.h>
#endif
namespace AtomicFile {
// Write beside the destination, flush/close, then replace atomically on its filesystem.
inline bool Write(const std::filesystem::path& path,const std::string& text,bool unique=false) {
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
    if(ec)return false;
    auto temp=path;temp+=".tmp";
    if(unique) {
        static std::atomic<uint64_t> serial{0};
#ifdef _WIN32
        const auto process=GetCurrentProcessId();
#else
        const auto process=getpid();
#endif
        temp+="."+std::to_string(process)+"."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"."+std::to_string(serial++);
    }
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;
     out.write(text.data(),static_cast<std::streamsize>(text.size()));out.flush();if(!out)return false;out.close();if(!out)return false;}
#ifdef _WIN32
    return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temp,path,ec);return !ec;
#endif
}
}
