// The examples of README.md, compiled and checked: keep both in sync.
#include "../EasyVDF.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "catch.hpp"

namespace {

const char* const AppManifest = R"("AppState"
{
	"appid"		"440"
	"Universe"		"1"
	"name"		"Team Fortress 2"
	"StateFlags"		"4"
	"installdir"		"Team Fortress 2"
	"SizeOnDisk"		"26843545600"
	"buildid"		"12345678"
	"LastOwner"		"76561198000000000"
	"AutoUpdateBehavior"		"0"
	"InstalledDepots"
	{
		"441"
		{
			"manifest"		"7707273133538421307"
			"size"		"432902423"
		}
	}
	"MountedDepots"
	{
		"441"		"7707273133538421307"
	}
	"UserConfig"
	{
		"language"		"english"
	}
}
)";

const char* const LibraryFolders = R"("libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"apps"
		{
			"228980"		"455893076"
			"440"		"26843545600"
		}
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
		"apps"
		{
			"730"		"35123456789"
		}
	}
}
)";

struct TempFile
{
    std::string path;

    explicit TempFile(std::string name) : path(std::move(name)) {}
    ~TempFile() { std::remove(path.c_str()); }
};

}

TEST_CASE("README: quick start", "[readme]")
{
    TempFile file("readme_appmanifest_440.acf");
    EasyVDF::parse(AppManifest).save(file.path);

    auto manifest = EasyVDF::load(file.path);

    std::string name = manifest["name"].as<std::string>();
    int         id   = manifest["appid"].as<int>();
    std::string lang = manifest["UserConfig"]["language"].value_or("english");

    CHECK(name == "Team Fortress 2");
    CHECK(id == 440);
    CHECK(lang == "english");

    manifest["UserConfig"]["language"] = "french";
    manifest.save(file.path);

    CHECK(EasyVDF::load(file.path)["UserConfig"]["language"].as<std::string>() == "french");
}

TEST_CASE("README: reading", "[readme]")
{
    EasyVDF::KeyValue a = EasyVDF::parse(R"("root" { "key" "value" })");
    CHECK(a.key() == "root");
    CHECK(a["key"].as<std::string>() == "value");

    std::istringstream input(R"("root" { "key" "value" })");
    EasyVDF::KeyValue b = EasyVDF::parse(input);
    CHECK(a == b);

    TempFile file("readme_installscript.vdf");
    {
        std::ofstream out(file.path, std::ios::binary);
        out << "\"InstallScript\"\n{\n\t\"Run Process\"\n\t{\n\t}\n}\n\"kvsignatures\"\n{\n\t\"InstallScript\"\t\t\"4284ab2e\"\n}\n";
    }
    CHECK_THROWS_AS(EasyVDF::load(file.path), EasyVDF::ParseError);

    std::ostringstream out;
    std::vector<EasyVDF::KeyValue> roots = EasyVDF::load_all(file.path);
    for (auto& root : roots)
        out << root.key() << '\n';
    CHECK(out.str() == "InstallScript\nkvsignatures\n");
}

TEST_CASE("README: getting values", "[readme]")
{
    auto manifest = EasyVDF::parse(AppManifest);
    std::ostringstream out;

    CHECK(manifest.key() == "AppState");

    auto appid = manifest["appid"].as<uint32_t>();
    auto name  = manifest["name"].as<std::string>();
    auto size  = manifest["SizeOnDisk"].as<uint64_t>();
    CHECK(appid == 440u);
    CHECK(name == "Team Fortress 2");
    CHECK(size == 26843545600ull);

    bool        updating = manifest["AutoUpdateBehavior"].value_or(0) != 0;
    std::string language = manifest["UserConfig"]["language"].value_or("english");
    uint64_t    build    = manifest["buildid"].value_or<uint64_t>(0);
    CHECK_FALSE(updating);
    CHECK(language == "english");
    CHECK(build == 12345678u);

    if (auto owner = manifest["LastOwner"].try_as<uint64_t>())
        out << "owner: " << *owner << '\n';

    if (manifest["InstalledDepots"]["441"])
        out << "depot 441 is installed\n";
    if (auto* depot = manifest["InstalledDepots"].find("441"))
        out << depot->key() << ": " << (*depot)["manifest"].as<std::string>() << '\n';

    CHECK(out.str() == "owner: 76561198000000000\ndepot 441 is installed\n441: 7707273133538421307\n");

    CHECK_NOTHROW(manifest.at("appid"));
    CHECK_THROWS_AS(manifest.at("nope"), EasyVDF::KeyError);
    CHECK(manifest.contains("appid"));
    CHECK_FALSE(manifest["InstalledDepots"]["999"]);
    CHECK_THROWS_AS(manifest["name"].as<int>(), EasyVDF::TypeError);

    // Reading never changed the document.
    CHECK(manifest.to_text() == AppManifest);
}

