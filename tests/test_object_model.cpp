// EasyVDF.h is included first on purpose: it must compile on its own.
#include "../EasyVDF.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <type_traits>

#include "catch.hpp"

using EasyVDF::KeyValue;
using EasyVDF::Node;
using EasyVDF::Type;

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

// The MSVC STL in debug mode allocates "container proxies" inside noexcept
// functions (std::string's default constructor among others): making one of
// those allocations fail calls std::terminate, whatever the code under test.
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
const bool CanInjectAllocationFailures = false;
#else
const bool CanInjectAllocationFailures = true;
#endif

// For the same reason, exact allocation counts only hold without those proxies.
#define CHECK_ALLOCATIONS(expr) do { if (CanInjectAllocationFailures) CHECK(expr); } while (false)

// Runs f with the 1st, 2nd, ... allocation failing until f completes without
// failure, and checks that every failed attempt released what it allocated.
template<typename F>
void CheckNoLeakOnAllocationFailure(F&& f)
{
    if (!CanInjectAllocationFailures)
    {
        WARN("allocation failure injection is not possible with this standard library configuration");
        long long live_before = g_live_allocations;
        f();
        CHECK(g_live_allocations == live_before);
        return;
    }

    for (int fail_at = 0; fail_at < 5000; ++fail_at)
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

        if (leaked != 0)
        {
            INFO("allocation #" << fail_at << " failed: " << (threw ? "threw" : "completed"));
            CHECK(leaked == 0);
        }
        if (!threw)
        {
            // At least one allocation was made to fail, unless f does not allocate.
            SUCCEED("completed after " << fail_at << " injected failures");
            return;
        }
    }
    FAIL("f never completed");
}

// "a=A b=B" for the children of an object.
std::string Dump(Node const& object)
{
    std::string r;
    for (auto const& [key, value] : object)
    {
        if (!r.empty())
            r += ' ';
        r += key + '=' + (value.type() == Type::String ? value.as<std::string>() : std::string("?"));
    }
    return r;
}

KeyValue MakeABC()
{
    KeyValue root("Root");
    root.add("a", "A");
    root.add("b", "B");
    root.add("c", "C");
    return root;
}

// A tree with long keys and values: every node needs heap allocations.
KeyValue MakeTree()
{
    KeyValue tree(LongKey);
    tree.add(LongKey, LongValue);
    tree.add(LongKey, int32_t(1));
    tree.add(LongKey).add(LongKey, LongValue).set_condition(LongValue);
    tree.add(LongKey + "2").add(LongKey).add(LongKey, 1.5f);
    return tree;
}

}

TEST_CASE("Typed assignment sets the matching type", "[object_model][assignment]")
{
    Node o;

    SECTION("Pointer")
    {
        o = EasyVDF::Pointer{0x90807060};
        REQUIRE(o.type() == Type::Pointer);
        CHECK(o.as<EasyVDF::Pointer>().value == 0x90807060u);
        CHECK(o.as<uint32_t>() == 0x90807060u);
        CHECK_THROWS_AS(o.as<int32_t>(), EasyVDF::TypeError);
        CHECK_THROWS_AS(o.as<EasyVDF::Color>(), EasyVDF::TypeError);
    }

    SECTION("Color")
    {
        o = EasyVDF::Color{0x66, 0x77, 0x88, 0x99};
        REQUIRE(o.type() == Type::Color);
        CHECK(o.as<EasyVDF::Color>() == EasyVDF::Color{0x66, 0x77, 0x88, 0x99});
        CHECK(o.as<std::string>() == "102 119 136 153");
        CHECK_THROWS_AS(o.as<int32_t>(), EasyVDF::TypeError);
        CHECK_THROWS_AS(o.as<EasyVDF::Pointer>(), EasyVDF::TypeError);
    }

    SECTION("Pointer and Color assigned to a child")
    {
        KeyValue root("Root");
        root.add("PointerKey", int32_t(1));
        root.add("ColorKey", int32_t(2));

        root["PointerKey"] = EasyVDF::Pointer{0x11223344};
        root["ColorKey"] = EasyVDF::Color{0x55, 0x66, 0x77, 0x88};

        REQUIRE(root["PointerKey"].type() == Type::Pointer);
        CHECK(root["PointerKey"].as<EasyVDF::Pointer>().value == 0x11223344u);
        REQUIRE(root["ColorKey"].type() == Type::Color);
        CHECK(root["ColorKey"].as<EasyVDF::Color>() == EasyVDF::Color{0x55, 0x66, 0x77, 0x88});
        CHECK(root.size() == 2);
    }
}

