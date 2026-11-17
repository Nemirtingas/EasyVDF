#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../EasyVDF.h"

#include "helpers.h"

#if defined(WIN64) || defined(_WIN64) || defined(__MINGW64__) || defined(WIN32) || defined(_WIN32) || defined(__MINGW32__)
    #define NATIVE_VDF "windows_eol.vdf"
#elif defined(__APPLE__)
    #define NATIVE_VDF "macos_eol.vdf"
#else
    #define NATIVE_VDF "linux_eol.vdf"
#endif

static void print_to_stream(std::ostream& os, EasyVDF::KeyValue const& o, int indent = 0)
{
    std::string sindent(indent, ' ');

    switch (o.type())
    {
        case EasyVDF::Type::None   : os << sindent << '"' << o.key() << '"' << ": (null)" << std::endl; break;
        case EasyVDF::Type::Object :
            os << sindent << '"' << o.key() << '"' << std::endl;
            os << sindent << '{' << std::endl;
            for (auto const& item : o)
            {
                print_to_stream(os, item, indent + 4);
            }
            os << sindent << '}' << std::endl;
            break;
        case EasyVDF::Type::String : os << sindent << '"' << o.key() << '"' << ": (string)" << '"' << o.as<std::string_view>().value() << '"' << std::endl; break;
        case EasyVDF::Type::Int32  : os << sindent << '"' << o.key() << '"' << ": (int32)" << o.as<int32_t>().value() << std::endl; break;
        case EasyVDF::Type::Float  : os << sindent << '"' << o.key() << '"' << ": (float)" << o.as<float>().value() << std::endl; break;
        case EasyVDF::Type::Pointer: os << sindent << '"' << o.key() << '"' << ": (pointer)" << o.as<std::string>().value() << std::endl; break;
        case EasyVDF::Type::Color  : os << sindent << '"' << o.key() << '"' << ": (color)" << o.as<std::string>().value() << std::endl; break;
        case EasyVDF::Type::UInt64 : os << sindent << '"' << o.key() << '"' << ": (uint64)" << o.as<uint64_t>().value() << std::endl; break;
        case EasyVDF::Type::Int64  : os << sindent << '"' << o.key() << '"' << ": (int64)" << o.as<int64_t>().value() << std::endl; break;
    }
}

static EasyVDF::KeyValue LoadAndPrint(const char* title, const char* path)
{
    auto start = std::chrono::steady_clock::now();
    EasyVDF::KeyValue o = EasyVDF::load(path).value();
    auto elapsed = std::chrono::steady_clock::now() - start;

    std::cout << "==================== " << title << " ====================" << std::endl;
    print_to_stream(std::cout, o);
    std::cout << "sizeof(EasyVDF::KeyValue): " << sizeof(EasyVDF::KeyValue) << ", parsing took: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << "us" << std::endl << std::endl;
    return o;
}

static void CheckTextFile(EasyVDF::KeyValue const& o)
{
    REQUIRE(o.type() == EasyVDF::Type::Object);
    CHECK(o.key() == "999999");
    REQUIRE(o.count("ObjectKey") == 1);
    CHECK(o["ObjectKey"].type() == EasyVDF::Type::Object);
    CHECK(o["ObjectKey"]["ObjectEntry"].as<std::string>() == "ObjectEntryValue");
    CHECK(o["Version"].type() == EasyVDF::Type::String);
    CHECK(o["Version"].as<std::string>() == "8");
    CHECK(o["Version"].as<int>() == 8);
}

TEST_CASE("Parse VDF with Linux EOL", "[parse_vdf_linux_eol]")
{
    CheckTextFile(LoadAndPrint("Linux EOL", "linux_eol.vdf"));
}

TEST_CASE("Parse VDF with MacOS EOL", "[parse_vdf_macos_eol]")
{
    CheckTextFile(LoadAndPrint("MacOS EOL", "macos_eol.vdf"));
}

TEST_CASE("Parse VDF with Windows EOL", "[parse_vdf_windows_eol]")
{
    CheckTextFile(LoadAndPrint("Windows EOL", "windows_eol.vdf"));
}

TEST_CASE("The three EOL flavours give the same document", "[parse_vdf_eol]")
{
    auto linux_doc = EasyVDF::load("linux_eol.vdf").value();
    CHECK(linux_doc == EasyVDF::load("macos_eol.vdf"));
    CHECK(linux_doc == EasyVDF::load("windows_eol.vdf"));
}

