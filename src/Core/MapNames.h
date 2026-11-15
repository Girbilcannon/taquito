#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace MapNames {
// Public, English map metadata only. No API key or account requests.
void Initialize(const std::filesystem::path& directory);
void Shutdown();
void Request(uint32_t map);
std::string Name(uint32_t map);
uint64_t Revision();
std::string Status();
}