TEST_CASE("Assigned Pointer and Color survive a binary round trip", "[object_model][assignment][serialize]")
{
    KeyValue root("Root");
    root.add("PointerKey", int32_t(0));
    root.add("ColorKey", int32_t(0));
    root["PointerKey"] = EasyVDF::Pointer{0x90807060};
    root["ColorKey"] = EasyVDF::Color{0x66, 0x77, 0x88, 0x99};

    for (EasyVDF::Format format : {EasyVDF::Format::Binary, EasyVDF::Format::Vbkv})
    {
        CAPTURE((int)format);
        KeyValue parsed = EasyVDF::parse(root.to_binary(format));

        REQUIRE(parsed.type() == Type::Object);
        CHECK(parsed.key() == "Root");
        REQUIRE(parsed.count("PointerKey") == 1);
        REQUIRE(parsed["PointerKey"].type() == Type::Pointer);
        CHECK(parsed["PointerKey"].as<EasyVDF::Pointer>().value == 0x90807060u);
        REQUIRE(parsed.count("ColorKey") == 1);
        REQUIRE(parsed["ColorKey"].type() == Type::Color);
        CHECK(parsed["ColorKey"].as<EasyVDF::Color>() == EasyVDF::Color{0x66, 0x77, 0x88, 0x99});
        CHECK(parsed == root);
    }
}

TEST_CASE("Copy assignment does not write to std::cout", "[object_model][assignment]")
{
    std::stringstream captured;
    std::streambuf* old_buf = std::cout.rdbuf(captured.rdbuf());

    KeyValue a("a", "A");
    KeyValue b("b", "B");
    b = a;
    Node n = a;
    n = b;

    std::cout.rdbuf(old_buf);

    CHECK(captured.str().empty());
}

TEST_CASE("Lookups return only exact key matches", "[object_model][lookup]")
{
    KeyValue root("Root");
    root.add("key", int32_t(1));
    root.add("Key", int32_t(2));
    root.add("key2", int32_t(3));
    root.add("ke", int32_t(4));
    root.add("key", int32_t(5));

    auto r = root.all("key");
    REQUIRE(r.size() == 2);
    auto it = r.begin();
    CHECK(it->as<int32_t>() == 1);
    CHECK((++it)->as<int32_t>() == 5);
    CHECK(++it == r.end());
    CHECK(root["key"].as<int32_t>() == 1);
    CHECK(root.count("key") == 2);

    KeyValue const& croot = root;
    auto cr = croot.all(std::string("Key"));
    REQUIRE(cr.size() == 1);
    CHECK(cr.front().as<int32_t>() == 2);
    CHECK(croot["Key"].as<int32_t>() == 2);
    CHECK(croot["ke"].as<int32_t>() == 4);
    CHECK(croot["key2"].as<int32_t>() == 3);

    CHECK(croot.all("missing").empty());
    CHECK(croot.all("").empty());
    CHECK(croot.count("missing") == 0);
    CHECK(croot.count("") == 0);
    CHECK_FALSE(croot.contains("k"));
    CHECK_FALSE(croot.contains("KEY"));
    CHECK(croot.find("") == nullptr);
    CHECK(croot[""].is_none());
}

