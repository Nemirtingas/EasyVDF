#include <sstream>
#include <string>

#include "../EasyVDF.h"
#include "catch.hpp"

namespace {

EasyVDF::ValveDataObject ParseText(std::string const& text)
{
    std::stringstream ss(text, std::ios::in | std::ios::binary);
    return EasyVDF::ValveDataObject::ParseObject(ss);
}

// Replaces every '\n' of an LF text by the given EOL sequence.
std::string WithEol(std::string const& lf_text, std::string const& eol)
{
    std::string result;
    for (char c : lf_text)
    {
        if (c == '\n')
            result += eol;
        else
            result += c;
    }
    return result;
}

// Takes the object by value: the const accessors of ValveDataObjectRefWrapper
// do not compile, so the checks go through a non-const object.
void CheckSimpleRoot(EasyVDF::ValveDataObject o)
{
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "root");
    REQUIRE(o.Collection().size() == 2);
    REQUIRE(o["k"].size() == 1);
    CHECK(o["k"][0].String() == "v");
    REQUIRE(o["sub"].size() == 1);
    REQUIRE(o["sub"][0].Type() == EasyVDF::ObjectType::Object);
    REQUIRE(o["sub"][0]["k2"].size() == 1);
    CHECK(o["sub"][0]["k2"][0].String() == "v2");
}

} // namespace

TEST_CASE("SkipSpaces trims blank lines and handles empty ranges", "[text_parser][skip_spaces]")
{
    auto trimmed = [](std::string const& line) {
        const char* s = line.data();
        const char* e = s + line.size();
        EasyVDF::Details::SkipSpaces(s, e);
        return std::string(s, e);
    };

    CHECK(trimmed("") == "");
    CHECK(trimmed("\n") == "");
    CHECK(trimmed("\r\n") == "");
    CHECK(trimmed("\r") == "");
    CHECK(trimmed("\t\n") == "");
    CHECK(trimmed(" \t \r\n") == "");
    CHECK(trimmed("\t\"k\"\t\"v\" \t\r\n") == "\"k\"\t\"v\"");
    CHECK(trimmed("{") == "{");
}

TEST_CASE("Text parser accepts blank lines anywhere", "[text_parser][blank_lines]")
{
    const std::string text =
        "\n"
        "  \t\n"
        "\"root\"\n"
        "\n"
        "{\n"
        "\n"
        "\t\"k\"\t\"v\"\n"
        "\t\n"
        "\t\"sub\"\n"
        "\n"
        "\t{\n"
        "\n"
        "\t\t\"k2\"\t\"v2\"\n"
        " \n"
        "\t}\n"
        "\n"
        "}\n"
        "\n"
        "\t \n";

    SECTION("LF")   { CheckSimpleRoot(ParseText(text)); }
    SECTION("CRLF") { CheckSimpleRoot(ParseText(WithEol(text, "\r\n"))); }
    SECTION("CR")   { CheckSimpleRoot(ParseText(WithEol(text, "\r"))); }
}

TEST_CASE("Text parser reports the blank-line repro without throwing", "[text_parser][blank_lines]")
{
    auto o = ParseText("\"root\"\n{\n\n\t\"k\"\t\"v\"\n}\n");
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "root");
    REQUIRE(o["k"].size() == 1);
    CHECK(o["k"][0].String() == "v");
}

TEST_CASE("Text parser ignores trailing spaces and tabs", "[text_parser][blank_lines]")
{
    const std::string text =
        "\"root\" \t\n"
        "{ \n"
        "\t\"k\"\t\"v\" \t\n"
        "\t\"sub\"\t\n"
        "\t{\t\n"
        "\t\t\"k2\"\t\"v2\"  \n"
        "\t} \n"
        "}\t\n";

    SECTION("LF")   { CheckSimpleRoot(ParseText(text)); }
    SECTION("CRLF") { CheckSimpleRoot(ParseText(WithEol(text, "\r\n"))); }
    SECTION("CR")   { CheckSimpleRoot(ParseText(WithEol(text, "\r"))); }
}
