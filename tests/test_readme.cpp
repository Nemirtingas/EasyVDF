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

#include "helpers.h"

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
    EasyVDF::parse(AppManifest).value().save(file.path).value();

    std::string name, lang;
    int id = 0;
    std::ostringstream err;
    auto quick_start = [&](std::string const& path) {
        EasyVDF::Result<EasyVDF::KeyValue> loaded = EasyVDF::load(path);
        if (!loaded)
        {
            err << loaded.error() << '\n';
            return 1;
        }
        EasyVDF::KeyValue& manifest = *loaded;

        name = manifest["name"].value_or("?");
        id   = manifest["appid"].value_or(0);
        lang = manifest["UserConfig"]["language"].value_or("english");

        manifest["UserConfig"]["language"] = "french";
        if (auto saved = manifest.save(path); !saved)
            err << saved.error() << '\n';
        return 0;
    };

    CHECK(quick_start(file.path) == 0);
    CHECK(name == "Team Fortress 2");
    CHECK(id == 440);
    CHECK(lang == "english");
    CHECK(err.str().empty());
    CHECK(EasyVDF::load(file.path).value()["UserConfig"]["language"].as<std::string>() == "french");

    CHECK(quick_start("does_not_exist.acf") == 1);
    CHECK(err.str() == "Io: EasyVDF: can't open \"does_not_exist.acf\" for reading\n");
}

TEST_CASE("README: reading", "[readme]")
{
    EasyVDF::KeyValue a = EasyVDF::parse(R"("root" { "key" "value" })").value();
    CHECK(a.key() == "root");
    CHECK(a["key"].as<std::string>() == "value");

    std::istringstream input(R"("root" { "key" "value" })");
    EasyVDF::KeyValue b = EasyVDF::parse(input).value();
    CHECK(a == b);

    TempFile file("readme_installscript.vdf");
    {
        std::ofstream out(file.path, std::ios::binary);
        out << "\"InstallScript\"\n{\n\t\"Run Process\"\n\t{\n\t}\n}\n\"kvsignatures\"\n{\n\t\"InstallScript\"\t\t\"4284ab2e\"\n}\n";
    }
    CHECK(IsParseError(EasyVDF::load(file.path)));

    std::ostringstream out;
    std::vector<EasyVDF::KeyValue> roots = EasyVDF::load_all(file.path).value();
    for (auto& root : roots)
        out << root.key() << '\n';
    CHECK(out.str() == "InstallScript\nkvsignatures\n");
}

TEST_CASE("README: getting values", "[readme]")
{
    auto manifest = EasyVDF::parse(AppManifest).value();
    std::ostringstream out;

    CHECK(manifest.key() == "AppState");

    bool        updating = manifest["AutoUpdateBehavior"].value_or(0) != 0;
    std::string language = manifest["UserConfig"]["language"].value_or("english");
    uint64_t    build    = manifest["buildid"].value_or<uint64_t>(0);
    CHECK_FALSE(updating);
    CHECK(language == "english");
    CHECK(build == 12345678u);

    EasyVDF::Result<uint32_t> appid = manifest["appid"].as<uint32_t>();    // "440" -> 440
    if (!appid)
        out << appid.error().message << '\n';
    std::string name = manifest["name"].as<std::string>().value_or("?");
    CHECK(appid == 440u);
    CHECK(name == "Team Fortress 2");
    CHECK(manifest["SizeOnDisk"].as<uint64_t>() == 26843545600ull);
    CHECK(Message(manifest["name"].as<uint32_t>()) == "EasyVDF: can't convert a String value to an integer");

    if (auto owner = manifest["LastOwner"].try_as<uint64_t>())
        out << "owner: " << *owner << '\n';

    if (manifest["InstalledDepots"]["441"])
        out << "depot 441 is installed\n";
    if (auto* depot = manifest["InstalledDepots"].find("441"))
        out << depot->key() << ": " << (*depot)["manifest"].as<std::string>().value() << '\n';

    CHECK(out.str() == "owner: 76561198000000000\ndepot 441 is installed\n441: 7707273133538421307\n");

    CHECK(Outcome(manifest.at("appid")) == "OK");
    CHECK(Outcome(manifest.at("nope")) == "Key");
    CHECK(manifest.contains("appid"));
    CHECK_FALSE(manifest["InstalledDepots"]["999"]);
    CHECK(Outcome(manifest["name"].as<int>()) == "Type");

    // Reading never changed the document.
    CHECK(manifest.to_text().value() == AppManifest);
}

