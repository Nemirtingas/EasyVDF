#include "../EasyVDF.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "helpers.h"

using EasyVDF::Color;
using EasyVDF::KeyValue;
using EasyVDF::Node;
using EasyVDF::Pointer;
using EasyVDF::Type;

namespace {

KeyValue MakeDocument()
{
    return KeyValue("AppState", {
        {"appid", "440"},
        {"name", "Team Fortress 2"},
        {"UserConfig", {
            {"language", "english"},
            {"beta", "0"},
        }},
        {"dll", "a.dll"},
        {"dll", "b.dll"},
    });
}

std::string Keys(Node const& object)
{
    std::string keys;
    for (auto const& [key, value] : object)
        keys += key + (value.is_object() ? "{} " : " ");
    return keys;
}

}

/////////////////////////////////////////////////////////////////////
// Types stored by constructors and assignments
/////////////////////////////////////////////////////////////////////

TEST_CASE("Scalars are stored with a predictable type", "[api][types]")
{
    CHECK(Node().type() == Type::None);
    CHECK(Node(nullptr).type() == Type::None);
    CHECK(Node::object().type() == Type::Object);

    CHECK(Node(true).type() == Type::Int32);
    CHECK(Node(true).as<int>() == 1);
    CHECK(Node(false).as<int>() == 0);

    // Signed integers: Int32 up to 32 bits, Int64 above, whatever the platform calls them.
    CHECK(Node(static_cast<signed char>(-5)).type() == Type::Int32);
    CHECK(Node(static_cast<short>(-5)).type() == Type::Int32);
    CHECK(Node(5).type() == Type::Int32);
    CHECK(Node(5L).type() == (sizeof(long) == 4 ? Type::Int32 : Type::Int64));
    CHECK(Node(5LL).type() == Type::Int64);
    CHECK(Node(int32_t(5)).type() == Type::Int32);
    CHECK(Node(int64_t(5)).type() == Type::Int64);

    // Unsigned integers: Int32 when they always fit, UInt64 otherwise.
    CHECK(Node(static_cast<unsigned char>(200)).type() == Type::Int32);
    CHECK(Node(static_cast<unsigned short>(60000)).type() == Type::Int32);
    CHECK(Node(5u).type() == Type::UInt64);
    CHECK(Node(5ul).type() == Type::UInt64);
    CHECK(Node(5ull).type() == Type::UInt64);
    CHECK(Node(uint32_t(4000000000u)).as<uint32_t>() == 4000000000u);
    CHECK(Node(std::size_t(5)).type() == Type::UInt64);

    CHECK(Node(1.5f).type() == Type::Float);
    CHECK(Node(3.14).type() == Type::Float);
    CHECK(Node(3.14).as<float>() == 3.14f);
    CHECK(Node(1.5L).type() == Type::Float);

    CHECK(Node("literal").type() == Type::String);
    CHECK(Node(std::string("string")).type() == Type::String);
    CHECK(Node(std::string_view("view")).type() == Type::String);
    const char* pointer = "pointer";
    CHECK(Node(pointer).type() == Type::String);
    char array[] = "array";
    CHECK(Node(array).as<std::string>() == "array");
    CHECK(Node(std::string_view("embedded\0null", 13)).as<std::string>()->size() == 13);

    CHECK(Node(Color{1, 2, 3, 4}).type() == Type::Color);
    CHECK(Node(Pointer{5}).type() == Type::Pointer);

    SECTION("assignment gives the same types")
    {
        Node n;
        n = 5LL;
        CHECK(n.type() == Type::Int64);
        n = 3.14;
        CHECK(n.type() == Type::Float);
        n = 5;
        CHECK(n.type() == Type::Int32);
        n = true;
        CHECK(n.type() == Type::Int32);
        n = uint64_t(1) << 63;
        CHECK(n.type() == Type::UInt64);
        n = "text";
        CHECK(n.type() == Type::String);
        n = std::string("text");
        CHECK(n.type() == Type::String);
        n = std::string_view("text");
        CHECK(n.type() == Type::String);
        n = Color{};
        CHECK(n.type() == Type::Color);
        n = Pointer{};
        CHECK(n.type() == Type::Pointer);
        n = Node::object();
        CHECK(n.type() == Type::Object);
        n = { {"k", "v"} };
        CHECK(n.type() == Type::Object);
        CHECK(n["k"].as<std::string>() == "v");
        n = {};
        CHECK(n.type() == Type::None);
        n = 1;
        n = nullptr;
        CHECK(n.type() == Type::None);
    }

    SECTION("KeyValue constructors")
    {
        CHECK(KeyValue().type() == Type::None);
        CHECK(KeyValue().key().empty());
        CHECK(KeyValue("k").type() == Type::Object);
        CHECK(KeyValue("k").key() == "k");
        CHECK(KeyValue("k", 3.14).type() == Type::Float);
        CHECK(KeyValue("k", 5LL).type() == Type::Int64);
        CHECK(KeyValue("k", "v").type() == Type::String);
        CHECK(KeyValue("k", std::string("v")).type() == Type::String);
        CHECK(KeyValue("k", Node::object()).type() == Type::Object);
        CHECK(KeyValue("k", { {"a", 1} }).type() == Type::Object);
        CHECK(KeyValue("k", nullptr).type() == Type::None);

        // A string is a key, never silently an object or a value.
        CHECK_FALSE((std::is_convertible<std::string, KeyValue>::value));
        CHECK_FALSE((std::is_convertible<const char*, KeyValue>::value));
        CHECK_FALSE((std::is_convertible<KeyValue, std::string>::value));
        CHECK_FALSE((std::is_convertible<Node, std::string>::value));
        CHECK_FALSE((std::is_convertible<Node, int>::value));
        CHECK_FALSE((std::is_convertible<Node, bool>::value));
        // Characters and raw pointers are not values.
        CHECK_FALSE((std::is_constructible<Node, char>::value));
        CHECK_FALSE((std::is_constructible<Node, wchar_t>::value));
        CHECK_FALSE((std::is_constructible<Node, int*>::value));
        CHECK_FALSE((std::is_constructible<Node, const wchar_t*>::value));
        CHECK_FALSE((std::is_assignable<Node&, char>::value));
        CHECK_FALSE((std::is_assignable<Node&, void*>::value));
    }
}

