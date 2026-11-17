// Result, ErrorInfo, and the errors of every operation that can fail: code, message, position.

#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <type_traits>
#include <vector>

#include "../EasyVDF.h"
#include "helpers.h"

using Catch::Matchers::Contains;
using EasyVDF::ErrorCode;
using EasyVDF::ErrorInfo;
using EasyVDF::Format;
using EasyVDF::KeyValue;
using EasyVDF::Node;
using EasyVDF::Result;

namespace {

// The error of parse() on data, which must be the same from memory, from a stream
// read by blocks of any size.
ErrorInfo ParseErrorOf(std::string const& data, EasyVDF::ReadOptions options = {})
{
    Result<KeyValue> from_memory = EasyVDF::parse(data, options);
    REQUIRE_FALSE(from_memory.has_value());
    ErrorInfo const& error = from_memory.error();

    for (std::size_t chunk_size : {std::size_t(1), std::size_t(3), std::size_t(7), std::size_t(16 * 1024)})
    {
        INFO("chunk size " << chunk_size);
        std::istringstream is(data);
        options.chunk_size = chunk_size;
        Result<KeyValue> from_stream = EasyVDF::parse(is, options);
        REQUIRE_FALSE(from_stream.has_value());
        CHECK(from_stream.error() == error);
    }

    CHECK(EasyVDF::is_parse_error(error.code));
    return error;
}

// "code line:column", to check an error in a single CHECK.
std::string Where(ErrorInfo const& error)
{
    return std::string(EasyVDF::to_string(error.code)) + " " + std::to_string(error.line) + ":" + std::to_string(error.column);
}

std::string Vbkv(std::string const& payload)
{
    std::string blob = "VBKV";
    const uint32_t crc = EasyVDF::Details::Crc32(payload);
    for (int shift = 0; shift < 32; shift += 8)
        blob += static_cast<char>((crc >> shift) & 0xFF);
    return blob + payload;
}

KeyValue Sample()
{
    KeyValue doc("Root", {
        {"string", "value"},
        {"int", int32_t(-5)},
        {"float", 1.5f},
        {"sub", {
            {"color", EasyVDF::Color{1, 2, 3, 4}},
            {"u64", uint64_t(1) << 40},
            {"pointer", EasyVDF::Pointer{7}},
            {"i64", -(int64_t(1) << 40)},
        }},
    });
    return doc;
}

bool FileExists(const char* path)
{
    return std::ifstream(path, std::ios::binary).is_open();
}

} // namespace

/////////////////////////////////////////////////////////////////////
// ErrorCode, ErrorInfo
/////////////////////////////////////////////////////////////////////

TEST_CASE("ErrorCode names", "[result][error_info]")
{
    const std::pair<ErrorCode, const char*> names[] = {
        {ErrorCode::Syntax, "Syntax"},
        {ErrorCode::UnexpectedEof, "UnexpectedEof"},
        {ErrorCode::EmptyInput, "EmptyInput"},
        {ErrorCode::TrailingData, "TrailingData"},
        {ErrorCode::InvalidRoot, "InvalidRoot"},
        {ErrorCode::UnknownNodeType, "UnknownNodeType"},
        {ErrorCode::UnsupportedNodeType, "UnsupportedNodeType"},
        {ErrorCode::UnsupportedEncoding, "UnsupportedEncoding"},
        {ErrorCode::ChecksumMismatch, "ChecksumMismatch"},
        {ErrorCode::DepthExceeded, "DepthExceeded"},
        {ErrorCode::Io, "Io"},
        {ErrorCode::Write, "Write"},
        {ErrorCode::Type, "Type"},
        {ErrorCode::Key, "Key"},
    };
    for (auto const& [code, name] : names)
    {
        CHECK(std::string(to_string(code)) == name);   // found by ADL
        std::ostringstream os;
        os << code;
        CHECK(os.str() == name);
    }
}

TEST_CASE("ErrorInfo", "[result][error_info]")
{
    const ErrorInfo error(ErrorCode::Syntax, "bad token at line 3, column 4", 3, 4);
    CHECK(error.code == ErrorCode::Syntax);
    CHECK(error.message == "bad token at line 3, column 4");
    CHECK(error.line == 3);
    CHECK(error.column == 4);

    const ErrorInfo io(ErrorCode::Io, "can't open");
    CHECK(io.line == 0);
    CHECK(io.column == 0);

    CHECK(error == ErrorInfo(ErrorCode::Syntax, "bad token at line 3, column 4", 3, 4));
    CHECK(error != ErrorInfo(ErrorCode::UnexpectedEof, "bad token at line 3, column 4", 3, 4));
    CHECK(error != ErrorInfo(ErrorCode::Syntax, "other", 3, 4));
    CHECK(error != ErrorInfo(ErrorCode::Syntax, "bad token at line 3, column 4", 2, 4));
    CHECK(error != ErrorInfo(ErrorCode::Syntax, "bad token at line 3, column 4", 3, 5));

    std::ostringstream os;
    os << error;
    CHECK(os.str() == "Syntax: bad token at line 3, column 4");
}