TEST_CASE("README: iterating", "[readme]")
{
    auto folders = EasyVDF::parse(LibraryFolders).value();
    std::ostringstream out;

    for (auto& [index, folder] : folders)
    {
        out << index << ": " << folder["path"].value_or("?") << '\n';

        for (auto& [appid, size] : folder["apps"])
            out << "  app " << appid << " uses " << size.as<uint64_t>().value() << " bytes\n";
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

    CHECK(folders.to_text().value() == LibraryFolders);
}

TEST_CASE("README: duplicate keys", "[readme]")
{
    auto doc = EasyVDF::parse(R"("root" { "dll" "a.dll"  "dll" "b.dll"  "other" "x" })").value();

    CHECK(doc["dll"].as<std::string>() == "a.dll");
    CHECK(doc.count("dll") == 2);

    std::string all;
    for (auto& dll : doc.all("dll"))
        all += dll.value_or("") + ';';
    CHECK(all == "a.dll;b.dll;");

    doc.add("dll", "c.dll");
    CHECK(doc.count("dll") == 3);
    CHECK(doc.erase("dll") == 3);
    CHECK(doc.size() == 1);
}

TEST_CASE("README: modifying and saving", "[readme]")
{
    TempFile file("readme_modified_440.acf");
    auto manifest = EasyVDF::parse(AppManifest).value();

    manifest["StateFlags"] = 4;
    manifest["UserConfig"]["language"] = "french";
    manifest["MountedDepots"].erase("441");
    manifest["InstalledDepots"].add("442", {
        {"manifest", "1234567890"},
        {"size", 4096},
    });

    EasyVDF::Result<void> saved_file = manifest.save(file.path);
    EasyVDF::Result<std::string> text = manifest.to_text();
    std::ostringstream streamed;
    streamed << manifest;
    REQUIRE(saved_file);
    CHECK(streamed.str() == text);

    auto saved = EasyVDF::load(file.path).value();
    CHECK(saved.to_text().value() == text);
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
        CHECK(doc.to_text().value() == "\"Settings\"\n{\n}\n");
        doc["a"]["b"] = 1;
        CHECK(doc.to_text().value() == "\"Settings\"\n{\n\t\"a\"\n\t{\n\t\t\"b\"\t\t\"1\"\n\t}\n}\n");
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
    CHECK(doc.to_text().value() ==
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
    }).save(binary_file.path, EasyVDF::Format::Binary).value();

    auto shortcuts = EasyVDF::load(binary_file.path).value();
    std::ostringstream out;
    for (auto& [index, shortcut] : shortcuts)
        out << index << ": " << shortcut["AppName"].value_or("") << " -> " << shortcut["Exe"].value_or("") << '\n';
    CHECK(out.str() == "0: My Game -> \"C:\\Games\\game.exe\"\n");

    CHECK(shortcuts["0"]["appid"].type() == EasyVDF::Type::Int32);

    shortcuts["0"]["IsHidden"] = 1;
    shortcuts["0"]["LastPlayTime"] = int32_t(1700000000);
    shortcuts.save(binary_file.path, EasyVDF::Format::Binary).value();
    shortcuts.save(vbkv_file.path, EasyVDF::Format::Vbkv).value();

    std::string bytes = shortcuts.to_binary().value();
    CHECK(bytes.front() == '\0');
    CHECK(bytes.back() == '\x08');
    CHECK(shortcuts.to_binary(EasyVDF::Format::Vbkv).value().compare(0, 4, "VBKV") == 0);

    CHECK(EasyVDF::load(binary_file.path) == shortcuts);
    CHECK(EasyVDF::load(vbkv_file.path) == shortcuts);
    CHECK(EasyVDF::parse(bytes).value()["0"]["LastPlayTime"].as<int>() == 1700000000);
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
    auto doc = EasyVDF::parse(text, options).value();

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
    CHECK(doc.to_text().value().find("\"path\"\t\t\"C:\\\\Games\"\n") != std::string::npos);
    CHECK(EasyVDF::parse(doc.to_text().value()) == doc);
}