/////////////////////////////////////////////////////////////////////
// Conversions
/////////////////////////////////////////////////////////////////////

TEST_CASE("as<T>() converts between the stored types", "[api][conversions]")
{
    SECTION("Int32")
    {
        Node n(int32_t(-1337));
        CHECK(n.as<int>() == -1337);
        CHECK(n.as<int16_t>() == -1337);
        CHECK(n.as<int64_t>() == -1337);
        CHECK(n.as<long>() == -1337L);
        CHECK(n.as<long long>() == -1337LL);
        CHECK(n.as<float>() == -1337.f);
        CHECK(n.as<double>() == -1337.0);
        CHECK(n.as<bool>() == true);
        CHECK(n.as<std::string>() == "-1337");
        CHECK(Outcome(n.as<int8_t>()) == "Type");
        // Int32 is how the binary format stores 32 bits unsigned values: same bits.
        CHECK(n.as<uint32_t>() == 4294965959u);
        CHECK(Outcome(n.as<uint16_t>()) == "Type");
        CHECK(Outcome(n.as<uint64_t>()) == "Type");
        CHECK(Outcome(n.as<std::string_view>()) == "Type");
        CHECK(Outcome(n.as<Color>()) == "Type");
        CHECK(Outcome(n.as<Pointer>()) == "Type");

        CHECK(Node(255).as<uint8_t>() == 255);
        CHECK(Outcome(Node(256).as<uint8_t>()) == "Type");
        CHECK(Node(-128).as<int8_t>() == -128);
        CHECK(Outcome(Node(-129).as<int8_t>()) == "Type");
        CHECK(Node((std::numeric_limits<int32_t>::min)()).as<std::string>() == "-2147483648");
        CHECK(Node(0).as<bool>() == false);
    }

    SECTION("Int64 and UInt64")
    {
        Node big(int64_t(-99999999999991337ll));
        CHECK(big.as<int64_t>() == -99999999999991337ll);
        CHECK(big.as<std::string>() == "-99999999999991337");
        CHECK(Outcome(big.as<int32_t>()) == "Type");
        CHECK(Outcome(big.as<uint64_t>()) == "Type");
        CHECK(Node(int64_t(42)).as<uint8_t>() == 42);
        CHECK(Node((std::numeric_limits<int64_t>::min)()).as<int64_t>() == (std::numeric_limits<int64_t>::min)());

        Node huge(uint64_t(0xfedcba9876543210ull));
        CHECK(huge.as<uint64_t>() == 0xfedcba9876543210ull);
        CHECK(huge.as<std::string>() == "18364758544493064720");
        CHECK(huge.as<double>() == 18364758544493064720.0);
        CHECK(Outcome(huge.as<int64_t>()) == "Type");
        CHECK(Outcome(huge.as<uint32_t>()) == "Type");
        CHECK(Node(uint64_t(7)).as<int>() == 7);
        CHECK(Node((std::numeric_limits<uint64_t>::max)()).as<uint64_t>() == (std::numeric_limits<uint64_t>::max)());
    }

    SECTION("Float")
    {
        Node f(3.1415f);
        CHECK(f.as<float>() == 3.1415f);
        CHECK(f.as<double>() == static_cast<double>(3.1415f));
        CHECK(f.as<std::string>() == "3.1415");
        CHECK(f.as<bool>() == true);
        CHECK(Outcome(f.as<int>()) == "Type");
        CHECK(Outcome(f.as<Color>()) == "Type");

        // Whole numbers convert to integers, in range only.
        CHECK(Node(42.f).as<int>() == 42);
        CHECK(Node(-42.f).as<int8_t>() == -42);
        CHECK(Node(-0.f).as<unsigned>() == 0u);
        CHECK(Node(4294967296.f).as<uint64_t>() == 4294967296ull);
        CHECK(Node(4294967296.f).as<int64_t>() == 4294967296ll);
        CHECK(Outcome(Node(4294967296.f).as<uint32_t>()) == "Type");
        CHECK(Outcome(Node(300.f).as<uint8_t>()) == "Type");
        CHECK(Outcome(Node(-1.f).as<unsigned>()) == "Type");
        CHECK(Outcome(Node(1e30f).as<int64_t>()) == "Type");
        CHECK(Outcome(Node(1e30f).as<uint64_t>()) == "Type");
        CHECK(Outcome(Node(-1e30f).as<int64_t>()) == "Type");
        CHECK(Outcome(Node(std::numeric_limits<float>::infinity()).as<int64_t>()) == "Type");
        CHECK(Outcome(Node(std::numeric_limits<float>::quiet_NaN()).as<int>()) == "Type");
        CHECK(Node(0.f).as<bool>() == false);
    }

    SECTION("String")
    {
        Node s("  -12  ");
        CHECK(s.as<std::string>() == "  -12  ");
        CHECK(s.as<std::string_view>() == "  -12  ");
        CHECK(s.as<int>() == -12);
        CHECK(s.as<double>() == -12.0);

        CHECK(Node("+5").as<int>() == 5);
        CHECK(Node("+5.5").as<double>() == 5.5);
        CHECK(Node("0x7FFFFFFF").as<int32_t>() == 0x7FFFFFFF);
        CHECK(Node("0XfF").as<uint8_t>() == 255);
        CHECK(Node("-0x10").as<int>() == -16);
        CHECK(Node("-2147483648").as<int32_t>() == (std::numeric_limits<int32_t>::min)());
        CHECK(Node("-9223372036854775808").as<int64_t>() == (std::numeric_limits<int64_t>::min)());
        CHECK(Node("18446744073709551615").as<uint64_t>() == (std::numeric_limits<uint64_t>::max)());
        CHECK(Node("-0").as<unsigned>() == 0u);
        CHECK(Node("1e3").as<float>() == 1000.f);
        CHECK(Node(".5").as<double>() == 0.5);
        CHECK(Node("76561198000000000").as<uint64_t>() == 76561198000000000ull);
        CHECK(Node("4294967295").as<Pointer>() == Pointer{0xFFFFFFFFu});

        for (const char* bad : {"", " ", "abc", "12abc", "1 2", "--1", "+-1", "0x", "0xZ", "1.5", "1e3", "-", "+", "12-"})
        {
            INFO("'" << bad << "'");
            CHECK(Outcome(Node(bad).as<int>()) == "Type");
        }
        for (const char* bad : {"", " ", "abc", "1.5x", "1,5", "--1", "1 2", "."})
        {
            INFO("'" << bad << "'");
            CHECK(Outcome(Node(bad).as<double>()) == "Type");
        }
        CHECK(Outcome(Node("2147483648").as<int32_t>()) == "Type");
        CHECK(Outcome(Node("-2147483649").as<int32_t>()) == "Type");
        CHECK(Outcome(Node("18446744073709551616").as<uint64_t>()) == "Type");
        CHECK(Outcome(Node("-9223372036854775809").as<int64_t>()) == "Type");
        CHECK(Outcome(Node("-1").as<uint64_t>()) == "Type");
        CHECK(Outcome(Node("256").as<uint8_t>()) == "Type");
        CHECK(Outcome(Node("4294967296").as<Pointer>()) == "Type");

        CHECK(Node("1").as<bool>() == true);
        CHECK(Node("7").as<bool>() == true);
        CHECK(Node("true").as<bool>() == true);
        CHECK(Node("TRUE").as<bool>() == true);
        CHECK(Node("0").as<bool>() == false);
        CHECK(Node("false").as<bool>() == false);
        CHECK(Node(" False ").as<bool>() == false);
        CHECK(Outcome(Node("yes").as<bool>()) == "Type");
        CHECK(Outcome(Node("").as<bool>()) == "Type");

        CHECK(Node("255 128 0").as<Color>() == Color{255, 128, 0, 255});
        CHECK(Node(" 1  2\t3 4 ").as<Color>() == Color{1, 2, 3, 4});
        for (const char* bad : {"", "1", "1 2", "1 2 3 4 5", "256 0 0", "-1 0 0", "a b c", "1,2,3"})
        {
            INFO("'" << bad << "'");
            CHECK(Outcome(Node(bad).as<Color>()) == "Type");
        }
    }

    SECTION("Color and Pointer")
    {
        Node c(Color{255, 128, 0, 64});
        CHECK(c.as<Color>() == Color{255, 128, 0, 64});
        CHECK(c.as<std::string>() == "255 128 0 64");
        CHECK(Node(c.as<std::string>().value()).as<Color>() == c.as<Color>());
        CHECK(Outcome(c.as<int>()) == "Type");
        CHECK(Outcome(c.as<bool>()) == "Type");

        Node p(Pointer{0x90807060});
        CHECK(p.as<Pointer>() == Pointer{0x90807060});
        CHECK(p.as<uint32_t>() == 0x90807060u);
        CHECK(p.as<int64_t>() == 0x90807060ll);
        CHECK(p.as<std::string>() == "2424336480");
        CHECK(Node(p.as<std::string>().value()).as<Pointer>() == p.as<Pointer>());
        CHECK(Outcome(p.as<int32_t>()) == "Type");
        CHECK(Outcome(p.as<double>()) == "Type");
    }

    SECTION("None and Object convert to nothing")
    {
        for (Node const& n : {Node(), Node::object(), Node({ {"k", "v"} })})
        {
            CHECK(Outcome(n.as<std::string>()) == "Type");
            CHECK(Outcome(n.as<std::string_view>()) == "Type");
            CHECK(Outcome(n.as<int>()) == "Type");
            CHECK(Outcome(n.as<double>()) == "Type");
            CHECK(Outcome(n.as<bool>()) == "Type");
            CHECK(Outcome(n.as<Color>()) == "Type");
            CHECK(Outcome(n.as<Pointer>()) == "Type");
            CHECK_FALSE(n.try_as<std::string>().has_value());
            CHECK(n.value_or("default") == "default");
        }
    }
}

