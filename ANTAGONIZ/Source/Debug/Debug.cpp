#include "Debug.hpp"
#include "Console.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include "termcolor/termcolor.hpp"

namespace Ge
{
    namespace
    {
        // Shader diagnostics can be much longer than 1 KiB. Size the message
        // before formatting, and keep its storage local to this call.
        std::string formatMessage(const char* prefix, const char* format, va_list arguments)
        {
            if (!format) return std::string(prefix) + "(null log format)\n";
            va_list measure;
            va_copy(measure, arguments);
            const int length = std::vsnprintf(nullptr, 0, format, measure);
            va_end(measure);
            if (length < 0) return std::string(prefix) + "(log formatting failed)\n";

            std::string message(prefix);
            const std::size_t offset = message.size();
            message.resize(offset + static_cast<std::size_t>(length) + 1);
            const int written = std::vsnprintf(&message[offset],
                static_cast<std::size_t>(length) + 1, format, arguments);
            if (written < 0 || written != length)
                return std::string(prefix) + "(log formatting failed)\n";
            message.back() = '\n';
            return message;
        }
    }
		Console * Debug::console = nullptr;
        void Debug::Log(const char *format, ...)
        {
                va_list argList;
                va_start(argList, format);
                const auto message = formatMessage("[ENGINE][USER] : ", format, argList);
                va_end(argList);

                std::cout << message;
				if (console != nullptr)
				{
					console->setBaseColor(ImColor(1.0f, 1.0f, 1.0f, 1.0f));
					console->AddLog("%s", message.c_str());
				}
        }

        void Debug::Error(const char *format, ...)
        {
                va_list argList;
                va_start(argList, format);
                const auto message = formatMessage("[ENGINE][ERROR] : ", format, argList);
                va_end(argList);

                std::cerr << termcolor::red << message << termcolor::reset;
				if (console != nullptr)
				{
					console->setBaseColor(ImColor(0.5f, 0.0f, 0.0f, 1.0f));
					console->AddLog("%s", message.c_str());
				}
                std::cin.ignore();
        }

        void Debug::INITSUCCESS(const char *format)
        {
            Debug::Info("Initialisation : %s", format);
        }

        void Debug::RELEASESUCCESS(const char *format)
        {
            Debug::Info("Liberation : %s", format);
        }

        void Debug::INITFAILED(const char *format)
        {
            Debug::Error("Erreur lors de l'initialisation : %s", format);
        }

        void Debug::Warn(const char *format, ...)
        {
                va_list argList;
                va_start(argList, format);
                const auto message = formatMessage("[ENGINE][WARN] : ", format, argList);
                va_end(argList);

                std::cout << termcolor::yellow << message << termcolor::reset;
				if (console != nullptr)
				{
					console->setBaseColor(ImColor(0.5f, 0.5f, 0.0f, 1.0f));
					console->AddLog("%s", message.c_str());
				}
        }

        void Debug::Info(const char *format, ...)
        {
                va_list argList;
                va_start(argList, format);
                const auto message = formatMessage("[ENGINE][INFO] : ", format, argList);
                va_end(argList);

                std::cout << termcolor::cyan << message << termcolor::reset;
				if (console != nullptr)
				{
					console->setBaseColor(ImColor(0.0f, 0.4f, 0.4f, 1.0f));
					console->AddLog("%s", message.c_str());
				}
        }

        void Debug::VLayer(const char *format, ...)
        {
                va_list argList;
                va_start(argList, format);
                const auto message = formatMessage("[ENGINE][VLAYER] : ", format, argList);
                va_end(argList);

                std::cout << termcolor::magenta << message << termcolor::reset;
				if (console != nullptr)
				{
					console->setBaseColor(ImColor(0.25f, 0.0f, 0.5f, 1.0f));
					console->AddLog("%s", message.c_str());
				}
        }

        void Debug::TempFile(std::string data, const char* path)
        {
            try
            {
                std::ofstream tempFile(path, std::ios::out | std::ios::app);

                if (!tempFile.is_open())
                {
                    std::cerr << "Impossible d'ouvrir le fichier temporaire pour ecriture." << std::endl;
                    return;
                }

                //tempFile << data;
				Debug::Error("TODO Fix TempFile Debug");
                tempFile.close();

                std::cout << "Fichier temporaire cree avec succes a : " << path << std::endl;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Erreur lors de la création du fichier temporaire : " << e.what() << std::endl;
            }
        }
}
