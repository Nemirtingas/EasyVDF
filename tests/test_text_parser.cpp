#include <sstream>
#include <string>
#include <vector>

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

TEST_CASE("getline returns the last line when there is no trailing EOL", "[text_parser][getline]")
{
    auto lines_of = [](std::string const& text) {
        std::stringstream ss(text, std::ios::in | std::ios::binary);
        std::vector<std::string> lines;
        std::string line;
        while (EasyVDF::Details::getline(ss, line))
            lines.push_back(line);
        return lines;
    };

    CHECK(lines_of("") == std::vector<std::string>{});
    CHECK(lines_of("a") == std::vector<std::string>{ "a" });
    CHECK(lines_of("a\nb") == (std::vector<std::string>{ "a\n", "b" }));
    CHECK(lines_of("a\r\nb") == (std::vector<std::string>{ "a\r\n", "b" }));
    CHECK(lines_of("a\rb") == (std::vector<std::string>{ "a\r", "b" }));
    CHECK(lines_of("a\nb\n") == (std::vector<std::string>{ "a\n", "b\n" }));
    CHECK(lines_of("a\r") == std::vector<std::string>{ "a\r" });
    CHECK(lines_of("a\n\n") == (std::vector<std::string>{ "a\n", "\n" }));
}

TEST_CASE("Text parser handles a file without trailing EOL", "[text_parser][getline]")
{
    const std::string text =
        "\"root\"\n"
        "{\n"
        "\t\"k\"\t\"v\"\n"
        "\t\"sub\"\n"
        "\t{\n"
        "\t\t\"k2\"\t\"v2\"\n"
        "\t}\n"
        "}";

    SECTION("LF")   { CheckSimpleRoot(ParseText(text)); }
    SECTION("CRLF") { CheckSimpleRoot(ParseText(WithEol(text, "\r\n"))); }
    SECTION("CR")   { CheckSimpleRoot(ParseText(WithEol(text, "\r"))); }
    SECTION("Trailing blanks") { CheckSimpleRoot(ParseText(text + "\n \t")); }
}

TEST_CASE("Text parser rejects an unterminated document", "[text_parser][unexpected_eof]")
{
    using Catch::Matchers::Contains;

    SECTION("Unclosed root object")
    {
        CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n"), EasyVDF::ParserException);
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n"), Contains("end of file") && Contains("line 3"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n\n \n"), Contains("end of file") && Contains("line 5"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{"), Contains("end of file") && Contains("line 2"));
    }
    SECTION("Unclosed nested object")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"sub\"\n\t{\n\t\t\"k\"\t\"v\"\n\t}\n"), Contains("end of file") && Contains("line 6"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"sub\"\n\t{\n\t\t\"k\"\t\"v\"\n}\n"), Contains("end of file") && Contains("line 6"));
    }
    SECTION("Root key without object")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n"), Contains("end of file") && Contains("line 1"));
        CHECK_THROWS_WITH(ParseText("\"root\""), Contains("end of file") && Contains("line 1"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n\n \n"), Contains("end of file") && Contains("line 3"));
    }
    SECTION("Item key without value at end of file")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\n"), Contains("end of file") && Contains("line 3"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\""), Contains("end of file") && Contains("line 3"));
    }
    SECTION("Item key without value before object end")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\n}\n"), Contains("Expected object start at line 4"));
    }
    SECTION("Data after object end")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n} \"x\"\n"), Contains("Got datas after object end at line 3"));
    }
}

TEST_CASE("Text parser accepts an empty root key", "[text_parser][unexpected_eof]")
{
    auto o = ParseText("\"\"\n{\n\t\"k\"\t\"v\"\n}\n");
    REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
    CHECK(o.Name() == "");
    REQUIRE(o["k"].size() == 1);
    CHECK(o["k"][0].String() == "v");
}

TEST_CASE("Text parser rejects data after the root object", "[text_parser][trailing_data]")
{
    using Catch::Matchers::Contains;

    SECTION("Second root object")
    {
        CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n}\n\"other\"\n{\n}\n"), EasyVDF::ParserException);
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n}\n\"other\"\n{\n}\n"), Contains("after root object at line 5"));
        CHECK_THROWS_WITH(ParseText("\"root\"\r\n{\r\n}\r\n\r\n\"other\"\r\n{\r\n}"), Contains("after root object at line 5"));
    }
    SECTION("Stray data")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n}\n}\n"), Contains("after root object at line 4"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n}\n\n  x"), Contains("after root object at line 5"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n}\n\"k\"\t\"v\"\n"), Contains("after root object at line 4"));
    }
    SECTION("Blank lines after the root object are accepted")
    {
        auto o = ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n}\n\n \t\n\r\n\r\t ");
        REQUIRE(o.Type() == EasyVDF::ObjectType::Object);
        CHECK(o.Name() == "root");
        REQUIRE(o.Collection().size() == 1);
        CHECK(o["k"][0].String() == "v");
    }
}
