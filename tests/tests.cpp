#include <fstream>
#include <chrono>

#include "../EasyVDF.h"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#if defined(WIN64) || defined(_WIN64) || defined(__MINGW64__) || defined(WIN32) || defined(_WIN32) || defined(__MINGW32__)
    #define NATIVE_VDF "windows_eol.vdf"
#elif defined(__linux__) || defined(linux)
    #define NATIVE_VDF "linux_eol.vdf"
#elif defined(__APPLE__)
    #define NATIVE_VDF "macos_eol.vdf"
#endif

static void print_to_stream(std::ostream& os, EasyVDF::ValveDataObject const& o, int indent = 0)
{
    std::string sindent(indent, ' ');
    
    switch(o.Type())
    {
        case EasyVDF::ObjectType::None      : os << sindent << '"' << o.Name() << '"' << ": (null)" << std::endl; break;
        case EasyVDF::ObjectType::Object    :
            os << sindent << '"' << o.Name() << '"' << std::endl;
            os << sindent << '{' << std::endl;
            indent += 2;
            for(auto item : o.Collection())
            {
                print_to_stream(os, item, indent + 2);
            }
            indent -= 2;
            os << sindent << '}' << std::endl;
            break;
        case EasyVDF::ObjectType::String    : os << sindent << '"' << o.Name() << '"' << ": (string)" << '"' << o.String() << '"' << std::endl; break;
        case EasyVDF::ObjectType::Int32     : os << sindent << '"' << o.Name() << '"' << ": (int32)" << o.Int32() << std::endl; break;
        case EasyVDF::ObjectType::Float     : os << sindent << '"' << o.Name() << '"' << ": (float)" << o.Float() << std::endl; break;
        case EasyVDF::ObjectType::Pointer   : os << sindent << '"' << o.Name() << '"' << ": (pointer)" << std::endl; break;
        case EasyVDF::ObjectType::WideString: os << sindent << '"' << o.Name() << '"' << ": (wide string)" << std::endl; break;
        case EasyVDF::ObjectType::Color     : os << sindent << '"' << o.Name() << '"' << ": (color)" << std::endl; break;
        case EasyVDF::ObjectType::UInt64    : os << sindent << '"' << o.Name() << '"' << ": (uint64)" << o.UInt64() << std::endl; break;
        case EasyVDF::ObjectType::Binary    : os << sindent << '"' << o.Name() << '"' << ": (binary)" << std::endl; break;
        case EasyVDF::ObjectType::Int64     : os << sindent << '"' << o.Name() << '"' << ": (int64)" << o.Int64() << std::endl; break;
    }
}

std::ostream& operator<<(std::ostream& os, EasyVDF::ValveDataObject const& o)
{
    print_to_stream(os, o);
    return os;
}

TEST_CASE("Parse VDF with Linux EOL", "[parse_vdf_linux_eol]")
{
    std::ifstream f("linux_eol.vdf", std::ios::binary | std::ios::in);
    
    auto start = std::chrono::steady_clock::now();
    start = std::chrono::steady_clock::now();
    EasyVDF::ValveDataObject o = EasyVDF::ValveDataObject::ParseObject(f);
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    std::cout << "==================== Linux EOL ====================" << std::endl;
    std::cout << o << std::endl;
    std::cout << "Sizeof(EasyVDF::ValveDataObject): " << sizeof(EasyVDF::ValveDataObject) << ", Parsing took: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << "µs" << std::endl << std::endl;
    
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "999999");
    REQUIRE(o["ObjectKey"].size() == 1);
    CHECK(o["ObjectKey"][0].Type() == EasyVDF::ObjectType::Object);
    CHECK(o["Version"][0].Type() == EasyVDF::ObjectType::String);
    CHECK(o["Version"][0].String() == "8");
}

TEST_CASE("Parse VDF with MacOS EOL", "[parse_vdf_macos_eol]")
{
    std::ifstream f("macos_eol.vdf", std::ios::binary | std::ios::in);
    
    auto start = std::chrono::steady_clock::now();
    EasyVDF::ValveDataObject o = EasyVDF::ValveDataObject::ParseObject(f);
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    std::cout << "==================== MacOS EOL ====================" << std::endl;
    std::cout << o << std::endl;
    std::cout << "Sizeof(EasyVDF::ValveDataObject): " << sizeof(EasyVDF::ValveDataObject) << ", Parsing took: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << "µs" << std::endl << std::endl;
    
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "999999");
    REQUIRE(o["ObjectKey"].size() == 1);
    CHECK(o["ObjectKey"][0].Type() == EasyVDF::ObjectType::Object);
    CHECK(o["Version"][0].Type() == EasyVDF::ObjectType::String);
    CHECK(o["Version"][0].String() == "8");
}

TEST_CASE("Parse VDF with Windows EOL", "[parse_vdf_windows_eol]")
{
    std::ifstream f("windows_eol.vdf", std::ios::binary | std::ios::in);
    
    auto start = std::chrono::steady_clock::now();
    EasyVDF::ValveDataObject o = EasyVDF::ValveDataObject::ParseObject(f);
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    std::cout << "==================== Windows EOL ====================" << std::endl;
    std::cout << o << std::endl;
    std::cout << "Sizeof(EasyVDF::ValveDataObject): " << sizeof(EasyVDF::ValveDataObject) << ", Parsing took: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << "µs" << std::endl << std::endl;
    
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "999999");
    REQUIRE(o["ObjectKey"].size() == 1);
    CHECK(o["ObjectKey"][0].Type() == EasyVDF::ObjectType::Object);
    CHECK(o["Version"][0].Type() == EasyVDF::ObjectType::String);
    CHECK(o["Version"][0].String() == "8");
}

