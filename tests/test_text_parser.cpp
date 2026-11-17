#include <sstream>
#include <string>
#include <vector>

#include "../EasyVDF.h"
#include "catch.hpp"

using Catch::Matchers::Contains;
using EasyVDF::KeyValue;
using EasyVDF::ParseError;
using EasyVDF::Type;

namespace {

// Parses through a stream read by small blocks and from memory: both must agree.
KeyValue ParseText(std::string const& text, EasyVDF::ReadOptions options = {})
{
    options.format = EasyVDF::Format::Text;
    KeyValue from_memory = EasyVDF::parse(text, options);

    for (std::size_t chunk_size : { std::size_t(1), std::size_t(3), std::size_t(16 * 1024) })
    {
        std::stringstream ss(text, std::ios::in | std::ios::binary);
        options.chunk_size = chunk_size;
        options.directives = nullptr;
        KeyValue from_stream = EasyVDF::parse(ss, options);
        if (from_stream != from_memory)
            FAIL("stream (chunk size " << chunk_size << ") and memory parsing differ for: " << text);
    }
    return from_memory;
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

void CheckSimpleRoot(KeyValue const& o)
{
    REQUIRE(o.type() == Type::Object);
    CHECK(o.key() == "root");
    REQUIRE(o.size() == 2);
    REQUIRE(o.count("k") == 1);
    CHECK(o["k"].as<std::string>() == "v");
    REQUIRE(o.count("sub") == 1);
    REQUIRE(o["sub"].type() == Type::Object);
    REQUIRE(o["sub"].count("k2") == 1);
    CHECK(o["sub"]["k2"].as<std::string>() == "v2");
}

// Line and column of the ParseError thrown for text, "line:column".
std::string ErrorPosition(std::string const& text)
{
    try
    {
        ParseText(text);
    }
    catch (ParseError const& e)
    {
        return std::to_string(e.line()) + ":" + std::to_string(e.column());
    }
    return "no error";
}

} // namespace

TEST_CASE("Text parser: truncated UTF-8 sequence does not read out of bounds", "[text_parser][utf8]")
{
    // Used to segfault: the 3 bytes lead 0xE2 made the scanner jump past the end of the line.
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"\xE2\n}\n"), ParseError);
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"\xE2\x82\n}\n"), ParseError);
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"\xF0\x9F\n}\n"), ParseError);
    CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\xC3\n}\n"), ParseError);
    CHECK_THROWS_AS(ParseText("\"ro\xF0\x9F\x98\n{\n}\n"), ParseError);
}

TEST_CASE("Text parser: multi-byte UTF-8 strings are kept verbatim", "[text_parser][utf8]")
{
    // A UTF-8 lead byte right before the closing quote must not swallow the quote.
    auto o = ParseText("\"r\xC3\xA9\"\n{\n\t\"k\xE2\x82\xAC\"\t\"\xF0\x9F\x98\x80\"\n\t\"k2\"\t\"a\xC3\xA9\"\n}\n");
    REQUIRE(o.type() == Type::Object);
    CHECK(o.key() == "r\xC3\xA9");
    REQUIRE(o.size() == 2);
    auto it = o.begin();
    CHECK(it->key() == "k\xE2\x82\xAC");
    CHECK(it->as<std::string>() == "\xF0\x9F\x98\x80");
    ++it;
    CHECK(it->key() == "k2");
    CHECK(it->as<std::string>() == "a\xC3\xA9");
}