TEST_CASE("Parse binary VDF", "[parse_binary_vdf]")
{
    EasyVDF::KeyValue o = LoadAndPrint("Binary VDF", "binary.vdf");

    REQUIRE(o.type() == EasyVDF::Type::Object);
    CHECK(o.key() == "RootObject");
    CHECK(o["ObjectKey"].type() == EasyVDF::Type::Object);
    CHECK(o["StringKey"].type() == EasyVDF::Type::String);
    CHECK(o["Int32Key"].type() == EasyVDF::Type::Int32);
    CHECK(o["FloatKey"].type() == EasyVDF::Type::Float);
    CHECK(o["PointerKey"].type() == EasyVDF::Type::Pointer);
    CHECK(o["ColorKey"].type() == EasyVDF::Type::Color);
    CHECK(o["UInt64Key"].type() == EasyVDF::Type::UInt64);
    CHECK(o["Int64Key"].type() == EasyVDF::Type::Int64);

    CHECK(o["ObjectKey"]["ObjectKeyValue"].as<std::string>() == "ObjectStringValue");
    CHECK(o["StringKey"].as<std::string>() == "StringValue");
    CHECK(o["Int32Key"].as<int32_t>() == -1337);
    CHECK(o["FloatKey"].as<float>() == 3.1415f);
    CHECK(o["PointerKey"].as<EasyVDF::Pointer>() == EasyVDF::Pointer{0x90807060});
    CHECK(o["ColorKey"].as<EasyVDF::Color>() == EasyVDF::Color{0x66, 0x77, 0x88, 0x99});
    CHECK(o["UInt64Key"].as<uint64_t>() == 0xfedcba9876543210ull);
    CHECK(o["Int64Key"].as<int64_t>() == -99999999999991337);
}

TEST_CASE("Serialize to text", "[serialize_object_as_text]")
{
    EasyVDF::KeyValue o = EasyVDF::load(NATIVE_VDF).value();

    std::stringstream sstr;
    o.write(sstr).value();

    const char* const expected =
        "\"999999\"\n"
        "{\n"
        "\t\"ObjectKey\"\n"
        "\t{\n"
        "\t\t\"ObjectEntry\"\t\t\"ObjectEntryValue\"\n"
        "\t}\n"
        "\t\"Version\"\t\t\"8\"\n"
        "}\n";

    CHECK(sstr.str() == expected);
    CHECK(o.to_text().value() == expected);
    CHECK(EasyVDF::parse(o.to_text().value()) == o);
}

TEST_CASE("Serialize to binary", "[binary_serialize]")
{
    EasyVDF::KeyValue o = EasyVDF::load(NATIVE_VDF).value();

    SECTION("Serializing to binary V1")
    {
        std::stringstream sstr;
        o.write(sstr, EasyVDF::Format::Binary).value();
        CHECK(memcmp(sstr.str().data(), "\x00\x39\x39\x39\x39\x39\x39\x00\x00\x4f", 10) == 0);
        CHECK(sstr.str() == o.to_binary().value());
    }

    SECTION("Serializing to binary V2")
    {
        std::stringstream sstr;
        o.write(sstr, EasyVDF::Format::Vbkv).value();
        const std::string blob = sstr.str();
        REQUIRE(blob.size() > 10);
        CHECK(memcmp(blob.data(), "VBKV", 4) == 0);
        CHECK(memcmp(blob.data() + 8, "\x00\x39", 2) == 0);
        CHECK(blob == o.to_binary(EasyVDF::Format::Vbkv).value());
    }
}

TEST_CASE("save() and load() round trip in every format", "[files]")
{
    struct Remove
    {
        const char* path;
        ~Remove() { std::remove(path); }
    };

    EasyVDF::KeyValue o = EasyVDF::load("binary.vdf").value();

    for (EasyVDF::Format format : { EasyVDF::Format::Binary, EasyVDF::Format::Vbkv })
    {
        Remove remove{ "test_files_round_trip.tmp" };
        o.save(remove.path, format).value();
        CHECK(EasyVDF::load(remove.path) == o);
    }

    {
        // Text does not keep the types: compare the text forms.
        Remove remove{ "test_files_round_trip.tmp" };
        o.save(remove.path).value();
        EasyVDF::KeyValue text = EasyVDF::load(remove.path).value();
        CHECK(text.to_text().value() == o.to_text().value());
        CHECK(text["Int32Key"].type() == EasyVDF::Type::String);
        CHECK(text["Int32Key"].as<int32_t>() == -1337);
        CHECK(text["FloatKey"].as<float>() == 3.1415f);
        CHECK(text["ColorKey"].as<EasyVDF::Color>() == EasyVDF::Color{0x66, 0x77, 0x88, 0x99});
        CHECK(text["PointerKey"].as<EasyVDF::Pointer>() == EasyVDF::Pointer{0x90807060});
        CHECK(text["UInt64Key"].as<uint64_t>() == 0xfedcba9876543210ull);
        CHECK(text["Int64Key"].as<int64_t>() == -99999999999991337);
    }
}

TEST_CASE("File errors throw IoError", "[files]")
{
    CHECK(Outcome(EasyVDF::load("no_such_file.vdf")) == "Io");
    CHECK(Outcome(EasyVDF::KeyValue("root").save("no_such_directory/file.vdf")) == "Io");

    // Nothing is created when the document can't be written.
    CHECK(Outcome(EasyVDF::KeyValue().save("test_files_not_created.tmp")) == "Write");
    CHECK_FALSE(std::ifstream("test_files_not_created.tmp").good());
}
