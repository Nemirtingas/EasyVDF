// Lists the achievements and stats of a Steam stats schema, a binary VDF file
// (Steam/appcache/stats/UserGameStatsSchema_<appid>.bin).
//
// Usage: achievements <schema file> [language]
//   achievements UserGameStatsSchema_480.bin french

#include <EasyVDF.h>

#include <iostream>
#include <string>

namespace {

// Localized texts are objects: { "english" "...", "french" "...", "token" "..." }.
// Some schemas store a plain string instead: value_or() handles both.
std::string Localized(const EasyVDF::Node& texts, const std::string& language)
{
    if (texts.is_object())
        return texts[language].value_or(texts["english"].value_or(""));
    return texts.value_or("");
}

void PrintAchievements(const EasyVDF::Node& bits, const std::string& language)
{
    for (const auto& [bit, achievement] : bits)
    {
        const EasyVDF::Node& display = achievement["display"];
        const bool hidden = display["hidden"].value_or(false);

        std::cout << "  [" << bit << "] " << achievement["name"].value_or("?")
                  << (hidden ? " (hidden)" : "") << '\n'
                  << "      " << Localized(display["name"], language)
                  << " - " << Localized(display["desc"], language) << '\n'
                  << "      icon: " << display["icon"].value_or("") << '\n';
    }
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <schema file> [language]\n";
        return 2;
    }
    const std::string language = argc > 2 ? argv[2] : "english";

    try
    {
        // The root key is the appid; the format (text or binary) is detected.
        const EasyVDF::KeyValue schema = EasyVDF::load(argv[1]);

        std::cout << schema["gamename"].value_or("Unknown game") << " (appid " << schema.key()
                  << ", schema version " << schema["version"].value_or(0) << ")\n";

        // Each entry of "stats" is either a block of achievements (type "ACHIEVEMENTS",
        // each bit of the stat is an achievement) or a single stat (type 1 = int, 2 = float, 3 = average rate).
        for (const auto& [id, stat] : schema["stats"])
        {
            const std::string type = stat["type"].value_or("");
            if (type == "ACHIEVEMENTS" || type == "4")
            {
                std::cout << "\nAchievements (stat " << id << "):\n";
                PrintAchievements(stat["bits"], language);
            }
            else
            {
                const char* kind = type == "1" ? "int" : type == "2" ? "float" : type == "3" ? "avgrate" : "?";
                std::cout << "\nStat " << id << ": " << stat["name"].value_or("?") << " (" << kind << ")"
                          << ", \"" << Localized(stat["display"]["name"], language) << "\""
                          << ", default " << stat["default"].value_or("0") << '\n';
            }
        }
        return 0;
    }
    catch (const EasyVDF::ParseError& e)
    {
        std::cerr << argv[1] << ": invalid VDF: " << e.what() << '\n';
    }
    catch (const EasyVDF::Error& e)
    {
        std::cerr << argv[1] << ": " << e.what() << '\n';
    }
    return 1;
}