TEST_CASE("Text parser: UTF-8 is not validated, stray bytes are kept verbatim", "[text_parser][utf8]")
{
    // Policy: strings are opaque bytes, only '"' and '\\' are significant.
    auto o = ParseText("\"root\"\n{\n\t\"\x80\xBF\"\t\"\xF8\xFF\xC3\"\n}\n");
    REQUIRE(o.size() == 1);
    CHECK(o.begin()->key() == "\x80\xBF");
    CHECK(o.begin()->as<std::string>() == "\xF8\xFF\xC3");
    CHECK(ParseText(o.to_text()) == o);
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
    REQUIRE(o.type() == Type::Object);
    CHECK(o.key() == "root");
    REQUIRE(o.count("k") == 1);
    CHECK(o["k"].as<std::string>() == "v");
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
    SECTION("Unclosed root object")
    {
        CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n"), ParseError);
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n"), Contains("end of file") && Contains("line 3"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n\n \n"), Contains("end of file") && Contains("line 5"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{"), Contains("end of file") && Contains("line 2"));
        CHECK_THROWS_WITH(ParseText("\"root\"\r\n{\r\n\t\"k\"\t\"v\"\r\n"), Contains("end of file") && Contains("line 3"));
        CHECK_THROWS_WITH(ParseText("\"root\"\r{\r\t\"k\"\t\"v\"\r"), Contains("end of file") && Contains("line 3"));
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
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\n}\n"), Contains("xpected a value or an object start") && Contains("\"k\"") && Contains("at line 4"));
    }
    SECTION("Data after object end")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n} \"x\"\n"), Contains("after root object at line 3"));
    }
    SECTION("Unterminated string")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\t\"v\n}\n"), Contains("end of file") && Contains("line 3, column 6"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\\\""), Contains("end of file") && Contains("line 3, column 2"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\\"), Contains("end of file") && Contains("line 3, column 2"));
    }
}

TEST_CASE("Text parser rejects empty input", "[text_parser][unexpected_eof]")
{
    // Used to return an empty object.
    CHECK_THROWS_AS(ParseText(""), ParseError);
    CHECK_THROWS_AS(ParseText(" "), ParseError);
    CHECK_THROWS_AS(ParseText("\n\n\t \r\n"), ParseError);
    CHECK_THROWS_AS(ParseText("// only a comment\n"), ParseError);
    CHECK_THROWS_AS(ParseText("\xEF\xBB\xBF"), ParseError);
    CHECK_THROWS_WITH(ParseText("\n \n"), Contains("end of file") && Contains("root key"));

    // Same through the format detection.
    CHECK_THROWS_AS(EasyVDF::parse(""), ParseError);
    CHECK_THROWS_AS(EasyVDF::parse("  \n"), ParseError);
    std::istringstream empty;
    CHECK_THROWS_AS(EasyVDF::parse(empty), ParseError);
}

TEST_CASE("Text parser accepts an empty root key", "[text_parser][unexpected_eof]")
{
    auto o = ParseText("\"\"\n{\n\t\"k\"\t\"v\"\n}\n");
    REQUIRE(o.type() == Type::Object);
    CHECK(o.key() == "");
    REQUIRE(o.count("k") == 1);
    CHECK(o["k"].as<std::string>() == "v");
}

TEST_CASE("Text parser rejects data after the root object", "[text_parser][trailing_data]")
{
    SECTION("Second root object")
    {
        CHECK_THROWS_AS(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n}\n\"other\"\n{\n}\n"), ParseError);
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n}\n\"other\"\n{\n}\n"), Contains("after root object at line 5"));
        CHECK_THROWS_WITH(ParseText("\"root\"\r\n{\r\n}\r\n\r\n\"other\"\r\n{\r\n}"), Contains("after root object at line 5"));
    }
    SECTION("Stray data")
    {
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n}\n}\n"), Contains("after root object at line 4"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n}\n\n  x"), Contains("after root object at line 5"));
        CHECK_THROWS_WITH(ParseText("\"root\"\n{\n}\n\"k\"\t\"v\"\n"), Contains("after root object at line 4"));
        CHECK_THROWS_WITH(ParseText("\"root\" { } {"), Contains("after root object at line 1, column 12"));
    }
    SECTION("Blank lines and comments after the root object are accepted")
    {
        auto o = ParseText("\"root\"\n{\n\t\"k\"\t\"v\"\n}\n\n \t\n\r\n\r\t // the end");
        REQUIRE(o.type() == Type::Object);
        CHECK(o.key() == "root");
        REQUIRE(o.size() == 1);
        CHECK(o["k"].as<std::string>() == "v");
    }
}