/////////////////////////////////////////////////////////////////////
// Result<T>
/////////////////////////////////////////////////////////////////////

TEST_CASE("Result<T> holds a value or an error", "[result]")
{
    SECTION("a value")
    {
        Result<std::string> r = "text";
        REQUIRE(r.has_value());
        CHECK(static_cast<bool>(r));
        CHECK(*r == "text");
        CHECK(r->size() == 4);
        CHECK(r.value() == "text");
        CHECK(r.value_or("other") == "text");

        *r += "!";
        CHECK(r.value() == "text!");
        r.value() += "?";
        CHECK(*r == "text!?");

        Result<std::string> const& cr = r;
        CHECK(*cr == "text!?");
        CHECK(cr->size() == 6);
        CHECK(cr.value() == "text!?");
    }

    SECTION("an error")
    {
        Result<std::string> r = ErrorInfo(ErrorCode::Io, "can't open");
        REQUIRE_FALSE(r.has_value());
        CHECK_FALSE(static_cast<bool>(r));
        CHECK(r.error() == ErrorInfo(ErrorCode::Io, "can't open"));
        CHECK(r.value_or("default") == "default");
        CHECK(std::move(r).value_or("moved default") == "moved default");

        // The error can be changed in place.
        r.error().message += " (detail)";
        CHECK(r.error().message == "can't open (detail)");
    }

    SECTION("conversions")
    {
        // Implicit for implicitly convertible values, explicit otherwise.
        static_assert(std::is_convertible_v<const char*, Result<std::string>>);
        static_assert(std::is_convertible_v<int, Result<double>>);
        static_assert(std::is_convertible_v<ErrorInfo, Result<int>>);
        static_assert(!std::is_convertible_v<std::string, Result<KeyValue>>);
        static_assert(std::is_constructible_v<Result<KeyValue>, std::string>);
        // A Result is not a value of another Result.
        static_assert(!std::is_constructible_v<Result<bool>, Result<int>>);
        static_assert(!std::is_constructible_v<Result<Node const&>, Node&&>);

        Result<double> d = 2;
        CHECK(*d == 2.0);
        Result<KeyValue> doc(std::string("Root"));
        CHECK(doc->key() == "Root");

        // A Result of Result.
        Result<Result<int>> nested = Result<int>(3);
        CHECK(**nested == 3);
    }

    SECTION("moving the value out")
    {
        Result<std::unique_ptr<int>> r = std::make_unique<int>(5);
        std::unique_ptr<int> p = std::move(r).value();
        REQUIRE(p);
        CHECK(*p == 5);

        Result<std::unique_ptr<int>> r2 = std::make_unique<int>(6);
        std::unique_ptr<int> p2 = *std::move(r2);
        CHECK(*p2 == 6);

        Result<std::unique_ptr<int>> r3 = std::make_unique<int>(7);
        CHECK(*std::move(r3).value_or(nullptr) == 7);
    }

    SECTION("copy and assignment")
    {
        Result<std::string> a = "a";
        Result<std::string> b = ErrorInfo(ErrorCode::Key, "missing");
        Result<std::string> c = a;
        CHECK(*c == "a");
        c = b;
        CHECK(c.error().code == ErrorCode::Key);
        c = std::move(a);
        CHECK(*c == "a");
    }
}

