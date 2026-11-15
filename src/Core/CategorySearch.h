#pragma once
#include "MarkerPack.h"
#include "AttributeNumbers.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace CategorySearch {
inline std::string Fold(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}
// Full Path search: every word must occur somewhere along the category's path.
// A matching parent retains its descendants; ancestors remain as navigation.
// Only the temporary effective mask changes, never checkbox/preset selections.
inline std::string MapLabel(const std::string& value) {
    auto text=Fold(value);
    // Compare the full map name, tolerating spaces, punctuation and curly apostrophes.
    for(const std::string apostrophe:{"\xE2\x80\x99","\xE2\x80\x98"})
        for(size_t at=0;(at=text.find(apostrophe,at))!=std::string::npos;)text.erase(at,apostrophe.size());
    text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char c){return c<128 && !std::isalnum(c);}),text.end());
    return text;
}
using MapMatches=std::unordered_set<const TacoPack::Category*>;
inline MapMatches FindMapMatches(const std::vector<TacoPack::Pack>& packs,uint32_t map) {
    MapMatches matches;
    if(!map)return matches;
    for(const auto& pack:packs) {
        for(const auto& [key,category]:pack.categories) {
            uint32_t id=0;
            if(const auto* value=TacoPack::Attribute(category->style,"mapid");
               value && AttributeNumbers::Unsigned(*value,id) && id==map)matches.insert(category);
        }
        for(const auto& marker:pack.markers)if(marker.map==map && marker.category)matches.insert(marker.category);
        for(const auto& trail:pack.trails)if(trail.map==map && trail.category)matches.insert(trail.category);
    }
    return matches;
}
inline bool Apply(std::vector<TacoPack::Pack>& packs,const std::string& query,bool enabled,
                  bool filterByMap=false,uint32_t map=0,const std::string& mapName={},
                  const MapMatches* mapIndex=nullptr) {
    std::vector<std::string> terms;
    if(enabled) {
        std::istringstream input(Fold(query));std::string word;
        while(input>>word)terms.push_back(std::move(word));
    }
    bool changed=false;
    const bool mapActive=filterByMap && map!=0;
    const auto label=mapActive ? MapLabel(mapName) : std::string{};
    MapMatches collected;
    if(mapActive && !mapIndex) {collected=FindMapMatches(packs,map);mapIndex=&collected;}
    for(auto& pack:packs) {
        const auto visit=[&](auto&& self,TacoPack::Category& category,const std::string& parent,bool inMapBranch)->bool {
            const std::string path=terms.empty() ? std::string{} : parent+" "+Fold(category.name)+" "+Fold(category.key);
            const bool namedMap=mapActive && !inMapBranch && !label.empty() &&
                (MapLabel(category.name).find(label)!=std::string::npos || MapLabel(category.key).find(label)!=std::string::npos);
            inMapBranch|=namedMap;
            const bool mapMatch=!mapActive || inMapBranch || mapIndex->contains(&category);
            const bool textMatch=std::all_of(terms.begin(),terms.end(),[&](const auto& term){return path.find(term)!=std::string::npos;});
            // Intersect at each actual category before retaining ancestors. An
            // ancestor shared by two unrelated matches must not qualify by itself.
            bool keep=mapMatch && textMatch;
            for(auto& child:category.children)keep|=self(self,*child,path,inMapBranch);
            const bool filtered=!keep;
            changed|=category.searchFiltered!=filtered;
            category.searchFiltered=filtered;
            return keep;
        };
        for(auto& category:pack.root.children)visit(visit,*category,{},false);
    }
    return changed;
}
}