TEST_CASE("Text parser reports lines and columns", "[text_parser][position]")
{
    CHECK(ErrorPosition("{\n}\n") == "1:1");
    CHECK(ErrorPosition("\"root\"\n  }\n") == "2:3");
    CHECK(ErrorPosition("\"root\" \"value\"") == "1:8");
    CHECK(ErrorPosition("\"root\"\n{\n\t\"k\" \"v\"\n\t{\n\t}\n}\n") == "4:2");
    CHECK(ErrorPosition("\"root\"\r\n{\r\n\t\"k\" \"v\"\r\n\t{\r\n\t}\r\n}\r\n") == "4:2");
    CHECK(ErrorPosition("\"root\"\r{\r\t\"k\" \"v\"\r\t{\r\t}\r}\r") == "4:2");
    CHECK(ErrorPosition("\"root\"\n{\n\t\"multi\" \"a\nb\nc\" }\n\t}\n") == "6:2");
    // End of file: the last line holding a character, no column.
    CHECK(ErrorPosition("\"root\"\n{\n") == "2:0");
    CHECK(ErrorPosition("\"root\"\n{\n ") == "3:0");

    try
    {
        ParseText("\"root\"\n{\n\t\"k\"\n}\n");
        FAIL("no exception");
    }
    catch (EasyVDF::Error const& e)
    {// ParseError is an EasyVDF::Error and a std::runtime_error.
        CHECK_THAT(e.what(), Contains("line 4, column 1"));
    }
}

TEST_CASE("Text parser: comments", "[text_parser][comments]")
{
    const std::string text =
        "// header comment\n"
        "\"root\" // after the root key\n"
        "{ // after a brace\n"
        "\t// on its own line\n"
        "\t\"k\"\t\"v\" // after a value\n"
        "\t\"sub\" // after a key\n"
        "\t{\n"
        "\t\t\"k2\"\t\"v2\"//no space\n"
        "\t}//}\n"
        "}\n"
        "// trailer";

    SECTION("LF")   { CheckSimpleRoot(ParseText(text)); }
    SECTION("CRLF") { CheckSimpleRoot(ParseText(WithEol(text, "\r\n"))); }
    SECTION("CR")   { CheckSimpleRoot(ParseText(WithEol(text, "\r"))); }

    SECTION("slashes that are not comments")
    {
        auto o = ParseText("\"root\"\n{\n\t\"url\"\t\"http://example.com\" // comment\n\turl2 http://example.com/x\n\t/ single\n\ta/b c//d\n}\n");
        REQUIRE(o.size() == 4);
        CHECK(o["url"].as<std::string>() == "http://example.com");
        CHECK(o["url2"].as<std::string>() == "http://example.com/x");
        CHECK(o["/"].as<std::string>() == "single");
        CHECK(o["a/b"].as<std::string>() == "c//d");
    }
}

TEST_CASE("Text parser: free layout", "[text_parser][layout]")
{
    SECTION("everything on one line")
    {
        CheckSimpleRoot(ParseText("\"root\" { \"k\" \"v\" \"sub\" { \"k2\" \"v2\" } }"));
        CheckSimpleRoot(ParseText("\"root\"{\"k\"\"v\"\"sub\"{\"k2\"\"v2\"}}"));
    }
    SECTION("key and brace on the same line")
    {
        CheckSimpleRoot(ParseText("\"root\" {\n\t\"k\" \"v\"\n\t\"sub\" {\n\t\t\"k2\" \"v2\"\n\t}\n}\n"));
    }
    SECTION("key and value on different lines")
    {
        CheckSimpleRoot(ParseText("\"root\"\n{\n\"k\"\n\n\"v\"\n\"sub\"\n{\n\"k2\"\n\"v2\"}}"));
    }
    SECTION("empty objects")
    {
        auto o = ParseText("\"root\" { \"a\" { } \"b\" {} }");
        REQUIRE(o.size() == 2);
        CHECK(o["a"].is_object());
        CHECK(o["a"].empty());
        CHECK(o["b"].is_object());
        CHECK(o.to_text() == "\"root\"\n{\n\t\"a\"\n\t{\n\t}\n\t\"b\"\n\t{\n\t}\n}\n");
    }
}

