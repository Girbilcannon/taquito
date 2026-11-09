#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace OnlinePacks {
struct Entry {
    std::string id, name, filename, description, repository, downloadUrl, pageUrl, categories, author, authorUsername;
    std::string version, lastUpdate, installedVersion, installedFilename;
    std::string downloadStatus;
    bool descriptionPending = false, descriptionReady = false, downloading = false, removing = false;
};
struct Snapshot { std::vector<Entry> entries; std::string status; bool loading = false; };
void Start(const std::filesystem::path& packsDirectory);
void Stop();
void Refresh();
void Describe(const std::string& id);
void Download(const std::string& id);
void Remove(const std::string& id,const std::string& expectedFilename);
// Queue a throttled disk check while the browser is visible; never scan on the render thread.
void RefreshInstalled();
void CheckForUpdates(const std::filesystem::path& packsDirectory);
std::string GetUpdateStatus();
Snapshot GetSnapshot();
bool ConsumeReloadRequest();
}