TEST_CASE("try_as<T>() and value_or()", "[api][conversions]")
{
    const KeyValue doc = MakeDocument();

    std::optional<int> appid = doc["appid"].try_as<int>();
    REQUIRE(appid.has_value());
    CHECK(*appid == 440);
    CHECK_FALSE(doc["name"].try_as<int>().has_value());
    CHECK_FALSE(doc["missing"].try_as<int>().has_value());
    CHECK(doc["missing"].try_as<std::string>() == std::nullopt);
    CHECK(doc["name"].try_as<std::string_view>() == std::string_view("Team Fortress 2"));
    CHECK(doc["appid"].try_as<uint8_t>() == std::nullopt);

    // The result type is the type of the default.
    CHECK((std::is_same<decltype(doc["appid"].value_or(0)), int>::value));
    CHECK((std::is_same<decltype(doc["appid"].value_or(0u)), unsigned>::value));
    CHECK((std::is_same<decltype(doc["appid"].value_or(0.0)), double>::value));
    CHECK((std::is_same<decltype(doc["appid"].value_or(false)), bool>::value));
    CHECK((std::is_same<decltype(doc["appid"].value_or<uint64_t>(0)), uint64_t>::value));
    CHECK((std::is_same<decltype(doc["name"].value_or("literal")), std::string>::value));
    CHECK((std::is_same<decltype(doc["name"].value_or(std::string())), std::string>::value));
    CHECK((std::is_same<decltype(doc["name"].value_or(std::string_view())), std::string>::value));
    CHECK((std::is_same<decltype(doc["name"].value_or(Color{})), Color>::value));

    CHECK(doc["appid"].value_or(0) == 440);
    CHECK(doc["appid"].value_or(0.5) == 440.0);
    CHECK(doc["appid"].value_or<uint64_t>(0) == 440u);
    CHECK(doc["appid"].value_or("x") == "440");
    CHECK(doc["appid"].value_or(false));
    CHECK(doc["missing"].value_or(7) == 7);
    CHECK(doc["missing"].value_or("default") == "default");
    CHECK(doc["missing"]["deeper"]["again"].value_or(1.5f) == 1.5f);
    // Not convertible: the default too.
    CHECK(doc["name"].value_or(7) == 7);
    CHECK(doc["UserConfig"].value_or("an object") == "an object");
    CHECK(doc["appid"].value_or<int8_t>(-1) == -1);

    const std::string fallback = "fallback";
    CHECK(doc["missing"].value_or(fallback) == "fallback");
    CHECK(doc["name"].value_or(fallback) == "Team Fortress 2");
    CHECK(doc["missing"].value_or(std::string_view("view")) == "view");
    CHECK(doc["missing"].value_or(Color{1, 2, 3, 4}) == Color{1, 2, 3, 4});

    // as<std::string_view>() does not copy.
    std::string_view view = doc["name"].as<std::string_view>().value();
    CHECK(view.data() == doc["name"].as<std::string_view>()->data());
}