TEST_CASE("Text parser: unquoted keys and values", "[text_parser][unquoted]")
{
    auto o = ParseText("root\n{\n\tk v\n\t\"quoted key\" unquoted_value\n\tunquoted_key \"quoted value\"\n\tsub { k2 v2 }\n\tsub2{k3 v3}\n\tback\\slash a\\nb\n}\n");
    CHECK(o.key() == "root");
    REQUIRE(o.size() == 6);
    CHECK(o["k"].as<std::string>() == "v");
    CHECK(o["quoted key"].as<std::string>() == "unquoted_value");
    CHECK(o["unquoted_key"].as<std::string>() == "quoted value");
    CHECK(o["sub"]["k2"].as<std::string>() == "v2");
    CHECK(o["sub2"]["k3"].as<std::string>() == "v3");
    // No escape sequences in unquoted tokens.
    CHECK(o["back\\slash"].as<std::string>() == "a\\nb");

    SECTION("text before a quote is a token, it is not skipped")
    {
        // Used to be accepted as the key "k" alone, "xyz" being silently dropped.
        auto p = ParseText("\"root\"\n{\n\txyz\"k\"\n}\n");
        REQUIRE(p.size() == 1);
        CHECK(p.begin()->key() == "xyz");
        CHECK(p["xyz"].as<std::string>() == "k");

        CHECK_THROWS_AS(ParseText("\"root\"\n{\n\txyz\"k\" \"v\"\n}\n"), ParseError);
        CHECK_THROWS_WITH(ParseText("xyz\"root\"\n{\n}\n"), Contains("line 1, column 4"));
    }
}

TEST_CASE("Text parser: values spanning lines", "[text_parser][multiline]")
{
    auto o = ParseText("\"root\"\n{\n\t\"k\"\t\"line 1\nline 2\r\nline 3\"\n\t\"key\non two lines\" \"v\"\n\t\"last\" \"x\"\n}\n");
    REQUIRE(o.size() == 3);
    CHECK(o["k"].as<std::string>() == "line 1\nline 2\r\nline 3");
    CHECK(o["key\non two lines"].as<std::string>() == "v");
    CHECK(o["last"].as<std::string>() == "x");

    // New lines are written as escape sequences and read back.
    CHECK(o.to_text().find("\"line 1\\nline 2\r\\nline 3\"") != std::string::npos);
    CHECK(ParseText(o.to_text()) == o);
}

TEST_CASE("Text parser: escape sequences are decoded", "[text_parser][escape]")
{
    auto o = ParseText("\"root\"\n{\n\t\"path\"\t\"C:\\\\\"\n\t\"quote\"\t\"a\\\"b\"\n\t\"k\\\\\"\t\"v\"\n\t\"all\"\t\"\\n\\t\\v\\b\\r\\f\\a\\\\\\?\\'\\\"\"\n\t\"unknown\"\t\"C:\\Program Files\\x\\0\"\n}\n");
    REQUIRE(o.size() == 5);
    // "\\" must not escape the closing quote.
    CHECK(o["path"].as<std::string>() == "C:\\");
    CHECK(o["quote"].as<std::string>() == "a\"b");
    CHECK(o["k\\"].as<std::string>() == "v");
    CHECK(o["all"].as<std::string>() == "\n\t\v\b\r\f\a\\?'\"");
    // Not an escape sequence: kept verbatim.
    CHECK(o["unknown"].as<std::string>() == "C:\\Program Files\\x\\0");

    SECTION("escapes can be disabled")
    {
        EasyVDF::ReadOptions options;
        options.escapes = false;
        auto raw = ParseText("\"root\"\n{\n\t\"path\"\t\"C:\\\\dir\\n\"\n\t\"end\\\"\t\"v\"\n}\n", options);
        CHECK(raw["path"].as<std::string>() == "C:\\\\dir\\n");
        CHECK(raw["end\\"].as<std::string>() == "v");

        EasyVDF::WriteOptions write_options;
        write_options.escapes = false;
        CHECK(raw.dump(write_options) == "\"root\"\n{\n\t\"path\"\t\t\"C:\\\\dir\\n\"\n\t\"end\\\"\t\t\"v\"\n}\n");

        raw["quote"] = "a\"b";
        CHECK_THROWS_AS(raw.dump(write_options), EasyVDF::WriteError);
    }
}