TEST_CASE("README: errors", "[readme]")
{
    for (std::string path : {std::string("does_not_exist.vdf"), std::string("readme_broken.vdf")})
    {
        TempFile broken("readme_broken.vdf");
        {
            std::ofstream out(broken.path, std::ios::binary);
            out << "\"root\"\n{\n\t\"key\" }\n";
        }

        std::ostringstream out, err;
        [&] {
            EasyVDF::Result<EasyVDF::KeyValue> doc = EasyVDF::load(path);
            if (!doc)
            {
                const EasyVDF::ErrorInfo& error = doc.error();   // code, message, line, column
                err << path << ':' << error.line << ':' << error.column << ": " << error.message << '\n';
                return;
            }
            out << (*doc)["name"].value_or("?") << '\n';   // *doc is the KeyValue, doc-> works too
        }();

        CHECK(out.str().empty());
        if (path == "does_not_exist.vdf")
            CHECK(err.str() == "does_not_exist.vdf:0:0: EasyVDF: can't open \"does_not_exist.vdf\" for reading\n");
        else
            CHECK(err.str() == "readme_broken.vdf:3:8: Expected a value or an object start ('{') after key \"key\" at line 3, column 8\n");
    }

    CHECK(EasyVDF::is_parse_error(EasyVDF::ErrorCode::DepthExceeded));
    CHECK_FALSE(EasyVDF::is_parse_error(EasyVDF::ErrorCode::Io));
    std::ostringstream os;
    os << EasyVDF::parse("\"root\"\n{\n\t\"k\"\n}\n").error();
    CHECK(os.str() == "Syntax: Expected a value or an object start ('{') after key \"k\" at line 4, column 1");
    CHECK(std::string(EasyVDF::to_string(EasyVDF::ErrorCode::UnexpectedEof)) == "UnexpectedEof");
}

namespace {

EasyVDF::Result<uint32_t> ReadAppId(const std::filesystem::path& path)
{
    EasyVDF::Result<EasyVDF::KeyValue> manifest = EasyVDF::load(path);
    if (!manifest)
        return manifest.error();                       // pass the error up
    return (*manifest)["appid"].as<uint32_t>();
}

} // namespace

TEST_CASE("README: Result", "[readme]")
{
    TempFile manifest_file("readme_result_appmanifest_440.acf");
    REQUIRE(EasyVDF::parse(AppManifest)->save(manifest_file.path));

    SECTION("API")
    {
        EasyVDF::Result<EasyVDF::KeyValue> r = EasyVDF::parse(AppManifest);
        CHECK(r.has_value());
        CHECK(r->key() == "AppState");
        CHECK((*r).key() == "AppState");
        CHECK(r.value().key() == "AppState");
        CHECK(r.map([](const EasyVDF::KeyValue& doc) { return doc.size(); }) == std::size_t(12));
        CHECK(r->at("appid")->as<int>() == 440);
    }

    SECTION("propagation and chains")
    {
        CHECK(ReadAppId(manifest_file.path) == 440u);
        CHECK(Outcome(ReadAppId("does_not_exist.vdf")) == "Io");

        auto language_of = [](const std::string& path) {
            EasyVDF::Result<EasyVDF::KeyValue> manifest = EasyVDF::load(path);
            std::string language = manifest
                .and_then([](const EasyVDF::KeyValue& doc) { return doc.at("UserConfig"); })
                .and_then([](const EasyVDF::Node& config) { return config.at("language"); })
                .and_then([](const EasyVDF::Node& node) { return node.as<std::string>(); })
                .value_or("english");                              // missing file, key or value: "english"
            return language;
        };
        EasyVDF::KeyValue french = EasyVDF::parse(AppManifest).value();
        french["UserConfig"]["language"] = "french";
        REQUIRE(french.save(manifest_file.path));
        CHECK(language_of(manifest_file.path) == "french");
        CHECK(language_of("does_not_exist.vdf") == "english");
    }

    SECTION("recover, add context")
    {
        TempFile config_file("config.vdf");
        auto load_and_save = [] {
            // Recover from some errors only, add context to the others.
            EasyVDF::Result<EasyVDF::KeyValue> config = EasyVDF::load("config.vdf")
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
            EasyVDF::Result<void> saved = config.and_then([](const EasyVDF::KeyValue& doc) { return doc.save("config.vdf"); });
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

    SECTION("bool values")
    {
        EasyVDF::KeyValue doc = EasyVDF::parse(R"("root" { "flag" "0" })").value();
        CHECK(static_cast<bool>(doc["flag"].as<bool>()));   // a value...
        CHECK(doc["flag"].as<bool>() == false);             // ...which is false
        CHECK_FALSE(doc["flag"].value_or(false));
    }

    SECTION("mistakes abort")
    {
        // The part of the snippet that does not abort (the rest is a CTest test).
        EasyVDF::KeyValue doc("Settings");
        doc["window"]["width"] = 1280;
        CHECK(doc["window"]["width"].value_or(0) == 1280);
    }
}
