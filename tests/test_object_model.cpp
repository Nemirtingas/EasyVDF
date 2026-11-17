// EasyVDF.h is included first on purpose: it must compile on its own.
#include "../EasyVDF.h"

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "catch.hpp"

using EasyVDF::ObjectType;
using EasyVDF::ValveDataObject;

namespace {

ValveDataObject RoundTripBinary(ValveDataObject const& o, int version)
{
    std::stringstream sstr(o.SerializeAsBinary(version));
    return ValveDataObject::ParseObject(sstr);
}

}

TEST_CASE("Typed assignment sets the matching type", "[object_model][assignment]")
{
    ValveDataObject o;

    SECTION("pointer_t")
    {
        o = EasyVDF::pointer_t{0x90807060};
        REQUIRE(o.Type() == ObjectType::Pointer);
        CHECK(o.Pointer().value == 0x90807060u);
        CHECK_THROWS_AS(o.Int32(), std::invalid_argument);
    }

    SECTION("color_t")
    {
        o = EasyVDF::color_t{0x99887766};
        REQUIRE(o.Type() == ObjectType::Color);
        CHECK(o.Color().value == 0x99887766u);
        CHECK_THROWS_AS(o.Int32(), std::invalid_argument);
    }

    SECTION("pointer_t and color_t through a reference wrapper")
    {
        ValveDataObject root("Root");
        root.Collection().emplace_back("PointerKey", int32_t(1));
        root.Collection().emplace_back("ColorKey", int32_t(2));

        root["PointerKey"][0] = EasyVDF::pointer_t{0x11223344};
        root["ColorKey"][0] = EasyVDF::color_t{0x55667788};

        REQUIRE(root["PointerKey"][0].Type() == ObjectType::Pointer);
        CHECK(root["PointerKey"][0].Pointer().value == 0x11223344u);
        REQUIRE(root["ColorKey"][0].Type() == ObjectType::Color);
        CHECK(root["ColorKey"][0].Color().value == 0x55667788u);
    }
}

TEST_CASE("Assigned pointer_t and color_t survive a binary round trip", "[object_model][assignment][serialize]")
{
    ValveDataObject root("Root");
    root.Collection().emplace_back("PointerKey", int32_t(0));
    root.Collection().emplace_back("ColorKey", int32_t(0));
    root.Collection()[0] = EasyVDF::pointer_t{0x90807060};
    root.Collection()[1] = EasyVDF::color_t{0x99887766};

    for (int version : {1, 2})
    {
        CAPTURE(version);
        ValveDataObject parsed = RoundTripBinary(root, version);

        REQUIRE(parsed.Type() == ObjectType::Object);
        CHECK(parsed.Name() == "Root");
        REQUIRE(parsed["PointerKey"].size() == 1);
        REQUIRE(parsed["PointerKey"][0].Type() == ObjectType::Pointer);
        CHECK(parsed["PointerKey"][0].Pointer().value == 0x90807060u);
        REQUIRE(parsed["ColorKey"].size() == 1);
        REQUIRE(parsed["ColorKey"][0].Type() == ObjectType::Color);
        CHECK(parsed["ColorKey"][0].Color().value == 0x99887766u);
    }
}

TEST_CASE("Copy assignment does not write to std::cout", "[object_model][assignment]")
{
    std::stringstream captured;
    std::streambuf* old_buf = std::cout.rdbuf(captured.rdbuf());

    ValveDataObject a("a", std::string("A"));
    ValveDataObject b("b", std::string("B"));
    b = a;

    std::cout.rdbuf(old_buf);

    CHECK(captured.str().empty());
}

TEST_CASE("operator[] returns only exact key matches", "[object_model][lookup]")
{
    ValveDataObject root("Root");
    auto& c = root.Collection();
    c.emplace_back("key", int32_t(1));
    c.emplace_back("Key", int32_t(2));
    c.emplace_back("key2", int32_t(3));
    c.emplace_back("ke", int32_t(4));
    c.emplace_back("key", int32_t(5));

    auto r = root["key"];
    REQUIRE(r.size() == 2);
    CHECK(r[0].Int32() == 1);
    CHECK(r[1].Int32() == 5);

    ValveDataObject const& croot = root;
    auto cr = croot[std::string("Key")];
    REQUIRE(cr.size() == 1);
    CHECK(cr[0].Int32() == 2);

    CHECK(root["missing"].empty());
    CHECK(root[""].empty());
}

TEST_CASE("operator[] does not confuse keys sharing a hash", "[object_model][lookup]")
{
    // A collision can only be found by brute force when size_t is 32 bits
    // (birthday bound around 2^16 keys). On 64 bits this test is a no-op.
    if (sizeof(size_t) > 4)
    {
        WARN("size_t is wider than 32 bits, no std::hash collision to test with");
        return;
    }

    std::hash<std::string> hasher;
    std::unordered_map<size_t, std::string> seen;
    std::string first, second;
    for (uint32_t i = 0; i < 2000000 && first.empty(); ++i)
    {
        std::string key = "key" + std::to_string(i);
        auto inserted = seen.emplace(hasher(key), key);
        if (!inserted.second)
        {
            first = inserted.first->second;
            second = key;
        }
    }
    REQUIRE_FALSE(first.empty());
    REQUIRE(hasher(first) == hasher(second));

    ValveDataObject root("Root");
    root.Collection().emplace_back(first, int32_t(1));
    root.Collection().emplace_back(second, int32_t(2));

    auto r1 = root[first];
    REQUIRE(r1.size() == 1);
    CHECK(r1[0].Name() == first);
    CHECK(r1[0].Int32() == 1);

    ValveDataObject const& croot = root;
    auto r2 = croot[second];
    REQUIRE(r2.size() == 1);
    CHECK(r2[0].Name() == second);
    CHECK(r2[0].Int32() == 2);
}