TEST_CASE("Result<T> combinators", "[result][combinators]")
{
    const Result<int> ok = 20;
    const Result<int> failed = ErrorInfo(ErrorCode::Type, "not a number");

    SECTION("map")
    {
        Result<std::string> mapped = ok.map([](int v) { return std::to_string(v * 2); });
        CHECK(*mapped == "40");

        Result<std::string> not_mapped = failed.map([](int v) { return std::to_string(v); });
        CHECK(not_mapped.error() == failed.error());

        // A function returning void gives a Result<void>.
        int seen = 0;
        Result<void> done = ok.map([&](int v) { seen = v; });
        CHECK(done.has_value());
        CHECK(seen == 20);
        Result<void> skipped = failed.map([&](int) { seen = -1; });
        CHECK(skipped.error().code == ErrorCode::Type);
        CHECK(seen == 20);

        // A function returning a reference gives a copy.
        std::string text = "text";
        auto copied = ok.map([&](int) -> std::string& { return text; });
        static_assert(std::is_same_v<decltype(copied), Result<std::string>>);

        // The value is moved along from a temporary Result.
        Result<std::unique_ptr<int>> moved = Result<std::unique_ptr<int>>(std::make_unique<int>(3))
            .map([](std::unique_ptr<int> p) { *p += 1; return p; });
        CHECK(**moved == 4);

        // Non-const lvalue: the function may modify the value.
        Result<int> mutable_result = 1;
        CHECK(mutable_result.map([](int& v) { v = 2; }).has_value());
        CHECK(*mutable_result == 2);
    }

    SECTION("and_then")
    {
        auto half = [](int v) -> Result<int> {
            if (v % 2 != 0)
                return ErrorInfo(ErrorCode::Type, "odd number " + std::to_string(v));
            return v / 2;
        };

        CHECK(*ok.and_then(half) == 10);
        CHECK(*ok.and_then(half).and_then(half) == 5);
        CHECK(ok.and_then(half).and_then(half).and_then(half).error().message == "odd number 5");
        CHECK(failed.and_then(half).error() == failed.error());

        // The function decides the value type.
        Result<std::string> text = ok.and_then([](int v) -> Result<std::string> { return std::to_string(v); });
        CHECK(*text == "20");

        // Chaining on references.
        KeyValue doc("Root", { {"a", { {"b", "x"} }} });
        Result<Node const&> b = std::as_const(doc).at("a").and_then([](Node const& a) { return a.at("b"); });
        REQUIRE(b);
        CHECK(&*b == &doc["a"]["b"]);
        Result<Node const&> missing = std::as_const(doc).at("a").and_then([](Node const& a) { return a.at("c"); });
        CHECK(missing.error().message == "EasyVDF: key \"c\" not found");
    }

    SECTION("or_else")
    {
        auto recover = [](ErrorInfo const& error) -> Result<int> {
            if (error.code == ErrorCode::Type)
                return -1;
            return error;
        };
        CHECK(*ok.or_else(recover) == 20);
        CHECK(*failed.or_else(recover) == -1);

        const Result<int> io = ErrorInfo(ErrorCode::Io, "disk");
        CHECK(io.or_else(recover).error() == io.error());

        // From a temporary, the error is moved to the function.
        Result<int> recovered = Result<int>(ErrorInfo(ErrorCode::Key, "k")).or_else([](ErrorInfo&& error) -> Result<int> {
            return static_cast<int>(error.message.size());
        });
        CHECK(*recovered == 1);
    }

    SECTION("map_error")
    {
        auto add_context = [](ErrorInfo error) {
            error.message = "config.vdf: " + error.message;
            return error;
        };
        CHECK(*ok.map_error(add_context) == 20);
        Result<int> with_context = failed.map_error(add_context);
        CHECK(with_context.error().message == "config.vdf: not a number");
        CHECK(with_context.error().code == ErrorCode::Type);
    }
}

TEST_CASE("Result<T&> refers to a node", "[result][reference]")
{
    KeyValue doc("Root", { {"appid", 440}, {"sub", { {"k", "v"} }} });

    SECTION("found")
    {
        Result<Node&> appid = doc.at("appid");
        REQUIRE(appid);
        CHECK(&*appid == &doc["appid"]);
        CHECK(appid->value_or(0) == 440);
        CHECK(&appid.value() == &doc["appid"]);

        // Writes go to the document.
        *appid = 441;
        CHECK(doc["appid"].value_or(0) == 441);
        appid.value() = 442;
        CHECK(doc["appid"].value_or(0) == 442);

        // value_or() copies.
        Node copy = appid.value_or(Node());
        copy = 1;
        CHECK(doc["appid"].value_or(0) == 442);
    }

    SECTION("missing")
    {
        Result<Node&> missing = doc.at("missing");
        REQUIRE_FALSE(missing);
        CHECK(missing.error() == ErrorInfo(ErrorCode::Key, "EasyVDF: key \"missing\" not found"));
        CHECK(missing.value_or(5).value_or(0) == 5);
    }

    SECTION("same error as at()")
    {
        CHECK(&*doc.at("sub") == &doc["sub"]);
    }

    SECTION("const and conversions")
    {
        Result<Node const&> const_sub = std::as_const(doc).at("sub");
        static_assert(std::is_same_v<decltype(*const_sub), Node const&>);
        CHECK(const_sub->size() == 1);

        // Node& converts to Node const&, with the value or the error.
        Result<Node const&> converted = doc.at("sub");
        CHECK(&*converted == &doc["sub"]);
        Result<Node const&> converted_error = doc.at("x");
        CHECK(converted_error.error().code == ErrorCode::Key);

        // Assignment rebinds, like a pointer.
        Result<Node const&> r = std::as_const(doc).at("appid");
        r = std::as_const(doc).at("sub");
        CHECK(&*r == &doc["sub"]);
        CHECK(doc["appid"].value_or(0) == 440);
    }

    SECTION("combinators")
    {
        Result<int> appid = doc.at("appid").map([](Node const& n) { return n.value_or(0); });
        CHECK(*appid == 440);

        Result<std::string> k = doc.at("sub")
            .and_then([](Node& sub) { return sub.at("k"); })
            .map([](Node const& n) { return n.value_or(std::string()); });
        CHECK(*k == "v");

        Result<Node&> fallback = doc.at("missing").or_else([&](ErrorInfo const&) { return doc.at("appid"); });
        CHECK(&*fallback == &doc["appid"]);

        Result<Node&> with_context = doc.at("missing").map_error([](ErrorInfo e) { e.message += "!"; return e; });
        CHECK(with_context.error().message == "EasyVDF: key \"missing\" not found!");
    }
}

