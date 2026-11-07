#include "LobbyLanguage.hpp"

#include <cassert>
#include <iostream>
#include <set>
#include <string>

int main()
{
    std::set<std::string> codes;
    for (const auto& option : LobbyLanguage::options)
    {
        assert(option.code[0] && option.badge[0]);
        assert(option.englishName[0] && option.frenchName[0]);
        assert(codes.insert(option.code).second);
        assert(LobbyLanguage::normalize(option.code) == option.code);
        assert(std::string(LobbyLanguage::badge(option.code)) == option.badge);
        assert(std::string(LobbyLanguage::label(option.code, false)) == option.englishName);
        assert(std::string(LobbyLanguage::label(option.code, true)) == option.frenchName);
        assert(LobbyLanguage::matchesFilter(option.code, ""));
        assert(LobbyLanguage::matchesFilter(option.code, option.code));
        assert(!LobbyLanguage::matchesFilter(option.code, "?"));
        assert(LobbyLanguage::isPreferred(option.code, option.code));
        assert(!LobbyLanguage::isPreferred(option.code, ""));
    }
    for (const auto& bad : {std::string(), std::string("FR"), std::string("fr "),
        std::string("unknown"), std::string(1000, 'a'), std::string("fr\0en", 5)})
    {
        assert(LobbyLanguage::normalize(bad).empty());
        assert(std::string(LobbyLanguage::badge(bad)) == "?");
        assert(LobbyLanguage::matchesFilter(bad, "?"));
        assert(!LobbyLanguage::matchesFilter(bad, "fr"));
    }
    assert(std::string(LobbyLanguage::defaultCode(true)) == "fr");
    assert(std::string(LobbyLanguage::defaultCode(false)) == "en");
    assert(LobbyLanguage::normalize("pt-BR") == "pt-BR");
    assert(LobbyLanguage::normalize("multi") == "multi");
    assert(!LobbyLanguage::isPreferred("fr", "en"));
    std::cout << "PASS: lobby language catalog, normalization, unknown tags, labels and filtering\n";
}