/////////////////////////////////////////////////////////////////////
// Lookup
/////////////////////////////////////////////////////////////////////

TEST_CASE("Const lookups never throw and never modify", "[api][lookup]")
{
    const KeyValue doc = MakeDocument();
    const std::string before = doc.to_text().value();

    CHECK(doc["appid"].as<int>() == 440);
    CHECK(doc["UserConfig"]["language"].as<std::string>() == "english");
    CHECK(doc["dll"].as<std::string>() == "a.dll"); // the first one

    CHECK(doc["missing"].is_none());
    CHECK(doc["missing"]["a"]["b"]["c"].is_none());
    CHECK(doc["appid"]["not an object"].is_none());
    CHECK(doc["UserConfig"]["language"]["x"].is_none());
    CHECK_FALSE(doc["missing"].has_value());
    CHECK(doc["missing"].empty());
    CHECK(doc["missing"].size() == 0);
    CHECK(doc["missing"].begin() == doc["missing"].end());

    CHECK(doc.contains("appid"));
    CHECK_FALSE(doc.contains("AppID"));
    CHECK_FALSE(doc.contains("missing"));
    CHECK(doc.count("dll") == 2);
    CHECK(doc.count("missing") == 0);
    CHECK(doc["appid"].count("x") == 0);
    CHECK_FALSE(doc["appid"].contains("x"));

    REQUIRE(doc.find("UserConfig") != nullptr);
    CHECK(doc.find("UserConfig")->key() == "UserConfig");
    CHECK(doc.find("UserConfig")->find("beta")->as<int>() == 0);
    CHECK(doc.find("missing") == nullptr);
    CHECK(doc["appid"].find("x") == nullptr);

    CHECK(doc.at("appid")->as<int>() == 440);
    CHECK(doc.at("UserConfig")->at("language")->as<std::string>() == "english");
    CHECK(Outcome(doc.at("missing")) == "Key");
    CHECK_THAT(Message(doc.at("missing")), Catch::Contains("\"missing\""));
    CHECK(Outcome(doc.at("appid")->at("x")) == "Key");

    // Keys are std::string_view: no std::string needed.
    const std::string_view key = "appid";
    CHECK(doc[key].as<int>() == 440);
    CHECK(doc[std::string("appid")].as<int>() == 440);
    CHECK(doc[std::string_view("appid_suffix", 5)].as<int>() == 440);

    CHECK(doc.to_text().value() == before);
    CHECK(doc.size() == 5);
}

TEST_CASE("Non-const operator[] creates keys lazily", "[api][lookup]")
{
    KeyValue doc = MakeDocument();
    const std::string before = doc.to_text().value();

    SECTION("reading does not add anything")
    {
        CHECK(doc["missing"].is_none());
        CHECK(doc["missing"]["a"]["b"].value_or(5) == 5);
        CHECK_FALSE(doc["UserConfig"]["nope"].has_value());
        CHECK_FALSE(doc["other"]["nope"].try_as<int>().has_value());
        for (int i = 0; i < 3; ++i)
            CHECK(doc["missing"].value_or("x") == "x");

        CHECK(doc.to_text().value() == before);
        CHECK(doc.size() == 5);
        CHECK(doc["UserConfig"].size() == 2);
        CHECK_FALSE(doc.contains("missing"));
        CHECK_FALSE(doc.contains("other"));
        CHECK(doc.find("missing") == nullptr);
        CHECK(doc.count("missing") == 0);
        CHECK(doc.all("missing").empty());
        CHECK(Outcome(doc.at("missing")) == "Key");
        CHECK(Keys(doc) == "appid name UserConfig{} dll dll ");
        CHECK(doc == MakeDocument());
        CHECK(EasyVDF::parse(doc.to_binary().value()) == doc);
        CHECK(KeyValue(doc).to_text().value() == before);
    }

    SECTION("assigning creates the key")
    {
        doc["StateFlags"] = 4;
        CHECK(doc.contains("StateFlags"));
        CHECK(doc["StateFlags"].as<int>() == 4);
        CHECK(doc.size() == 6);
        CHECK(Keys(doc) == "appid name UserConfig{} dll dll StateFlags ");
    }

    SECTION("assigning creates the whole path")
    {
        doc["a"]["b"]["c"] = "deep";
        CHECK(doc["a"].is_object());
        CHECK(doc["a"]["b"].is_object());
        CHECK(doc["a"]["b"]["c"].as<std::string>() == "deep");
        CHECK(doc["a"].size() == 1);
        CHECK(std::as_const(doc)["a"]["b"]["c"].as<std::string>() == "deep");
        CHECK(Keys(doc) == "appid name UserConfig{} dll dll a{} ");
        CHECK(EasyVDF::parse(doc.to_text().value()).value()["a"]["b"]["c"].as<std::string>() == "deep");

        doc["UserConfig"]["new"]["x"] = 1;
        CHECK(Keys(doc["UserConfig"]) == "language beta new{} ");
    }

    SECTION("a key appears where it is assigned, not where it was first read")
    {
        CHECK(doc["first"].is_none());
        CHECK(doc["second"].is_none());
        doc["third"] = 3;
        doc["second"] = 2;
        doc.add("fourth", 4);
        doc["first"] = 1;
        CHECK(Keys(doc) == "appid name UserConfig{} dll dll third second fourth first ");
        CHECK(doc.to_text().value().find("\"third\"\t\t\"3\"\n\t\"second\"\t\t\"2\"\n\t\"fourth\"\t\t\"4\"\n\t\"first\"\t\t\"1\"\n}") != std::string::npos);
    }

    SECTION("a reference to a missing key can be assigned later")
    {
        Node& later = doc["later"];
        Node& nested = doc["group"]["nested"];
        CHECK(doc.size() == 5);
        doc["other"] = 1;
        later = "now";
        nested = 2;
        CHECK(doc["later"].as<std::string>() == "now");
        CHECK(doc["group"]["nested"].as<int>() == 2);
        CHECK(&doc["later"] == &later);
        CHECK(&doc["group"]["nested"] == &nested);
        // Visible to const readers right away. Keys that got their value between two
        // accesses to the parent are appended in the order they were first requested.
        CHECK(std::as_const(doc).contains("later"));
        CHECK(std::as_const(doc).size() == 8);
        CHECK(Keys(doc) == "appid name UserConfig{} dll dll later group{} other ");
    }

    SECTION("an object created by a read vanishes with its content")
    {
        doc["group"]["k"] = 1;
        CHECK(doc.contains("group"));
        doc["group"]["k"] = nullptr;
        // "group" was explicitly given a child: it stays, empty.
        CHECK(doc["group"].is_object());
        CHECK(doc["group"].empty());

        doc["ghost"]["k"];
        CHECK_FALSE(doc.contains("ghost"));
        CHECK(doc["ghost"].is_none());
    }

    SECTION("existing keys are returned, the first one for duplicates")
    {
        doc["dll"] = "first.dll";
        std::vector<std::string> dlls;
        for (auto const& dll : doc.all("dll"))
            dlls.push_back(dll.as<std::string>().value());
        CHECK(dlls == std::vector<std::string>{"first.dll", "b.dll"});
        CHECK(doc.size() == 5);
    }

    SECTION("indexing a scalar")
    {
        // Reading through a const node is always safe...
        CHECK(std::as_const(doc)["appid"]["x"].is_none());
        // ...but a non-const scalar can't get children: doc["appid"]["x"] aborts
        // (tested by the no_exceptions_scalar_aborts CTest test).
        CHECK(doc.to_text().value() == before);

        // Replace the value to turn it into an object.
        doc["appid"] = Node::object();
        doc["appid"]["x"] = 1;
        CHECK(doc["appid"]["x"].as<int>() == 1);
    }

    SECTION("a node without value becomes an object on demand")
    {
        Node node;
        node["a"] = 1;
        node["b"]["c"] = 2;
        CHECK(node.is_object());
        CHECK(Keys(node) == "a b{} ");

        KeyValue root;
        root.set_key("root");
        root["k"] = "v";
        CHECK(root.to_text().value() == "\"root\"\n{\n\t\"k\"\t\t\"v\"\n}\n");
    }
}