TEST_CASE("Result<void> is a success or an error", "[result][void]")
{
    const Result<void> ok;
    const Result<void> failed = ErrorInfo(ErrorCode::Write, "can't write");

    CHECK(ok.has_value());
    CHECK(static_cast<bool>(ok));
    CHECK_FALSE(failed.has_value());
    CHECK(failed.error().code == ErrorCode::Write);

    CHECK(*ok.map([] { return 3; }) == 3);
    CHECK(failed.map([] { return 3; }).error() == failed.error());

    int calls = 0;
    CHECK(ok.and_then([&]() -> Result<void> { ++calls; return {}; }).has_value());
    CHECK(failed.and_then([&]() -> Result<void> { ++calls; return {}; }).error() == failed.error());
    CHECK(calls == 1);
    CHECK(ok.and_then([]() -> Result<int> { return ErrorInfo(ErrorCode::Io, "later"); }).error().message == "later");

    CHECK(failed.or_else([](ErrorInfo const&) -> Result<void> { return {}; }).has_value());
    CHECK(ok.or_else([](ErrorInfo const&) -> Result<void> { return ErrorInfo(ErrorCode::Io, "x"); }).has_value());
    CHECK(failed.map_error([](ErrorInfo e) { e.code = ErrorCode::Io; return e; }).error().code == ErrorCode::Io);

    Result<void> moved = Result<void>(ErrorInfo(ErrorCode::Io, "m")).map_error([](ErrorInfo&& e) { return std::move(e); });
    CHECK(moved.error().message == "m");
}

/////////////////////////////////////////////////////////////////////
// parse, parse_all
/////////////////////////////////////////////////////////////////////

TEST_CASE("parse and parse_all succeed", "[result][parse]")
{
    const std::string text = "\"root\"\n{\n\t\"k\"\t\"v\"\n}\n";
    Result<KeyValue> doc = EasyVDF::parse(text);
    REQUIRE(doc);
    CHECK(*doc == EasyVDF::parse(text));
    CHECK((*doc)["k"].value_or("") == "v");

    std::istringstream is(text);
    CHECK(EasyVDF::parse(is).value() == *doc);

    const KeyValue sample = Sample();
    for (Format format : {Format::Binary, Format::Vbkv})
    {
        CHECK(EasyVDF::parse(sample.to_binary(format).value()).value() == sample);
        std::istringstream binary(sample.to_binary(format).value());
        CHECK(EasyVDF::parse(binary).value() == sample);
    }

    Result<std::vector<KeyValue>> roots = EasyVDF::parse_all("a { k 1 }\nb { k 2 }\n");
    REQUIRE(roots);
    REQUIRE(roots->size() == 2);
    CHECK((*roots)[1].key() == "b");
    std::istringstream all("a { }");
    CHECK(EasyVDF::parse_all(all).value().size() == 1);
}

