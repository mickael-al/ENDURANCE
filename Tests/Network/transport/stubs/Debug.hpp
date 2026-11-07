#pragma once
namespace Ge { struct Debug { template<class... T> static void Log(const char*,T...){} template<class... T> static void Info(const char*,T...){} template<class... T> static void Warn(const char*,T...){} template<class... T> static void Error(const char*,T...){} }; }