TEST_CASE("References to nodes stay valid when their parent changes", "[api][lookup]")
{
    KeyValue doc = MakeDocument();
    Node& name = doc["name"];
    Node& language = doc["UserConfig"]["language"];
    KeyValue* config = doc.find("UserConfig");
    const std::string_view name_view = name.as<std::string_view>().value();

    for (int i = 0; i < 1000; ++i)
    {
        doc.add("k" + std::to_string(i), i);
        doc["UserConfig"]["k" + std::to_string(i)] = i;
    }
    doc.insert(doc.begin(), KeyValue("first", 1));
    doc.erase("dll");
    doc.sort([](KeyValue const& l, KeyValue const& r) { return l.key() < r.key(); });

    CHECK(&doc["name"] == &name);
    CHECK(&doc["UserConfig"]["language"] == &language);
    CHECK(doc.find("UserConfig") == config);
    CHECK(name.as<std::string>() == "Team Fortress 2");
    CHECK(name_view == "Team Fortress 2");
    CHECK(language.as<std::string>() == "english");
    CHECK(config->size() == 1002);

    // Moving the document keeps them valid too.
    KeyValue moved = std::move(doc);
    CHECK(&moved["name"] == &name);
    CHECK(moved.find("UserConfig") == config);
    language = "french";
    CHECK(moved["UserConfig"]["language"].as<std::string>() == "french");
}

/////////////////////////////////////////////////////////////////////
// Duplicate keys, children
/////////////////////////////////////////////////////////////////////

TEST_CASE("Duplicate keys", "[api][duplicates]")
{
    KeyValue doc = MakeDocument();

    doc.add("dll", "c.dll");
    doc.add("dll", { {"path", "d.dll"} });
    CHECK(doc.count("dll") == 4);
    CHECK(doc["dll"].as<std::string>() == "a.dll");

    std::vector<std::string> seen;
    for (auto& dll : doc.all("dll"))
        seen.push_back(dll.is_object() ? dll["path"].as<std::string>().value() : dll.as<std::string>().value());
    CHECK(seen == std::vector<std::string>{"a.dll", "b.dll", "c.dll", "d.dll"});

    SECTION("all() gives modifiable entries")
    {
        int i = 0;
        for (KeyValue& dll : doc.all("dll"))
            dll = i++;
        const KeyValue& cdoc = doc;
        int expected = 0;
        for (KeyValue const& dll : cdoc.all("dll"))
        {
            CHECK(dll.key() == "dll");
            CHECK(dll.as<int>() == expected++);
        }
        CHECK(expected == 4);
    }

    SECTION("all() of a temporary key")
    {
        std::size_t count = 0;
        for (auto& dll : doc.all(std::string("d") + "ll"))
            count += dll.has_value() ? 1 : 0;
        CHECK(count == 4);

        auto range = std::as_const(doc).all("dll");
        CHECK(range.size() == 4);
        CHECK_FALSE(range.empty());
        CHECK(range.front().as<std::string>() == "a.dll");
        CHECK(std::as_const(doc).all("nope").empty());
        CHECK(std::as_const(doc)["appid"].all("x").empty());
        CHECK(Node().all("x").size() == 0);
    }

    SECTION("erase removes them all")
    {
        CHECK(doc.erase("dll") == 4);
        CHECK(doc.count("dll") == 0);
        CHECK(doc.erase("dll") == 0);
        CHECK(Keys(doc) == "appid name UserConfig{} ");
        CHECK(Node().erase("x") == 0);
        CHECK(Node(5).erase("x") == 0);
    }

    SECTION("erase one of them")
    {
        auto it = doc.begin();
        while (it != doc.end() && !(it->key() == "dll" && it->value_or("") == "b.dll"))
            ++it;
        REQUIRE(it != doc.end());
        it = doc.erase(it);
        CHECK(it->as<std::string>() == "c.dll");
        CHECK(doc.count("dll") == 3);
    }

    SECTION("duplicates survive every format")
    {
        for (EasyVDF::Format format : {EasyVDF::Format::Text, EasyVDF::Format::Binary, EasyVDF::Format::Vbkv})
            CHECK(EasyVDF::parse(doc.dump(format).value()) == doc);
    }
}

