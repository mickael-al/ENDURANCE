#pragma once

#include "PathManager.hpp"
#include "json_struct.h"

#include <filesystem>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

// Settings and progress are written outside the project directory. Replacing a
// save is transactional from the game's point of view: the previous version
// stays available as a backup until a new file has been installed.
class SaveSystem
{
public:
    template <typename T>
    static bool save(const T& data, const std::string& filename)
    {
        try
        {
            const std::filesystem::path path = filePath(filename);
            std::filesystem::create_directories(path.parent_path());
            const std::filesystem::path temporary(path.string() + ".tmp");
            const std::filesystem::path backup(path.string() + ".bak");
            const std::string json = JS::serializeStruct(data);

            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) return false;
                output << json;
                output.flush();
                if (!output.good()) return false;
            }

            std::error_code error;
            const bool hadSave = std::filesystem::exists(path);
            if (hadSave)
            {
                std::filesystem::remove(backup, error);
                if (error) return false;
                std::filesystem::rename(path, backup, error);
                if (error) return false;
            }

            std::filesystem::rename(temporary, path, error);
            if (!error) return true;

            if (hadSave)
            {
                std::error_code restoreError;
                std::filesystem::rename(backup, path, restoreError);
            }
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    template <typename T>
    static bool load(T& data, const std::string& filename)
    {
        try
        {
            const std::filesystem::path path = filePath(filename);
            for (const std::filesystem::path& candidatePath :
                 {path, std::filesystem::path(path.string() + ".bak")})
            {
                std::ifstream input(candidatePath, std::ios::binary);
                if (!input) continue;
                std::ostringstream buffer;
                buffer << input.rdbuf();
                if (input.bad()) continue;

                T candidate = data;
                const std::string json = buffer.str();
                JS::ParseContext context(json);
                if (context.parseTo(candidate) == JS::Error::NoError)
                {
                    data = std::move(candidate);
                    return true;
                }
            }
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    static std::filesystem::path filePath(const std::string& filename)
    {
        return std::filesystem::path(PathManager::getHomeDirectory()) /
            "ENDURANCE" / filename;
    }
};
