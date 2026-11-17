// EasyVDF.h is included first on purpose: it must compile on its own.
#include "../EasyVDF.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "catch.hpp"

using EasyVDF::ObjectType;
using EasyVDF::ValveDataObject;

/////////////////////////////////////////////////////////////////////
// Global operator new/delete replacement: counts allocations and can be
// told to fail the Nth one, to check that moves do not allocate and that
// nothing leaks when an allocation throws. Inactive unless armed.
/////////////////////////////////////////////////////////////////////
namespace {
long long g_live_allocations = 0;
long long g_total_allocations = 0;
long long g_fail_countdown = -1; // < 0: disabled, N: let N allocations through, fail the next one.
}

void* operator new(std::size_t size)
{
    if (g_fail_countdown >= 0 && g_fail_countdown-- == 0)
        throw std::bad_alloc();

    void* p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr)
        throw std::bad_alloc();

    ++g_live_allocations;
    ++g_total_allocations;
    return p;
}

void operator delete(void* p) noexcept
{
    if (p == nullptr)
        return;

    --g_live_allocations;
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

namespace {

// Long enough to defeat the small string optimization of every implementation.
const std::string LongKey   = "a key long enough to need a heap allocation";
const std::string LongValue = "a value long enough to need a heap allocation";

// Runs f with the 1st, 2nd, ... allocation failing until f completes without
// failure, and checks that every failed attempt released what it allocated.
template<typename F>
void CheckNoLeakOnAllocationFailure(F&& f)
{
    for (int fail_at = 0; fail_at < 1000; ++fail_at)
    {
        long long live_before = g_live_allocations;
        bool threw = false;

        {
            struct Disarm { ~Disarm() { g_fail_countdown = -1; } } disarm;
            g_fail_countdown = fail_at;
            try
            {
                f();
            }
            catch (std::bad_alloc const&)
            {
                threw = true;
            }
        }
        long long leaked = g_live_allocations - live_before;

        INFO("allocation #" << fail_at << " failed: " << (threw ? "threw" : "completed"));
        CHECK(leaked == 0);
        if (!threw)
            return;
    }
    FAIL("f never completed");
}

// "a=A b=B" for a collection of String values.
std::string Dump(EasyVDF::ValveCollection const& c)
{
    std::string r;
    for (auto const& item : c)
    {
        if (!r.empty())
            r += ' ';
        r += item.Name() + '=' + (item.Type() == ObjectType::String ? item.String() : std::string("?"));
    }
    return r;
}

ValveDataObject MakeABC()
{
    ValveDataObject root("Root");
    root.Collection().emplace_back("a", std::string("A"));
    root.Collection().emplace_back("b", std::string("B"));
    root.Collection().emplace_back("c", std::string("C"));
    return root;
}

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

TEST_CASE("Collection algorithms keep keys paired with values", "[object_model][assignment]")
{
    ValveDataObject root = MakeABC();
    auto& c = root.Collection();
    REQUIRE(Dump(c) == "a=A b=B c=C");

    SECTION("erase")
    {
        c.erase(c.begin());
        CHECK(Dump(c) == "b=B c=C");
    }

    SECTION("insert in the middle")
    {
        c.insert(c.begin() + 1, ValveDataObject("x", std::string("X")));
        CHECK(Dump(c) == "a=A x=X b=B c=C");
    }

    SECTION("std::swap")
    {
        std::swap(c[0], c[1]);
        CHECK(Dump(c) == "b=B a=A c=C");
    }

    SECTION("std::sort")
    {
        std::sort(c.begin(), c.end(), [](ValveDataObject const& l, ValveDataObject const& r) { return l.Name() > r.Name(); });
        CHECK(Dump(c) == "c=C b=B a=A");
    }

    SECTION("remove_if + erase")
    {
        c.erase(std::remove_if(c.begin(), c.end(), [](ValveDataObject const& o) { return o.Name() == "b"; }), c.end());
        CHECK(Dump(c) == "a=A c=C");
    }

    SECTION("lookup after reordering")
    {
        std::reverse(c.begin(), c.end());
        CHECK(root["a"][0].String() == "A");
        CHECK(root["b"][0].String() == "B");
        CHECK(root["c"][0].String() == "C");
    }
}

TEST_CASE("ValveDataObject copy and move carry the name with the value", "[object_model][assignment]")
{
    ValveDataObject src = MakeABC();
    src.Name("Source");

    SECTION("copy construction is deep")
    {
        ValveDataObject copy(src);
        CHECK(copy.Name() == "Source");
        CHECK(Dump(copy.Collection()) == "a=A b=B c=C");
        copy.Collection()[0].String() = "changed";
        CHECK(src.Collection()[0].String() == "A");
    }

    SECTION("copy assignment")
    {
        ValveDataObject dst("dst", int32_t(5));
        dst = src;
        CHECK(dst.Name() == "Source");
        CHECK(Dump(dst.Collection()) == "a=A b=B c=C");
        CHECK(src.Name() == "Source");
        CHECK(Dump(src.Collection()) == "a=A b=B c=C");
    }

    SECTION("move construction")
    {
        ValveDataObject dst(std::move(src));
        CHECK(dst.Name() == "Source");
        CHECK(Dump(dst.Collection()) == "a=A b=B c=C");
    }

    SECTION("move assignment")
    {
        ValveDataObject dst("dst", int32_t(5));
        dst = std::move(src);
        CHECK(dst.Name() == "Source");
        CHECK(Dump(dst.Collection()) == "a=A b=B c=C");
    }

    SECTION("self copy assignment")
    {
        ValveDataObject& alias = src;
        src = alias;
        CHECK(src.Name() == "Source");
        CHECK(Dump(src.Collection()) == "a=A b=B c=C");
    }

    SECTION("self move assignment")
    {
        ValveDataObject& alias = src;
        src = std::move(alias);
        CHECK(src.Name() == "Source");
        CHECK(Dump(src.Collection()) == "a=A b=B c=C");
    }

    SECTION("assign a child to its parent (copy)")
    {
        src = src.Collection()[1];
        CHECK(src.Name() == "b");
        CHECK(src.String() == "B");
    }

    SECTION("assign a child to its parent (move)")
    {
        src = std::move(src.Collection()[1]);
        CHECK(src.Name() == "b");
        CHECK(src.String() == "B");
    }
}

TEST_CASE("Assigning through a reference wrapper keeps the key", "[object_model][assignment]")
{
    ValveDataObject root = MakeABC();
    root.Collection().emplace_back("obj", ValveDataObject("child", std::string("Child")));

    SECTION("from a ValveDataObject copy")
    {
        ValveDataObject other("other", std::string("Other"));
        root["a"][0] = other;
        CHECK(Dump(root.Collection()).find("a=Other b=B c=C") == 0);
        CHECK(other.Name() == "other");
        CHECK(other.String() == "Other");
    }

    SECTION("from a moved ValveDataObject")
    {
        ValveDataObject other("other", std::string("Other"));
        root["a"][0] = std::move(other);
        CHECK(Dump(root.Collection()).find("a=Other b=B c=C") == 0);
    }

    SECTION("from another reference wrapper")
    {
        root["a"][0] = root["c"][0];
        CHECK(Dump(root.Collection()).find("a=C b=B c=C") == 0);
    }

    SECTION("from a moved reference wrapper")
    {
        auto refs = root["c"];
        root["a"][0] = std::move(refs[0]);
        CHECK(root["a"][0].String() == "C");
        CHECK(root.Collection()[0].Name() == "a");
        CHECK(root.Collection()[2].Name() == "c");
    }

    SECTION("self assignment")
    {
        auto refs = root["a"];
        refs[0] = refs[0];
        CHECK(Dump(root.Collection()).find("a=A b=B c=C") == 0);
        refs[0] = std::move(refs[0]);
        CHECK(Dump(root.Collection()).find("a=A b=B c=C") == 0);
    }

    SECTION("an object element receives the value of its own child (copy)")
    {
        root["obj"][0] = root["obj"][0]["child"][0];
        REQUIRE(root["obj"].size() == 1);
        CHECK(root["obj"][0].Type() == ObjectType::String);
        CHECK(root["obj"][0].String() == "Child");
    }

    SECTION("an object element receives the value of its own child (move)")
    {
        auto obj = root["obj"];
        auto child = obj[0]["child"];
        obj[0] = std::move(child[0]);
        REQUIRE(root["obj"].size() == 1);
        CHECK(root["obj"][0].Type() == ObjectType::String);
        CHECK(root["obj"][0].String() == "Child");
    }

    SECTION("typed values")
    {
        root["b"][0] = int32_t(42);
        CHECK(root.Collection()[1].Name() == "b");
        CHECK(root.Collection()[1].Int32() == 42);
    }
}

static_assert(std::is_nothrow_move_constructible<ValveDataObject>::value, "moves must be noexcept");
static_assert(std::is_nothrow_move_assignable<ValveDataObject>::value, "moves must be noexcept");

TEST_CASE("Moves do not allocate", "[object_model][exception_safety]")
{
    ValveDataObject a = MakeABC();

    long long total_before = g_total_allocations;
    ValveDataObject b(std::move(a));
    a = std::move(b);
    b = std::move(a);
    std::swap(a, b);
    CHECK(g_total_allocations == total_before);

    SECTION("vector growth only allocates the new buffer")
    {
        auto& c = a.Collection();
        c.shrink_to_fit();
        REQUIRE(c.size() == c.capacity());

        total_before = g_total_allocations;
        c.reserve(c.capacity() + 1);
        CHECK(g_total_allocations - total_before == 1);
        CHECK(Dump(c) == "a=A b=B c=C");
    }
}

TEST_CASE("Constructors and assignments do not leak when an allocation fails", "[object_model][exception_safety]")
{
    ValveDataObject child(LongKey, LongValue);
    ValveDataObject tree(LongKey);
    tree.Collection().emplace_back(child);
    tree.Collection().emplace_back(LongKey, int32_t(1));
    tree.Collection().emplace_back(LongKey, ValveDataObject(LongKey, LongValue));

    SECTION("default") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o; }); }
    SECTION("key") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey); }); }
    SECTION("key, string const&") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, LongValue); }); }
    SECTION("key, string&&") { CheckNoLeakOnAllocationFailure([&] { std::string v = LongValue; ValveDataObject o(LongKey, std::move(v)); }); }
    SECTION("key, int32_t") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, int32_t(1)); }); }
    SECTION("key, float") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, 1.f); }); }
    SECTION("key, pointer_t") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, EasyVDF::pointer_t{1}); }); }
    SECTION("key, color_t") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, EasyVDF::color_t{1}); }); }
    SECTION("key, int64_t") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, int64_t(1)); }); }
    SECTION("key, uint64_t") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, uint64_t(1)); }); }
    SECTION("key, ValveDataObject const&") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(LongKey, tree); }); }
    SECTION("key, ValveDataObject&&") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject tmp(tree); ValveDataObject o(LongKey, std::move(tmp)); }); }
    SECTION("copy") { CheckNoLeakOnAllocationFailure([&] { ValveDataObject o(tree); }); }
    SECTION("copy assignment")
    {
        CheckNoLeakOnAllocationFailure([&] {
            ValveDataObject o(LongKey, LongValue);
            bool threw = false;
            try
            {
                o = tree;
            }
            catch (std::bad_alloc const&)
            {
                threw = true;
            }
            if (threw)
            {// Strong guarantee: o is unchanged.
                if (o.Name() != LongKey || o.Type() != ObjectType::String || o.String() != LongValue)
                    FAIL("copy assignment modified the target before throwing");
                throw std::bad_alloc();
            }
        });
    }
    SECTION("assignments on a moved-from object")
    {
        CheckNoLeakOnAllocationFailure([&] {
            ValveDataObject src(LongKey, LongValue);
            ValveDataObject o(std::move(src));
            src = LongValue;
            src.Name(LongKey);
            o = int32_t(1);
        });
    }
    SECTION("reference wrapper assignment")
    {
        CheckNoLeakOnAllocationFailure([&] {
            ValveDataObject root(LongKey);
            root.Collection().emplace_back(LongKey, int32_t(1));
            root[LongKey][0] = tree;
        });
    }
}