TEST_CASE("Text round trip is lossless for any string", "[text_parser][escape]")
{
    std::vector<std::string> strings = {
        "", " ", "plain", "C:\\Program Files (x86)\\Steam", "\\", "\\\\", "\"", "\\\"", "a\"b\\c", "ends with backslash\\",
        "tab\there", "new\nline", "cr\rlf\r\n", "\\n is not a new line", "\\t", "{", "}", "{ \"k\" \"v\" }", "// not a comment",
        "[$WIN32]", "#base", "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", "\x01\x02\x7F\x80\xFF", "  leading and trailing  ", "'", "?", "\a\b\f\v",
    };

    // Every byte value but 0, in one string.
    std::string all_bytes;
    for (int c = 1; c < 256; ++c)
        all_bytes += static_cast<char>(c);
    strings.push_back(all_bytes);

    KeyValue doc("root \"\\\n\t");
    for (std::string const& s : strings)
    {
        doc.add(s, s);
        doc.add("k", s);
        doc.add(s).add(s, s);
    }

    const std::string text = doc.to_text();
    KeyValue parsed = ParseText(text);
    REQUIRE(parsed == doc);
    CHECK(parsed.key() == "root \"\\\n\t");
    CHECK(parsed.to_text() == text);

    auto values = parsed.all("k");
    REQUIRE(values.size() == strings.size());
    std::size_t i = 0;
    for (auto const& value : values)
        CHECK(value.as<std::string>() == strings[i++]);
}

TEST_CASE("Text parser: conditionals", "[text_parser][conditionals]")
{
    const std::string text =
        "\"root\"\n"
        "{\n"
        "\t\"k\"\t\t\"v\"\t\t[$WIN32]\n"
        "\t\"k\"\t\t\"v2\"\t\t[!$WIN32]\n"
        "\t\"plain\"\t\t\"x\"\n"
        "\t\"sub\"\t\t[$X360 || $PS3]\n"
        "\t{\n"
        "\t\t\"k2\"\t\t\"v2\"\n"
        "\t}\n"
        "}\n";

    auto o = ParseText(text);
    REQUIRE(o.size() == 4);

    auto it = o.begin();
    CHECK(it->key() == "k");
    CHECK(it->as<std::string>() == "v");
    CHECK(it->condition() == "$WIN32");
    ++it;
    CHECK(it->key() == "k");
    CHECK(it->as<std::string>() == "v2");
    CHECK(it->condition() == "!$WIN32");
    ++it;
    CHECK(it->key() == "plain");
    CHECK(it->condition().empty());
    ++it;
    CHECK(it->key() == "sub");
    CHECK(it->condition() == "$X360 || $PS3");
    CHECK((*it)["k2"].as<std::string>() == "v2");

    // Kept and written back.
    CHECK(o.to_text() == text);

    SECTION("other layouts")
    {
        auto p = ParseText("root [$A] { k v [$B] k2 \"v2\"[$C]\nsub\n[$D]\n{ } \"[$not]\" \"[$a value]\" }");
        CHECK(p.condition() == "$A");
        CHECK(p.find("k")->condition() == "$B");
        CHECK(p.find("k2")->condition() == "$C");
        CHECK(p.find("sub")->condition() == "$D");
        // Quoted: a plain string.
        CHECK(p["[$not]"].as<std::string>() == "[$a value]");
        CHECK(p.find("[$not]")->condition().empty());
        CHECK(ParseText(p.to_text()) == p);
    }

    SECTION("set_condition")
    {
        KeyValue doc("root");
        doc.add("k", "v").set_condition("$LINUX");
        CHECK(doc.to_text() == "\"root\"\n{\n\t\"k\"\t\t\"v\"\t\t[$LINUX]\n}\n");
        doc.find("k")->set_condition("");
        CHECK(doc.to_text() == "\"root\"\n{\n\t\"k\"\t\t\"v\"\n}\n");

        // The condition is part of the entry: copies keep it, comparisons see it.
        KeyValue a("k", "v"), b("k", "v");
        a.set_condition("$OSX");
        CHECK(a != b);
        KeyValue c(a);
        CHECK(c.condition() == "$OSX");
        CHECK(c == a);
        // Assigning a value keeps the key and the condition.
        a = 5;
        CHECK(a.condition() == "$OSX");
    }

    SECTION("misplaced or malformed conditionals")
    {
        CHECK_THROWS_AS(ParseText("[$WIN32] \"root\" { }"), ParseError);
        CHECK_THROWS_AS(ParseText("\"root\" { [$WIN32] \"k\" \"v\" }"), ParseError);
        CHECK_THROWS_AS(ParseText("\"root\" { \"k\" [$A] [$B] \"v\" }"), ParseError);
        CHECK_THROWS_AS(ParseText("\"root\" { \"k\" [$A] \"v\" [$B] }"), ParseError);
        CHECK_THROWS_AS(ParseText("\"root\" { \"k\" \"v\" [$A] [$B] }"), ParseError);
        CHECK_THROWS_WITH(ParseText("\"root\" { \"k\" \"v\" [$WIN32\n}"), Contains("conditional") && Contains("line 1, column 18"));
        CHECK_THROWS_AS(ParseText("\"root\" { \"k\" \"v\" [$WIN32"), ParseError);
    }
}