TEST_CASE("Children operations keep keys paired with values", "[object_model][assignment]")
{
    KeyValue root = MakeABC();
    REQUIRE(Dump(root) == "a=A b=B c=C");

    SECTION("erase")
    {
        auto next = root.erase(root.begin());
        CHECK(next->key() == "b");
        CHECK(Dump(root) == "b=B c=C");
    }

    SECTION("insert in the middle")
    {
        auto inserted = root.insert(++root.begin(), KeyValue("x", "X"));
        CHECK(inserted->key() == "x");
        CHECK(Dump(root) == "a=A x=X b=B c=C");
        root.insert(root.begin(), KeyValue("first", "1"));
        root.insert(root.end(), KeyValue("last", "2"));
        CHECK(Dump(root) == "first=1 a=A x=X b=B c=C last=2");
    }

    SECTION("std::swap")
    {
        auto first = root.begin();
        auto second = ++root.begin();
        std::swap(*first, *second);
        CHECK(Dump(root) == "b=B a=A c=C");
    }

    SECTION("sort")
    {
        root.sort([](KeyValue const& l, KeyValue const& r) { return l.key() > r.key(); });
        CHECK(Dump(root) == "c=C b=B a=A");
        CHECK(root["a"].as<std::string>() == "A");
        CHECK(root["b"].as<std::string>() == "B");
        CHECK(root["c"].as<std::string>() == "C");
    }

    SECTION("erase by key")
    {
        CHECK(root.erase("b") == 1);
        CHECK(root.erase("b") == 0);
        CHECK(Dump(root) == "a=A c=C");
    }

    SECTION("erase while iterating")
    {
        for (auto it = root.begin(); it != root.end();)
        {
            if (it->key() != "b")
                it = root.erase(it);
            else
                ++it;
        }
        CHECK(Dump(root) == "b=B");
    }

    SECTION("erase with a key owned by the erased child")
    {
        CHECK(root.erase(root.find("b")->key()) == 1);
        CHECK(Dump(root) == "a=A c=C");
    }

    SECTION("clear")
    {
        root.clear();
        CHECK(root.empty());
        CHECK(root.is_object());
        CHECK(root.to_text() == "\"Root\"\n{\n}\n");
    }
}

TEST_CASE("KeyValue copy and move carry the key with the value", "[object_model][assignment]")
{
    KeyValue src = MakeABC();
    src.set_key("Source");

    SECTION("copy construction is deep")
    {
        KeyValue copy(src);
        CHECK(copy.key() == "Source");
        CHECK(Dump(copy) == "a=A b=B c=C");
        CHECK(copy == src);
        copy["a"] = "changed";
        CHECK(src["a"].as<std::string>() == "A");
        CHECK(copy != src);
    }

    SECTION("copy assignment")
    {
        KeyValue dst("dst", int32_t(5));
        dst = src;
        CHECK(dst.key() == "Source");
        CHECK(Dump(dst) == "a=A b=B c=C");
        CHECK(src.key() == "Source");
        CHECK(Dump(src) == "a=A b=B c=C");
    }

    SECTION("move construction")
    {
        KeyValue dst(std::move(src));
        CHECK(dst.key() == "Source");
        CHECK(Dump(dst) == "a=A b=B c=C");
    }

    SECTION("move assignment")
    {
        KeyValue dst("dst", int32_t(5));
        dst = std::move(src);
        CHECK(dst.key() == "Source");
        CHECK(Dump(dst) == "a=A b=B c=C");
    }

    SECTION("self copy assignment")
    {
        KeyValue& alias = src;
        src = alias;
        CHECK(src.key() == "Source");
        CHECK(Dump(src) == "a=A b=B c=C");
    }

    SECTION("self move assignment")
    {
        KeyValue& alias = src;
        src = std::move(alias);
        CHECK(src.key() == "Source");
        CHECK(Dump(src) == "a=A b=B c=C");
    }

    SECTION("assign a child to its parent (copy)")
    {
        src = *src.find("b");
        CHECK(src.key() == "b");
        CHECK(src.as<std::string>() == "B");
    }

    SECTION("assign a child to its parent (move)")
    {
        src = std::move(*src.find("b"));
        CHECK(src.key() == "b");
        CHECK(src.as<std::string>() == "B");
    }
}