TEST_CASE("A moved-from object is empty and usable", "[object_model][moved_from]")
{
    bool by_assignment = GENERATE(false, true);
    CAPTURE(by_assignment);

    ValveDataObject a("a", std::string("A"));
    if (by_assignment)
    {
        ValveDataObject b;
        b = std::move(a);
        CHECK(b.Name() == "a");
    }
    else
    {
        ValveDataObject b(std::move(a));
        CHECK(b.Name() == "a");
    }

    // Observers
    CHECK(a.Name().empty());
    CHECK(a.Type() == ObjectType::None);
    CHECK(a.Empty());
    CHECK((a == nullptr));
    CHECK_THROWS_AS(a.String(), std::invalid_argument);
    CHECK_THROWS_AS(a.Collection(), std::invalid_argument);
    CHECK_THROWS_AS(a.Int32(), std::invalid_argument);
    CHECK_THROWS_AS(a["key"], std::invalid_argument);
    CHECK_THROWS_AS(a.SerializeAsText(), EasyVDF::SerializeException);
    CHECK_THROWS_AS(a.SerializeAsBinary(), EasyVDF::SerializeException);

    // Copies of it are empty too.
    ValveDataObject copy(a);
    CHECK(copy.Name().empty());
    CHECK(copy.Type() == ObjectType::None);
    ValveDataObject assigned("x", int32_t(1));
    assigned = a;
    CHECK(assigned.Name().empty());
    CHECK(assigned.Type() == ObjectType::None);

    SECTION("assign a value") { a = int32_t(3); CHECK(a.Int32() == 3); CHECK(a.Name().empty()); }
    SECTION("assign a string") { a = std::string("S"); CHECK(a.String() == "S"); }
    SECTION("assign nullptr") { a = nullptr; CHECK(a.Empty()); }
    SECTION("rename") { a.Name("renamed"); CHECK(a.Name() == "renamed"); CHECK(a.Empty()); }
    SECTION("assign an object")
    {
        a = MakeABC();
        CHECK(a.Name() == "Root");
        CHECK(Dump(a.Collection()) == "a=A b=B c=C");
    }
    SECTION("assign through a reference wrapper")
    {
        EasyVDF::ValveDataObjectRef ref(&a);
        ref = ValveDataObject("other", std::string("Other"));
        CHECK(a.Name().empty());
        CHECK(a.String() == "Other");
    }
    SECTION("move from it again")
    {
        ValveDataObject b(std::move(a));
        CHECK(b.Empty());
        a = std::move(b);
        CHECK(a.Empty());
    }
    // a is destroyed at the end of each section.
}

