#include <fstream>
#include <sstream>
#include <string>

#include "../EasyVDF.h"
#include "catch.hpp"

namespace {

EasyVDF::ValveDataObject ParseText(std::string const& text)
{
    std::istringstream is(text);
    return EasyVDF::ValveDataObject::ParseObject(is);
}

}

TEST_CASE("Text parser: truncated UTF-8 sequence does not read out of bounds", "[text_parser][utf8]")
{
    // Used to segfault: the 3 bytes lead 0xE2 made the scanner jump past the end of the line.
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"\xE2\n}\n"), EasyVDF::ParserException);
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"\xE2\x82\n}\n"), EasyVDF::ParserException);
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"\xF0\x9F\n}\n"), EasyVDF::ParserException);
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\xC3\n}\n"), EasyVDF::ParserException);
    CHECK_THROWS_AS(ParseText("\"ro\xF0\x9F\x98\n{\n}\n"), EasyVDF::ParserException);
}

TEST_CASE("Text parser: multi-byte UTF-8 strings are kept verbatim", "[text_parser][utf8]")
{
    // A UTF-8 lead byte right before the closing quote must not swallow the quote.
    auto o = ParseText("\"r\xC3\xA9\"\n{\n\t\"k\xE2\x82\xAC\"\t\"\xF0\x9F\x98\x80\"\n\t\"k2\"\t\"a\xC3\xA9\"\n}\n");
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "r\xC3\xA9");
    REQUIRE(o.Collection().size() == 2);
    CHECK(o.Collection()[0].Name() == "k\xE2\x82\xAC");
    CHECK(o.Collection()[0].String() == "\xF0\x9F\x98\x80");
    CHECK(o.Collection()[1].Name() == "k2");
    CHECK(o.Collection()[1].String() == "a\xC3\xA9");
}

TEST_CASE("Text parser: UTF-8 is not validated, stray bytes are kept verbatim", "[text_parser][utf8]")
{
    // Policy: strings are opaque bytes, only '"' and '\\' are significant.
    auto o = ParseText("\"root\"\n{\n\t\"\x80\xBF\"\t\"\xF8\xFF\xC3\"\n}\n");
    REQUIRE(o.Collection().size() == 1);
    CHECK(o.Collection()[0].Name() == "\x80\xBF");
    CHECK(o.Collection()[0].String() == "\xF8\xFF\xC3");
}

namespace {

EasyVDF::ValveDataObject ParseBinary(std::string const& blob, size_t chunk_size)
{
    std::istringstream is(blob);
    return EasyVDF::ValveDataObject::ParseObject(is, chunk_size);
}

// Deep comparison, returns an empty string when equal, else a description of the first difference.
std::string Compare(EasyVDF::ValveDataObject const& a, EasyVDF::ValveDataObject const& b, std::string const& path = "")
{
    std::string here = path + "/" + a.Name();
    if (a.Name() != b.Name())
        return here + ": name differs ('" + a.Name() + "' vs '" + b.Name() + "')";
    if (a.Type() != b.Type())
        return here + ": type differs (" + std::to_string((int)a.Type()) + " vs " + std::to_string((int)b.Type()) + ")";

    switch (a.Type())
    {
        case EasyVDF::ObjectType::Object:
        {
            auto const& ca = a.Collection();
            auto const& cb = b.Collection();
            if (ca.size() != cb.size())
                return here + ": collection size differs";
            for (size_t i = 0; i < ca.size(); ++i)
            {
                std::string r = Compare(ca[i], cb[i], here);
                if (!r.empty())
                    return r;
            }
            return std::string();
        }
        case EasyVDF::ObjectType::String : return a.String() == b.String() ? std::string() : here + ": string differs ('" + a.String() + "' vs '" + b.String() + "')";
        case EasyVDF::ObjectType::Int32  : return a.Int32() == b.Int32() ? std::string() : here + ": int32 differs";
        case EasyVDF::ObjectType::Float  : return a.Float() == b.Float() ? std::string() : here + ": float differs";
        case EasyVDF::ObjectType::Pointer: return a.Pointer().value == b.Pointer().value ? std::string() : here + ": pointer differs";
        case EasyVDF::ObjectType::Color  : return a.Color().value == b.Color().value ? std::string() : here + ": color differs";
        case EasyVDF::ObjectType::Int64  : return a.Int64() == b.Int64() ? std::string() : here + ": int64 differs";
        case EasyVDF::ObjectType::UInt64 : return a.UInt64() == b.UInt64() ? std::string() : here + ": uint64 differs";
        default: return here + ": unexpected type";
    }
}

EasyVDF::ValveDataObject MakeSample()
{
    using EasyVDF::ValveDataObject;

    ValveDataObject nested("Nested\xC3\xA9");
    nested.Collection().emplace_back(ValveDataObject("NestedString", std::string("NestedValue")));
    nested.Collection().emplace_back(ValveDataObject("EmptyObject"));
    nested.Collection().emplace_back(ValveDataObject("Wrapper", ValveDataObject("Inner", std::string("InnerValue"))));
    nested.Collection().emplace_back(ValveDataObject("NestedInt", int32_t(0x7FFFFFFF)));

    ValveDataObject o("RootObject");
    auto& c = o.Collection();
    c.emplace_back(ValveDataObject("StringKey", std::string("StringValue")));
    c.emplace_back(ValveDataObject("EmptyString", std::string()));
    c.emplace_back(ValveDataObject("Utf8\xE2\x82\xAC", std::string("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xC3\xA9\xF0\x9F\x98\x80")));
    c.emplace_back(ValveDataObject("Int32Key", int32_t(-1337)));
    c.emplace_back(ValveDataObject("Int32Min", int32_t(-2147483647 - 1)));
    c.emplace_back(ValveDataObject("FloatKey", float(3.1415f)));
    c.emplace_back(ValveDataObject("FloatNeg", float(-1.5e-30f)));
    c.emplace_back(ValveDataObject("PointerKey", EasyVDF::pointer_t{0x90807060}));
    c.emplace_back(ValveDataObject("ColorKey", EasyVDF::color_t{0x99887766}));
    c.emplace_back(std::move(nested));
    c.emplace_back(ValveDataObject("UInt64Key", uint64_t(0xfedcba9876543210ull)));
    c.emplace_back(ValveDataObject("Int64Key", int64_t(-99999999999991337ll)));
    c.emplace_back(ValveDataObject("Last\xF0\x9F\x98\x80", std::string("\xF0\x9F\x98\x80")));
    return o;
}

}