TEST_CASE("Iterating children", "[api][iteration]")
{
    KeyValue doc = MakeDocument();

    SECTION("structured bindings")
    {
        std::string keys;
        for (auto& [key, value] : doc)
        {
            static_assert(std::is_same<decltype(key), const std::string>::value, "the key is const");
            static_assert(std::is_same<decltype(value), Node>::value, "the value is modifiable");
            keys += key + ' ';
            if (value.is_string())
                value = value.as<std::string>().value() + "!";
        }
        CHECK(keys == "appid name UserConfig dll dll ");
        CHECK(doc["name"].as<std::string>() == "Team Fortress 2!");

        for (auto const& [key, value] : std::as_const(doc))
        {
            static_assert(std::is_same<decltype(value), const Node>::value, "const iteration gives const values");
            CHECK(std::as_const(doc).find(key) != nullptr);
            CHECK(value.has_value());
        }

        // By value: a copy.
        for (auto [key, value] : doc)
            value = "copy";
        CHECK(doc["appid"].as<std::string>() == "440!");

        auto& [root_key, root_value] = doc;
        CHECK(root_key == "AppState");
        CHECK(&root_value == &static_cast<Node&>(doc));
    }

    SECTION("plain iteration")
    {
        std::size_t count = 0;
        for (KeyValue& child : doc)
        {
            CHECK_FALSE(child.key().empty());
            ++count;
        }
        CHECK(count == doc.size());

        for (const KeyValue& child : std::as_const(doc))
            CHECK(child.has_value());

        Node::iterator it = doc.begin();
        Node::const_iterator cit = it; // converts
        CHECK(cit == doc.cbegin());
        CHECK(it->key() == "appid");
        CHECK((*it).key() == "appid");
        CHECK((it++)->key() == "appid");
        CHECK(it->key() == "name");
        CHECK(std::distance(doc.begin(), doc.end()) == 5);
        CHECK(std::distance(doc.cbegin(), doc.cend()) == 5);

        static_assert(std::is_same<std::iterator_traits<Node::iterator>::iterator_category, std::forward_iterator_tag>::value, "");
        static_assert(std::is_same<std::iterator_traits<Node::iterator>::reference, KeyValue&>::value, "");
        static_assert(std::is_same<std::iterator_traits<Node::const_iterator>::reference, KeyValue const&>::value, "");

        auto found = std::find_if(doc.begin(), doc.end(), [](KeyValue const& kv) { return kv.is_object(); });
        REQUIRE(found != doc.end());
        CHECK(found->key() == "UserConfig");
    }

    SECTION("nothing to iterate")
    {
        for (Node const& node : {Node(), Node(5), Node("text"), Node::object()})
        {
            CHECK(node.begin() == node.end());
            CHECK(node.empty());
            CHECK(node.size() == 0);
        }
        Node scalar("text");
        CHECK(scalar.begin() == scalar.end());
    }

    SECTION("appending while iterating is safe")
    {
        std::string keys;
        for (auto& [key, value] : doc)
        {
            keys += key + ' ';
            if (key == "appid")
            {
                doc.add("added", 1);
                doc["assigned"] = 2;
                CHECK(doc["never assigned"].is_none());
            }
        }
        CHECK(keys == "appid name UserConfig dll dll added assigned ");
    }
}

TEST_CASE("Building and editing objects", "[api][editing]")
{
    SECTION("initializer lists")
    {
        Node n = {
            {"string", "text"},
            {"int", 5},
            {"float", 1.5f},
            {"empty", Node::object()},
            {"nested", {
                {"a", 1},
                {"b", { {"c", "d"} }},
            }},
            {"dup", 1},
            {"dup", 2},
        };
        CHECK(n.is_object());
        CHECK(n.size() == 7);
        CHECK(n["int"].type() == Type::Int32);
        CHECK(n["empty"].is_object());
        CHECK(n["nested"]["b"]["c"].as<std::string>() == "d");
        CHECK(n.count("dup") == 2);

        CHECK(Node(std::initializer_list<KeyValue>{}).is_object());
        CHECK(Node(std::initializer_list<KeyValue>{}).empty());
    }

    SECTION("add")
    {
        KeyValue doc("root");
        KeyValue& added = doc.add("k", "v");
        CHECK(added.key() == "k");
        CHECK(&added == doc.find("k"));

        KeyValue& object = doc.add("sub");
        CHECK(object.is_object());
        object.add("a", 1);
        object.add("b").add("c", 2.5f);
        std::string key = "moved key";
        doc.add(std::move(key), std::string("moved value"));
        doc.add("copy", static_cast<Node const&>(object));

        CHECK(doc.to_text().value() ==
            "\"root\"\n{\n"
            "\t\"k\"\t\t\"v\"\n"
            "\t\"sub\"\n\t{\n\t\t\"a\"\t\t\"1\"\n\t\t\"b\"\n\t\t{\n\t\t\t\"c\"\t\t\"2.5\"\n\t\t}\n\t}\n"
            "\t\"moved key\"\t\t\"moved value\"\n"
            "\t\"copy\"\n\t{\n\t\t\"a\"\t\t\"1\"\n\t\t\"b\"\n\t\t{\n\t\t\t\"c\"\t\t\"2.5\"\n\t\t}\n\t}\n"
            "}\n");
    }

    SECTION("renaming")
    {
        KeyValue doc = MakeDocument();
        doc.find("appid")->set_key("AppID");
        CHECK_FALSE(doc.contains("appid"));
        CHECK(doc["AppID"].as<int>() == 440);
        doc.set_key("Renamed");
        CHECK(doc.to_text().value().rfind("\"Renamed\"\n{\n\t\"AppID\"", 0) == 0);
    }

    SECTION("equality")
    {
        KeyValue a = MakeDocument();
        KeyValue b = MakeDocument();
        CHECK(a == b);
        CHECK_FALSE(a != b);

        b["UserConfig"]["language"] = "french";
        CHECK(a != b);
        b["UserConfig"]["language"] = "english";
        CHECK(a == b);

        b.set_key("Other");
        CHECK(a != b);
        CHECK(static_cast<Node const&>(a) == static_cast<Node const&>(b)); // the values are equal

        b.add("extra", 1);
        CHECK(static_cast<Node const&>(a) != static_cast<Node const&>(b));
        b.erase("extra");
        b.sort([](KeyValue const& l, KeyValue const& r) { return l.key() < r.key(); });
        CHECK(static_cast<Node const&>(a) != static_cast<Node const&>(b)); // order matters

        CHECK(Node(5) == Node(5));
        CHECK(Node(5) != Node(6));
        CHECK(Node(5) != Node("5"));          // same text, different types
        CHECK(Node(5) != Node(int64_t(5)));
        CHECK(Node("a") == Node(std::string("a")));
        CHECK(Node() == Node(nullptr));
        CHECK(Node() != Node::object());
        CHECK(Node::object() == Node(std::initializer_list<KeyValue>{}));
        CHECK(Node(1.5f) == Node(1.5));
        CHECK(Node(Color{1, 2, 3, 4}) != Node(Color{1, 2, 3, 5}));
    }
}