TEST_CASE("Assigning a value keeps the key", "[object_model][assignment]")
{
    KeyValue root = MakeABC();
    root.add("obj").add("child", "Child");

    SECTION("from a KeyValue copy")
    {
        KeyValue other("other", "Other");
        root["a"] = other;
        CHECK(Dump(root).find("a=Other b=B c=C") == 0);
        CHECK(other.key() == "other");
        CHECK(other.as<std::string>() == "Other");
    }

    SECTION("from a moved KeyValue")
    {
        KeyValue other("other", "Other");
        root["a"] = std::move(other);
        CHECK(Dump(root).find("a=Other b=B c=C") == 0);
    }

    SECTION("from another child")
    {
        root["a"] = root["c"];
        CHECK(Dump(root).find("a=C b=B c=C") == 0);
    }

    SECTION("from a moved child: the source loses its value and leaves the tree")
    {
        root["a"] = std::move(root["c"]);
        CHECK(root["a"].as<std::string>() == "C");
        CHECK(Dump(root) == "a=C b=B obj=?");
        CHECK_FALSE(root.contains("c"));
    }

    SECTION("on a KeyValue itself")
    {
        KeyValue& a = *root.find("a");
        a = *root.find("c");
        CHECK(a.key() == "c");
        a = static_cast<Node const&>(*root.find("b"));
        CHECK(a.key() == "c");
        CHECK(a.as<std::string>() == "B");
        a = "text";
        CHECK(a.key() == "c");
        a = 5;
        CHECK(a.key() == "c");
        CHECK(a.as<int>() == 5);
    }

    SECTION("self assignment")
    {
        Node& a = root["a"];
        Node& alias = root["a"];
        a = alias;
        CHECK(Dump(root).find("a=A b=B c=C") == 0);
        a = std::move(alias);
        CHECK(Dump(root).find("a=A b=B c=C") == 0);
    }

    SECTION("an object receives the value of its own child (copy)")
    {
        root["obj"] = root["obj"]["child"];
        REQUIRE(root.count("obj") == 1);
        CHECK(root["obj"].type() == Type::String);
        CHECK(root["obj"].as<std::string>() == "Child");
    }

    SECTION("an object receives the value of its own child (move)")
    {
        Node& obj = root["obj"];
        obj = std::move(obj["child"]);
        REQUIRE(root.count("obj") == 1);
        CHECK(root["obj"].type() == Type::String);
        CHECK(root["obj"].as<std::string>() == "Child");
    }

    SECTION("an object receives a string owned by its own child")
    {
        root["obj"] = root["obj"]["child"].as<std::string_view>();
        CHECK(root["obj"].as<std::string>() == "Child");
    }

    SECTION("typed values")
    {
        root["b"] = int32_t(42);
        auto second = ++root.begin();
        CHECK(second->key() == "b");
        CHECK(second->as<int32_t>() == 42);
    }
}

static_assert(std::is_nothrow_move_constructible<Node>::value, "moves must be noexcept");
static_assert(std::is_nothrow_move_assignable<Node>::value, "moves must be noexcept");
static_assert(std::is_nothrow_move_constructible<KeyValue>::value, "moves must be noexcept");
static_assert(std::is_nothrow_move_assignable<KeyValue>::value, "moves must be noexcept");
static_assert(std::is_nothrow_default_constructible<Node>::value, "an empty node does not allocate");
static_assert(std::is_nothrow_default_constructible<KeyValue>::value, "an empty node does not allocate");

TEST_CASE("Moves do not allocate", "[object_model][exception_safety]")
{
    KeyValue a = MakeTree();
    const KeyValue expected = a;

    long long total_before = g_total_allocations;
    KeyValue b(std::move(a));
    a = std::move(b);
    b = std::move(a);
    std::swap(a, b);
    Node n(std::move(static_cast<Node&>(a)));
    Node m;
    m = std::move(n);
    static_cast<Node&>(a) = std::move(m);
    CHECK_ALLOCATIONS(g_total_allocations == total_before);
    CHECK(static_cast<Node const&>(a) == static_cast<Node const&>(expected));

    SECTION("an empty Node or KeyValue does not allocate")
    {
        total_before = g_total_allocations;
        {
            Node node;
            KeyValue key_value;
            Node null(nullptr);
            Node number(5);
            Node small_string("short");
            CHECK(node.is_none());
            CHECK(key_value.is_none());
        }
        CHECK_ALLOCATIONS(g_total_allocations == total_before);
    }

    SECTION("lookups do not allocate")
    {
        KeyValue const& tree = expected;
        total_before = g_total_allocations;
        bool found = tree[LongKey].has_value() && tree.contains(LongKey) && tree.find(LongKey) != nullptr
            && tree.count(LongKey) == 3 && tree.at(LongKey).is_string() && !tree["missing"]["deeper"].has_value()
            && tree[LongKey + "2"][LongKey][LongKey].value_or(0.f) == 1.5f && tree[LongKey].as<std::string_view>() == LongValue;
        long long allocations = g_total_allocations - total_before;
        CHECK(found);
        // Only the temporary LongKey + "2" allocates.
        CHECK_ALLOCATIONS(allocations == 1);
    }

    SECTION("a child is one allocation, plus its long strings")
    {
        KeyValue root("root");
        root.add("warmup", 0);
        root.add("warmup", 0);

        total_before = g_total_allocations;
        root.add("k", 5);
        CHECK_ALLOCATIONS(g_total_allocations - total_before <= 2); // the child, maybe the growth of the list
    }
}

