#pragma once
#include "MarkerPack.h"
namespace CheckpointRuntime {
void Initialize(const std::filesystem::path& directory);
void SetContext(const std::string& character,uint32_t shard,uint32_t instance,double seconds);
void SaveProgress();
// UI thread only. Rebuild only after pack adoption; no graph traversal in rendering.
void Load(std::vector<TacoPack::Pack>& packs);
// Capture ID-based progress before old pack objects are destroyed.
void PrepareReload();
void Clear();
bool Visible(const TacoPack::Marker& marker);
uint32_t Tint(const TacoPack::Marker& marker,uint32_t ordinary);
// Native contact never substitutes for manual/auto activation of reset actions.
bool Visible(const TacoPack::Trail& trail);
uint32_t Tint(const TacoPack::Trail& trail,uint32_t ordinary);
void UpdateContact(uint32_t map,TacoPack::Vec3 player,bool active);
bool ResetFrom(const TacoPack::Marker& activator);
double RevealLimit(const TacoPack::Trail& trail,const std::vector<TacoPack::Vec3>& strip,bool map);
struct Prompt {uint64_t generation=0;size_t pack=0,run=0,node=0;bool action=false,reset=false;std::string text,caption,key;uint64_t serial=0;};
Prompt GetPrompt(bool showInfo);
bool Act(const Prompt& prompt);
bool ConsumeResetMessage(std::string& message);
std::string Status();
struct Diagnostics { size_t nodes=0,runs=0,blockedRuns=0,completedRuns=0;uint64_t contacts=0,resets=0; };
Diagnostics GetDiagnostics();
}
