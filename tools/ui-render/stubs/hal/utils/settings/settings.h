#pragma once
#include <string>
#include <map>
#include <cstring>
extern bool preview_analog;
#define ESP_OK 0
struct Settings {
    inline static std::map<std::string,std::string> storage;
    std::string ns;
    Settings(const std::string& name,bool=false):ns(name){}
    int GetInt(const char* key,int fallback=0){if(std::string(key)=="analog")return preview_analog;auto s=GetString(key);return s.empty()?fallback:std::stoi(s);}
    std::string GetString(const char* key,const std::string& fallback=""){auto it=storage.find(ns+key);return it==storage.end()?fallback:it->second;}
    void SetInt(const char* key,int value){storage[ns+key]=std::to_string(value);}
    bool GetBool(const char* key,bool fallback=false){return GetInt(key,fallback);}
    void SetBool(const char* key,bool value){SetInt(key,value);}
    int GetBlob(const char*,void*,size_t*){return -1;}
    int SetBlob(const char*,const void*,size_t){return 0;}
};
