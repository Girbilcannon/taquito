#include "ApiKeyStorage.h"
#include "AddonLifetime.h"
#include "AtomicFile.h"
#include <fstream>
#include <algorithm>
#ifdef _WIN32
#include <Windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace {
constexpr std::string_view Header="TAQUITO-API-KEY:1\n";
constexpr size_t MaxKeyBytes=511,MaxFileBytes=16384;
void Erase(std::string& value) {
    if(!value.empty()) {
#ifdef _WIN32
        SecureZeroMemory(value.data(),value.size());
#else
        volatile char* bytes=value.data();for(size_t i=0;i<value.size();++i)bytes[i]=0;
#endif
    }
    value.clear();
}
struct SecretText {std::string value;~SecretText(){Erase(value);}};
struct State {
    std::filesystem::path path;
    std::string key,status;
    ~State(){Erase(key);}
};
AddonLifetime::Storage<State> storage;
State& state=storage.Get();
bool Valid(std::string_view key) {
    return !key.empty() && key.size()<=MaxKeyBytes &&
        std::all_of(key.begin(),key.end(),[](unsigned char c){return c>=33 && c<=126;});
}
std::string_view Trim(std::string_view value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos)return {};
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
#ifdef _WIN32
struct LocalBlob {
    DATA_BLOB data{};
    ~LocalBlob(){if(data.pbData){SecureZeroMemory(data.pbData,data.cbData);LocalFree(data.pbData);}}
};
bool Protect(std::string_view plain,std::string& encrypted) {
    DATA_BLOB input{static_cast<DWORD>(plain.size()),reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    LocalBlob output;
    if(!CryptProtectData(&input,L"Taquito API key",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output.data))return false;
    encrypted.assign(reinterpret_cast<const char*>(output.data.pbData),output.data.cbData);
    return true;
}
bool Unprotect(std::string_view encrypted,std::string& plain) {
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.data()))};
    LocalBlob output;
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output.data))return false;
    if(!output.data.cbData || output.data.cbData>MaxKeyBytes)return false;
    plain.assign(reinterpret_cast<const char*>(output.data.pbData),output.data.cbData);
    return Valid(plain);
}
#else
// The released addon is a Windows DLL, including under Wine. Portable checks
// must never introduce an unencrypted fallback when OS protection is absent.
bool Protect(std::string_view,std::string&){return false;}
bool Unprotect(std::string_view,std::string&){return false;}
#endif
}

void ApiKeyStorage::Initialize(const std::filesystem::path& directory) {
    Shutdown();state.path=directory/"api-key.dat";
    std::error_code ec;
    const bool exists=std::filesystem::exists(state.path,ec);
    if(ec){state.status="Could not access the saved API key.";return;}
    if(!exists)return;
    const auto size=std::filesystem::file_size(state.path,ec);
    if(ec || size<=Header.size() || size>MaxFileBytes){state.status="Could not read the saved API key. Save a replacement or remove it.";return;}
    std::ifstream input(state.path,std::ios::binary);
    std::string bytes(static_cast<size_t>(size),'\0');
    if(!input.read(bytes.data(),static_cast<std::streamsize>(bytes.size())) || !bytes.starts_with(Header)) {
        state.status="Could not read the saved API key. Save a replacement or remove it.";return;
    }
    SecretText plain;
    if(!Unprotect(std::string_view(bytes).substr(Header.size()),plain.value)) {
        state.status="Could not unlock the saved API key. Save it again in this Windows account or Wine prefix.";return;
    }
    state.key=plain.value;state.status="API key saved.";
}
void ApiKeyStorage::Shutdown(){Erase(state.key);state.path.clear();state.status.clear();}
bool ApiKeyStorage::HasKey(){return !state.key.empty();}
std::string ApiKeyStorage::CopyKey(){return state.key;}
const std::string& ApiKeyStorage::Status(){return state.status;}
bool ApiKeyStorage::Save(std::string_view value) {
    value=Trim(value);
    if(!Valid(value)){state.status="Enter an API key without spaces (up to 511 characters).";return false;}
    if(state.path.empty()){state.status="API key storage is not ready.";return false;}
    std::string encrypted;
    if(!Protect(value,encrypted)){state.status="Could not protect the API key. It was not saved.";return false;}
    const std::string body=std::string(Header)+encrypted;
    if(!AtomicFile::Write(state.path,body)){state.status="Could not save the API key. The previous key is unchanged.";return false;}
    // Callers pass their edit buffer, not state.key's own storage.
    Erase(state.key);state.key.assign(value);state.status="API key saved.";return true;
}
bool ApiKeyStorage::Remove() {
    if(state.path.empty()){state.status="API key storage is not ready.";return false;}
    auto temp=state.path;temp+=".tmp";
    // Remove an interrupted encrypted save as well, so no stale key is retained.
    for(const auto& file:{temp,state.path}) {
        std::error_code ec;std::filesystem::remove(file,ec);
        if(ec){state.status="Could not remove the saved API key.";return false;}
    }
    Erase(state.key);state.status="API key removed.";return true;
}