TEST_CASE("Constructors and assignments do not leak when an allocation fails", "[object_model][exception_safety]")
{
    const KeyValue tree = MakeTree();

    SECTION("default") { CheckNoLeakOnAllocationFailure([&] { KeyValue o; Node n; }); }
    SECTION("key") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey); }); }
    SECTION("key, string const&") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, LongValue); }); }
    SECTION("key, string&&") { CheckNoLeakOnAllocationFailure([&] { std::string v = LongValue; KeyValue o(LongKey, std::move(v)); }); }
    SECTION("key, const char*") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, LongValue.c_str()); }); }
    SECTION("key, int32_t") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, int32_t(1)); }); }
    SECTION("key, float") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, 1.f); }); }
    SECTION("key, Pointer") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, EasyVDF::Pointer{1}); }); }
    SECTION("key, Color") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, EasyVDF::Color{1, 2, 3, 4}); }); }
    SECTION("key, int64_t") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, int64_t(1)); }); }
    SECTION("key, uint64_t") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, uint64_t(1)); }); }
    SECTION("key, Node const&") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(LongKey, static_cast<Node const&>(tree)); }); }
    SECTION("key, Node&&") { CheckNoLeakOnAllocationFailure([&] { Node tmp(tree); KeyValue o(LongKey, std::move(tmp)); }); }
    SECTION("initializer list")
    {
        CheckNoLeakOnAllocationFailure([&] {
            KeyValue o(LongKey, { {LongKey, LongValue}, {LongKey, 5}, {LongKey, { {LongKey, LongValue} }} });
        });
    }
    SECTION("copy") { CheckNoLeakOnAllocationFailure([&] { KeyValue o(tree); }); }
    SECTION("copy assignment")
    {
        CheckNoLeakOnAllocationFailure([&] {
            KeyValue o(LongKey, LongValue);
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
                if (o.key() != LongKey || o.type() != Type::String || o.as<std::string_view>() != LongValue)
                    FAIL("copy assignment modified the target before throwing");
                throw std::bad_alloc();
            }
        });
    }
    SECTION("value assignment")
    {
        CheckNoLeakOnAllocationFailure([&] {
            KeyValue o(LongKey, LongValue);
            bool threw = false;
            try
            {
                static_cast<Node&>(o) = tree;
            }
            catch (std::bad_alloc const&)
            {
                threw = true;
            }
            if (threw)
            {
                if (o.key() != LongKey || o.type() != Type::String || o.as<std::string_view>() != LongValue)
                    FAIL("value assignment modified the target before throwing");
                throw std::bad_alloc();
            }
        });
    }
    SECTION("assignments on a moved-from object")
    {
        CheckNoLeakOnAllocationFailure([&] {
            KeyValue src(LongKey, LongValue);
            KeyValue o(std::move(src));
            src = LongValue;
            src.set_key(LongKey);
            o = int32_t(1);
        });
    }
    SECTION("building a tree")
    {
        CheckNoLeakOnAllocationFailure([&] {
            KeyValue root(LongKey);
            root.add(LongKey, int32_t(1));
            root[LongKey] = static_cast<Node const&>(tree);
            root[LongValue][LongKey][LongValue] = LongValue;
            root[LongValue + "!"].value_or(0);
            root.insert(root.begin(), tree);
            root.add(LongKey).set_condition(LongValue);
            root.erase(LongKey);
            for (auto& child : root.all(LongValue))
                child = 5;
        });
    }
    SECTION("parsing text")
    {
        const std::string text = tree.to_text();
        CheckNoLeakOnAllocationFailure([&] { EasyVDF::parse(text); });
        CheckNoLeakOnAllocationFailure([&] { std::istringstream is(text); EasyVDF::parse(is); });
        CheckNoLeakOnAllocationFailure([&] { try { EasyVDF::parse(text.substr(0, text.size() - 3)); } catch (EasyVDF::ParseError const&) {} });
    }
    SECTION("parsing binary")
    {
        const std::string binary = tree.to_binary();
        const std::string vbkv = tree.to_binary(EasyVDF::Format::Vbkv);
        CheckNoLeakOnAllocationFailure([&] { EasyVDF::parse(binary); });
        CheckNoLeakOnAllocationFailure([&] { std::istringstream is(vbkv); EasyVDF::parse(is); });
        CheckNoLeakOnAllocationFailure([&] { try { EasyVDF::parse(std::string_view(binary).substr(0, binary.size() - 3)); } catch (EasyVDF::ParseError const&) {} });
    }
    SECTION("writing")
    {
        CheckNoLeakOnAllocationFailure([&] { tree.to_text(); });
        CheckNoLeakOnAllocationFailure([&] { tree.to_binary(EasyVDF::Format::Vbkv); });
    }
}