TEST_CASE("parse: text errors have a code and a position", "[result][parse][text]")
{
    EasyVDF::ReadOptions text;
    text.format = Format::Text;

    // Empty input.
    CHECK(Where(ParseErrorOf("")) == "EmptyInput 0:0");
    CHECK(Where(ParseErrorOf("  \n")) == "EmptyInput 1:0");
    CHECK(Where(ParseErrorOf("// only a comment\n")) == "EmptyInput 1:0");
    CHECK(Where(ParseErrorOf("\xEF\xBB\xBF")) == "EmptyInput 1:0");
    CHECK(Where(ParseErrorOf("#base \"a.vdf\"\n")) == "EmptyInput 1:0");

    // Truncated input: the end of file, on the last line holding a character.
    CHECK(Where(ParseErrorOf("\"root\"\n{\n\t\"k\"\t\"v\"\n")) == "UnexpectedEof 3:0");
    CHECK(Where(ParseErrorOf("\"root\"\n{")) == "UnexpectedEof 2:0");
    CHECK(Where(ParseErrorOf("\"root\"")) == "UnexpectedEof 1:0");
    CHECK(Where(ParseErrorOf("\"root\"\n{\n\t\"k\"\n")) == "UnexpectedEof 3:0");
    CHECK(Where(ParseErrorOf("\"root\"\r\n{\r\n\t\"k\"\t\"v\"\r\n")) == "UnexpectedEof 3:0");
    // An unterminated string: where it starts.
    CHECK(Where(ParseErrorOf("\"root\"\n{\n\t\"k\"\t\"v\n}\n")) == "UnexpectedEof 3:6");
    CHECK(Where(ParseErrorOf("\"root\"\n{\n\t\"k\\")) == "UnexpectedEof 3:2");
    CHECK(Where(ParseErrorOf("\"root\" { \"k\" \"v\" [$WIN32")) == "UnexpectedEof 1:18");

    // Syntax errors: where the token starts.
    CHECK(Where(ParseErrorOf("\"root\"\n{\n\t\"k\"\n}\n")) == "Syntax 4:1");
    CHECK(Where(ParseErrorOf("{ }")) == "Syntax 1:1");
    CHECK(Where(ParseErrorOf("\"root\" \"v\"", text)) == "Syntax 1:8");
    CHECK(Where(ParseErrorOf("\"root\" { { } }")) == "Syntax 1:10");
    CHECK(Where(ParseErrorOf("\"root\" { \"k\" \"v\" [$WIN32\n}")) == "Syntax 1:18");
    CHECK(Where(ParseErrorOf("\"root\" { \"k\" [$A] [$B] \"v\" }")) == "Syntax 1:19");

    // Trailing data.
    CHECK(Where(ParseErrorOf("\"root\" { } {")) == "TrailingData 1:12");
    CHECK(Where(ParseErrorOf("\"root\"\n{\n}\n\n  x")) == "TrailingData 5:3");
    const ErrorInfo several = ParseErrorOf("a { }\nb { }\n");
    CHECK(Where(several) == "TrailingData 2:1");
    CHECK_THAT(several.message, Contains("parse_all"));

    // UTF-16.
    CHECK(Where(ParseErrorOf(std::string("\xFF\xFE\"\0r\0", 6))) == "UnsupportedEncoding 1:1");

    // The messages are the ones of the exceptions, position included.
    CHECK(ParseErrorOf("\"root\"\n{\n\t\"k\"\n}\n").message == "Expected a value or an object start ('{') after key \"k\" at line 4, column 1");
}

TEST_CASE("parse: depth limit", "[result][parse][depth]")
{
    auto nested_text = [](std::size_t depth) {
        std::string text = "\"root\"\n";
        for (std::size_t i = 0; i < depth; ++i)
            text += "{\n\"k\"\n";
        text += "\"v\"\n";
        return text + std::string(depth, '}');
    };
    EasyVDF::ReadOptions options;
    CHECK(EasyVDF::parse(nested_text(options.max_depth)).has_value());
    const ErrorInfo text = ParseErrorOf(nested_text(options.max_depth + 1));
    CHECK(Where(text) == "DepthExceeded " + std::to_string(2 * (options.max_depth + 1)) + ":1");

    auto nested_binary = [](std::size_t depth) {
        std::string blob;
        for (std::size_t i = 0; i < depth; ++i)
            blob += std::string("\x00o\x00", 3);
        return blob + std::string(depth, '\x08');
    };
    CHECK(EasyVDF::parse(nested_binary(options.max_depth)).has_value());
    CHECK(Where(ParseErrorOf(nested_binary(options.max_depth + 1))) == "DepthExceeded 0:0");
    CHECK(Where(ParseErrorOf(Vbkv(nested_binary(100000)))) == "DepthExceeded 0:0");

    options.max_depth = 2;
    CHECK(Where(ParseErrorOf(nested_binary(3), options)) == "DepthExceeded 0:0");
    CHECK(ParseErrorOf(nested_text(3), options).code == ErrorCode::DepthExceeded);
}

