#ifndef ENDURANCE_STEAM_LAUNCH_ARGUMENTS_HPP
#define ENDURANCE_STEAM_LAUNCH_ARGUMENTS_HPP

#include <cstdint>
#include <cstring>
#include <limits>

// Steam passes this argument when an invitation starts a closed game.
// Keep parsing independent of the SDK; OnlineSessionManager validates the ID type.
inline std::uint64_t parseSteamLobbyLaunchArguments(int argc, const char* const* argv)
{
	if (argc <= 1 || argv == nullptr) return 0;
	std::uint64_t lobbyId = 0;
	bool found = false;
	for (int index = 1; index < argc; ++index)
	{
		if (argv[index] == nullptr) return 0;
		if (std::strcmp(argv[index], "+connect_lobby") != 0) continue;
		if (found || index + 1 >= argc) return 0;
		found = true;
		const char* value = argv[++index];
		if (value == nullptr || *value == '\0') return 0;
		for (; *value != '\0'; ++value)
		{
			if (*value < '0' || *value > '9') return 0;
			const std::uint64_t digit = static_cast<std::uint64_t>(*value - '0');
			if (lobbyId > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
				return 0;
			lobbyId = lobbyId * 10 + digit;
		}
		if (lobbyId == 0) return 0;
	}
	return lobbyId;
}

#endif
