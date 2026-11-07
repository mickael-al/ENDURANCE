#include "PathManager.hpp"
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#elif __linux__
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <iostream>
#include <cstring>
#endif

namespace fs = std::filesystem;

std::string PathManager::homeProjectPath = "";

std::string PathManager::getHomeDirectory()
{
	std::string homeDir;

#ifdef _WIN32
	char homeDrive[MAX_PATH];
	if (GetEnvironmentVariable("HOMEDRIVE", homeDrive, MAX_PATH) > 0)
	{
		homeDir = std::string(homeDrive) + "/Users/Public/Antagoniz";
	}
#elif __linux__
	const char* homePath = getenv("HOME");
	if (homePath != nullptr)
	{
		homeDir = std::string(homePath) + "/.Antagoniz";
	}
#endif

	return homeDir;
}

void PathManager::initDirectory()
{
#ifdef _WIN32
	fs::create_directories(getHomeDirectory());
#elif __linux__
	mkdir(getHomeDirectory().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
}

void PathManager::workDirectory(const char* path)
{
    fs::path executablePath;
    std::error_code error;
#ifdef _WIN32
    std::wstring buffer(256, L'\0');
    while (buffer.size() <= 32768)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            break;
        }
        if (length < buffer.size())
        {
            buffer.resize(length);
            executablePath = fs::path(buffer);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
#elif __linux__
    executablePath = fs::read_symlink("/proc/self/exe", error);
    if (error)
    {
        executablePath.clear();
    }
#endif
    if (executablePath.empty() && path != nullptr && path[0] != '\0')
    {
        error.clear();
        executablePath = fs::absolute(fs::path(path), error);
        if (error)
        {
            return;
        }
    }
    if (!executablePath.empty() && !executablePath.parent_path().empty())
    {
        error.clear();
        fs::current_path(executablePath.parent_path(), error);
    }
}

std::string & PathManager::getHomeProjectPath()
{
	return homeProjectPath;
}

void PathManager::setHomeProjectPath(std::string path)
{
	homeProjectPath = path;
}