TEST_CASE("parse: binary errors", "[result][parse][binary]")
{
    const KeyValue sample = Sample();

    SECTION("truncated input, whatever the chunk size")
    {
        for (Format format : {Format::Binary, Format::Vbkv})
        {
            const std::string blob = sample.to_binary(format).value();
            CHECK(ParseErrorOf(blob.substr(0, 0)).code == ErrorCode::EmptyInput);
            // The last byte is the optional list terminator: the prefix without it is complete.
            for (std::size_t length = 1; length + 1 < blob.size(); ++length)
            {
                INFO("format " << static_cast<int>(format) << ", prefix length " << length);
                // "V", "VB", "VBK" are not a VBKV header: they are read as an unquoted text key.
                const bool text = format == Format::Vbkv && length < 4;
                CHECK(Where(ParseErrorOf(blob.substr(0, length))) == (text ? "UnexpectedEof 1:0" : "UnexpectedEof 0:0"));
            }
        }
    }

    SECTION("node types")
    {
        for (int type : {5, 9})
        {
            std::string blob("\x00Root\x00", 6);
            blob += static_cast<char>(type);
            blob += std::string("key\x00value\x00\x08", 11);
            CHECK(Where(ParseErrorOf(blob)) == "UnsupportedNodeType 0:0");
            CHECK(Where(ParseErrorOf(Vbkv(blob))) == "UnsupportedNodeType 0:0");
        }
        for (int type : {12, 0x42, 0xFF})
        {
            std::string blob("\x00Root\x00", 6);
            blob += static_cast<char>(type);
            blob += std::string("key\x00value\x00\x08", 11);
            const ErrorInfo error = ParseErrorOf(blob);
            CHECK(Where(error) == "UnknownNodeType 0:0");
            CHECK_THAT(error.message, Contains(std::to_string(type)));
        }
    }

    SECTION("trailing data")
    {
        std::string v1 = sample.to_binary(Format::Binary).value();
        std::string payload = sample.to_binary(Format::Vbkv).value().substr(8);
        v1.pop_back();
        payload.pop_back();
        CHECK(Where(ParseErrorOf(v1 + "\x08\x08")) == "TrailingData 0:0");
        CHECK(Where(ParseErrorOf(v1 + "\x0b")) == "TrailingData 0:0");
        CHECK(Where(ParseErrorOf(v1 + v1)) == "TrailingData 0:0");
        CHECK(Where(ParseErrorOf(Vbkv(payload + "garbage"))) == "TrailingData 0:0");
        CHECK(Where(ParseErrorOf(Vbkv(payload + payload))) == "TrailingData 0:0");
    }

    SECTION("checksum")
    {
        std::string blob = sample.to_binary(Format::Vbkv).value();
        blob[4] ^= 0x01;
        const ErrorInfo error = ParseErrorOf(blob);
        CHECK(Where(error) == "ChecksumMismatch 0:0");
        CHECK_THAT(error.message, Contains("checksum mismatch"));

        // Corrupting the payload gives a checksum error or a structural one, never a document.
        const std::string good = sample.to_binary(Format::Vbkv).value();
        for (std::size_t position = 8; position < good.size(); ++position)
        {
            std::string corrupted = good;
            corrupted[position] ^= 0x20;
            CHECK_FALSE(EasyVDF::parse(corrupted).has_value());
        }

        // Without verification, a wrong checksum is accepted.
        EasyVDF::ReadOptions options;
        options.verify_crc = false;
        CHECK(EasyVDF::parse(blob, options).value() == sample);
    }

    SECTION("the root must be an object")
    {
        EasyVDF::ReadOptions options;
        options.format = Format::Binary;
        CHECK(Where(ParseErrorOf(sample.to_text().value(), options)) == "InvalidRoot 0:0");
        CHECK(Where(ParseErrorOf(std::string("\x01k\x00v\x00", 5), options)) == "InvalidRoot 0:0");
    }
}

TEST_CASE("parse_all: errors from memory and from streams", "[result][parse]")
{
    for (const char* text : {"", "  \n", "a { } b", "a { } b {", "a { } }", "a { }\nb c\n", "a { \"k\" }"})
    {
        INFO(text);
        Result<std::vector<KeyValue>> roots = EasyVDF::parse_all(text);
        REQUIRE_FALSE(roots);

        std::istringstream is(text);
        CHECK(EasyVDF::parse_all(is).error() == roots.error());
    }
    CHECK(Where(EasyVDF::parse_all("a { }\nb c\n").error()) == "Syntax 2:3");
    CHECK(Where(EasyVDF::parse_all("a { } b").error()) == "UnexpectedEof 1:0");
    CHECK(Where(EasyVDF::parse_all("a { } }").error()) == "Syntax 1:7");

    const KeyValue doc("Root", { {"k", "v"} });
    CHECK(Where(EasyVDF::parse_all(doc.to_binary().value() + doc.to_binary().value()).error()) == "TrailingData 0:0");
}

