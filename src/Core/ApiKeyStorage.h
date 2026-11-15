#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace ApiKeyStorage {
// Manual-key provider only. A future keychain provider can replace the lookup
// without putting credentials in ordinary settings or pathing-state events.
// Calls currently belong to the addon/UI thread. No API requests are made here.
void Initialize(const std::filesystem::path& directory);
void Shutdown();
bool HasKey();
std::string CopyKey(); // Caller owns the plaintext copy and should erase it after use.
bool Save(std::string_view key);
bool Remove();
const std::string& Status();
}
