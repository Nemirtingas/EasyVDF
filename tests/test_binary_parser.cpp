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
