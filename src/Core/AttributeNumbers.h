#pragma once
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace AttributeNumbers {
inline bool Space(char c) noexcept {
    return c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\v' || c=='\f';
}
inline std::string_view Trim(std::string_view text) noexcept {
    while(!text.empty() && Space(text.front())) text.remove_prefix(1);
    while(!text.empty() && Space(text.back())) text.remove_suffix(1);
    return text;
}
// Optional XML values are normal input, not exceptions. Keep strtof's existing
// decimal/exponent/hex and locale support, but validate the complete value.
// Leave the caller's result and errno unchanged when input is absent or invalid.
inline bool Float(const std::string& text,float& result) noexcept {
    if(Trim(text).empty()) return false;
    const int previousError=errno;
    errno=0;
    char* end=nullptr;
    const float value=std::strtof(text.c_str(),&end);
    const int error=errno;
    errno=previousError;
    const char* limit=text.data()+text.size();
    if(end==text.c_str() || error==ERANGE || !std::isfinite(value)) return false;
    while(end<limit && Space(*end)) ++end;
    if(end!=limit) return false;
    result=value;
    return true;
}
inline float FloatOr(const std::string& text,float fallback) noexcept {
    float result=0;
    return Float(text,result) ? result : fallback;
}
inline bool Unsigned(std::string_view text,uint32_t& result,int base=10) noexcept {
    text=Trim(text);
    if(!text.empty() && text.front()=='+') text.remove_prefix(1);
    if(text.empty()) return false;
    uint32_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,base);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) return false;
    result=value;
    return true;
}
}