TEST_CASE("A moved-from object is empty and usable", "[object_model][moved_from]")
{
    bool by_assignment = GENERATE(false, true);
    CAPTURE(by_assignment);

    KeyValue a("a", "A");
    a.set_condition("$WIN32");
    if (by_assignment)
    {
        KeyValue b;
        b = std::move(a);
        CHECK(b.key() == "a");
        CHECK(b.condition() == "$WIN32");
    }
    else
    {
        KeyValue b(std::move(a));
        CHECK(b.key() == "a");
        CHECK(b.condition() == "$WIN32");
    }

    // Observers
    CHECK(a.key().empty());
    CHECK(a.condition().empty());
    CHECK(a.type() == Type::None);
    CHECK(a.is_none());
    CHECK_FALSE(a.has_value());
    CHECK_FALSE(static_cast<bool>(a));
    CHECK(a.empty());
    CHECK(a.begin() == a.end());
    CHECK_THROWS_AS(a.as<std::string>(), EasyVDF::TypeError);
    CHECK_THROWS_AS(a.as<int32_t>(), EasyVDF::TypeError);
    CHECK_THROWS_AS(a.at("key"), EasyVDF::KeyError);
    CHECK(std::as_const(a)["key"].is_none());
    CHECK_THROWS_AS(a.to_text(), EasyVDF::WriteError);
    CHECK_THROWS_AS(a.to_binary(), EasyVDF::WriteError);
    CHECK(a == KeyValue());

    // Copies of it are empty too.
    KeyValue copy(a);
    CHECK(copy.key().empty());
    CHECK(copy.type() == Type::None);
    KeyValue assigned("x", int32_t(1));
    assigned = a;
    CHECK(assigned.key().empty());
    CHECK(assigned.type() == Type::None);

    SECTION("assign a value") { a = int32_t(3); CHECK(a.as<int32_t>() == 3); CHECK(a.key().empty()); }
    SECTION("assign a string") { a = std::string("S"); CHECK(a.as<std::string>() == "S"); }
    SECTION("assign nullptr") { a = nullptr; CHECK(a.is_none()); }
    SECTION("rename") { a.set_key("renamed"); CHECK(a.key() == "renamed"); CHECK(a.is_none()); }
    SECTION("assign an object")
    {
        a = MakeABC();
        CHECK(a.key() == "Root");
        CHECK(Dump(a) == "a=A b=B c=C");
    }
    SECTION("use it as an object")
    {
        a["k"] = "v";
        a.add("k2", 2);
        CHECK(a.is_object());
        CHECK(Dump(a) == "k=v k2=?");
    }
    SECTION("move from it again")
    {
        KeyValue b(std::move(a));
        CHECK(b.is_none());
        a = std::move(b);
        CHECK(a.is_none());
    }
    // a is destroyed at the end of each section.
}

