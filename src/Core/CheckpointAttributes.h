#pragma once
#include "AttributeRuntime.h"
#include "AttributeNumbers.h"
#include <charconv>
#include <cmath>
#include <unordered_set>
#include <algorithm>
#include <cctype>
namespace CheckpointAttributes {
inline std::string Trim(const std::string& text) {
    const auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    return first==std::string::npos ? std::string{} : text.substr(first,last-first+1);
}
inline std::vector<std::string> Guids(const std::string& text,bool& invalid,bool deduplicate=true) {
    std::vector<std::string> result;std::unordered_set<std::string> seen;size_t start=0;
    if(Trim(text).empty())return result;
    do {
        auto end=text.find_first_of(",\r\n",start);auto part=Trim(text.substr(start,end==std::string::npos ? end : end-start));
        if(part.empty()){if(end==std::string::npos)break;start=end+1;continue;}
        const auto key=AttributeRuntime::CanonicalGuid(part);
        if(key.empty())invalid=true;else if(!deduplicate || seen.insert(key).second)result.push_back(key);
        if(end==std::string::npos)break;start=end+1;
    }while(start<=text.size());return result;
}
inline bool Number(const std::string& text,float& result) {
    return AttributeNumbers::Float(text,result);
}
inline void Populate(TacoPack::Style& style) {
    auto get=[&](const char* key)->const std::string* {auto it=style.attributes.find(key);return it==style.attributes.end()?nullptr:&it->second;};
    auto flag=[&](const char* key) {auto*v=get(key);if(!v)return false;auto t=Trim(*v);for(auto&c:t)if(c>='A'&&c<='Z')c+=('a'-'A');return t=="true"||t=="1";};
    style.cpFinish=flag("cpfinish");style.resetMessage=get("resetmessage")?*get("resetmessage"):std::string{};
    style.checkpoint=flag("ischeckpoint");style.checkpointErrors=0;
    style.cpMemory=flag("cpmemory");style.keepVisible=flag("keepvisible");
    style.cpInfo=get("cpinfo")?*get("cpinfo"):std::string{};
    style.cpTriggerText=get("cptriggertext")?*get("cptriggertext"):(style.cpFinish?"Finish":"Start");
    if(Trim(style.cpTriggerText).empty())style.cpTriggerText="Start";
    style.cpManual=false;
    if(auto*v=get("cptrigger")){auto t=Trim(*v);for(auto&c:t)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));style.cpManual=t=="manual";if(t!="auto"&&t!="manual")style.checkpointErrors|=256;}
    style.cpExpanded=false;for(const char*key:{"cptrigger","cpinfo","cptriggertext","cpmemory","keepvisible","cpsegments"})style.cpExpanded|=get(key)!=nullptr;
    style.cpSegments.clear();style.cpSegmentIds.clear();
    if(auto*v=get("cpsegments")){bool bad=false;style.cpSegments=Guids(*v,bad);if(bad)style.checkpointErrors|=512;}
    if(auto*v=get("cpsegmentids")){bool bad=false;style.cpSegmentIds=Guids(*v,bad,false);if(bad)style.checkpointErrors|=1024;}
    style.cpTrail=flag("cptrail");style.trailDraw=flag("traildraw");style.cpFollowsWorld=false;style.drawSpeed=20;
    if(auto*v=get("cptrailmap")){auto t=Trim(*v);for(auto&c:t)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));style.cpFollowsWorld=t=="followsworld";if(t!="standard"&&t!="followsworld")style.checkpointErrors|=2048;}
    if(auto*v=get("drawspeed")){float speed=0;if(Number(*v,speed)&&speed>=0)style.drawSpeed=speed;else style.checkpointErrors|=4096;}
    style.resetGuids.clear();if(auto*text=get("resetguid")){bool invalid=false;style.resetGuids=Guids(*text,invalid);if(invalid)style.checkpointErrors|=1;}
    style.cpReset.clear();style.cpResetConfigured=false;
    if(auto*text=get("cpreset")) {style.cpResetConfigured=!Trim(*text).empty();style.cpReset=AttributeRuntime::CanonicalGuid(*text);if(style.cpResetConfigured&&style.cpReset.empty())style.checkpointErrors|=2;}
    float range=0;
    const auto*trigger=get("triggerrange");
    const bool explicitRange=trigger&&Number(*trigger,range)&&range>=0;
    style.cpResetActivationAllowed=style.autoTrigger || explicitRange;
    if(!style.cpReset.empty()&&style.autoTrigger&&!explicitRange)style.triggerRange=2.0f;
    if(!style.cpReset.empty()&&!style.cpResetActivationAllowed)style.checkpointErrors|=4;
    style.cpColor=style.cpPreColor=style.tint;style.cpRadius=2;style.nextCheckCount=2;style.nextChecks.clear();
    if(!style.checkpoint&&!style.cpFinish)return; // Dedicated reset markers do not need native visual options.
    const auto color=[&](const char*key,uint32_t&target,uint32_t error){
        if(auto*value=get(key)) {
            auto text=Trim(*value);uint32_t parsed=0;auto r=std::from_chars(text.data(),text.data()+text.size(),parsed,16);
            if(text.size()!=8||r.ec!=std::errc{}||r.ptr!=text.data()+text.size())style.checkpointErrors|=error;
            else target=parsed; // Native checkpoint colors are RRGGBBAA, never ARGB.
        }
    };
    color("cpcolor",style.cpColor,8);color("cpprecolor",style.cpPreColor,16);
    if(auto*value=get("cpradius")){float radius=0;if(Number(*value,radius)&&radius>0)style.cpRadius=std::max(.01f,radius);else style.checkpointErrors|=32;}
    else if(style.cpFinish&&!style.cpReset.empty()&&explicitRange)style.cpRadius=std::max(.01f,range);
    if(style.cpFinish&&!style.cpReset.empty()&&!style.autoTrigger)style.cpManual=true;
    if(auto*value=get("nextcheckcount")){
        auto text=Trim(*value);uint32_t count=0;auto r=std::from_chars(text.data(),text.data()+text.size(),count);
        if(text.empty()||r.ec!=std::errc{}||r.ptr!=text.data()+text.size())style.checkpointErrors|=64;else style.nextCheckCount=count;
    }
    if(auto*value=get("nextcheck")){bool invalid=false;style.nextChecks=Guids(*value,invalid);if(invalid)style.checkpointErrors|=128;}
}
}
