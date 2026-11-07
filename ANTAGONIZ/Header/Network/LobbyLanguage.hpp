#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Lobby conversation languages are independent of the game's translated UI.
// Only catalogued codes are accepted from Steam metadata; legacy lobbies remain
// visible with an unspecified language instead of being guessed as English.
namespace LobbyLanguage
{
	struct Option
	{
		const char* code;
		const char* badge;
		const char* englishName;
		const char* frenchName;
	};

	inline constexpr Option options[] = {
		{ "en", "EN", "English", "Anglais" },
		{ "fr", "FR", "French", "Français" },
		{ "de", "DE", "German", "Allemand" },
		{ "es", "ES", "Spanish", "Espagnol" },
		{ "it", "IT", "Italian", "Italien" },
		{ "pt", "PT", "Portuguese", "Portugais" },
		{ "pt-BR", "PT-BR", "Portuguese (Brazil)", "Portugais (Brésil)" },
		{ "nl", "NL", "Dutch", "Néerlandais" },
		{ "pl", "PL", "Polish", "Polonais" },
		{ "ru", "RU", "Russian", "Russe" },
		{ "uk", "UK", "Ukrainian", "Ukrainien" },
		{ "tr", "TR", "Turkish", "Turc" },
		{ "cs", "CS", "Czech", "Tchèque" },
		{ "da", "DA", "Danish", "Danois" },
		{ "fi", "FI", "Finnish", "Finnois" },
		{ "sv", "SV", "Swedish", "Suédois" },
		{ "no", "NO", "Norwegian", "Norvégien" },
		{ "el", "EL", "Greek", "Grec" },
		{ "hu", "HU", "Hungarian", "Hongrois" },
		{ "ro", "RO", "Romanian", "Roumain" },
		{ "bg", "BG", "Bulgarian", "Bulgare" },
		{ "ar", "AR", "Arabic", "Arabe" },
		{ "ja", "JA", "Japanese", "Japonais" },
		{ "ko", "KO", "Korean", "Coréen" },
		{ "zh-CN", "ZH-CN", "Chinese (Simplified)", "Chinois simplifié" },
		{ "zh-TW", "ZH-TW", "Chinese (Traditional)", "Chinois traditionnel" },
		{ "th", "TH", "Thai", "Thaï" },
		{ "vi", "VI", "Vietnamese", "Vietnamien" },
		{ "id", "ID", "Indonesian", "Indonésien" },
		{ "multi", "MULTI", "Multilingual", "Multilingue" }
	};
	inline constexpr std::size_t optionCount = sizeof(options) / sizeof(options[0]);
	inline constexpr Option unspecified{ "", "?", "Unspecified", "Non précisée" };

	inline const Option& display(std::string_view code)
	{
		for (const Option& option : options)
		{
			if (code == option.code) return option;
		}
		return unspecified;
	}

	inline std::string normalize(std::string_view code)
	{
		return display(code).code;
	}

	inline const char* defaultCode(bool french)
	{
		return french ? "fr" : "en";
	}

	inline const char* label(std::string_view code, bool french)
	{
		const Option& option = display(code);
		return french ? option.frenchName : option.englishName;
	}

	inline const char* badge(std::string_view code)
	{
		return display(code).badge;
	}

	inline bool isPreferred(std::string_view language, std::string_view preferred)
	{
		return !preferred.empty() && language == preferred;
	}

	inline bool matchesFilter(std::string_view language, std::string_view filter)
	{
		if (filter.empty()) return true;
		const Option& option = display(language);
		if (filter == "?") return option.code[0] == '\0';
		return filter == option.code;
	}
}