TEST_CASE("An object holding a moved-from child stays usable", "[object_model][moved_from]")
{
    KeyValue root = MakeABC();
    KeyValue taken(std::move(*root.find("b")));
    CHECK(taken.key() == "b");
    CHECK(taken.as<std::string>() == "B");

    // A child without value is not part of its parent anymore.
    CHECK(root.size() == 2);
    CHECK(Dump(root) == "a=A c=C");
    CHECK(root.count("a") == 1);
    CHECK_FALSE(root.contains("b"));
    CHECK_FALSE(root.contains(""));
    CHECK(root.to_text() == "\"Root\"\n{\n\t\"a\"\t\t\"A\"\n\t\"c\"\t\t\"C\"\n}\n");

    KeyValue copy(root);
    CHECK(copy.size() == 2);
    CHECK(copy == root);

    root.insert(++root.begin(), std::move(taken));
    CHECK(Dump(root) == "a=A b=B c=C");
}

TEST_CASE("A node without value can't be written", "[object_model][serialize]")
{
    SECTION("a document without value throws")
    {
        KeyValue none;
        none.set_key("none");
        CHECK_THROWS_AS(none.to_text(), EasyVDF::WriteError);
        CHECK_THROWS_AS(none.to_binary(EasyVDF::Format::Binary), EasyVDF::WriteError);
        CHECK_THROWS_AS(none.to_binary(EasyVDF::Format::Vbkv), EasyVDF::WriteError);

        KeyValue reset = MakeABC();
        reset = nullptr;
        CHECK_THROWS_AS(reset.to_text(), EasyVDF::WriteError);
        CHECK_THROWS_AS(reset.to_binary(), EasyVDF::WriteError);

        KeyValue moved = MakeABC();
        KeyValue taken(std::move(moved));
        CHECK_THROWS_AS(moved.to_text(), EasyVDF::WriteError);
    }

    SECTION("a document that is not an object throws")
    {
        CHECK_THROWS_AS(KeyValue("k", "v").to_text(), EasyVDF::WriteError);
        CHECK_THROWS_AS(KeyValue("k", 5).to_binary(), EasyVDF::WriteError);
        CHECK_THROWS_WITH(KeyValue("k", 5).to_binary(EasyVDF::Format::Vbkv), Catch::Contains("Int32") && Catch::Contains("object"));
    }

    // VDF has no representation for a key without a value: such a child is
    // never part of its parent, so nothing unreadable can be written.
    KeyValue root = MakeABC();
    std::string expected_dump = "a=A b=B c=C";

    SECTION("child without value added")
    {
        root.add("none", Node());
        root.add("null", nullptr);
        root.insert(root.begin(), KeyValue());
    }
    SECTION("child reset")
    {
        root["b"] = nullptr;
        expected_dump = "a=A c=C";
    }
    SECTION("child reset()")
    {
        root["b"].reset();
        expected_dump = "a=A c=C";
    }
    SECTION("moved-from child")
    {
        KeyValue taken(std::move(*root.find("b")));
        expected_dump = "a=A c=C";
    }
    SECTION("nested child")
    {
        KeyValue& inner = root.add("inner");
        inner.add("none", int32_t(0));
        inner["none"] = nullptr;
        expected_dump = "a=A b=B c=C inner=?";
        CHECK(inner.is_object());
        CHECK(inner.empty());
    }

    CHECK(Dump(root) == expected_dump);
    for (EasyVDF::Format format : {EasyVDF::Format::Text, EasyVDF::Format::Binary, EasyVDF::Format::Vbkv})
    {
        KeyValue parsed = EasyVDF::parse(root.dump(format));
        CHECK(Dump(parsed) == expected_dump);
        CHECK(parsed == root);
    }
}

TEST_CASE("Serializing nodes with values still works", "[object_model][serialize]")
{
    SECTION("empty object")
    {
        KeyValue root("Root");
        CHECK(root.to_text() == "\"Root\"\n{\n}\n");
        CHECK(root.to_binary() == std::string("\x00Root\x00\x08", 7));
    }

    SECTION("text and binary")
    {
        KeyValue root = MakeABC();

        CHECK(root.to_text() ==
            "\"Root\"\n"
            "{\n"
            "\t\"a\"\t\t\"A\"\n"
            "\t\"b\"\t\t\"B\"\n"
            "\t\"c\"\t\t\"C\"\n"
            "}\n");

        KeyValue parsed = EasyVDF::parse(root.to_binary(EasyVDF::Format::Vbkv));
        CHECK(Dump(parsed) == "a=A b=B c=C");
    }
}