TEST_CASE("A collection holding a moved-from element stays usable", "[object_model][moved_from]")
{
    ValveDataObject root = MakeABC();
    ValveDataObject taken(std::move(root.Collection()[1]));
    CHECK(taken.Name() == "b");

    CHECK(root.Collection().size() == 3);
    CHECK(root["a"].size() == 1);
    CHECK(root["b"].empty());
    CHECK(root[""].size() == 1);

    ValveDataObject copy(root);
    CHECK(copy.Collection()[1].Empty());
    CHECK(copy.Collection()[1].Name().empty());

    root.Collection()[1] = std::move(taken);
    CHECK(Dump(root.Collection()) == "a=A b=B c=C");
}

TEST_CASE("Serializing a node without value throws", "[object_model][serialize]")
{
    ValveDataObject root = MakeABC();

    SECTION("default constructed child")
    {
        root.Collection().emplace_back();
        root.Collection().back().Name("none");
    }
    SECTION("child assigned nullptr")
    {
        root["b"][0] = nullptr;
    }
    SECTION("moved-from child")
    {
        ValveDataObject taken(std::move(root.Collection()[1]));
    }
    SECTION("nested child")
    {
        ValveDataObject inner("inner");
        inner.Collection().emplace_back("none", int32_t(0));
        inner.Collection()[0] = nullptr;
        root.Collection().emplace_back(std::move(inner));
    }

    CHECK_THROWS_AS(root.SerializeAsText(), EasyVDF::SerializeException);
    CHECK_THROWS_AS(root.SerializeAsBinary(1), EasyVDF::SerializeException);
    CHECK_THROWS_AS(root.SerializeAsBinary(2), EasyVDF::SerializeException);
}