TEST_CASE("README: iterating", "[readme]")
{
    auto folders = EasyVDF::parse(LibraryFolders);
    std::ostringstream out;

    for (auto& [index, folder] : folders)
    {
        out << index << ": " << folder["path"].value_or("?") << '\n';

        for (auto& [appid, size] : folder["apps"])
            out << "  app " << appid << " uses " << size.as<uint64_t>() << " bytes\n";
    }

    for (const EasyVDF::KeyValue& folder : folders)
        out << folder.key() << " has " << folder["apps"].size() << " apps\n";

    CHECK(out.str() ==
        "0: C:\\Program Files (x86)\\Steam\n"
        "  app 228980 uses 455893076 bytes\n"
        "  app 440 uses 26843545600 bytes\n"
        "1: D:\\SteamLibrary\n"
        "  app 730 uses 35123456789 bytes\n"
        "0 has 2 apps\n"
        "1 has 1 apps\n");

    // Not an object: an empty loop.
    int iterations = 0;
    for (auto& child : folders["0"]["path"])
        iterations += child.has_value() ? 1 : 1;
    for (auto& child : folders["missing"]["apps"])
        iterations += child.has_value() ? 1 : 1;
    CHECK(iterations == 0);

    CHECK(folders.to_text() == LibraryFolders);
}

TEST_CASE("README: duplicate keys", "[readme]")
{
    auto doc = EasyVDF::parse(R"("root" { "dll" "a.dll"  "dll" "b.dll"  "other" "x" })");

    CHECK(doc["dll"].as<std::string>() == "a.dll");
    CHECK(doc.count("dll") == 2);

    std::string all;
    for (auto& dll : doc.all("dll"))
        all += dll.as<std::string>() + ';';
    CHECK(all == "a.dll;b.dll;");

    doc.add("dll", "c.dll");
    CHECK(doc.count("dll") == 3);
    CHECK(doc.erase("dll") == 3);
    CHECK(doc.size() == 1);
}

TEST_CASE("README: modifying and saving", "[readme]")
{
    TempFile file("readme_modified_440.acf");
    auto manifest = EasyVDF::parse(AppManifest);

    manifest["StateFlags"] = 4;
    manifest["UserConfig"]["language"] = "french";
    manifest["MountedDepots"].erase("441");
    manifest["InstalledDepots"].add("442", {
        {"manifest", "1234567890"},
        {"size", 4096},
    });

    manifest.save(file.path);
    std::string text = manifest.to_text();
    std::ostringstream streamed;
    streamed << manifest;
    CHECK(streamed.str() == text);

    auto saved = EasyVDF::load(file.path);
    CHECK(saved.to_text() == text);
    CHECK(saved["StateFlags"].as<int>() == 4);
    CHECK(saved["UserConfig"]["language"].as<std::string>() == "french");
    CHECK(saved["MountedDepots"].empty());
    CHECK(saved["MountedDepots"].is_object());
    CHECK(saved["InstalledDepots"]["442"]["size"].as<int>() == 4096);
    CHECK(saved["InstalledDepots"].size() == 2);

    SECTION("keys that are only read are not created")
    {
        EasyVDF::KeyValue doc("Settings");
        doc["a"]["b"].value_or(0);
        CHECK(doc.empty());
        CHECK(doc.to_text() == "\"Settings\"\n{\n}\n");
        doc["a"]["b"] = 1;
        CHECK(doc.to_text() == "\"Settings\"\n{\n\t\"a\"\n\t{\n\t\t\"b\"\t\t\"1\"\n\t}\n}\n");
    }

    SECTION("references stay valid when the parent grows")
    {
        EasyVDF::Node& config = manifest["UserConfig"];
        for (int i = 0; i < 100; ++i)
            manifest["NewKey" + std::to_string(i)] = "value";
        config["language"] = "german";
        CHECK(manifest["UserConfig"]["language"].as<std::string>() == "german");
    }
}