TEST_CASE("Parse binary VDF", "[parse_binary_vdf]")
{
    std::ifstream f("binary.vdf", std::ios::binary | std::ios::in);
    
    auto start = std::chrono::steady_clock::now();
    EasyVDF::ValveDataObject o = EasyVDF::ValveDataObject::ParseObject(f);
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    std::cout << "==================== Binary VDF ====================" << std::endl;
    std::cout << o << std::endl;
    std::cout << "Sizeof(EasyVDF::ValveDataObject): " << sizeof(EasyVDF::ValveDataObject) << ", Parsing took: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << "µs" << std::endl << std::endl;
    
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "RootObject");
    CHECK(o["ObjectKey"][0].Type() == EasyVDF::ObjectType::Object);
    CHECK(o["StringKey"][0].Type() == EasyVDF::ObjectType::String);
    CHECK(o["Int32Key"][0].Type() == EasyVDF::ObjectType::Int32);
    CHECK(o["FloatKey"][0].Type() == EasyVDF::ObjectType::Float);
    CHECK(o["PointerKey"][0].Type() == EasyVDF::ObjectType::Pointer);
    CHECK(o["ColorKey"][0].Type() == EasyVDF::ObjectType::Color);
    CHECK(o["UInt64Key"][0].Type() == EasyVDF::ObjectType::UInt64);
    CHECK(o["Int64Key"][0].Type() == EasyVDF::ObjectType::Int64);
    
    CHECK(o["StringKey"][0].String() == "StringValue");
    CHECK(o["Int32Key"][0].Int32() == -1337);
    CHECK(o["FloatKey"][0].Float() == 3.1415f);
    CHECK(o["PointerKey"][0].Pointer().value == EasyVDF::pointer_t{0x90807060}.value);
    CHECK(o["ColorKey"][0].Color().value == EasyVDF::color_t{0x99887766}.value);
    CHECK(o["UInt64Key"][0].UInt64() == 0xfedcba9876543210ull);
    CHECK(o["Int64Key"][0].Int64() == -99999999999991337);
}

TEST_CASE("Serialize to text", "[serialize_object_as_text]")
{
    std::ifstream f(NATIVE_VDF, std::ios::binary | std::ios::in);
    std::stringstream sstr;
    
    EasyVDF::ValveDataObject o = EasyVDF::ValveDataObject::ParseObject(f);
    
    o.SerializeAsText(sstr);
    
    CHECK(sstr.str() == ""
    "\"999999\"\n"
    "{\n"
    "\t\"ObjectKey\"\n"
    "\t{\n"
    "\t\t\"ObjectEntry\"\t\t\"ObjectEntryValue\"\n"
    "\t}\n"
    "\t\"Version\"\t\t\"8\"\n"
    "}\n"
    );
}

TEST_CASE("Serialize to binary", "[binary_serialize]")
{
    std::ifstream f(NATIVE_VDF, std::ios::binary | std::ios::in);
    std::stringstream sstr;
    
    EasyVDF::ValveDataObject o = EasyVDF::ValveDataObject::ParseObject(f);
    
    SECTION("Serializing to binary V1")
    {
        o.SerializeAsBinary(sstr, 1);
        CHECK(memcmp(sstr.str().data(), "\x00\x39\x39\x39\x39\x39\x39\x00\x00\x4f", 10) == 0);
    }
    
    sstr.str(std::string());
    SECTION("Serializing to binary V2")
    {
        o.SerializeAsBinary(sstr, 2);
        CHECK(memcmp(sstr.str().data(), "\x56\x42\x4b\x56\x00\x00\x00\x00\x00\x39", 10) == 0);
    }
}

int main (int argc, char *argv[])
{
    // global setup...

    int result = Catch::Session().run(argc, argv);

    // global clean-up...

    return result;
    //EasyVDF::ValveDataObject o("RootObject");
    //
    //auto& c = o.Collection();
    ////c.emplace_back(EasyVDF::ValveDataObject{"null"      , nullptr});
    //c.emplace_back(EasyVDF::ValveDataObject{"ObjectKey" , EasyVDF::ValveDataObject{"ObjectKeyValue", "ObjectStringValue"}});
    //c.emplace_back(EasyVDF::ValveDataObject{"StringKey" , "StringValue"});
    //c.emplace_back(EasyVDF::ValveDataObject{"Int32Key"     , int32_t(-1337)});
    //c.emplace_back(EasyVDF::ValveDataObject{"FloatKey"     , float(3.1415)});
    //c.emplace_back(EasyVDF::ValveDataObject{"PointerKey"   , EasyVDF::pointer_t{0x90807060}});
    ////c.emplace_back(EasyVDF::ValveDataObject{"WideStringKey", EasyVDF::pointer_t{0x90807060}});
    //c.emplace_back(EasyVDF::ValveDataObject{"ColorKey"     , EasyVDF::color_t{0x99887766}});
    //c.emplace_back(EasyVDF::ValveDataObject{"UInt64Key"    , uint64_t{0xfedcba9876543210ull}});
    ////c.emplace_back(EasyVDF::ValveDataObject{"BinaryKey"    , EasyVDF::color_t{0x99887766}});
    //c.emplace_back(EasyVDF::ValveDataObject{"Int64Key"     , int64_t{-99999999999991337ll}});
    
    return 0;
}