TEST_CASE("parse: a stream read error is an Io error", "[result][parse][io]")
{
    struct FailingBuffer : std::streambuf
    {
        std::string data;
        std::size_t position = 0;
        char current = 0;

        explicit FailingBuffer(std::string d) : data(std::move(d)) {}

        int_type underflow() override
        {
            if (position == data.size())
                throw std::runtime_error("device error");

            current = data[position++];
            setg(&current, &current, &current + 1);
            return traits_type::to_int_type(current);
        }
    };

    // Whatever the parser saw before the failure: nothing, a truncated document,
    // or a complete one (the stream fails while looking for trailing data).
    for (const char* data : {"", "\"root\" { \"k\" \"v\"", "\"root\" { \"k\" \"v\" }", "\"root\" { \"k\" \"v"})
    {
        INFO(data);
        for (std::size_t chunk_size : {std::size_t(1), std::size_t(4), std::size_t(16 * 1024)})
        {
            EasyVDF::ReadOptions options;
            options.chunk_size = chunk_size;

            FailingBuffer buffer(data);
            std::istream is(&buffer);
            Result<KeyValue> doc = EasyVDF::parse(is, options);
            REQUIRE_FALSE(doc);
            CHECK(doc.error() == ErrorInfo(ErrorCode::Io, "EasyVDF: failed to read the input stream"));

            FailingBuffer buffer_all(data);
            std::istream is_all(&buffer_all);
            CHECK(EasyVDF::parse_all(is_all, options).error().code == ErrorCode::Io);

            FailingBuffer buffer_throw(data);
            std::istream is_throw(&buffer_throw);
        }
    }

    // A binary document too.
    const std::string blob = Sample().to_binary().value();
    FailingBuffer buffer(blob);
    std::istream is(&buffer);
    CHECK(EasyVDF::parse(is).error().code == ErrorCode::Io);
}

/////////////////////////////////////////////////////////////////////
// load, load_all
/////////////////////////////////////////////////////////////////////

TEST_CASE("load and load_all", "[result][load]")
{
    SECTION("files")
    {
        Result<KeyValue> doc = EasyVDF::load("binary.vdf");
        REQUIRE(doc);
        CHECK(*doc == EasyVDF::load("binary.vdf"));
        CHECK(EasyVDF::load_all("linux_eol.vdf").value() == EasyVDF::load_all("linux_eol.vdf"));
    }

    SECTION("missing file")
    {
        Result<KeyValue> doc = EasyVDF::load("no_such_file.vdf");
        REQUIRE_FALSE(doc);
        CHECK(doc.error() == ErrorInfo(ErrorCode::Io, "EasyVDF: can't open \"no_such_file.vdf\" for reading"));

        Result<std::vector<KeyValue>> all = EasyVDF::load_all("no_such_file.vdf");
        CHECK(all.error() == doc.error());
    }

    SECTION("malformed file")
    {
        const char* path = "test_result_malformed.tmp";
        {
            std::ofstream f(path, std::ios::binary);
            f << "\"root\"\n{\n\t\"k\"\n}\n";
        }
        CHECK(Where(EasyVDF::load(path).error()) == "Syntax 4:1");
        CHECK(Where(EasyVDF::load_all(path).error()) == "Syntax 4:1");
        std::remove(path);
    }
}

/////////////////////////////////////////////////////////////////////
// dump, to_text, to_binary, write, save
/////////////////////////////////////////////////////////////////////

TEST_CASE("dump, to_text, to_binary, write and save succeed", "[result][dump]")
{
    const KeyValue sample = Sample();
    CHECK(sample.dump() == sample.to_text());
    CHECK(sample.dump(Format::Binary) == sample.to_binary());
    CHECK(sample.dump(Format::Vbkv) == sample.to_binary(Format::Vbkv));
    CHECK(EasyVDF::parse(sample.to_text().value()).value().to_text() == sample.to_text());
    CHECK(EasyVDF::parse(sample.to_binary(Format::Vbkv).value()) == sample);

    std::ostringstream os;
    CHECK(sample.write(os, Format::Binary).has_value());
    CHECK(os.str() == sample.to_binary().value());

    const char* path = "test_result_save.tmp";
    REQUIRE(sample.save(path, Format::Vbkv).has_value());
    CHECK(EasyVDF::load(path).value() == sample);
    std::remove(path);
}