TEST_CASE("Text parser: #include and #base directives", "[text_parser][directives]")
{
    const std::string text =
        "#base \"base1.vdf\"\n"
        "#include other.vdf\n"
        "\"#BASE\" \"dir/base 2.vdf\" // case insensitive\n"
        "\"root\"\n"
        "{\n"
        "\t\"k\" \"v\"\n"
        "\t\"#include\" \"not a directive here\"\n"
        "}\n"
        "#include \"after.vdf\"\n";

    std::vector<EasyVDF::Directive> directives;
    EasyVDF::ReadOptions options;
    options.format = EasyVDF::Format::Text;
    options.directives = &directives;
    auto o = EasyVDF::parse(text, options);

    using Kind = EasyVDF::Directive::Kind;
    REQUIRE(directives.size() == 4);
    CHECK(directives[0].kind == Kind::Base);
    CHECK(directives[0].path == "base1.vdf");
    CHECK(directives[1].kind == Kind::Include);
    CHECK(directives[1].path == "other.vdf");
    CHECK(directives[2].kind == Kind::Base);
    CHECK(directives[2].path == "dir/base 2.vdf");
    CHECK(directives[3].kind == Kind::Include);
    CHECK(directives[3].path == "after.vdf");

    CHECK(o.key() == "root");
    REQUIRE(o.size() == 2);
    CHECK(o["k"].as<std::string>() == "v");
    CHECK(o["#include"].as<std::string>() == "not a directive here");

    // Without ReadOptions::directives they are parsed and ignored.
    CHECK(ParseText(text) == o);

    SECTION("a root object may be named like a directive")
    {
        auto p = ParseText("#base\n{\n\tk v\n}\n");
        CHECK(p.key() == "#base");
        CHECK(p["k"].as<std::string>() == "v");
    }

    SECTION("errors")
    {
        CHECK_THROWS_WITH(ParseText("#base \"a.vdf\"\n"), Contains("end of file") && Contains("root key"));
        CHECK_THROWS_AS(ParseText("#base\n"), ParseError);
        CHECK_THROWS_AS(ParseText("#base }\n\"root\" { }"), ParseError);
        CHECK_THROWS_AS(ParseText("#unknown \"a.vdf\"\n\"root\" { }"), ParseError);
    }
}

TEST_CASE("Text parser: byte order marks", "[text_parser][bom]")
{
    const std::string text = "\"root\"\n{\n\t\"k\"\t\"v\"\n\t\"sub\"\n\t{\n\t\t\"k2\"\t\"v2\"\n\t}\n}\n";

    CheckSimpleRoot(ParseText("\xEF\xBB\xBF" + text));
    // Through the format detection too, from memory and from a stream of 1 byte blocks.
    CheckSimpleRoot(EasyVDF::parse("\xEF\xBB\xBF" + text));
    std::istringstream is("\xEF\xBB\xBF" + text);
    EasyVDF::ReadOptions options;
    options.chunk_size = 1;
    CheckSimpleRoot(EasyVDF::parse(is, options));

    // The mark is only skipped at the very start, and only when complete.
    CHECK(ParseText("\xEF\xBB\xBFroot { }").key() == "root");
    CHECK(ParseText("\xEF\xBBroot { }").key() == "\xEF\xBBroot");
    CHECK(ParseText("\xEFroot { }").key() == "\xEFroot");
    CHECK_THROWS_AS(ParseText("\xEF\xBB\xBF\xEF\xBB\xBF\"root\" { } x"), ParseError);

    // UTF-16 is refused with a clear message.
    CHECK_THROWS_WITH(ParseText(std::string("\xFF\xFE\"\0r\0", 6)), Contains("UTF-16"));
    CHECK_THROWS_WITH(ParseText(std::string("\xFE\xFF\0\"\0r", 6)), Contains("UTF-16"));
    CHECK(ParseText("\xFFroot { }").key() == "\xFFroot");
}

