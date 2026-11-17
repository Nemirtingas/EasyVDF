#include <fstream>
#include <sstream>
#include <streambuf>
#include <string>

#include "../EasyVDF.h"
#include "catch.hpp"

using Catch::Matchers::Contains;
using EasyVDF::Format;
using EasyVDF::KeyValue;
using EasyVDF::Node;
using EasyVDF::ParseError;
using EasyVDF::Type;

namespace {

KeyValue ParseBinary(std::string const& blob, std::size_t chunk_size)
{
    std::istringstream is(blob);
    EasyVDF::ReadOptions options;
    options.chunk_size = chunk_size;
    return EasyVDF::parse(is, options);
}

std::string ReadFile(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::in);
    REQUIRE(f);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Deep comparison, returns an empty string when equal, else a description of the first difference.
std::string Compare(KeyValue const& a, KeyValue const& b, std::string const& path = "")
{
    std::string here = path + "/" + a.key();
    if (a.key() != b.key())
        return here + ": name differs ('" + a.key() + "' vs '" + b.key() + "')";
    if (a.type() != b.type())
        return here + ": type differs (" + std::to_string((int)a.type()) + " vs " + std::to_string((int)b.type()) + ")";

    switch (a.type())
    {
        case Type::Object:
        {
            if (a.size() != b.size())
                return here + ": collection size differs";
            auto ib = b.begin();
            for (auto const& child : a)
            {
                std::string r = Compare(child, *ib++, here);
                if (!r.empty())
                    return r;
            }
            return std::string();
        }
        case Type::String : return a.as<std::string>() == b.as<std::string>() ? std::string() : here + ": string differs ('" + a.as<std::string>() + "' vs '" + b.as<std::string>() + "')";
        case Type::Int32  : return a.as<int32_t>() == b.as<int32_t>() ? std::string() : here + ": int32 differs";
        case Type::Float  : return a.as<float>() == b.as<float>() ? std::string() : here + ": float differs";
        case Type::Pointer: return a.as<EasyVDF::Pointer>() == b.as<EasyVDF::Pointer>() ? std::string() : here + ": pointer differs";
        case Type::Color  : return a.as<EasyVDF::Color>() == b.as<EasyVDF::Color>() ? std::string() : here + ": color differs";
        case Type::Int64  : return a.as<int64_t>() == b.as<int64_t>() ? std::string() : here + ": int64 differs";
        case Type::UInt64 : return a.as<uint64_t>() == b.as<uint64_t>() ? std::string() : here + ": uint64 differs";
        default: return here + ": unexpected type";
    }
}

KeyValue MakeSample()
{
    KeyValue nested("Nested\xC3\xA9");
    nested.add("NestedString", "NestedValue");
    nested.add("EmptyObject");
    nested.add("Wrapper").add("Inner", "InnerValue");
    nested.add("NestedInt", int32_t(0x7FFFFFFF));

    KeyValue o("RootObject");
    o.add("StringKey", "StringValue");
    o.add("EmptyString", "");
    o.add("Utf8\xE2\x82\xAC", "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xC3\xA9\xF0\x9F\x98\x80");
    o.add("Int32Key", int32_t(-1337));
    o.add("Int32Min", int32_t(-2147483647 - 1));
    o.add("FloatKey", 3.1415f);
    o.add("FloatNeg", -1.5e-30f);
    o.add("PointerKey", EasyVDF::Pointer{0x90807060});
    o.add("ColorKey", EasyVDF::Color{0x66, 0x77, 0x88, 0x99});
    o.insert(o.end(), std::move(nested));
    o.add("UInt64Key", uint64_t(0xfedcba9876543210ull));
    o.add("Int64Key", int64_t(-99999999999991337ll));
    o.add("Last\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80");
    return o;
}

KeyValue MakeBinaryVdfFile()
{
    KeyValue expected("RootObject");
    expected.add("ObjectKey").add("ObjectKeyValue", "ObjectStringValue");
    expected.add("StringKey", "StringValue");
    expected.add("Int32Key", int32_t(-1337));
    expected.add("FloatKey", 3.1415f);
    expected.add("PointerKey", EasyVDF::Pointer{0x90807060});
    expected.add("ColorKey", EasyVDF::Color{0x66, 0x77, 0x88, 0x99});
    expected.add("UInt64Key", uint64_t(0xfedcba9876543210ull));
    expected.add("Int64Key", int64_t(-99999999999991337ll));
    return expected;
}

// Returns "ParseError", "OK" or the type of the other exception, to report the outcome in a single CHECK.
std::string ParseOutcome(std::string const& blob, std::size_t chunk_size)
{
    try
    {
        ParseBinary(blob, chunk_size);
        return "OK";
    }
    catch (ParseError const&)
    {
        return "ParseError";
    }
    catch (std::exception const& e)
    {
        return std::string("other exception: ") + e.what();
    }
}

// Same payload with the VBKV checksum recomputed, for hand made VBKV blobs.
std::string Vbkv(std::string const& payload)
{
    std::string blob = "VBKV";
    const uint32_t crc = EasyVDF::Details::Crc32(payload);
    for (int shift = 0; shift < 32; shift += 8)
        blob += static_cast<char>((crc >> shift) & 0xFF);
    return blob + payload;
}

// A stream buffer that can only be read forward: no seek, no putback area.
class PipeBuffer : public std::streambuf
{
    std::string _Data;
    std::size_t _Position = 0;
    char _Current = 0;

protected:
    int_type underflow() override
    {
        if (_Position == _Data.size())
            return traits_type::eof();

        _Current = _Data[_Position++];
        setg(&_Current, &_Current, &_Current + 1);
        return traits_type::to_int_type(_Current);
    }