TEST_CASE("dump, to_text, to_binary, write and save: write errors", "[result][dump][errors]")
{
    KeyValue with_null("root");
    with_null.add("k", std::string("a\0b", 3));
    KeyValue null_key("root");
    null_key.add(std::string("a\0b", 3), "v");
    KeyValue quote("root");
    quote.add("k", "a\"b");
    EasyVDF::WriteOptions raw;
    raw.escapes = false;

    struct Case
    {
        KeyValue const& doc;
        EasyVDF::WriteOptions options;
        const char* message;
    };
    const KeyValue none;
    const KeyValue scalar("k", 5);
    const Case cases[] = {
        {none, Format::Text, "EasyVDF: can't write a document without value (Type::None)"},
        {none, Format::Vbkv, "EasyVDF: can't write a document without value (Type::None)"},
        {scalar, Format::Binary, "EasyVDF: can't write a document whose root is a Int32, it must be an object"},
        {with_null, Format::Binary, "EasyVDF: can't write a key or a string containing a null character in binary"},
        {with_null, Format::Vbkv, "EasyVDF: can't write a key or a string containing a null character in binary"},
        {null_key, Format::Binary, "EasyVDF: can't write a key or a string containing a null character in binary"},
        {quote, raw, "EasyVDF: can't write a string containing '\"' with escape sequences disabled"},
    };

    for (Case const& c : cases)
    {
        INFO(c.message);
        const ErrorInfo expected(ErrorCode::Write, c.message);

        CHECK(c.doc.dump(c.options).error() == expected);

        // Nothing is written to the stream.
        std::ostringstream os;
        CHECK(c.doc.write(os, c.options).error() == expected);
        CHECK(os.str().empty());

        // No file is created.
        const char* path = "test_result_not_created.tmp";
        CHECK(c.doc.save(path, c.options).error() == expected);
        CHECK_FALSE(FileExists(path));
        CHECK_FALSE(FileExists(path));
    }

    CHECK(none.to_text().error().code == ErrorCode::Write);
    CHECK(scalar.to_binary(Format::Vbkv).error().code == ErrorCode::Write);
}

TEST_CASE("save: I/O errors", "[result][save][io]")
{
    const KeyValue doc("root");
    Result<void> saved = doc.save("no_such_directory/file.vdf");
    REQUIRE_FALSE(saved);
    CHECK(saved.error() == ErrorInfo(ErrorCode::Io, "EasyVDF: can't open \"no_such_directory/file.vdf\" for writing"));
}

/////////////////////////////////////////////////////////////////////
// The other errors of the throwing API
/////////////////////////////////////////////////////////////////////

TEST_CASE("as<T>() errors have the Type code", "[result][type]")
{
    KeyValue doc("root", { {"appid", 440}, {"name", "x"} });

    Result<int> missing = doc["missing"].as<int>();
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == ErrorInfo(ErrorCode::Type, "EasyVDF: can't convert a missing value (Type::None) to an integer"));

    Result<int> not_a_number = doc["name"].as<int>();
    CHECK(not_a_number.error() == ErrorInfo(ErrorCode::Type, "EasyVDF: can't convert a String value to an integer"));

    CHECK(doc["appid"].as<int>() == 440);
    CHECK(doc["appid"].as<std::string>() == "440");
    CHECK(doc["appid"].as<uint8_t>().error().code == ErrorCode::Type);

    // A scalar used as an object (doc["appid"]["x"] = 1) has no error value: it aborts,
    // see the no_exceptions_scalar_aborts CTest test.
}

TEST_CASE("Result comparisons", "[result][comparison]")
{
    const Result<int> five = 5;
    const Result<int> other_five = 5;
    const Result<int> six = 6;
    const Result<int> failed = ErrorInfo(ErrorCode::Type, "m");
    const Result<int> same_failure = ErrorInfo(ErrorCode::Type, "m");
    const Result<int> other_failure = ErrorInfo(ErrorCode::Key, "m");

    // With a value: equal when the Result holds an equal value.
    CHECK(five == 5);
    CHECK(5 == five);
    CHECK(five != 6);
    CHECK(6 != five);
    CHECK_FALSE(failed == 0);
    CHECK(failed != 0);
    CHECK(Node("440").as<uint32_t>() == 440u);
    CHECK(Node("x").as<std::string>() == "x");

    // Between Results: same values, or same errors.
    CHECK(five == other_five);
    CHECK(five != six);
    CHECK(five != failed);
    CHECK(failed == same_failure);
    CHECK(failed != other_failure);

    // Result<void>: both successes, or the same error.
    CHECK(Result<void>() == Result<void>());
    CHECK(Result<void>() != Result<void>(ErrorInfo(ErrorCode::Io, "m")));
    CHECK(Result<void>(ErrorInfo(ErrorCode::Io, "m")) == Result<void>(ErrorInfo(ErrorCode::Io, "m")));

    // References compare the nodes they refer to.
    KeyValue doc("root", { {"k", "v"} });
    CHECK(doc.at("k") == Node("v"));
    CHECK_FALSE(doc.at("missing") == Node());
}