TEST_CASE("README: building a document", "[readme]")
{
    EasyVDF::KeyValue doc("AppState", {
        {"appid", 440},
        {"name", "Team Fortress 2"},
        {"SizeOnDisk", uint64_t(26843545600)},
        {"UserConfig", {
            {"language", "english"},
        }},
    });
    doc["InstalledDepots"]["441"]["manifest"] = "7707273133538421307";

    CHECK(doc["appid"].type() == EasyVDF::Type::Int32);
    CHECK(doc["SizeOnDisk"].type() == EasyVDF::Type::UInt64);
    CHECK(doc["UserConfig"].type() == EasyVDF::Type::Object);
    CHECK(doc.to_text() ==
        "\"AppState\"\n"
        "{\n"
        "\t\"appid\"\t\t\"440\"\n"
        "\t\"name\"\t\t\"Team Fortress 2\"\n"
        "\t\"SizeOnDisk\"\t\t\"26843545600\"\n"
        "\t\"UserConfig\"\n"
        "\t{\n"
        "\t\t\"language\"\t\t\"english\"\n"
        "\t}\n"
        "\t\"InstalledDepots\"\n"
        "\t{\n"
        "\t\t\"441\"\n"
        "\t\t{\n"
        "\t\t\t\"manifest\"\t\t\"7707273133538421307\"\n"
        "\t\t}\n"
        "\t}\n"
        "}\n");
}

TEST_CASE("README: binary files", "[readme]")
{
    TempFile binary_file("readme_shortcuts.vdf");
    TempFile vbkv_file("readme_shortcuts.vbkv");

    EasyVDF::KeyValue("shortcuts", {
        {"0", {
            {"appid", int32_t(-1234)},
            {"AppName", "My Game"},
            {"Exe", "\"C:\\Games\\game.exe\""},
        }},
    }).save(binary_file.path, EasyVDF::Format::Binary);

    auto shortcuts = EasyVDF::load(binary_file.path);
    std::ostringstream out;
    for (auto& [index, shortcut] : shortcuts)
        out << index << ": " << shortcut["AppName"].value_or("") << " -> " << shortcut["Exe"].value_or("") << '\n';
    CHECK(out.str() == "0: My Game -> \"C:\\Games\\game.exe\"\n");

    CHECK(shortcuts["0"]["appid"].type() == EasyVDF::Type::Int32);

    shortcuts["0"]["IsHidden"] = 1;
    shortcuts["0"]["LastPlayTime"] = int32_t(1700000000);
    shortcuts.save(binary_file.path, EasyVDF::Format::Binary);
    shortcuts.save(vbkv_file.path, EasyVDF::Format::Vbkv);

    std::string bytes = shortcuts.to_binary();
    CHECK(bytes.front() == '\0');
    CHECK(bytes.back() == '\x08');
    CHECK(shortcuts.to_binary(EasyVDF::Format::Vbkv).compare(0, 4, "VBKV") == 0);

    CHECK(EasyVDF::load(binary_file.path) == shortcuts);
    CHECK(EasyVDF::load(vbkv_file.path) == shortcuts);
    CHECK(EasyVDF::parse(bytes)["0"]["LastPlayTime"].as<int>() == 1700000000);
}