TEST_CASE("Text parser: nesting depth limit", "[text_parser][depth]")
{
    auto nested = [](std::size_t depth) {
        std::string text;
        for (std::size_t i = 0; i < depth; ++i)
            text += "\"o\" {\n";
        for (std::size_t i = 0; i < depth; ++i)
            text += "}\n";
        return text;
    };

    EasyVDF::ReadOptions options;
    CHECK_NOTHROW(ParseText(nested(options.max_depth)));
    CHECK_THROWS_WITH(ParseText(nested(options.max_depth + 1)), Contains("nested deeper") && Contains("line " + std::to_string(options.max_depth + 1)));

    // A hostile input does not overflow the stack.
    CHECK_THROWS_AS(ParseText(nested(200000)), ParseError);
    CHECK_THROWS_AS(ParseText(std::string(1000000, '{')), ParseError);

    options.max_depth = 3;
    CHECK_NOTHROW(ParseText(nested(3), options));
    CHECK_THROWS_AS(ParseText(nested(4), options), ParseError);

    options.max_depth = 500;
    KeyValue deep = ParseText(nested(500), options);
    EasyVDF::Node const* node = &deep;
    std::size_t depth = 1;
    while (node->contains("o"))
    {
        node = &(*node)["o"];
        ++depth;
    }
    CHECK(depth == 500);
}

TEST_CASE("Text values convert to numbers", "[text_parser][conversions]")
{
    auto o = ParseText("root { int 42 neg -7 hex 0x2A big 18446744073709551615 float 1.5 exp -2.5e3 bool1 1 bool0 0 word true color \"255 128 0 64\" rgb \"1 2 3\" text abc empty \"\" spaced \" 12 \" }");

    CHECK(o["int"].type() == Type::String);
    CHECK(o["int"].as<int>() == 42);
    CHECK(o["int"].as<uint8_t>() == 42);
    CHECK(o["int"].as<int64_t>() == 42);
    CHECK(o["int"].as<double>() == 42.0);
    CHECK(o["int"].as<bool>());
    CHECK(o["neg"].as<int>() == -7);
    CHECK(o["neg"].as<int8_t>() == -7);
    CHECK_THROWS_AS(o["neg"].as<unsigned>(), EasyVDF::TypeError);
    CHECK(o["hex"].as<int>() == 42);
    CHECK(o["big"].as<uint64_t>() == 18446744073709551615ull);
    CHECK_THROWS_AS(o["big"].as<int64_t>(), EasyVDF::TypeError);
    CHECK(o["float"].as<float>() == 1.5f);
    CHECK(o["float"].as<double>() == 1.5);
    CHECK_THROWS_AS(o["float"].as<int>(), EasyVDF::TypeError);
    CHECK(o["exp"].as<double>() == -2500.0);
    CHECK(o["bool1"].as<bool>());
    CHECK_FALSE(o["bool0"].as<bool>());
    CHECK(o["word"].as<bool>());
    CHECK(o["color"].as<EasyVDF::Color>() == EasyVDF::Color{255, 128, 0, 64});
    CHECK(o["rgb"].as<EasyVDF::Color>() == EasyVDF::Color{1, 2, 3, 255});
    CHECK(o["spaced"].as<int>() == 12);

    CHECK_THROWS_AS(o["text"].as<int>(), EasyVDF::TypeError);
    CHECK_THROWS_AS(o["text"].as<double>(), EasyVDF::TypeError);
    CHECK_THROWS_AS(o["text"].as<bool>(), EasyVDF::TypeError);
    CHECK_THROWS_AS(o["empty"].as<int>(), EasyVDF::TypeError);
    CHECK(o["text"].value_or(5) == 5);
    CHECK(o["empty"].value_or(1.5) == 1.5);
    CHECK_FALSE(o["text"].try_as<int>().has_value());
    CHECK(o["int"].try_as<int>() == 42);
}