/////////////////////////////////////////////////////////////////////
// Text output
/////////////////////////////////////////////////////////////////////

TEST_CASE("Floats are written with the shortest text that reads back identically", "[api][float]")
{
    CHECK(Node(3.1415f).as<std::string>() == "3.1415");
    CHECK(Node(0.1f).as<std::string>() == "0.1");
    CHECK(Node(1.f).as<std::string>() == "1");
    CHECK(Node(-2.5f).as<std::string>() == "-2.5");
    CHECK(Node(0.f).as<std::string>() == "0");
    CHECK(Node(16777216.f).as<double>() == 16777216.0);

    // 6 significant digits (the old output) are not enough for these.
    std::vector<float> values = {
        3.14159274f, 0.1f, 1.f / 3.f, 16777217.f, 1234567.875f, 1.00000012f, -1.5e-30f, 3.40282347e38f, 1.17549435e-38f,
        1.401298464e-45f, 123456.789f, 0.000123456789f, 8388608.5f, -0.f, 1e10f, 2.71828175f,
    };
    // And a deterministic sweep over the whole range of bit patterns.
    uint32_t bits = 0x12345678u;
    for (int i = 0; i < 20000; ++i)
    {
        bits = bits * 1664525u + 1013904223u;
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        if (std::isfinite(f))
            values.push_back(f);
    }

    KeyValue doc("floats");
    for (float f : values)
        doc.add("f", f);

    KeyValue parsed = EasyVDF::parse(doc.to_text().value()).value();
    REQUIRE(parsed.size() == values.size());

    std::size_t i = 0, mismatches = 0;
    for (auto const& [key, value] : parsed)
    {
        const float expected = values[i++];
        const float read = value.as<float>().value();
        if (std::memcmp(&read, &expected, sizeof(float)) != 0)
        {
            ++mismatches;
            UNSCOPED_INFO(key << ": " << value.as<std::string>().value() << " does not read back as the same float");
        }
    }
    CHECK(mismatches == 0);

    // Binary keeps the bits, including the special values.
    KeyValue special("special");
    special.add("inf", std::numeric_limits<float>::infinity());
    special.add("ninf", -std::numeric_limits<float>::infinity());
    special.add("nan", std::numeric_limits<float>::quiet_NaN());
    KeyValue special_parsed = EasyVDF::parse(special.to_binary().value()).value();
    CHECK(special_parsed["inf"].as<float>() == std::numeric_limits<float>::infinity());
    CHECK(special_parsed["ninf"].as<float>() == -std::numeric_limits<float>::infinity());
    CHECK(std::isnan(special_parsed["nan"].as<float>().value()));
}

TEST_CASE("Number conversions do not depend on the global locale", "[api][float]")
{
    struct RestoreLocale
    {
        std::locale previous = std::locale();
        ~RestoreLocale() { std::locale::global(previous); }
    } restore;

    // A locale whose decimal point is a comma and which groups thousands.
    struct CommaNumpunct : std::numpunct<char>
    {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };
    std::locale::global(std::locale(std::locale::classic(), new CommaNumpunct));

    CHECK(Node(1234567.5f).as<std::string>() == "1234567.5");
    CHECK(Node(1234567).as<std::string>() == "1234567");
    CHECK(Node("1234567.5").as<double>() == 1234567.5);
    CHECK(Node("1234567").as<int>() == 1234567);
    CHECK(Outcome(Node("1,5").as<double>()) == "Type");

    KeyValue doc("root", { {"f", 0.5f}, {"i", 1000000}, {"u", uint64_t(18446744073709551615ull)} });
    CHECK(doc.to_text().value() == "\"root\"\n{\n\t\"f\"\t\t\"0.5\"\n\t\"i\"\t\t\"1000000\"\n\t\"u\"\t\t\"18446744073709551615\"\n}\n");
}

TEST_CASE("Stream output", "[api][stream]")
{
    const KeyValue doc = MakeDocument();

    std::ostringstream document;
    document << doc;
    CHECK(document.str() == doc.to_text().value());

    std::ostringstream values;
    values << doc["name"] << '|' << doc["appid"] << '|' << doc["missing"] << '|' << Node(1.5f) << '|' << Node(Color{1, 2, 3, 4});
    CHECK(values.str() == "Team Fortress 2|440||1.5|1 2 3 4");

    std::ostringstream object;
    object << doc["UserConfig"];
    CHECK(object.str() == "{\n\t\"language\"\t\t\"english\"\n\t\"beta\"\t\t\"0\"\n}\n");

    std::ostringstream entry;
    entry << *doc.find("name") << *doc.find("UserConfig") << KeyValue();
    CHECK(entry.str() == "\"name\"\t\t\"Team Fortress 2\"\n\"UserConfig\"\n{\n\t\"language\"\t\t\"english\"\n\t\"beta\"\t\t\"0\"\n}\n");

    // write() is dump() into a stream.
    for (EasyVDF::Format format : {EasyVDF::Format::Text, EasyVDF::Format::Binary, EasyVDF::Format::Vbkv})
    {
        std::ostringstream os;
        doc.write(os, format).value();
        CHECK(os.str() == doc.dump(format).value());
    }
    CHECK(doc.dump().value() == doc.to_text().value());
    CHECK(doc.dump(EasyVDF::Format::Auto).value() == doc.to_text().value());
}

