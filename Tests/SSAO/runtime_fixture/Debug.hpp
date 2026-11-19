#pragma once
#include <cstdio>
namespace Ge { struct Debug {
    static void INITSUCCESS(const char*) {}
    static void INITFAILED(const char*) {}
    template<class... Args> static void Error(const char* format,Args... args) { std::fprintf(stderr,format,args...); }
}; }