namespace adl_check {

// This namespace has its own operator== and operator<, which hide any
// operator declared at global scope: Pointer/Color operators must be
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

TEST_CASE("Pointer and Color comparison operators", "[object_model][operators]")
{
    CHECK(adl_check::AllComparisons(EasyVDF::Pointer{1}, EasyVDF::Pointer{2}));
    CHECK(adl_check::AllComparisons(EasyVDF::Color{0, 0, 0, 1}, EasyVDF::Color{0, 0, 0, 2}));
    CHECK(adl_check::AllComparisons(EasyVDF::Color{0, 9, 9, 9}, EasyVDF::Color{1, 0, 0, 0}));

    // Catch's decomposition also lives in its own namespace.
    Node o(EasyVDF::Pointer{0x90807060});
    CHECK(o.as<EasyVDF::Pointer>() == EasyVDF::Pointer{0x90807060});
    CHECK(o.as<EasyVDF::Pointer>() != EasyVDF::Pointer{0});
    o = EasyVDF::Color{0x66, 0x77, 0x88, 0x99};
    CHECK(o.as<EasyVDF::Color>() == EasyVDF::Color{0x66, 0x77, 0x88, 0x99});
    CHECK(o.as<EasyVDF::Color>() > EasyVDF::Color{});
}

TEST_CASE("Conversion errors name the types", "[object_model][operators]")
{
    Node o(EasyVDF::Pointer{1});
    CHECK_THROWS_WITH(o.as<EasyVDF::Color>(), Catch::Contains("Pointer value to a Color"));
    CHECK_THROWS_WITH(o.as<float>(), Catch::Contains("Pointer value to a floating point number"));
    CHECK_THROWS_WITH(Node("x").as<int>(), Catch::Contains("String value to an integer"));
    CHECK_THROWS_WITH(Node(1).as<std::string_view>(), Catch::Contains("Int32 value to a string"));
    CHECK_THROWS_WITH(Node::object().as<bool>(), Catch::Contains("Object value to a bool"));
    CHECK_THROWS_WITH(Node().as<EasyVDF::Pointer>(), Catch::Contains("missing value") && Catch::Contains("a Pointer"));
    CHECK_THROWS_WITH(Node(1).add("k", 1), Catch::Contains("Int32 value has no children"));
}

TEST_CASE("Const objects are fully readable", "[object_model][operators]")
{
    KeyValue root("Root");
    root.add("none", int32_t(0));
    root["none"] = nullptr;
    root.add("int", int32_t(1));
    root.add("sub").add("s", "text");

    KeyValue const& croot = root;
    CHECK(croot.has_value());
    CHECK(croot["none"].is_none());
    CHECK_FALSE(croot["none"]);
    CHECK(croot["int"]);
    CHECK(croot["int"].as<int>() == 1);
    CHECK(croot["sub"]["s"].as<std::string>() == "text");
    CHECK(croot["sub"]["s"].as<std::string_view>() == "text");
    CHECK(croot["sub"]["s"]["deeper"]["again"].is_none());
    CHECK(croot["int"]["deeper"].value_or(7) == 7);
    CHECK(croot.at("sub").at("s").is_string());
    CHECK(croot.find("int")->key() == "int");
    CHECK(croot.size() == 2);
    CHECK(Dump(croot) == "int=? sub=?");
    CHECK(croot.to_text() == root.to_text());

    Node const& child = croot["sub"];
    CHECK(child.size() == 1);
    for (auto const& [key, value] : child)
    {
        CHECK(key == "s");
        CHECK(value.as<std::string>() == "text");
    }
}

TEST_CASE("Assigning a std::string&& moves it", "[object_model][operators]")
{
    KeyValue root("Root");
    root.add("s", int32_t(0));
    Node& s = root["s"];
    std::string value = LongValue;

    long long total_before = g_total_allocations;
    s = value;
    long long copy_allocations = g_total_allocations - total_before;
    CHECK(s.as<std::string_view>() == LongValue);
    CHECK_ALLOCATIONS(copy_allocations == 1);

    // Moving does not allocate a copy of the string buffer.
    total_before = g_total_allocations;
    s = std::move(value);
    long long move_allocations = g_total_allocations - total_before;
    CHECK_ALLOCATIONS(move_allocations == 0);
    CHECK(move_allocations < copy_allocations);
    CHECK(root["s"].as<std::string_view>() == LongValue);
}