/////////////////////////////////////////////////////////////////////
// Regressions found by review
/////////////////////////////////////////////////////////////////////

TEST_CASE("An object that got a value keeps existing once emptied", "[api][lookup]")
{
    KeyValue doc("root");

    SECTION("erase by key")
    {
        doc["a"]["b"] = 1;
        CHECK(doc["a"].erase("b") == 1);
        CHECK(doc.contains("a"));
        CHECK(doc["a"].is_object());
        CHECK(doc["a"].empty());
    }
    SECTION("erase by iterator")
    {
        doc["a"]["b"] = 1;
        Node& a = doc["a"];
        a.erase(std::as_const(a).begin());
        CHECK(doc.contains("a"));
        CHECK(a.is_object());
    }
    SECTION("clear")
    {
        doc["a"]["b"] = 1;
        doc["a"].clear();
        CHECK(doc["a"].is_object());
        CHECK(doc.to_text().value() == "\"root\"\n{\n\t\"a\"\n\t{\n\t}\n}\n");
    }
    SECTION("copies behave the same")
    {
        doc["a"]["b"] = 1;
        KeyValue copy(doc);
        copy["a"].erase("b");
        CHECK(copy.contains("a"));
        CHECK(copy["a"].is_object());
    }
    SECTION("an object that never had a value does not appear")
    {
        CHECK(doc["a"]["b"].is_none());
        CHECK(doc["a"].erase("b") == 0);
        doc["a"].clear();
        CHECK_FALSE(doc.contains("a"));
        CHECK(KeyValue(doc).empty());
    }
}

TEST_CASE("Floating point text is plain decimal when reasonable", "[api][float]")
{
    CHECK(Node(100000.f).as<std::string>() == "100000");
    CHECK(Node(1e10f).as<std::string>() == "10000000000");
    CHECK(Node(0.0001f).as<std::string>() == "0.0001");
    CHECK(Node(-0.5f).as<std::string>() == "-0.5");
    CHECK(Node(16777216.f).as<std::string>() == "16777216");
    CHECK(Node(1234567.875f).as<std::string>() == "1234567.9");
    // Very large or very small numbers use the scientific notation.
    CHECK(Node(1e20f).as<std::string>()->find('e') != std::string::npos);
    CHECK(Node(1.5e-30f).as<std::string>()->find('e') != std::string::npos);
    CHECK(Node(Node(1e20f).as<std::string>().value()).as<float>() == 1e20f);
    CHECK(Node(Node(1.5e-30f).as<std::string>().value()).as<float>() == 1.5e-30f);

    // Strings are parsed as the requested type, with its range.
    CHECK(Outcome(Node("1e39").as<float>()) == "Type");
    CHECK(Outcome(Node("-1e39").as<float>()) == "Type");
    CHECK(Node("1e39").as<double>() == 1e39);
    CHECK(Outcome(Node("1e999").as<double>()) == "Type");
    CHECK(Node("1e-60").as<float>() == 0.f);
    CHECK(Node("3.4028235e38").as<float>() == (std::numeric_limits<float>::max)());

    // Malformed signs and hexadecimal floats are refused on every platform.
    for (const char* bad : {"+-5", "++5", "-+5", "+", "0x10.8", "0x1p3", "0X10"})
    {
        INFO("'" << bad << "'");
        CHECK(Outcome(Node(bad).as<double>()) == "Type");
        CHECK(Outcome(Node(bad).as<float>()) == "Type");
    }
    CHECK(Node("+5").as<double>() == 5.0);
    CHECK(Node("-5").as<float>() == -5.f);

    // A double out of the range of a float is stored as an infinity.
    CHECK(Node(1e300).as<float>() == std::numeric_limits<float>::infinity());
    CHECK(Node(-1e300).as<float>() == -std::numeric_limits<float>::infinity());
    CHECK(Node(static_cast<double>((std::numeric_limits<float>::max)())).as<float>() == (std::numeric_limits<float>::max)());
}

TEST_CASE("Int32 values read as 32 bits unsigned", "[api][conversions]")
{
    // The appid of a non-Steam shortcut, as stored in shortcuts.vdf.
    const uint32_t appid = 3000000000u;
    KeyValue shortcuts("shortcuts");
    shortcuts.add("0").add("appid", static_cast<int32_t>(appid));

    KeyValue parsed = EasyVDF::parse(shortcuts.to_binary().value()).value();
    CHECK(parsed["0"]["appid"].type() == Type::Int32);
    CHECK(parsed["0"]["appid"].as<int32_t>() == -1294967296);
    CHECK(parsed["0"]["appid"].as<uint32_t>() == appid);
    CHECK(parsed["0"]["appid"].value_or<uint32_t>(0) == appid);
    CHECK(parsed["0"]["appid"].try_as<uint32_t>() == appid);
    // Only for the 32 bits unsigned types: everything else is range checked.
    CHECK(Outcome(parsed["0"]["appid"].as<uint64_t>()) == "Type");
    CHECK(Outcome(parsed["0"]["appid"].as<uint16_t>()) == "Type");
    CHECK(Outcome(Node("-1294967296").as<uint32_t>()) == "Type");
    CHECK(Outcome(Node(int64_t(-1294967296)).as<uint32_t>()) == "Type");
}

TEST_CASE("A stream read error is an IoError, not a truncated document", "[api][stream]")
{
    // Fails after a few bytes, like a device error in the middle of a file.
    struct FailingBuffer : std::streambuf
    {
        std::string data = "\"root\" { \"k\" \"v\"";
        std::size_t position = 0;
        char current = 0;

        int_type underflow() override
        {
            if (position == data.size())
                throw std::runtime_error("device error");

            current = data[position++];
            setg(&current, &current, &current + 1);
            return traits_type::to_int_type(current);
        }
    };

    FailingBuffer buffer;
    std::istream is(&buffer);
    CHECK(Outcome(EasyVDF::parse(is)) == "Io");

    // The end of a stream is not an error.
    std::istringstream complete("\"root\" { \"k\" \"v\" }");
    CHECK(EasyVDF::parse(complete).value()["k"].as<std::string>() == "v");
    std::istringstream truncated("\"root\" { \"k\" \"v\"");
    CHECK(IsParseError(EasyVDF::parse(truncated)));
}