TEST_CASE("Serializing nodes with values still works", "[object_model][serialize]")
{
    SECTION("empty object")
    {
        ValveDataObject root("Root");
        CHECK(root.SerializeAsText() == "\"Root\"\n{\n}\n");
    }

    SECTION("removing the empty node makes the tree serializable")
    {
        ValveDataObject root = MakeABC();
        root.Collection().emplace_back();
        REQUIRE_THROWS_AS(root.SerializeAsText(), EasyVDF::SerializeException);
        root.Collection().pop_back();

        CHECK(root.SerializeAsText() ==
            "\"Root\"\n"
            "{\n"
            "\t\"a\"\t\t\"A\"\n"
            "\t\"b\"\t\t\"B\"\n"
            "\t\"c\"\t\t\"C\"\n"
            "}\n");

        ValveDataObject parsed = RoundTripBinary(root, 2);
        CHECK(Dump(parsed.Collection()) == "a=A b=B c=C");
    }
}

namespace adl_check {

// This namespace has its own operator== and operator<, which hide any
// operator declared at global scope: pointer_t/color_t operators must be
// found by argument-dependent lookup.
struct Unrelated {};
inline bool operator==(Unrelated, Unrelated) { return true; }
inline bool operator<(Unrelated, Unrelated) { return false; }

template<typename T>
bool AllComparisons(T small, T big)
{
    return small == small && small != big && small < big && small <= big && big > small && big >= small
        && !(small == big) && !(small != small) && !(big < small) && !(big <= small) && !(small > big) && !(small >= big);
}

}

