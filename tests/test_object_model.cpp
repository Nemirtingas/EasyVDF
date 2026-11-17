#include <sstream>
#include <string>

#include "../EasyVDF.h"

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