TEST_CASE("parse_all reads the files that hold several root objects", "[text_parser][parse_all]")
{
    // The layout of a signed Steam file (installscript.vdf, ...).
    const std::string text =
        "\"InstallScript\"\n"
        "{\n"
        "\t\"Run Process\"\n"
        "\t{\n"
        "\t\t\"process 1\"\t\t\"setup.exe\"\n"
        "\t}\n"
        "}\n"
        "\"kvsignatures\"\n"
        "{\n"
        "\t\"InstallScript\"\t\t\"4284ab2eaacb3706\"\n"
        "}\n";

    // One document is one root object: parse() keeps refusing the rest, and says what to use.
    CHECK_THROWS_WITH(ParseText(text), Contains("after root object at line 8, column 1") && Contains("parse_all"));

    EasyVDF::ReadOptions options;
    std::vector<KeyValue> roots = EasyVDF::parse_all(text, options);
    REQUIRE(roots.size() == 2);
    CHECK(roots[0].key() == "InstallScript");
    CHECK(roots[0]["Run Process"]["process 1"].as<std::string>() == "setup.exe");
    CHECK(roots[1].key() == "kvsignatures");
    CHECK(roots[1]["InstallScript"].as<std::string>() == "4284ab2eaacb3706");

    // Writing them one after the other gives the file back.
    CHECK(roots[0].to_text() + roots[1].to_text() == text);

    SECTION("from a stream, whatever the chunk size")
    {
        for (std::size_t chunk_size : { std::size_t(1), std::size_t(2), std::size_t(7), std::size_t(4096) })
        {
            std::istringstream is(text);
            options.chunk_size = chunk_size;
            CHECK(EasyVDF::parse_all(is, options) == roots);
        }
    }

    SECTION("a single root, directives, comments")
    {
        std::vector<EasyVDF::Directive> directives;
        options.directives = &directives;
        auto single = EasyVDF::parse_all("#base \"a.vdf\"\nroot { k v } // end\n#include b.vdf\n", options);
        REQUIRE(single.size() == 1);
        CHECK(single[0] == EasyVDF::parse("root { k v }"));
        CHECK(directives.size() == 2);

        auto three = EasyVDF::parse_all("a{} b { k v } \"a\" { }");
        REQUIRE(three.size() == 3);
        CHECK(three[0].key() == "a");
        CHECK(three[1]["k"].as<std::string>() == "v");
        CHECK(three[2].key() == "a");
    }

    SECTION("errors are the same as parse()")
    {
        CHECK_THROWS_AS(EasyVDF::parse_all(""), ParseError);
        CHECK_THROWS_AS(EasyVDF::parse_all("  \n"), ParseError);
        CHECK_THROWS_AS(EasyVDF::parse_all("a { } b"), ParseError);
        CHECK_THROWS_AS(EasyVDF::parse_all("a { } b {"), ParseError);
        CHECK_THROWS_AS(EasyVDF::parse_all("a { } }"), ParseError);
        CHECK_THROWS_WITH(EasyVDF::parse_all("a { }\nb c\n"), Contains("line 2, column 3"));
        // Stray data that is not another root object is reported without the hint.
        CHECK_THROWS_WITH(EasyVDF::parse("a { } }"), Contains("after root object") && !Contains("parse_all"));
    }

    SECTION("binary documents hold exactly one root")
    {
        KeyValue doc("root", { {"k", 5} });
        for (EasyVDF::Format format : { EasyVDF::Format::Binary, EasyVDF::Format::Vbkv })
        {
            auto binary_roots = EasyVDF::parse_all(doc.to_binary(format));
            REQUIRE(binary_roots.size() == 1);
            CHECK(binary_roots[0] == doc);
            CHECK_THROWS_AS(EasyVDF::parse_all(doc.to_binary(format) + doc.to_binary(format)), ParseError);
        }
    }
}
