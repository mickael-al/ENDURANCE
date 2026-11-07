#include "SteamLaunchArguments.hpp"

#include <cassert>
#include <initializer_list>
#include <iostream>

namespace
{
	std::uint64_t parse(std::initializer_list<const char*> args)
	{
		return parseSteamLobbyLaunchArguments(static_cast<int>(args.size()), args.begin());
	}
}

int main()
{
	constexpr std::uint64_t lobby = 109775243434946019ULL;
	assert(parse({"game.exe", "+connect_lobby", "109775243434946019"}) == lobby);
	assert(parse({"game.exe", "-windowed", "+connect_lobby", "109775243434946019", "-debug"}) == lobby);
	assert(parse({"game.exe", "+connect_lobby", "000109775243434946019"}) == lobby);
	assert(parse({"game.exe", "+connect_lobby", "18446744073709551615"}) == (std::numeric_limits<std::uint64_t>::max)());
	assert(parse({"game.exe"}) == 0);
	assert(parse({"game.exe", "+connect_lobby"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", ""}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "0"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "-1"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "+1"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", " 123"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "123 "}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "123bad"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "18446744073709551616"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "999999999999999999999999999999999999"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", "123", "+connect_lobby", "456"}) == 0);
	assert(parse({"game.exe", "+connect_lobby", nullptr}) == 0);
	assert(parse({"game.exe", nullptr}) == 0);
	assert(parseSteamLobbyLaunchArguments(3, nullptr) == 0);
	assert(parseSteamLobbyLaunchArguments(-1, nullptr) == 0);
	std::cout << "PASS: 20 Steam invitation launch argument cases\n";
}