TEST_CASE("README: text format details", "[readme]")
{
    const char* const text =
        "// comments\n"
        "#base \"defaults.vdf\"                    // directives: parsed, see below\n"
        "\"root\"\n"
        "{\n"
        "    unquoted_key   unquoted_value       // quotes are optional\n"
        "    \"one line\" { \"a\" \"1\" \"b\" \"2\" }      // layout is free\n"
        "    \"path\"   \"C:\\\\Games\\\\Steam\"         // escapes\n"
        "    \"multi\"  \"line 1\n"
        "line 2\"                                 // values may span lines\n"
        "    \"music\"  \"win.mp3\"   [$WIN32]       // conditionals\n"
        "    \"console\" [$X360] { \"k\" \"v\" }\n"
        "}\n";

    std::vector<EasyVDF::Directive> directives;
    EasyVDF::ReadOptions options;
    options.directives = &directives;
    auto doc = EasyVDF::parse(text, options);

    std::ostringstream out;
    for (auto& d : directives)
        out << (d.kind == EasyVDF::Directive::Kind::Base ? "#base " : "#include ") << d.path << '\n';
    CHECK(out.str() == "#base defaults.vdf\n");

    CHECK(doc["unquoted_key"].as<std::string>() == "unquoted_value");
    CHECK(doc["one line"]["b"].as<int>() == 2);
    CHECK(doc["path"].as<std::string>() == "C:\\Games\\Steam");
    CHECK(doc["multi"].as<std::string>() == "line 1\nline 2");
    CHECK(doc.find("music")->condition() == "$WIN32");
    CHECK(doc.find("console")->condition() == "$X360");

    doc["path"] = R"(C:\Games)";
    CHECK(doc.to_text().find("\"path\"\t\t\"C:\\\\Games\"\n") != std::string::npos);
    CHECK(EasyVDF::parse(doc.to_text()) == doc);
}

TEST_CASE("README: errors", "[readme]")
{
    std::ostringstream err;
    const char* path = "does_not_exist.vdf";
    try
    {
        auto doc = EasyVDF::load(path);
    }
    catch (const EasyVDF::ParseError& e)
    {
        err << path << ':' << e.line() << ':' << e.column() << ": " << e.what() << '\n';
    }
    catch (const EasyVDF::Error& e)
    {
        err << e.what() << '\n';
    }
    CHECK(err.str() == "EasyVDF: can't open \"does_not_exist.vdf\" for reading\n");

    try
    {
        EasyVDF::parse("\"root\"\n{\n\t\"key\" }\n");
        FAIL("no exception");
    }
    catch (const EasyVDF::ParseError& e)
    {
        CHECK(e.line() == 3);
        CHECK(e.column() == 8);
    }
}

TEST_CASE("README: errors have a code", "[readme]")
{
    try
    {
        EasyVDF::load("does_not_exist.vdf");
        FAIL("no exception");
    }
    catch (const EasyVDF::Error& e)
    {
        CHECK(e.code() == EasyVDF::ErrorCode::Io);
        CHECK(e.info() == EasyVDF::try_load("does_not_exist.vdf").error());
    }
}

namespace {

EasyVDF::Result<uint32_t> ReadAppId(const std::filesystem::path& path)
{
    EasyVDF::Result<EasyVDF::KeyValue> manifest = EasyVDF::try_load(path);
    if (!manifest)
        return manifest.error();                       // pass the error up

    if (auto appid = (*manifest)["appid"].try_as<uint32_t>())
        return *appid;
    return EasyVDF::ErrorInfo(EasyVDF::ErrorCode::Type, "appid is not a number");
}

} // namespace

