// A quick tour of EasyVDF: build a document, read values, modify it, save it
// as text and as binary, read it back, and handle errors.
//
// Usage: quick_tour
// Writes quick_tour.vdf and quick_tour.vbkv in the current directory.

#include <EasyVDF.h>

#include <cstdint>
#include <iostream>
#include <string>

int main()
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
    // value_or() is the simplest way: missing keys chain safely and give the default.
    const std::string language = manifest["UserConfig"]["language"].value_or("english");
    const int         flags    = manifest["StateFlags"].value_or(4);
    std::cout << "language: " << language << ", flags: " << flags << '\n';

    // as<T>() returns a Result: the value, or the reason it can't be converted.
    const EasyVDF::Result<uint32_t> appid = manifest["appid"].as<uint32_t>();
    if (appid)
        std::cout << "appid " << *appid << '\n';

    const EasyVDF::Result<int> bad = manifest["name"].as<int>();
    if (!bad)
        std::cout << "name is not a number: " << bad.error().message << '\n';

    // try_as<T>() gives a std::optional.
    if (auto size = manifest["SizeOnDisk"].try_as<uint64_t>())
        std::cout << manifest["name"].value_or("?") << " uses " << *size << " bytes\n";

    // find() returns a pointer, nullptr when the key is missing; at() a Result.
    if (const EasyVDF::KeyValue* depot = manifest["InstalledDepots"].find("481"))
        std::cout << "depot " << depot->key() << " manifest " << (*depot)["manifest"].value_or("") << '\n';
    if (auto config = manifest.at("UserConfig"))
        std::cout << "UserConfig has " << config->size() << " key(s)\n";

    // Children are KeyValue: structured bindings give the key and the value.
    for (const auto& [depot_id, depot] : manifest["InstalledDepots"])
        std::cout << "depot " << depot_id << ": " << depot["size"].value_or<uint64_t>(0) << " bytes\n";

    // all() iterates every value of a duplicate key.
    for (const auto& dll : manifest.all("dll"))
        std::cout << "dll: " << dll.value_or("") << '\n';

    // ---------------------------------------------------------------- modify
    manifest["UserConfig"]["language"] = "french";   // replace a value
    manifest["StateFlags"] = 4;                      // add a key
    manifest.erase("dll");                           // remove every "dll"

    // ---------------------------------------------------------------- save and reload
    // save() returns a Result<void>: test it like any other Result.
    for (auto saved : {manifest.save("quick_tour.vdf"),                             // text
                       manifest.save("quick_tour.vbkv", EasyVDF::Format::Vbkv)})    // "VBKV" + CRC32 header
    {
        if (!saved)
        {
            std::cerr << "error: " << saved.error() << '\n';
            return 1;
        }
    }

    // load() detects the format.
    const EasyVDF::Result<EasyVDF::KeyValue> from_text   = EasyVDF::load("quick_tour.vdf");
    const EasyVDF::Result<EasyVDF::KeyValue> from_binary = EasyVDF::load("quick_tour.vbkv");
    if (!from_text || !from_binary)
    {
        std::cerr << "error: " << (from_text ? from_binary.error() : from_text.error()) << '\n';
        return 1;
    }

    // Text does not store types: everything reads back as strings, as<T>() converts them.
    std::cout << "\nfrom text:   " << (*from_text)["UserConfig"]["language"].value_or("")
              << ", appid " << (*from_text)["appid"].value_or(0)
              << " (stored as " << ((*from_text)["appid"].type() == EasyVDF::Type::String ? "String" : "?") << ")\n";
    // Binary keeps them.
    std::cout << "from binary: " << (*from_binary)["UserConfig"]["language"].value_or("")
              << ", appid " << (*from_binary)["appid"].value_or(0)
              << " (stored as " << ((*from_binary)["appid"].type() == EasyVDF::Type::Int32 ? "Int32" : "?") << ")\n";

    // ---------------------------------------------------------------- errors
    const EasyVDF::Result<EasyVDF::KeyValue> broken = EasyVDF::parse("\"root\"\n{\n    \"key\" \"value\"\n");   // the closing brace is missing
    if (!broken)
    {
        const EasyVDF::ErrorInfo& error = broken.error();
        std::cout << "\nparse error [" << error.code << "] at line " << error.line << ": " << error.message << '\n';
    }

    // and_then() chains operations that can fail: the first error stops the chain.
    const EasyVDF::Result<std::string> depot_manifest = manifest.at("InstalledDepots")
        .and_then([](const EasyVDF::Node& depots) { return depots.at("482"); })
        .and_then([](const EasyVDF::Node& depot) { return depot["manifest"].as<std::string>(); });
    std::cout << "depot 482: " << (depot_manifest ? *depot_manifest : depot_manifest.error().message) << '\n';
    return 0;
}