TEST_CASE("pointer_t and color_t comparison operators", "[object_model][operators]")
{
    CHECK(adl_check::AllComparisons(EasyVDF::pointer_t{1}, EasyVDF::pointer_t{2}));
    CHECK(adl_check::AllComparisons(EasyVDF::color_t{1}, EasyVDF::color_t{2}));

    // Catch's decomposition also lives in its own namespace.
    ValveDataObject o("p", EasyVDF::pointer_t{0x90807060});
    CHECK(o.Pointer() == EasyVDF::pointer_t{0x90807060});
    CHECK(o.Pointer() != EasyVDF::pointer_t{0});
    o = EasyVDF::color_t{0x99887766};
    CHECK(o.Color() == EasyVDF::color_t{0x99887766});
    CHECK(o.Color() > EasyVDF::color_t{0});
}

TEST_CASE("Color() reports the Color type in its error", "[object_model][operators]")
{
    ValveDataObject o("p", EasyVDF::pointer_t{1});
    CHECK_THROWS_WITH(o.Color(), Catch::Contains("Color") && !Catch::Contains("Pointer"));
}

TEST_CASE("operator==(nullptr) works on const objects", "[object_model][operators]")
{
    ValveDataObject root("Root");
    root.Collection().emplace_back("none", int32_t(0));
    root.Collection()[0] = nullptr;
    root.Collection().emplace_back("int", int32_t(1));

    ValveDataObject const& croot = root;
    CHECK_FALSE(croot == nullptr);
    CHECK(croot.Collection()[0] == nullptr);

    const EasyVDF::ValveDataObjectConstRef none = croot["none"][0];
    const EasyVDF::ValveDataObjectConstRef value = croot["int"][0];
    CHECK(none == nullptr);
    CHECK_FALSE(value == nullptr);

    const EasyVDF::ValveDataObjectRef ref = root["none"][0];
    CHECK(ref == nullptr);
}

TEST_CASE("Assigning a std::string&& through a reference wrapper moves it", "[object_model][operators]")
{
    ValveDataObject root("Root");
    root.Collection().emplace_back("s", int32_t(0));
    auto refs = root["s"];
    std::string value = LongValue;

    // Moving only allocates the std::string object, not a copy of its buffer.
    long long total_before = g_total_allocations;
    refs[0] = std::move(value);
    CHECK(g_total_allocations - total_before == 1);
    CHECK(root.Collection()[0].String() == LongValue);
}
