// A quick tour of EasyVDF: build a document, read values, modify it, save it
// as text and as binary, and read it back.
//
// Usage: quick_tour
// Writes quick_tour.vdf and quick_tour.vbkv in the current directory.

#include <EasyVDF.h>

#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    try
    {
        // ---------------------------------------------------------------- build
        // A document is a KeyValue: a root key and its object. Braces nest objects.
        EasyVDF::KeyValue manifest("AppState", {
            {"appid",      480},
            {"name",       "Spacewar"},
            {"SizeOnDisk", uint64_t(26843545600)},
            {"UserConfig", {
                {"language", "english"},
            }},
        });

        // operator[] creates what is missing when you assign to it.
        manifest["InstalledDepots"]["481"]["manifest"] = "7707273133538421307";
        manifest["InstalledDepots"]["481"]["size"]     = uint64_t(1024);

        // Duplicate keys are legal in VDF: add() always appends.
        manifest.add("dll", "steam_api.dll");
        manifest.add("dll", "steam_api64.dll");

        std::cout << "--- Document as text ---\n" << manifest << '\n';

        // ---------------------------------------------------------------- read
        // as<T>() converts (and throws EasyVDF::TypeError when it can't).
        const auto appid = manifest["appid"].as<uint32_t>();
        const auto name  = manifest["name"].as<std::string>();

        // value_or() never throws: missing keys chain safely and give the default.
        const std::string language = manifest["UserConfig"]["language"].value_or("english");
        const int         flags    = manifest["StateFlags"].value_or(4);

        // try_as<T>() gives a std::optional.
        if (auto size = manifest["SizeOnDisk"].try_as<uint64_t>())
            std::cout << name << " (" << appid << ") uses " << *size << " bytes\n";

        std::cout << "language: " << language << ", flags: " << flags << '\n';

        // find() returns a pointer, nullptr when the key is missing.
        if (const EasyVDF::KeyValue* depot = manifest["InstalledDepots"].find("481"))
            std::cout << "depot " << depot->key() << " manifest " << (*depot)["manifest"].as<std::string>() << '\n';

        // Children are KeyValue: structured bindings give the key and the value.
        for (const auto& [depot_id, depot] : manifest["InstalledDepots"])
            std::cout << "depot " << depot_id << ": " << depot["size"].as<uint64_t>() << " bytes\n";

        // all() iterates every value of a duplicate key.
        for (const auto& dll : manifest.all("dll"))
            std::cout << "dll: " << dll.as<std::string>() << '\n';

        // ---------------------------------------------------------------- modify
        manifest["UserConfig"]["language"] = "french";   // replace a value
        manifest["StateFlags"] = 4;                      // add a key
        manifest.erase("dll");                           // remove every "dll"

        // ---------------------------------------------------------------- save and reload
        manifest.save("quick_tour.vdf");                              // text
        manifest.save("quick_tour.vbkv", EasyVDF::Format::Vbkv);     // binary, "VBKV" + CRC32 header

        // load() detects the format.
        const EasyVDF::KeyValue from_text   = EasyVDF::load("quick_tour.vdf");
        const EasyVDF::KeyValue from_binary = EasyVDF::load("quick_tour.vbkv");

        // Text does not store types: everything reads back as strings, as<T>() converts them.
        std::cout << "\nfrom text:   " << from_text["UserConfig"]["language"].as<std::string>()
                  << ", appid " << from_text["appid"].as<int>()
                  << " (stored as " << (from_text["appid"].type() == EasyVDF::Type::String ? "String" : "?") << ")\n";
        // Binary keeps them.
        std::cout << "from binary: " << from_binary["UserConfig"]["language"].as<std::string>()
                  << ", appid " << from_binary["appid"].as<int>()
                  << " (stored as " << (from_binary["appid"].type() == EasyVDF::Type::Int32 ? "Int32" : "?") << ")\n";

        // ---------------------------------------------------------------- errors
        try
        {
            EasyVDF::parse("\"root\"\n{\n    \"key\" \"value\"\n");   // the closing brace is missing
        }
        catch (const EasyVDF::ParseError& e)
        {
            std::cout << "\nparse error at line " << e.line() << ", column " << e.column() << ": " << e.what() << '\n';
        }

        return 0;
    }
    catch (const EasyVDF::Error& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