TEST_CASE("README: error handling without exceptions", "[readme]")
{
    TempFile manifest_file("readme_result_appmanifest_440.acf");
    REQUIRE(EasyVDF::parse(AppManifest).try_save(manifest_file.path).has_value());

    SECTION("try_load")
    {
        for (std::string path : {manifest_file.path, std::string("does_not_exist.vdf")})
        {
            std::ostringstream out, err;
            [&] {
                EasyVDF::Result<EasyVDF::KeyValue> doc = EasyVDF::try_load(path);
                if (!doc)
                {
                    const EasyVDF::ErrorInfo& error = doc.error();   // code, message, line, column
                    err << path << ':' << error.line << ':' << error.column << ": " << error.message << '\n';
                    return;
                }
                out << (*doc)["name"].value_or("?") << '\n';   // *doc is the KeyValue, doc-> works too
            }();
            if (path == manifest_file.path)
                CHECK(out.str() == "Team Fortress 2\n");
            else
                CHECK(err.str() == "does_not_exist.vdf:0:0: EasyVDF: can't open \"does_not_exist.vdf\" for reading\n");
        }
    }

    SECTION("propagation and chains")
    {
        CHECK(ReadAppId(manifest_file.path).value() == 440);
        CHECK(ReadAppId("does_not_exist.vdf").error().code == EasyVDF::ErrorCode::Io);

        auto language_of = [](const std::string& path) {
            EasyVDF::Result<EasyVDF::KeyValue> manifest = EasyVDF::try_load(path);
            std::string language = manifest
                .and_then([](const EasyVDF::KeyValue& doc) { return doc.try_at("UserConfig"); })
                .and_then([](const EasyVDF::Node& config) { return config.try_at("language"); })
                .map([](const EasyVDF::Node& node) { return node.value_or(std::string()); })
                .value_or("english");                              // missing file, key or value: "english"
            return language;
        };
        EasyVDF::KeyValue french = EasyVDF::parse(AppManifest);
        french["UserConfig"]["language"] = "french";
        REQUIRE(french.try_save(manifest_file.path).has_value());
        CHECK(language_of(manifest_file.path) == "french");
        CHECK(language_of("does_not_exist.vdf") == "english");
    }

    SECTION("recover, add context")
    {
        TempFile config_file("config.vdf");
        auto load_and_save = [] {
            // Recover from some errors only, add context to the others.
            EasyVDF::Result<EasyVDF::KeyValue> config = EasyVDF::try_load("config.vdf")
                .or_else([](const EasyVDF::ErrorInfo& error) -> EasyVDF::Result<EasyVDF::KeyValue> {
                    if (error.code == EasyVDF::ErrorCode::Io)
                        return EasyVDF::KeyValue("Config");        // no file yet: start empty
                    return error;
                })
                .map_error([](EasyVDF::ErrorInfo error) {
                    error.message = "config.vdf: " + error.message;
                    return error;
                });

            // and_then() also chains to a Result<void>: the error is the one of the load, or of the save.
            EasyVDF::Result<void> saved = config.and_then([](const EasyVDF::KeyValue& doc) { return doc.try_save("config.vdf"); });
            return saved;
        };

        // No file: an empty one is created.
        CHECK(load_and_save().has_value());
        CHECK(EasyVDF::load("config.vdf") == EasyVDF::KeyValue("Config"));

        // A malformed file: the error, with context.
        {
            std::ofstream out("config.vdf", std::ios::binary);
            out << "\"Config\"\n{\n\t\"k\"\n}\n";
        }
        EasyVDF::Result<void> saved = load_and_save();
        REQUIRE_FALSE(saved);
        std::ostringstream err;
        err << saved.error() << '\n';
        CHECK(err.str() == "Syntax: config.vdf: Expected a value or an object start ('{') after key \"k\" at line 4, column 1\n");
    }

    SECTION("Result API")
    {
        EasyVDF::Result<EasyVDF::KeyValue> r = EasyVDF::try_parse(AppManifest);
        CHECK(r.has_value());
        CHECK(r->key() == "AppState");
        CHECK((*r).key() == "AppState");
        CHECK(r.value().key() == "AppState");
        CHECK(r.map([](const EasyVDF::KeyValue& doc) { return doc.size(); }).value() == 12);

        std::ostringstream os;
        os << EasyVDF::try_parse("\"root\"\n{\n\t\"k\"\n}\n").error();
        CHECK(os.str() == "Syntax: Expected a value or an object start ('{') after key \"k\" at line 4, column 1");
        CHECK(std::string(EasyVDF::to_string(EasyVDF::ErrorCode::UnexpectedEof)) == "UnexpectedEof");
    }

    SECTION("compiling without exceptions")
    {
        // The part of the snippet that does not abort.
        TempFile settings("settings.vdf");
        EasyVDF::KeyValue doc("Settings");
        doc["window"]["width"] = 1280;                         // OK: objects are created as needed
        if (auto saved = doc.try_save("settings.vdf"); !saved)
            FAIL(saved.error());
        CHECK(EasyVDF::load("settings.vdf")["window"]["width"].value_or(0) == 1280);
        CHECK_THROWS_AS(doc["window"]["width"]["x"] = 1, EasyVDF::TypeError);
    }
}
