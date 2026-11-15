#pragma once
#include "MarkerPack.h"
#include "AttributeNumbers.h"
#include <unordered_map>

namespace CategoryMapFilter {
// Recompute after map, preference or pack changes; never while drawing each row.
inline void Apply(std::vector<TacoPack::Pack>& packs,uint32_t map,bool enabled) {
    struct Match { bool hasMap=false,hit=false,hasAsset=false,unknownAsset=false; };
    for(auto& pack:packs) {
        std::unordered_map<const TacoPack::Category*,Match> direct;
        if(enabled && map) {
            for(const auto& [key,category]:pack.categories) {
                uint32_t id=0;
                if(const auto* value=TacoPack::Attribute(category->style,"mapid");
                   value && AttributeNumbers::Unsigned(*value,id) && id) {
                    auto& match=direct[category];match.hasMap=true;match.hit=id==map;
                }
            }
            const auto add=[&](const auto& item) {
                if(!item.category)return;
                auto& match=direct[item.category];match.hasAsset=true;
                if(item.map) { match.hasMap=true;match.hit|=item.map==map; }
                else match.unknownAsset=true;
            };
            for(const auto& item:pack.markers)add(item);
            for(const auto& item:pack.trails)add(item);
        }
        const auto visit=[&](auto&& self,TacoPack::Category& category)->bool {
            if(!enabled || !map) {
                category.mapFiltered=false;
                for(auto& child:category.children)self(self,*child);
                return true;
            }
            const auto found=direct.find(&category);
            const Match own=found==direct.end() ? Match{} : found->second;
            bool keep=own.hit || own.unknownAsset;
            // Keep unmapped helper leaves. Structural headings follow their
            // children so missing MapID on headings cannot retain every map.
            keep|=!own.hasMap && !own.hasAsset && category.children.empty();
            for(auto& child:category.children) {
                const bool childKept=self(self,*child);
                if(!child->hidden)keep|=childKept;
            }
            category.mapFiltered=!keep;
            return keep;
        };
        for(auto& category:pack.root.children)visit(visit,*category);
    }
}
}