TEST_CASE("Binary parser: round trip with every chunk size", "[binary_parser][chunks]")
{
    auto sample = MakeSample();
    for (int version : {1, 2})
    {
        std::string blob = sample.SerializeAsBinary(version);
        for (size_t chunk_size = 1; chunk_size <= blob.size() + 1; ++chunk_size)
        {
            INFO("version " << version << ", chunk size " << chunk_size);
            std::string diff;
            try
            {
                diff = Compare(sample, ParseBinary(blob, chunk_size));
            }
            catch (std::exception& e)
            {
                diff = std::string("exception: ") + e.what();
            }
            CHECK(diff == "");
        }
        INFO("version " << version << ", default chunk size");
        std::istringstream is(blob);
        CHECK(Compare(sample, EasyVDF::ValveDataObject::ParseObject(is)) == "");
    }
}

TEST_CASE("Binary parser: binary.vdf with every chunk size", "[binary_parser][chunks]")
{
    std::ifstream f("binary.vdf", std::ios::binary | std::ios::in);
    REQUIRE(f);
    std::string blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(blob.size() > 8);

    using EasyVDF::ValveDataObject;
    ValveDataObject expected("RootObject");
    auto& c = expected.Collection();
    c.emplace_back(ValveDataObject("ObjectKey", ValveDataObject("ObjectKeyValue", std::string("ObjectStringValue"))));
    c.emplace_back(ValveDataObject("StringKey", std::string("StringValue")));
    c.emplace_back(ValveDataObject("Int32Key", int32_t(-1337)));
    c.emplace_back(ValveDataObject("FloatKey", float(3.1415f)));
    c.emplace_back(ValveDataObject("PointerKey", EasyVDF::pointer_t{0x90807060}));
    c.emplace_back(ValveDataObject("ColorKey", EasyVDF::color_t{0x99887766}));
    c.emplace_back(ValveDataObject("UInt64Key", uint64_t(0xfedcba9876543210ull)));
    c.emplace_back(ValveDataObject("Int64Key", int64_t(-99999999999991337ll)));

    // The library writes the exact same bytes back.
    CHECK(expected.SerializeAsBinary(2) == blob);

    for (size_t chunk_size = 1; chunk_size <= blob.size() + 1; ++chunk_size)
    {
        INFO("chunk size " << chunk_size);
        std::string diff;
        try
        {
            diff = Compare(expected, ParseBinary(blob, chunk_size));
        }
        catch (std::exception& e)
        {
            diff = std::string("exception: ") + e.what();
        }
        CHECK(diff == "");
    }
}

TEST_CASE("Binary parser: unknown node type throws ParserException", "[binary_parser][errors]")
{
    for (unsigned char type : {5, 9, 12, 0x42, 0x7F, 0x80, 0xFF})
    {
        INFO("node type " << (int)type);
        std::string blob("\x00Root\x00", 6);
        blob += static_cast<char>(type);
        blob += std::string("key\x00value\x00\x08", 11);

        try
        {
            ParseBinary(blob, 1024);
            FAIL("no exception thrown");
        }
        catch (EasyVDF::ParserException const& e)
        {
            CHECK_THAT(e.what(), Catch::Matchers::Contains(std::to_string((int)type)));
        }
    }
}
