#include "AddonLifetime.h"
#include "CheckpointRuntime.h"
#include "AttributeRuntime.h"
#include "AppSettings.h"
#include "CheckpointAttributes.h"
#include "../third_party/jsonxx.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <unordered_set>

namespace {
using Marker=TacoPack::Marker;
constexpr size_t None=std::numeric_limits<size_t>::max();
struct Node {
    const Marker*marker=nullptr;std::vector<size_t>next,incoming;
    const TacoPack::Trail*trail=nullptr;std::string trailKey;
    const TacoPack::Style& Style()const{return marker?marker->style:trail->style;}
    const std::string& Key()const{return marker?marker->stateKey:trailKey;}
    uint32_t Map()const{return marker?marker->map:trail->map;}
    const void* Identity()const{return marker?static_cast<const void*>(marker):trail;}
    bool Eligible()const{return marker?AttributeRuntime::Available(*marker):AttributeRuntime::Available(*trail);}
};
struct Contact {bool initialized=false,inside=false;};
struct Run {
    size_t root=0;bool blocked=false,finished=false,expanded=false;std::string signature;
    std::map<std::string,double>reveals;size_t recent=None;uint64_t serial=0;
    std::vector<size_t>members;
    std::set<size_t>armed,completed;
    std::map<size_t,std::vector<size_t>>forks;
    std::map<size_t,size_t>choices;
    std::unordered_map<size_t,Contact>contact;
};
struct Segment {const TacoPack::Trail*trail=nullptr;const std::vector<TacoPack::Vec3>*strip=nullptr;double length=0;};
struct Graph {
    TacoPack::Pack*pack=nullptr;std::unordered_map<std::string,Segment>segments;
    std::unordered_map<const std::vector<TacoPack::Vec3>*,std::string>stripIds;
    std::unordered_map<std::string,std::vector<size_t>>segmentOwners;
    std::string packKey;
    std::vector<Node>nodes;std::unordered_map<std::string,size_t>index;
    std::vector<Run>runs;std::unordered_map<std::string,size_t>roots;
};
struct SavedRun {
    std::map<std::string,double>reveals;
    std::string signature;bool finished=false;
    std::vector<std::string>armed,completed;
    std::map<std::string,std::vector<std::string>>forks;
    std::map<std::string,std::string>choices;
};
struct RuntimeState {
    std::map<std::pair<std::string,std::string>,SavedRun>savedRuns;
    std::vector<Graph> graphs;
    std::unordered_map<const Marker*,size_t> markerPacks;
    std::unordered_map<const void*,uint32_t> paint;
    std::unordered_map<uint32_t,std::vector<std::pair<size_t,size_t>>> mapRuns;
    uint32_t lastMap=0;bool lastActive=false;TacoPack::Vec3 previousPlayer;
    uint64_t contactsPassed=0,resets=0,generation=0;
    std::filesystem::path directory;std::string character,notice;
    double seconds=0;uint32_t shard=0,instance=0;bool noticePending=false;
    std::unordered_map<const TacoPack::Trail*,size_t>trailPacks;

};
AddonLifetime::Storage<RuntimeState> storage;
auto& [savedRuns,graphs,markerPacks,paint,mapRuns,lastMap,lastActive,previousPlayer,contactsPassed,resets,generation,memoryDirectory,currentCharacter,resetNotice,cpSeconds,lastShard,lastInstance,noticePending,trailPacks]=storage.Get();
double Distance2(TacoPack::Vec3 a,TacoPack::Vec3 b) {
    const double x=static_cast<double>(a.x)-b.x,y=static_cast<double>(a.y)-b.y,z=static_cast<double>(a.z)-b.z;
    return x*x+y*y+z*z;
}
// Closest distance between finite 3D segments, including point segments.
double SegmentDistance2(TacoPack::Vec3 a,TacoPack::Vec3 b,TacoPack::Vec3 c,TacoPack::Vec3 d) {
    const double u[]={double(b.x)-a.x,double(b.y)-a.y,double(b.z)-a.z};
    const double v[]={double(d.x)-c.x,double(d.y)-c.y,double(d.z)-c.z};
    const double w[]={double(a.x)-c.x,double(a.y)-c.y,double(a.z)-c.z};
    auto dot=[](const double*x,const double*y){return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];};
    const double aa=dot(u,u),bb=dot(u,v),cc=dot(v,v),dd=dot(u,w),ee=dot(v,w);
    double t=0,q=0;
    if(aa<=1e-20)q=cc>1e-20?std::clamp(ee/cc,0.,1.):0;
    else if(cc<=1e-20)t=std::clamp(-dd/aa,0.,1.);
    else {const double denom=aa*cc-bb*bb;
        if(denom>1e-20)t=std::clamp((bb*ee-cc*dd)/denom,0.,1.);
        q=(bb*t+ee)/cc;
        if(q<0){q=0;t=std::clamp(-dd/aa,0.,1.);}else if(q>1){q=1;t=std::clamp((bb-dd)/aa,0.,1.);}
    }
    double distance=0;for(int i=0;i<3;++i){double delta=w[i]+t*u[i]-q*v[i];distance+=delta*delta;}
    return std::isfinite(distance)?distance:std::numeric_limits<double>::infinity();
}
double ContactDistance2(const Node&node,TacoPack::Vec3 previous,TacoPack::Vec3 player) {
    if(node.marker)return SegmentDistance2(previous,player,node.marker->position,node.marker->position);
    double nearest=std::numeric_limits<double>::infinity();
    for(const auto&strip:node.trail->strips)for(size_t i=1;i<strip.size();++i)
        if(Distance2(strip[i-1],strip[i])>1e-20)
            nearest=std::min(nearest,SegmentDistance2(previous,player,strip[i-1],strip[i]));
    return nearest;
}
bool Inside(const Node&node,TacoPack::Vec3 player) {
    const double radius=node.Style().cpRadius;
    return ContactDistance2(node,player,player)<=radius*radius;
}
void Persist(Graph&,Run&,bool erase=false);
void Repaint(Graph& graph,Run& run) {
    for(auto node:run.members)paint.erase(graph.nodes[node].Identity());
    if(run.blocked)return;
    std::unordered_map<size_t,uint32_t>depths;
    if(!run.finished)for(auto armed:run.armed) {
        const uint32_t horizon=graph.nodes[armed].Style().nextCheckCount;
        std::deque<std::pair<size_t,uint32_t>>queue{{armed,0}};
        std::unordered_map<size_t,uint32_t>visited;
        while(!queue.empty()) {
            auto [node,depth]=queue.front();queue.pop_front();
            if(run.completed.count(node))continue;
            auto found=visited.find(node);if(found!=visited.end()&&found->second<=depth)continue;
            visited[node]=depth;
            auto seen=depths.find(node);if(seen==depths.end()||depth<seen->second)depths[node]=depth;
            if(depth<horizon&&!graph.nodes[node].Style().cpFinish)for(auto next:graph.nodes[node].next)queue.emplace_back(next,depth+1);
        }
    }
    for(auto node:run.members)if(graph.nodes[node].Style().keepVisible&&!depths.count(node))depths[node]=1;
    for(auto[node,depth]:depths) {
        const auto&definition=graph.nodes[node];const auto&style=definition.Style();
        const auto base=[&](const char*key,uint32_t value){return !style.checkpoint&&!style.attributes.count(key)?style.tint:value;};
        if(run.armed.count(node)&&!run.finished)paint[definition.Identity()]=base("cpcolor",style.cpColor);
        else {
            const auto color=base("cpprecolor",style.cpPreColor);
            const double alpha=static_cast<double>(color&255u)*std::pow(.75,depth);
            paint[definition.Identity()]=(color&0xffffff00u)|static_cast<uint32_t>(std::clamp(std::floor(alpha),0.,255.));
        }
    }
}
void Start(Graph&graph,Run&run) {
    ++run.serial;run.finished=false;run.reveals.clear();run.recent=None;run.armed.clear();run.completed.clear();run.forks.clear();run.choices.clear();run.contact.clear();
    run.armed.insert(run.root);run.contact[run.root]={};Repaint(graph,run);
}
void Complete(Graph&graph,Run&run,size_t node,TacoPack::Vec3 player) {
    ++run.serial;const auto&style=graph.nodes[node].Style();
    if(style.cpFinish&&!style.cpReset.empty()&&style.cpReset!=graph.nodes[run.root].Key())return;
    if(run.finished) {run.finished=false;run.completed.clear();run.forks.clear();run.choices.clear();run.reveals.clear();}
    for(const auto&id:style.cpSegments)if(graph.segments.count(id))run.reveals.try_emplace(id,cpSeconds);
    run.recent=node;
    // Choosing a fork drops only its frontier alternatives. Repaint keeps reachable rejoins.
    for(auto it=run.forks.begin();it!=run.forks.end();) {
        const auto&alternatives=it->second;
        if(std::find(alternatives.begin(),alternatives.end(),node)!=alternatives.end()) {
            run.choices[it->first]=node;
            for(auto sibling:alternatives)if(sibling!=node){run.armed.erase(sibling);run.contact.erase(sibling);}
            it=run.forks.erase(it);
        }else ++it;
    }
    run.armed.erase(node);run.contact.erase(node);run.completed.insert(node);
    std::vector<size_t>next;
    for(auto successor:graph.nodes[node].next)if(!run.completed.count(successor))next.push_back(successor);
    if(graph.nodes[node].Style().cpFinish||(graph.nodes[node].Style().nextChecks.empty()&&!(graph.nodes[node].Style().checkpointErrors&128u))) {run.finished=true;run.armed.clear();run.contact.clear();run.forks.clear();
        if(run.expanded){run.completed.clear();run.choices.clear();run.reveals.clear();run.armed.insert(run.root);run.contact[run.root]={true,Inside(graph.nodes[run.root],player)};}
    }
    else {
        if(next.size()>1)run.forks[node]=next;
        for(auto successor:next)if(run.armed.insert(successor).second) {
            const auto&definition=graph.nodes[successor];
            // R::Visible still sees old paint until repaint; category/mount/behavior checked below after repaint.
            run.contact[successor]={true,Inside(definition,player)};
        }
    }
    Repaint(graph,run);
    for(auto successor:next)if(run.armed.count(successor)&&!graph.nodes[successor].Eligible())run.contact[successor].initialized=false;
    Persist(graph,run,run.finished);
    ++contactsPassed;
}
std::string Signature(const Graph&graph,const Run&run) {
    auto members=run.members;std::sort(members.begin(),members.end());std::string text;
    for(auto i:members){const auto&node=graph.nodes[i];text+=node.Key()+":"+std::to_string(node.Map())+">";
        for(auto next:node.next)text+=graph.nodes[next].Key()+",";text+=node.Style().cpFinish?"F":"C";text+=node.trail?"T":"M";
        for(const auto&target:node.Style().nextChecks)text+="/"+target;
        text+=(node.Style().checkpointErrors&128u)?"invalid;":";";
        const auto point=[&](TacoPack::Vec3 p){text+="@"+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z);};
        if(node.marker)point(node.marker->position);else for(const auto&strip:node.trail->strips){text+="|";for(auto p:strip)point(p);}
        const auto&s=node.Style();text+=std::to_string(s.cpRadius)+std::to_string(s.cpManual)+std::to_string(s.keepVisible)+std::to_string(s.cpMemory)+s.cpReset;
        for(const auto&id:s.cpSegments){text+="S"+id;auto seg=graph.segments.find(id);if(seg!=graph.segments.end()){const auto&st=seg->second.trail->style;text+=std::to_string(seg->second.trail->map)+std::to_string(st.trailDraw)+std::to_string(st.cpFollowsWorld)+std::to_string(st.drawSpeed);for(auto p:*seg->second.strip)point(p);}}}return text;
}
void Restore(Graph&graph,Run&run,const SavedRun&saved) {
    run.finished=saved.finished;run.armed.clear();run.completed.clear();run.contact.clear();run.forks.clear();run.choices.clear();
    for(const auto&key:saved.armed){auto node=graph.index.at(key);run.armed.insert(node);run.contact[node]={};}
    for(const auto&key:saved.completed)run.completed.insert(graph.index.at(key));
    for(const auto&[key,values]:saved.forks)for(const auto&value:values)run.forks[graph.index.at(key)].push_back(graph.index.at(value));
    for(const auto&[key,value]:saved.choices)run.choices[graph.index.at(key)]=graph.index.at(value);
    run.reveals.clear();for(const auto&[id,elapsed]:saved.reveals)run.reveals[id]=cpSeconds-elapsed;
    Repaint(graph,run);
}
void DefinitionWarnings(TacoPack::Pack&pack,const TacoPack::Style&style,const std::string&guid) {
    const uint32_t errors=style.checkpointErrors;
    const char* labels[]={"resetguid contains an invalid GUID; invalid entries ignored", "cpReset requires one valid root GUID",
        "cpReset requires a valid triggerRange or autoTrigger=true; reset action disabled", "cpColor must be eight RRGGBBAA hex digits; using ordinary color",
        "cpPreColor must be eight RRGGBBAA hex digits; using ordinary color", "cpRadius must be positive finite meters; using 2",
        "nextCheckCount must be a nonnegative integer; using 2", "nextCheck contains an invalid GUID; invalid links ignored"};
    const char*extra[]={"cpTrigger must be auto or manual","cpSegments contains invalid GUIDs","cpSegmentIds contains invalid GUIDs","cpTrailMap must be standard or followsWorld","drawSpeed must be nonnegative finite"};
    for(size_t i=0;i<5;++i)if(errors&(256u<<i))pack.diagnostics.push_back("Checkpoint: "+std::string(extra[i]));
    for(size_t i=0;i<8;++i)if(errors&(1u<<i))pack.diagnostics.push_back("Entry "+(guid.empty()?std::string("(no GUID)"):guid)+": "+labels[i]);
}
std::string Hash(const std::string& text) {
    uint64_t hash=14695981039346656037ull;for(unsigned char c:text){hash^=c;hash*=1099511628211ull;}
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
std::filesystem::path MemoryPath(const Graph&g,const Run&r) {
    return memoryDirectory/"checkpoints"/(Hash(g.packKey+"\n"+g.nodes[r.root].Key()+"\n"+currentCharacter)+".json");
}
SavedRun Snapshot(const Graph&g,const Run&r) {
    SavedRun s;s.signature=r.signature;s.finished=r.finished;
    for(auto n:r.armed)s.armed.push_back(g.nodes[n].Key());for(auto n:r.completed)s.completed.push_back(g.nodes[n].Key());
    for(const auto&[n,values]:r.forks)for(auto v:values)s.forks[g.nodes[n].Key()].push_back(g.nodes[v].Key());
    for(auto[n,v]:r.choices)s.choices[g.nodes[n].Key()]=g.nodes[v].Key();
    for(const auto&[id,start]:r.reveals)s.reveals[id]=std::clamp(cpSeconds-start,0.,604800.);
    return s;
}
void Persist(Graph&g,Run&r,bool erase) {
    if(!g.nodes[r.root].Style().cpMemory||memoryDirectory.empty()||currentCharacter.empty()||r.blocked)return;
    const auto path=MemoryPath(g,r);
    if(erase){AppSettings::QueueFileDelete(path);return;}
    auto snapshot=Snapshot(g,r);const auto pack=g.packKey,root=g.nodes[r.root].Key(),character=currentCharacter;
    AppSettings::QueueSerializedSave(path,[snapshot=std::move(snapshot),pack,root,character]{
        jsonxx::Object out,forks,choices,reveals;jsonxx::Array armed,completed;
        out.import("version",jsonxx::Value(1));out.import("pack",jsonxx::Value(pack));out.import("root",jsonxx::Value(root));out.import("character",jsonxx::Value(character));out.import("graph",jsonxx::Value(snapshot.signature));
        for(const auto&key:snapshot.armed)armed.import(jsonxx::Value(key));for(const auto&key:snapshot.completed)completed.import(jsonxx::Value(key));
        for(const auto&[key,values]:snapshot.forks){jsonxx::Array a;for(const auto&v:values)a.import(jsonxx::Value(v));forks.import(key,jsonxx::Value(a));}
        for(const auto&[key,value]:snapshot.choices)choices.import(key,jsonxx::Value(value));
        for(const auto&[key,value]:snapshot.reveals)reveals.import(key,jsonxx::Value(value*1000));
        out.import("armed",jsonxx::Value(armed));out.import("completed",jsonxx::Value(completed));out.import("forks",jsonxx::Value(forks));out.import("choices",jsonxx::Value(choices));out.import("revealedMilliseconds",jsonxx::Value(reveals));return out.json();
    },4*1024*1024,true);
}
bool ReadMemory(Graph&g,Run&r,SavedRun&saved) {
    if(!g.nodes[r.root].Style().cpMemory||memoryDirectory.empty()||currentCharacter.empty())return false;
    const auto path=MemoryPath(g,r);std::error_code ec;if(!std::filesystem::exists(path,ec))return false;
    auto invalid=[&](){if(g.pack)g.pack->diagnostics.push_back("Checkpoint memory invalid/stale; starting fresh: "+g.nodes[r.root].Key());return false;};
    const auto size=std::filesystem::file_size(path,ec);if(ec||size>4*1024*1024)return invalid();
    std::ifstream input(path,std::ios::binary);std::string body(static_cast<size_t>(size),'\0');jsonxx::Object json;
    if(!input.read(body.data(),static_cast<std::streamsize>(body.size())))return invalid();
    // Bound nesting before handing untrusted local data to the recursive JSON parser.
    unsigned depth=0;bool quoted=false,escaped=false;
    for(char c:body){if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"')quoted=true;else if(c=='{'||c=='['){if(++depth>32)return invalid();}else if(c=='}'||c==']'){if(!depth)return invalid();--depth;}}
    if(depth||quoted||!json.parse(body))return invalid();
    const auto equal=[&](const char*key,const std::string&value){return json.has<jsonxx::String>(key)&&json.get<jsonxx::String>(key)==value;};
    if(!json.has<jsonxx::Number>("version")||json.get<jsonxx::Number>("version")!=1||!equal("pack",g.packKey)||!equal("root",g.nodes[r.root].Key())||!equal("character",currentCharacter)||!equal("graph",r.signature))return invalid();
    std::set<size_t>members(r.members.begin(),r.members.end());auto valid=[&](const std::string&id){auto it=g.index.find(id);return it!=g.index.end()&&members.count(it->second);};
    auto array=[&](const char*key,std::vector<std::string>&out){if(!json.has<jsonxx::Array>(key))return false;const auto&a=json.get<jsonxx::Array>(key);std::set<std::string>seen;
        for(unsigned i=0;i<a.size();++i){if(!a.has<jsonxx::String>(i))return false;auto id=a.get<jsonxx::String>(i);if(!valid(id)||!seen.insert(id).second)return false;out.push_back(id);}return true;};
    if(!array("armed",saved.armed)||!array("completed",saved.completed))return invalid();
    if(!saved.completed.empty()&&std::find(saved.completed.begin(),saved.completed.end(),g.nodes[r.root].Key())==saved.completed.end())return invalid();
    for(const auto&id:saved.armed)if(std::find(saved.completed.begin(),saved.completed.end(),id)!=saved.completed.end())return invalid();
    if(!json.has<jsonxx::Object>("forks")||!json.has<jsonxx::Object>("revealedMilliseconds"))return invalid();
    const auto&forks=json.get<jsonxx::Object>("forks");
    for(const auto&[key,value]:forks.kv_map()){
        if(!valid(key)||std::find(saved.completed.begin(),saved.completed.end(),key)==saved.completed.end()||!forks.has<jsonxx::Array>(key))return invalid();const auto&a=forks.get<jsonxx::Array>(key);if(a.size()<2)return invalid();
        std::set<std::string>seen;
        for(unsigned i=0;i<a.size();++i){if(!a.has<jsonxx::String>(i))return invalid();const auto id=a.get<jsonxx::String>(i);
            if(!valid(id)||!seen.insert(id).second||std::find(saved.armed.begin(),saved.armed.end(),id)==saved.armed.end())return invalid();
            const auto&next=g.nodes[g.index.at(key)].next;if(std::find(next.begin(),next.end(),g.index.at(id))==next.end())return invalid();saved.forks[key].push_back(id);}
    }
    if(!json.has<jsonxx::Object>("choices"))return invalid();
    const auto&choices=json.get<jsonxx::Object>("choices");
    for(const auto&[key,value]:choices.kv_map()){
        if(!valid(key)||!choices.has<jsonxx::String>(key))return invalid();const auto id=choices.get<jsonxx::String>(key);
        if(!valid(id)||std::find(saved.completed.begin(),saved.completed.end(),key)==saved.completed.end()||std::find(saved.completed.begin(),saved.completed.end(),id)==saved.completed.end())return invalid();
        const auto&next=g.nodes[g.index.at(key)].next;if(std::find(next.begin(),next.end(),g.index.at(id))==next.end())return invalid();saved.choices[key]=id;
    }
    if(saved.completed.empty()&&(saved.armed.size()!=1||saved.armed.front()!=g.nodes[r.root].Key()||!saved.forks.empty()||!saved.choices.empty()))return invalid();
    std::unordered_set<std::string>allowed;for(auto n:r.members)for(const auto&id:g.nodes[n].Style().cpSegments)allowed.insert(id);
    const auto&reveals=json.get<jsonxx::Object>("revealedMilliseconds");
    for(const auto&[key,value]:reveals.kv_map()){
        if(saved.completed.empty()||!allowed.count(key)||!g.segments.count(key)||!reveals.has<jsonxx::Number>(key))return invalid();const auto ms=reveals.get<jsonxx::Number>(key);
        if(!std::isfinite(ms)||ms<0||ms>604800000)return invalid();saved.reveals[key]=ms/1000;
    }
    saved.signature=r.signature;return true;
}

}
void CheckpointRuntime::PrepareReload(){
    for(auto&graph:graphs)for(auto&run:graph.runs)if(!run.blocked) {
        savedRuns[{graph.packKey+"\n"+currentCharacter,graph.nodes[run.root].Key()}]=Snapshot(graph,run);
        Persist(graph,run,run.finished);
    }
    graphs.clear();markerPacks.clear();trailPacks.clear();paint.clear();mapRuns.clear();lastMap=0;lastActive=false;++generation;
}
void CheckpointRuntime::Clear(){++generation;trailPacks.clear();graphs.clear();markerPacks.clear();paint.clear();mapRuns.clear();savedRuns.clear();lastMap=0;lastActive=false;contactsPassed=resets=0;}
void CheckpointRuntime::Load(std::vector<TacoPack::Pack>&packs) {
    PrepareReload();graphs.resize(packs.size());
    std::unordered_set<std::string>knownGuids;
    for(const auto&pack:packs)for(const auto&marker:pack.markers)if(!marker.stateKey.empty())knownGuids.insert(marker.stateKey);
    for(size_t pi=0;pi<packs.size();++pi) {
        auto&pack=packs[pi];auto&graph=graphs[pi];graph.pack=&pack;
        auto path=pack.path.filename().lexically_normal().generic_u8string();graph.packKey.assign(reinterpret_cast<const char*>(path.data()),path.size());for(auto&c:graph.packKey)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto identities=pack.guidDefinitions;
        std::unordered_map<std::string,size_t> loadedIdentities;
        for(const auto&marker:pack.markers)if(!marker.stateKey.empty())++loadedIdentities[marker.stateKey];
        for(const auto&trail:pack.trails){auto key=AttributeRuntime::CanonicalGuid(trail.guid);if(!key.empty())++loadedIdentities[key];}
        for(const auto&[key,count]:loadedIdentities)identities[key]=std::max(identities[key],count);
        std::unordered_map<std::string,std::vector<Node>>definitions;
        for(auto&marker:pack.markers) {
            DefinitionWarnings(pack,marker.style,marker.guid);
            for(const auto&target:marker.style.resetGuids)if(!knownGuids.count(target))pack.diagnostics.push_back("resetguid target is not currently loaded; retained without clearing state: "+target);
            if(marker.style.cpResetConfigured)markerPacks[&marker]=pi;
            if(!marker.style.checkpoint&&!marker.style.cpFinish)continue;
            markerPacks[&marker]=pi;
            if(marker.stateKey.empty()){pack.diagnostics.push_back("Checkpoint requires a valid own GUID; marker excluded from native course");continue;}
            if(!std::isfinite(marker.position.x)||!std::isfinite(marker.position.y)||!std::isfinite(marker.position.z)){pack.diagnostics.push_back("Checkpoint has nonfinite coordinates; excluded: "+marker.stateKey);continue;}
            definitions[marker.stateKey].push_back({&marker,{},{}});
        }
        for(const auto&trail:pack.trails) {
            DefinitionWarnings(pack,trail.style,trail.guid);
            if(!trail.style.cpFinish)continue;
            auto key=AttributeRuntime::CanonicalGuid(trail.guid);
            if(key.empty()){pack.diagnostics.push_back("Finish trail requires a valid own GUID; excluded");continue;}
            Node node;node.trail=&trail;node.trailKey=key;definitions[key].push_back(std::move(node));
        }
        std::vector<std::string>keys;
        for(const auto&[key,markers]:definitions) {
            if(markers.size()!=1||identities[key]!=1){pack.diagnostics.push_back("Duplicate checkpoint GUID "+key+"; all definitions excluded from native course");continue;}
            keys.push_back(key);
        }
        std::sort(keys.begin(),keys.end());
        if(keys.size()>100000){keys.resize(100000);pack.diagnostics.push_back("Checkpoint node safety limit (100000) reached; remaining nodes excluded");}
        for(const auto&key:keys){graph.index[key]=graph.nodes.size();graph.nodes.push_back(definitions[key][0]);}
        for(size_t i=0;i<graph.nodes.size();++i) {
            auto&node=graph.nodes[i];
            for(const auto&key:node.Style().nextChecks) {
                auto found=graph.index.find(key);
                if(found==graph.index.end()){pack.diagnostics.push_back("Checkpoint "+node.Key()+": nextCheck target missing, duplicated or not a checkpoint: "+key);continue;}
                auto next=found->second;
                
                if(!node.Style().cpFinish){node.next.push_back(next);graph.nodes[next].incoming.push_back(i);}
            }
        }
        for(const auto&node:graph.nodes)if(!node.Style().cpFinish&&node.next.empty()&&(!node.Style().nextChecks.empty()||(node.Style().checkpointErrors&128u)))
            pack.diagnostics.push_back("Checkpoint has no usable successors; invalid dead end will not finish the run: "+node.Key());
        std::unordered_set<std::string>badSegments;
        for(const auto&trail:pack.trails)if(trail.style.cpTrail) {
            trailPacks[&trail]=pi;
            bool valid=trail.style.cpSegmentIds.size()==trail.strips.size() && !(trail.style.checkpointErrors&1024u);
            const auto key=AttributeRuntime::CanonicalGuid(trail.guid);valid &= !key.empty()&&identities[key]==1;
            uint32_t xmlMap=0;if(const auto*value=TacoPack::Attribute(trail.style,"mapid");value && AttributeNumbers::Unsigned(*value,xmlMap) && xmlMap && xmlMap!=trail.map)valid=false;
            if(!valid){pack.diagnostics.push_back("Invalid controlled trail GUID/segment mapping/map: "+trail.asset);continue;}
            for(size_t si=0;si<trail.strips.size();++si) {
                const auto&id=trail.style.cpSegmentIds[si];const auto&strip=trail.strips[si];double length=0;
                for(size_t i=1;i<strip.size();++i)length+=std::sqrt(Distance2(strip[i-1],strip[i]));
                if(!std::isfinite(length)||graph.segments.count(id)){badSegments.insert(id);continue;}
                graph.segments[id]={&trail,&strip,length};graph.stripIds[&strip]=id;
            }
        }
        for(const auto&id:badSegments){graph.segments.erase(id);pack.diagnostics.push_back("Duplicate/invalid checkpoint segment ID: "+id);}
        for(const auto&node:graph.nodes)for(const auto&id:node.Style().cpSegments)if(!graph.segments.count(id))pack.diagnostics.push_back("cpSegments target missing/invalid: "+id);
        std::set<size_t> roots;
        for(size_t i=0;i<graph.nodes.size();++i)if(graph.nodes[i].marker&&graph.nodes[i].Style().checkpoint&&graph.nodes[i].incoming.empty())roots.insert(i);
        for(const auto&marker:pack.markers)if(marker.style.cpResetConfigured&&!marker.style.cpReset.empty()) {
            auto root=graph.index.find(marker.style.cpReset);
            if(root==graph.index.end() || !graph.nodes[root->second].Style().checkpoint)pack.diagnostics.push_back("cpReset target is missing, duplicated or not a checkpoint: "+marker.style.cpReset);
            else roots.insert(root->second); // Explicit roots also allow cyclic components.
        }
        std::vector<size_t>owner(graph.nodes.size(),None);
        for(auto root:roots) {
            const size_t ri=graph.runs.size();graph.runs.emplace_back();auto&run=graph.runs.back();run.root=root;
            graph.roots[graph.nodes[root].Key()]=ri;
            std::vector<size_t>stack{root};std::unordered_set<size_t>visited;
            while(!stack.empty()) {
                auto node=stack.back();stack.pop_back();if(!visited.insert(node).second)continue;
                if(owner[node]!=None&&owner[node]!=ri) {
                    run.blocked=true;graph.runs[owner[node]].blocked=true;
                    pack.diagnostics.push_back("Checkpoint run roots share nodes; overlapping runs disabled: "+graph.nodes[root].Key()+" and "+graph.nodes[graph.runs[owner[node]].root].Key());
                    continue; // The existing owner already traversed this shared tail.
                }
                owner[node]=ri;run.members.push_back(node);
                for(auto next:graph.nodes[node].next)stack.push_back(next);
            }
        }
        // Report unrooted cyclic components once, with iterative traversal.
        std::vector<bool>diagnosed(graph.nodes.size(),false);
        for(size_t i=0;i<graph.nodes.size();++i)if(owner[i]==None&&!diagnosed[i]) {
            pack.diagnostics.push_back("Checkpoint component has no natural start or cpReset root; hidden until authored with a root: "+graph.nodes[i].Key());
            std::vector<size_t>stack{i};
            while(!stack.empty()){auto node=stack.back();stack.pop_back();if(diagnosed[node]||owner[node]!=None)continue;diagnosed[node]=true;
                for(auto next:graph.nodes[node].next)stack.push_back(next);for(auto previous:graph.nodes[node].incoming)stack.push_back(previous);}
        }
        for(size_t ri=0;ri<graph.runs.size();++ri){auto&run=graph.runs[ri];if(!run.blocked){
            run.signature=Signature(graph,run);Start(graph,run);
            run.expanded=false;for(auto n:run.members){const auto&style=graph.nodes[n].Style();run.expanded|=style.cpExpanded;
                if(style.cpFinish&&!style.cpReset.empty()&&style.cpReset!=graph.nodes[run.root].Key())pack.diagnostics.push_back("Combined finish/reset must target its active route root: "+graph.nodes[n].Key());}
            run.signature=Hash(Signature(graph,run));
            auto saved=savedRuns.find({graph.packKey+"\n"+currentCharacter,graph.nodes[run.root].Key()});
            if(saved!=savedRuns.end()) {
                if(saved->second.signature==run.signature)Restore(graph,run,saved->second);
                else pack.diagnostics.push_back("Checkpoint course structure changed; new run baseline established: "+graph.nodes[run.root].Key());
            }
            if(saved==savedRuns.end()) {SavedRun disk;if(ReadMemory(graph,run,disk))Restore(graph,run,disk);}
            std::set<uint32_t>maps;for(auto n:run.members)maps.insert(graph.nodes[n].Map());
            for(auto map:maps)mapRuns[map].emplace_back(pi,ri);
            // Rendering visits only routes linked to this physical segment.
            std::unordered_set<std::string>linked;
            for(auto n:run.members)for(const auto&id:graph.nodes[n].Style().cpSegments)
                if(graph.segments.count(id)&&linked.insert(id).second)graph.segmentOwners[id].push_back(ri);
        }}
    }
}
bool CheckpointRuntime::Visible(const Marker&marker){return !(marker.style.checkpoint||marker.style.cpFinish)||paint.count(&marker)!=0;}
uint32_t CheckpointRuntime::Tint(const Marker&marker,uint32_t ordinary){if(!marker.style.checkpoint&&!marker.style.cpFinish)return ordinary;auto it=paint.find(&marker);return it==paint.end()?ordinary:it->second;}
bool CheckpointRuntime::Visible(const TacoPack::Trail&trail){return trail.style.cpTrail||!trail.style.cpFinish||paint.count(&trail)!=0;}
uint32_t CheckpointRuntime::Tint(const TacoPack::Trail&trail,uint32_t ordinary){if(trail.style.cpTrail)return ordinary;auto it=paint.find(&trail);return it==paint.end()?ordinary:it->second;}
void CheckpointRuntime::UpdateContact(uint32_t map,TacoPack::Vec3 player,bool active) {
    if(!active||!map){lastActive=false;return;}
    const bool baseline=!lastActive||lastMap!=map||!std::isfinite(Distance2(previousPlayer,player))||Distance2(previousPlayer,player)>200.*200.;
    const auto previous=previousPlayer;previousPlayer=player;lastActive=true;lastMap=map;
    auto found=mapRuns.find(map);if(found==mapRuns.end())return;
    size_t chosenPack=None,chosenRun=0,chosenNode=0;double nearest=std::numeric_limits<double>::infinity();std::string chosenKey;
    for(auto[pi,ri]:found->second) {
        auto&graph=graphs[pi];auto&run=graph.runs[ri];if((run.finished&&!run.expanded)||(run.expanded&&currentCharacter.empty()))continue;
        size_t chosen=None;double near=std::numeric_limits<double>::infinity();
        for(auto node:run.armed) {
            auto&state=run.contact[node];const auto&definition=graph.nodes[node];const auto&style=definition.Style();
            if(definition.Map()!=map||!definition.Eligible()||(style.cpFinish&&!style.cpReset.empty()&&(!style.cpResetActivationAllowed||style.cpReset!=graph.nodes[run.root].Key()))){state={};continue;}
            const bool inside=Inside(definition,player);
            if(baseline||!state.initialized){state={true,inside};continue;}
            const double radius=style.cpRadius;
            const bool entered=!state.inside&&(inside||ContactDistance2(definition,previous,player)<=radius*radius);state.inside=inside;
            if(!entered||style.cpManual)continue;
            const double distance=ContactDistance2(definition,player,player);
            if(distance<near||(distance==near&&(chosen==None||definition.Key()<graph.nodes[chosen].Key()))){near=distance;chosen=node;}
        }
        if(chosen==None)continue;
        if(run.expanded)Complete(graph,run,chosen,player);
        else if(near<nearest||(near==nearest&&(chosenPack==None||graph.nodes[chosen].Key()<chosenKey))){nearest=near;chosenPack=pi;chosenRun=ri;chosenNode=chosen;chosenKey=graph.nodes[chosen].Key();}
    }
    if(chosenPack!=None)Complete(graphs[chosenPack],graphs[chosenPack].runs[chosenRun],chosenNode,player);
}
bool CheckpointRuntime::ResetFrom(const Marker&activator) {
    const auto&s=activator.style;
    if(s.cpFinish||s.cpReset.empty()||!s.cpResetActivationAllowed)return false;
    auto pack=markerPacks.find(&activator);if(pack==markerPacks.end())return false;
    auto&graph=graphs[pack->second];auto found=graph.roots.find(s.cpReset);if(found==graph.roots.end())return false;
    auto&run=graph.runs[found->second];if(run.blocked)return false;
    Start(graph,run);Persist(graph,run,true);++resets;return true;
}
CheckpointRuntime::Diagnostics CheckpointRuntime::GetDiagnostics(){
    Diagnostics out;out.contacts=contactsPassed;out.resets=resets;
    for(const auto&graph:graphs){out.nodes+=graph.nodes.size();out.runs+=graph.runs.size();for(const auto&run:graph.runs){out.blockedRuns+=run.blocked?1:0;out.completedRuns+=run.finished?1:0;}}
    return out;
}
void CheckpointRuntime::Initialize(const std::filesystem::path&directory){Clear();memoryDirectory=directory;currentCharacter.clear();cpSeconds=0;lastShard=lastInstance=0;noticePending=false;}
void CheckpointRuntime::SaveProgress(){for(auto&g:graphs)for(auto&r:g.runs)if(!r.blocked)Persist(g,r,r.finished);}
void CheckpointRuntime::SetContext(const std::string&character,uint32_t shard,uint32_t instance,double seconds){
    if(std::isfinite(seconds)&&seconds>=cpSeconds)cpSeconds=seconds;
    if(shard!=lastShard||instance!=lastInstance){lastActive=false;lastShard=shard;lastInstance=instance;}
    if(character==currentCharacter)return;
    for(auto&g:graphs)for(auto&r:g.runs)if(!r.blocked){savedRuns[{g.packKey+"\n"+currentCharacter,g.nodes[r.root].Key()}]=Snapshot(g,r);Persist(g,r,r.finished);}
    currentCharacter=character;lastActive=false;++generation;noticePending=false;
    for(auto&g:graphs)for(auto&r:g.runs)if(!r.blocked){Start(g,r);auto it=savedRuns.find({g.packKey+"\n"+currentCharacter,g.nodes[r.root].Key()});
        if(it!=savedRuns.end()&&it->second.signature==r.signature)Restore(g,r,it->second);else{SavedRun disk;if(ReadMemory(g,r,disk))Restore(g,r,disk);}}
}
double CheckpointRuntime::RevealLimit(const TacoPack::Trail&trail,const std::vector<TacoPack::Vec3>&strip,bool map){
    if(!trail.style.cpTrail||(map&&!trail.style.cpFollowsWorld))return std::numeric_limits<double>::infinity();
    const auto pi=trailPacks.find(&trail);if(pi==trailPacks.end())return 0;
    const auto&g=graphs[pi->second];const auto id=g.stripIds.find(&strip);if(id==g.stripIds.end())return 0;
    const auto seg=g.segments.find(id->second);if(seg==g.segments.end())return 0;
    double limit=0;const auto owners=g.segmentOwners.find(id->second);if(owners==g.segmentOwners.end())return 0;
    for(auto ri:owners->second){const auto&r=g.runs[ri];if(r.blocked||r.finished)continue;auto it=r.reveals.find(id->second);if(it!=r.reveals.end()){
        const double distance=!trail.style.trailDraw||trail.style.drawSpeed==0?seg->second.length:std::max(0.,cpSeconds-it->second)*trail.style.drawSpeed*1.016;
        limit=std::max(limit,std::min(distance,seg->second.length));}}
    return limit;
}
CheckpointRuntime::Prompt CheckpointRuntime::GetPrompt(bool showInfo){
    Prompt chosen;double nearest=std::numeric_limits<double>::infinity();if(!lastActive)return chosen;
    for(size_t pi=0;pi<graphs.size();++pi){const auto&g=graphs[pi];for(size_t ri=0;ri<g.runs.size();++ri){const auto&r=g.runs[ri];if(r.blocked||(r.expanded&&currentCharacter.empty()))continue;
        auto consider=[&](size_t n,bool reset,bool recent){const auto&node=g.nodes[n];const auto&s=node.Style();if(node.Map()!=lastMap||!node.Eligible()||!Inside(node,previousPlayer))return;
            if(s.cpFinish&&!s.cpReset.empty()&&(!s.cpResetActivationAllowed||s.cpReset!=g.nodes[r.root].Key()))return;
            const bool action=reset||(!recent&&r.armed.count(n)&&s.cpManual);
            const auto text=reset?std::string("Would you like to reset this checkpoint system?"):(showInfo?s.cpInfo:std::string{});
            if(!action&&CheckpointAttributes::Trim(text).empty())return;
            const auto distance=ContactDistance2(node,previousPlayer,previousPlayer);
            if(chosen.generation&&((!action&&chosen.action)||(action==chosen.action&&distance>=nearest)))return;
            nearest=distance;chosen={generation,pi,ri,n,action,reset,text,reset?"Reset Route":s.cpTriggerText,
                std::to_string(generation)+g.packKey+g.nodes[r.root].Key()+node.Key()+(reset?"reset":action?"manual":recent?"recent":"auto")+std::to_string(r.serial),r.serial};
        };
        const bool running=!r.finished&&!r.completed.empty();
        if(running&&g.nodes[r.root].Style().keepVisible)consider(r.root,true,false);
        for(auto n:r.armed)consider(n,false,false);
        if(r.recent!=None)consider(r.recent,false,true);
    }}return chosen;
}
bool CheckpointRuntime::Act(const Prompt&prompt){
    if(!prompt.action||!lastActive||prompt.generation!=generation||prompt.pack>=graphs.size())return false;
    auto&g=graphs[prompt.pack];if(prompt.run>=g.runs.size()||prompt.node>=g.nodes.size())return false;
    auto&r=g.runs[prompt.run];const auto&node=g.nodes[prompt.node];const auto&s=node.Style();
    if(r.blocked||std::find(r.members.begin(),r.members.end(),prompt.node)==r.members.end()||prompt.serial!=r.serial||(r.expanded&&currentCharacter.empty())||node.Map()!=lastMap||!node.Eligible()||!Inside(node,previousPlayer))return false;
    if(prompt.reset){if(prompt.node!=r.root||!s.keepVisible||r.finished||r.completed.empty())return false;Start(g,r);Persist(g,r,true);++resets;resetNotice=s.resetMessage;noticePending=true;return true;}
    if(!r.armed.count(prompt.node)||!s.cpManual)return false;
    if(s.cpFinish&&!s.cpReset.empty()&&(!s.cpResetActivationAllowed||s.cpReset!=g.nodes[r.root].Key()))return false;
    Complete(g,r,prompt.node,previousPlayer);return true;
}
bool CheckpointRuntime::ConsumeResetMessage(std::string&message){if(!noticePending)return false;message=resetNotice;noticePending=false;return true;}
std::string CheckpointRuntime::Status(){for(const auto&g:graphs)for(const auto&r:g.runs)if(g.nodes[r.root].Style().cpMemory&&!currentCharacter.empty()&&AppSettings::SaveFailed(MemoryPath(g,r)))return "Checkpoint memory could not be saved.";return {};}