    pos_type seekoff(off_type, std::ios_base::seekdir, std::ios_base::openmode) override { return pos_type(off_type(-1)); }
    pos_type seekpos(pos_type, std::ios_base::openmode) override { return pos_type(off_type(-1)); }

public:
    explicit PipeBuffer(std::string data) : _Data(std::move(data)) {}
};

}

TEST_CASE("Binary parser: round trip with every chunk size", "[binary_parser][chunks]")
{
    auto sample = MakeSample();
    for (Format format : {Format::Binary, Format::Vbkv})
    {
        std::string blob = sample.to_binary(format);
        for (std::size_t chunk_size = 1; chunk_size <= blob.size() + 1; ++chunk_size)
        {
            INFO("format " << (int)format << ", chunk size " << chunk_size);
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
        INFO("format " << (int)format << ", default chunk size");
        std::istringstream is(blob);
        CHECK(Compare(sample, EasyVDF::parse(is)) == "");
        CHECK(EasyVDF::parse(blob) == sample);
        // Writing what was read gives the same bytes.
        CHECK(EasyVDF::parse(blob).to_binary(format) == blob);
    }
}

TEST_CASE("Binary parser: binary.vdf with every chunk size", "[binary_parser][chunks]")
{
    const std::string blob = ReadFile("binary.vdf");
    REQUIRE(blob.size() > 8);

    KeyValue expected = MakeBinaryVdfFile();

    // The library writes the exact same bytes back.
    CHECK(expected.to_binary(Format::Vbkv) == blob);

    for (std::size_t chunk_size = 1; chunk_size <= blob.size() + 1; ++chunk_size)
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

TEST_CASE("Binary parser: real Valve files round-trip byte for byte", "[binary_parser][valve]")
{
    // Written by Dota 2 (userdata/<id>/570/remote/cfg/dismissed_help_tips.txt): VBKV magic,
    // CRC-32 of the rest, an empty root object closed by 0x0B, then the 0x0B list terminator.
    const std::string dota("VBKV" "\x56\x00\x66\xe6" "\x00" "dismissed_help_tips\x00" "\x0b\x0b", 31);
    KeyValue doc = EasyVDF::parse(dota);   // checksum verified
    CHECK(doc.key() == "dismissed_help_tips");
    CHECK(doc.is_object());
    CHECK(doc.size() == 0);
    CHECK(doc.to_binary(Format::Vbkv) == dota);
    CHECK(EasyVDF::Details::Crc32(std::string_view(dota).substr(8)) == 0xe6660056u);

    // Same layout as a Steam stats schema (UserGameStatsSchema / stats_schema.vdf), which ends with 08 08.
    static const char schema_raw[] =
        "\x00" "480\x00"
            "\x00" "stats\x00"
                "\x00" "1\x00"
                    "\x00" "bits\x00"
                        "\x00" "0\x00"
                            "\x01" "name\x00" "ACH_WIN\x00"
                            "\x00" "display\x00"
                                "\x00" "name\x00"
                                    "\x01" "english\x00" "Winner\x00"
                                "\x08"
                                "\x01" "hidden\x00" "0\x00"
                            "\x08"
                        "\x08"
                    "\x08"
                    "\x01" "type\x00" "ACHIEVEMENTS\x00"
                "\x08"
            "\x08"
            "\x01" "version\x00" "2\x00"
        "\x08"
        "\x08";
    const std::string schema(schema_raw, sizeof(schema_raw) - 1);
    REQUIRE(schema.size() == 117);
    REQUIRE(schema.substr(schema.size() - 2) == "\x08\x08");
    KeyValue stats = EasyVDF::parse(schema);
    CHECK(stats.key() == "480");
    CHECK(stats["stats"]["1"]["type"].as<std::string>() == "ACHIEVEMENTS");
    CHECK(stats["stats"]["1"]["bits"]["0"]["display"]["name"]["english"].as<std::string>() == "Winner");
    CHECK(stats["version"].as<int>() == 2);
    CHECK(stats.to_binary(Format::Binary) == schema);
}

TEST_CASE("Binary parser: unknown node type throws ParseError", "[binary_parser][errors]")
{
    // 5 (WideString) and 9 (binary blob) are known but not supported.
    for (int type : {5, 9, 12, 0x42, 0x7F, 0x80, 0xFF})
    {
        INFO("node type " << type);
        std::string blob("\x00Root\x00", 6);
        blob += static_cast<char>(type);
        blob += std::string("key\x00value\x00\x08", 11);

        try
        {
            ParseBinary(blob, 1024);
            FAIL("no exception thrown");
        }
        catch (ParseError const& e)
        {
            CHECK_THAT(e.what(), Contains(std::to_string((int)type)));
            CHECK(e.line() == 0);
        }

        CHECK_THROWS_AS(EasyVDF::parse(blob), ParseError);
        CHECK_THROWS_AS(EasyVDF::parse(Vbkv(blob)), ParseError);
    }

    CHECK_THROWS_WITH(EasyVDF::parse(std::string("\x00R\x00\x05k\x00\x00\x00\x08", 9)), Contains("WideString"));
}

TEST_CASE("Binary parser: truncated input throws ParseError", "[binary_parser][errors]")
{
    const std::string file_blob = ReadFile("binary.vdf");

    auto sample = MakeSample();
    for (std::string const& blob : {sample.to_binary(Format::Binary), sample.to_binary(Format::Vbkv), file_blob})
    {
        // Every strict prefix is truncated: in the root header, a key, a value, or a missing end marker.
        // This includes the inputs shorter than the 4 bytes magic, and the empty one.
        // The last byte is the optional list terminator written after the root: dropping it alone
        // is not a truncation (see "optional list terminator" below), so stop one byte earlier.
        REQUIRE(blob.size() >= 2);
        CHECK(blob[blob.size() - 1] == blob[blob.size() - 2]);
        for (std::size_t length = 0; length + 1 < blob.size(); ++length)
        {
            for (std::size_t chunk_size : {std::size_t(1), std::size_t(3), std::size_t(7), std::size_t(64), std::size_t(10 * 1024)})
            {
                INFO("prefix length " << length << " of " << blob.size() << ", chunk size " << chunk_size);
                CHECK(ParseOutcome(blob.substr(0, length), chunk_size) == "ParseError");
            }

            // From memory too.
            try
            {
                EasyVDF::parse(std::string_view(blob).substr(0, length));
                FAIL("prefix length " << length << " parsed");
            }
            catch (ParseError const&)
            {
            }
        }
    }

    // Nested object closed but root end marker missing.
    CHECK(ParseOutcome(std::string("\x00Root\x00\x00Sub\x00\x01k\x00v\x00\x08", 17), 1024) == "ParseError");
}

TEST_CASE("Binary parser: data after the root object", "[binary_parser][trailing]")
{
    auto sample = MakeSample();
    // The writer ends the root list with an extra end marker; strip it to get the bare root object.
    std::string v1 = sample.to_binary(Format::Binary);
    std::string v2 = sample.to_binary(Format::Vbkv);
    REQUIRE(v1.substr(v1.size() - 2) == "\x08\x08");
    REQUIRE(v2.substr(v2.size() - 2) == "\x0b\x0b");
    v1.pop_back();
    v2.pop_back();
    // The VBKV checksum covers everything after the header: recompute it for the altered payloads.
    const std::string p2 = v2.substr(8);

    // Without the list terminator (old EasyVDF output, hand made files) both still parse.
    CHECK(Compare(sample, ParseBinary(v1, 1024)) == "");
    CHECK(Compare(sample, ParseBinary(Vbkv(p2), 1024)) == "");

    for (std::size_t chunk_size : {std::size_t(1), std::size_t(2), std::size_t(5), std::size_t(16), v1.size(), v1.size() + 1, v2.size(), v2.size() + 1, std::size_t(10 * 1024)})
    {
        INFO("chunk size " << chunk_size);

        // Accepted: a single extra end marker, 0x08 or the root end marker itself.
        CHECK(ParseOutcome(v1 + "\x08", chunk_size) == "OK");
        CHECK(ParseOutcome(Vbkv(p2 + "\x08"), chunk_size) == "OK");
        CHECK(ParseOutcome(Vbkv(p2 + "\x0b"), chunk_size) == "OK");
        CHECK(Compare(sample, ParseBinary(v1 + "\x08", chunk_size)) == "");
        CHECK(Compare(sample, ParseBinary(Vbkv(p2 + "\x0b"), chunk_size)) == "");

        // Rejected: anything else.
        CHECK(ParseOutcome(v1 + "\x0b", chunk_size) == "ParseError");
        CHECK(ParseOutcome(v1 + "\x08\x08", chunk_size) == "ParseError");
        CHECK(ParseOutcome(Vbkv(p2 + "\x0b\x0b"), chunk_size) == "ParseError");
        CHECK(ParseOutcome(v1 + std::string(1, '\0'), chunk_size) == "ParseError");
        CHECK(ParseOutcome(Vbkv(p2 + "garbage"), chunk_size) == "ParseError");
        CHECK(ParseOutcome(Vbkv(p2 + "\x08garbage"), chunk_size) == "ParseError");
        // A second root object (used to be parsed into the same object, leaking the first one).
        CHECK(ParseOutcome(v1 + v1, chunk_size) == "ParseError");
        CHECK(ParseOutcome(Vbkv(p2 + p2), chunk_size) == "ParseError");
    }
}

TEST_CASE("Binary parser: values are decoded as little-endian from unaligned offsets", "[binary_parser][endianness]")
{
    // Hand written blob (independent of the serializer): keys have odd lengths so values are unaligned.
    static const char raw[] =
        "\x00" "R\x00"
        "\x02" "i\x00"   "\x04\x03\x02\x81"
        "\x03" "fl\x00"  "\x00\x00\xc0\xbf"
        "\x04" "p\x00"   "\x11\x22\x33\x44"
        "\x06" "col\x00" "\xa1\xb2\xc3\xd4"
        "\x0a" "i6\x00"  "\xfe\xff\xff\xff\xff\xff\xff\xff"
        "\x07" "u\x00"   "\x08\x07\x06\x05\x04\x03\x02\xf1"
        "\x08";
    const std::string blob(raw, sizeof(raw) - 1);
    REQUIRE(blob.size() == 58);

    for (std::size_t chunk_size = 0; chunk_size <= blob.size() + 1; ++chunk_size)
    {
        INFO("chunk size " << chunk_size);
        const KeyValue o = ParseBinary(blob, chunk_size);
        REQUIRE(o.type() == Type::Object);
        CHECK(o.key() == "R");
        REQUIRE(o.size() == 6);
        CHECK(o["i"].as<int32_t>() == int32_t(-2130574588)); // 0x81020304
        CHECK(o["fl"].as<float>() == -1.5f);
        CHECK(o["p"].as<EasyVDF::Pointer>().value == 0x44332211u);
        CHECK(o["col"].as<EasyVDF::Color>() == EasyVDF::Color{0xa1, 0xb2, 0xc3, 0xd4});
        CHECK(o["i6"].as<int64_t>() == -2);
        CHECK(o["u"].as<uint64_t>() == 0xf102030405060708ull);
    }

    // The serializer writes the same bytes, whatever the byte order of the host
    // (plus the 0x08 list terminator that closes the root list).
    CHECK(EasyVDF::parse(blob).to_binary() == blob + "\x08");

    KeyValue built("R");
    built.add("i", int32_t(-2130574588));
    built.add("fl", -1.5f);
    built.add("p", EasyVDF::Pointer{0x44332211u});
    built.add("col", EasyVDF::Color{0xa1, 0xb2, 0xc3, 0xd4});
    built.add("i6", int64_t(-2));
    built.add("u", uint64_t(0xf102030405060708ull));
    CHECK(built.to_binary() == blob + "\x08");
}

TEST_CASE("Binary parser: VBKV magic and tiny chunk sizes", "[binary_parser][endianness]")
{
    auto sample = MakeSample();
    const std::string v2 = sample.to_binary(Format::Vbkv);
    REQUIRE(v2.compare(0, 4, "VBKV") == 0);
    for (std::size_t chunk_size : {std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(4)})
    {
        INFO("chunk size " << chunk_size);
        CHECK(Compare(sample, ParseBinary(v2, chunk_size)) == "");
        CHECK(Compare(sample, ParseBinary(sample.to_binary(Format::Binary), chunk_size)) == "");
    }
}

TEST_CASE("VBKV checksum is written and verified", "[binary_parser][crc]")
{
    // The standard CRC-32 check value.
    CHECK(EasyVDF::Details::Crc32("123456789") == 0xCBF43926u);
    CHECK(EasyVDF::Details::Crc32("") == 0u);

    const std::string blob = ReadFile("binary.vdf");
    // binary.vdf: CRC32 of the 178 bytes after the header, little-endian.
    CHECK(blob.substr(4, 4) == std::string("\x39\x1b\x90\x82", 4));
    CHECK(Vbkv(blob.substr(8)) == blob);

    KeyValue expected = MakeBinaryVdfFile();

    SECTION("a corrupted payload is detected, whatever the chunk size")
    {
        for (std::size_t position = 8; position < blob.size(); ++position)
        {
            std::string corrupted = blob;
            corrupted[position] ^= 0x20;
            for (std::size_t chunk_size : {std::size_t(1), std::size_t(5), std::size_t(4096)})
            {
                INFO("corrupted byte " << position << ", chunk size " << chunk_size);
                CHECK(ParseOutcome(corrupted, chunk_size) == "ParseError");
            }
        }
    }

    SECTION("a wrong checksum is reported")
    {
        for (std::size_t position = 4; position < 8; ++position)
        {
            std::string corrupted = blob;
            corrupted[position] ^= 0x01;
            CHECK_THROWS_WITH(EasyVDF::parse(corrupted), Contains("checksum mismatch") && Contains("0x82901b39"));
        }
    }

    SECTION("verification can be disabled")
    {
        // Files written by older versions of this library have a zero checksum.
        std::string zero_crc = blob;
        zero_crc.replace(4, 4, 4, '\0');
        CHECK_THROWS_AS(EasyVDF::parse(zero_crc), ParseError);

        EasyVDF::ReadOptions options;
        options.verify_crc = false;
        CHECK(EasyVDF::parse(zero_crc, options) == expected);
        for (std::size_t chunk_size : {std::size_t(1), std::size_t(4096)})
        {
            std::istringstream is(zero_crc);
            options.chunk_size = chunk_size;
            CHECK(EasyVDF::parse(is, options) == expected);
        }
    }

    SECTION("the checksum covers the trailing list terminator")
    {
        // binary.vdf ends with 0B 0B; without the list terminator the file is still valid,
        // but only with the checksum of the shorter payload.
        REQUIRE(blob.substr(blob.size() - 2) == "\x0b\x0b");
        const std::string payload = blob.substr(8, blob.size() - 9);
        CHECK(EasyVDF::parse(Vbkv(payload)) == expected);
        CHECK_THROWS_WITH(EasyVDF::parse(blob.substr(0, blob.size() - 1)), Contains("checksum mismatch"));
    }
}

TEST_CASE("Binary parser: both end markers end an object in both formats", "[binary_parser][end_markers]")
{
    KeyValue expected("R", { {"S", { {"k", "v"} }}, {"i", int32_t(1)} });

    const std::string with_08("\x00R\x00\x00S\x00\x01k\x00v\x00\x08\x02i\x00\x01\x00\x00\x00\x08", 20);
    const std::string with_0b("\x00R\x00\x00S\x00\x01k\x00v\x00\x0b\x02i\x00\x01\x00\x00\x00\x0b", 20);
    const std::string mixed  ("\x00R\x00\x00S\x00\x01k\x00v\x00\x0b\x02i\x00\x01\x00\x00\x00\x08", 20);

    for (std::string const& payload : {with_08, with_0b, mixed})
    {
        CHECK(EasyVDF::parse(payload) == expected);
        CHECK(EasyVDF::parse(Vbkv(payload)) == expected);
    }

    CHECK(expected.to_binary(Format::Binary) == with_08 + "\x08");
    CHECK(expected.to_binary(Format::Vbkv) == Vbkv(with_0b + "\x0b"));
}

TEST_CASE("Binary parser: nesting depth limit", "[binary_parser][depth]")
{
    auto nested = [](std::size_t depth) {
        std::string blob;
        for (std::size_t i = 0; i < depth; ++i)
            blob += std::string("\x00o\x00", 3);
        blob += std::string(depth, '\x08');
        return blob;
    };

    EasyVDF::ReadOptions options;
    CHECK_NOTHROW(EasyVDF::parse(nested(options.max_depth)));
    CHECK_THROWS_WITH(EasyVDF::parse(nested(options.max_depth + 1)), Contains("nested deeper"));

    // A hostile input does not overflow the stack.
    CHECK_THROWS_AS(EasyVDF::parse(nested(1000000)), ParseError);
    CHECK_THROWS_AS(EasyVDF::parse(Vbkv(nested(1000000))), ParseError);
    CHECK(ParseOutcome(nested(100000), 7) == "ParseError");

    options.max_depth = 2;
    CHECK_NOTHROW(EasyVDF::parse(nested(2), options));
    CHECK_THROWS_AS(EasyVDF::parse(nested(3), options), ParseError);

    options.max_depth = 500;
    CHECK(EasyVDF::parse(nested(500), options).to_binary() == nested(500) + "\x08");
}

TEST_CASE("Streams are read sequentially, without seeking", "[binary_parser][streams]")
{
    auto sample = MakeSample();

    for (std::string const& data : {sample.to_binary(Format::Binary), sample.to_binary(Format::Vbkv), sample.to_text(), "\xEF\xBB\xBF" + sample.to_text()})
    {
        const KeyValue expected = EasyVDF::parse(data);
        for (std::size_t chunk_size : {std::size_t(1), std::size_t(4), std::size_t(16 * 1024)})
        {
            INFO("chunk size " << chunk_size);
            PipeBuffer buffer(data);
            std::istream pipe(&buffer);
            REQUIRE(pipe.tellg() == std::istream::pos_type(-1));
            pipe.clear();

            EasyVDF::ReadOptions options;
            options.chunk_size = chunk_size;
            CHECK(EasyVDF::parse(pipe, options) == expected);
        }
    }
}

TEST_CASE("Short inputs are handled", "[binary_parser][streams]")
{
    // Used to throw "Failed to read stream" for anything shorter than 4 bytes.
    CHECK(EasyVDF::parse("a{}").key() == "a");
    CHECK(EasyVDF::parse("a{}").empty());
    CHECK(EasyVDF::parse(std::string("\x00\x00\x08", 3)).key() == "");
    CHECK(EasyVDF::parse(std::string("\x00\x00\x08", 3)).is_object());

    std::istringstream text("a{}");
    CHECK(EasyVDF::parse(text).key() == "a");
    std::istringstream binary(std::string("\x00\x00\x08", 3));
    CHECK(EasyVDF::parse(binary).is_object());

    for (const char* input : {"", "V", "VB", "VBK", "VBKV", "a", "a{", "{", "}", "\""})
    {
        INFO("input '" << input << "'");
        CHECK_THROWS_AS(EasyVDF::parse(input), ParseError);
        std::istringstream is(input);
        CHECK_THROWS_AS(EasyVDF::parse(is), ParseError);
    }

    // A text document whose root key starts like the VBKV magic.
    for (const char* input : {"V{}", "VB{}", "VBK{}", "VBKW{}", "VBK { k v }"})
    {
        INFO("input '" << input << "'");
        const std::string expected_key(input, std::string(input).find_first_of(" {"));
        CHECK(EasyVDF::parse(input).key() == expected_key);
        for (std::size_t chunk_size : {std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(100)})
        {
            std::istringstream is(input);
            EasyVDF::ReadOptions options;
            options.chunk_size = chunk_size;
            CHECK(EasyVDF::parse(is, options).key() == expected_key);
        }
    }
}

TEST_CASE("The format can be forced", "[binary_parser][format]")
{
    auto sample = MakeSample();
    EasyVDF::ReadOptions options;

    options.format = Format::Binary;
    CHECK(EasyVDF::parse(sample.to_binary(Format::Binary), options) == sample);
    CHECK(EasyVDF::parse(sample.to_binary(Format::Vbkv), options) == sample);
    CHECK_THROWS_WITH(EasyVDF::parse(sample.to_text(), options), Contains("root item type is not an object"));

    options.format = Format::Vbkv;
    CHECK(EasyVDF::parse(sample.to_binary(Format::Binary), options) == sample);
    CHECK(EasyVDF::parse(sample.to_binary(Format::Vbkv), options) == sample);

    options.format = Format::Text;
    CHECK(EasyVDF::parse(sample.to_text(), options).to_text() == sample.to_text());
    CHECK_THROWS_AS(EasyVDF::parse(sample.to_binary(Format::Binary), options), ParseError);
    // "VBKV" is a valid unquoted root key.
    CHECK(EasyVDF::parse("VBKV { k v }", options).key() == "VBKV");
}

TEST_CASE("Binary writer rejects what the format can't hold", "[binary_parser][errors]")
{
    KeyValue with_null("root");
    with_null.add("k", std::string("a\0b", 3));
    CHECK_THROWS_AS(with_null.to_binary(), EasyVDF::WriteError);
    CHECK_THROWS_AS(with_null.to_binary(Format::Vbkv), EasyVDF::WriteError);
    CHECK_NOTHROW(with_null.to_text());

    KeyValue null_key("root");
    null_key.add(std::string("a\0b", 3), "v");
    CHECK_THROWS_AS(null_key.to_binary(), EasyVDF::WriteError);

    // Conditionals only exist in text.
    KeyValue conditional("root");
    conditional.add("k", "v").set_condition("$WIN32");
    CHECK(EasyVDF::parse(conditional.to_binary()).find("k")->condition().empty());
}
