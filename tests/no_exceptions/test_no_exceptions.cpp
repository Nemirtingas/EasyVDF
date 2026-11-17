// EasyVDF compiled with exceptions disabled (-fno-exceptions, /EHs-c- /D_HAS_EXCEPTIONS=0).
// Catch2 needs exceptions: this test has its own tiny checks.
//
// Usage: test_no_exceptions                 runs the checks, from tests/data
//        test_no_exceptions value-on-error  Result::value() on an error: must abort
//        test_no_exceptions scalar-object   a scalar used as an object: must abort

#include <EasyVDF.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _MSC_VER
    #include <stdlib.h>
#endif

// EASYVDF_NO_EXCEPTIONS is not given on the command line: the header detects it.
#ifndef EASYVDF_NO_EXCEPTIONS
    #error "EasyVDF did not detect that exceptions are disabled"
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    #error "this test must be compiled with exceptions disabled"
#endif

namespace {

int g_failures = 0;

#define EXPECT(condition)                                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(condition))                                                              \
        {                                                                              \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (false)

/////////////////////////////////////////////////////////////////////
// The throwing API is not declared. The free functions are found by ADL on
// ReadOptions, so that a missing one is a substitution failure, not an error.
/////////////////////////////////////////////////////////////////////

template<typename N, typename = void> struct HasAs : std::false_type {};
template<typename N> struct HasAs<N, std::void_t<decltype(std::declval<N const&>().template as<int>())>> : std::true_type {};

template<typename N, typename = void> struct HasAt : std::false_type {};
template<typename N> struct HasAt<N, std::void_t<decltype(std::declval<N&>().at("k"))>> : std::true_type {};

template<typename K, typename = void> struct HasDump : std::false_type {};
template<typename K> struct HasDump<K, std::void_t<decltype(std::declval<K const&>().dump())>> : std::true_type {};

template<typename K, typename = void> struct HasToText : std::false_type {};
template<typename K> struct HasToText<K, std::void_t<decltype(std::declval<K const&>().to_text())>> : std::true_type {};

template<typename K, typename = void> struct HasToBinary : std::false_type {};
template<typename K> struct HasToBinary<K, std::void_t<decltype(std::declval<K const&>().to_binary())>> : std::true_type {};

template<typename K, typename = void> struct HasWrite : std::false_type {};
template<typename K> struct HasWrite<K, std::void_t<decltype(std::declval<K const&>().write(std::declval<std::ostream&>()))>> : std::true_type {};

template<typename K, typename = void> struct HasSave : std::false_type {};
template<typename K> struct HasSave<K, std::void_t<decltype(std::declval<K const&>().save("f"))>> : std::true_type {};

template<typename O, typename = void> struct HasParse : std::false_type {};
template<typename O> struct HasParse<O, std::void_t<decltype(parse(std::string_view(), std::declval<O const&>()))>> : std::true_type {};

template<typename O, typename = void> struct HasParseAll : std::false_type {};
template<typename O> struct HasParseAll<O, std::void_t<decltype(parse_all(std::string_view(), std::declval<O const&>()))>> : std::true_type {};

template<typename O, typename = void> struct HasLoad : std::false_type {};
template<typename O> struct HasLoad<O, std::void_t<decltype(load(std::filesystem::path(), std::declval<O const&>()))>> : std::true_type {};

template<typename O, typename = void> struct HasLoadAll : std::false_type {};
template<typename O> struct HasLoadAll<O, std::void_t<decltype(load_all(std::filesystem::path(), std::declval<O const&>()))>> : std::true_type {};

template<typename O, typename = void> struct HasTryParse : std::false_type {};
template<typename O> struct HasTryParse<O, std::void_t<decltype(try_parse(std::string_view(), std::declval<O const&>()))>> : std::true_type {};

static_assert(!HasAs<EasyVDF::Node>::value, "as<T>() must not be declared without exceptions");
static_assert(!HasAt<EasyVDF::Node>::value, "at() must not be declared without exceptions");
static_assert(!HasDump<EasyVDF::KeyValue>::value, "dump() must not be declared without exceptions");
static_assert(!HasToText<EasyVDF::KeyValue>::value, "to_text() must not be declared without exceptions");
static_assert(!HasToBinary<EasyVDF::KeyValue>::value, "to_binary() must not be declared without exceptions");
static_assert(!HasWrite<EasyVDF::KeyValue>::value, "write() must not be declared without exceptions");
static_assert(!HasSave<EasyVDF::KeyValue>::value, "save() must not be declared without exceptions");
static_assert(!HasParse<EasyVDF::ReadOptions>::value, "parse() must not be declared without exceptions");
static_assert(!HasParseAll<EasyVDF::ReadOptions>::value, "parse_all() must not be declared without exceptions");
static_assert(!HasLoad<EasyVDF::ReadOptions>::value, "load() must not be declared without exceptions");
static_assert(!HasLoadAll<EasyVDF::ReadOptions>::value, "load_all() must not be declared without exceptions");
// The detection works: the non-throwing functions are found.
static_assert(HasTryParse<EasyVDF::ReadOptions>::value, "try_parse() must be declared");

using EasyVDF::ErrorCode;
using EasyVDF::ErrorInfo;
using EasyVDF::Format;
using EasyVDF::KeyValue;
using EasyVDF::Node;
using EasyVDF::Result;

std::string Where(ErrorInfo const& error)
{
    return std::string(to_string(error.code)) + " " + std::to_string(error.line) + ":" + std::to_string(error.column);
}

void TestParse()
{
    Result<KeyValue> doc = EasyVDF::try_parse("\"AppState\"\n{\n\t\"appid\"\t\"440\"\n\t\"UserConfig\" { \"language\" \"english\" }\n}\n");
    EXPECT(doc.has_value());
    if (!doc)
        return;

    EXPECT(doc->key() == "AppState");
    EXPECT((*doc)["appid"].value_or(0) == 440);
    EXPECT((*doc)["UserConfig"]["language"].value_or("") == "english");
    EXPECT((*doc)["missing"]["key"].value_or(7) == 7);
    EXPECT((*doc)["appid"].try_as<uint32_t>() == 440u);

    // The errors and their positions.
    EXPECT(Where(EasyVDF::try_parse("").error()) == "EmptyInput 0:0");
    EXPECT(Where(EasyVDF::try_parse("\"root\"\n{\n\t\"k\"\t\"v\"\n").error()) == "UnexpectedEof 3:0");
    EXPECT(Where(EasyVDF::try_parse("\"root\"\n{\n\t\"k\"\n}\n").error()) == "Syntax 4:1");
    EXPECT(Where(EasyVDF::try_parse("\"root\" { } {").error()) == "TrailingData 1:12");
    EXPECT(EasyVDF::try_parse("\"root\"\n{\n\t\"k\"\n}\n").error().message
        == "Expected a value or an object start ('{') after key \"k\" at line 4, column 1");

    // Streams, by small blocks.
    EasyVDF::ReadOptions options;
    options.chunk_size = 3;
    std::istringstream is("a { b { c d } }");
    Result<KeyValue> from_stream = EasyVDF::try_parse(is, options);
    EXPECT(from_stream && (*from_stream)["b"]["c"].value_or("") == "d");

    Result<std::vector<KeyValue>> roots = EasyVDF::try_parse_all("a { }\nb { }\n");
    EXPECT(roots && roots->size() == 2);
}

void TestBinary()
{
    KeyValue doc("Root", { {"int", int32_t(-5)}, {"sub", { {"u64", uint64_t(1) << 40} }} });

    Result<std::string> vbkv = doc.try_to_binary(Format::Vbkv);
    EXPECT(vbkv.has_value());
    if (!vbkv)
        return;

    Result<KeyValue> back = EasyVDF::try_parse(*vbkv);
    EXPECT(back && *back == doc);

    std::string corrupted = *vbkv;
    corrupted[4] ^= 1;
    EXPECT(EasyVDF::try_parse(corrupted).error().code == ErrorCode::ChecksumMismatch);
    EXPECT(EasyVDF::try_parse(vbkv->substr(0, vbkv->size() / 2)).error().code == ErrorCode::UnexpectedEof);

    std::string nested;
    for (int i = 0; i < 200; ++i)
        nested += std::string("\x00o\x00", 3);
    EXPECT(EasyVDF::try_parse(nested).error().code == ErrorCode::DepthExceeded);
}

void TestFiles()
{
    Result<KeyValue> binary = EasyVDF::try_load("binary.vdf");
    EXPECT(binary && binary->key() == "RootObject");

    Result<KeyValue> missing = EasyVDF::try_load("no_such_file.vdf");
    EXPECT(!missing && missing.error() == ErrorInfo(ErrorCode::Io, "EasyVDF: can't open \"no_such_file.vdf\" for reading"));
    EXPECT(EasyVDF::try_load_all("no_such_file.vdf").error().code == ErrorCode::Io);

    KeyValue doc("Root", { {"k", "v"} });
    const char* path = "test_no_exceptions.tmp";
    EXPECT(doc.try_save(path, Format::Binary).has_value());
    Result<KeyValue> reloaded = EasyVDF::try_load(path);
    EXPECT(reloaded && *reloaded == doc);
    std::remove(path);

    EXPECT(doc.try_save("no_such_directory/file.vdf").error().code == ErrorCode::Io);
    EXPECT(KeyValue().try_save(path).error().code == ErrorCode::Write);
}

void TestWrite()
{
    KeyValue doc("Root", { {"k", "v"} });
    Result<std::string> text = doc.try_to_text();
    EXPECT(text && *text == "\"Root\"\n{\n\t\"k\"\t\t\"v\"\n}\n");

    std::ostringstream os;
    EXPECT(doc.try_write(os).has_value());
    EXPECT(os.str() == *text);
    std::ostringstream streamed;
    streamed << doc;
    EXPECT(streamed.str() == *text);

    KeyValue with_null("Root");
    with_null.add("k", std::string("a\0b", 3));
    EXPECT(with_null.try_to_binary().error().code == ErrorCode::Write);
    EXPECT(KeyValue("k", 5).try_dump().error().code == ErrorCode::Write);
}

void TestResult()
{
    KeyValue doc("Root", { {"appid", 440}, {"sub", { {"k", "v"} }} });

    Result<Node&> appid = doc.try_at("appid");
    EXPECT(appid && appid->value_or(0) == 440);
    *appid = 441;
    EXPECT(doc["appid"].value_or(0) == 441);
    EXPECT(&appid.value() == &doc["appid"]);

    Result<Node&> missing = doc.try_at("missing");
    EXPECT(!missing && missing.error().code == ErrorCode::Key);

    Result<std::string> k = doc.try_at("sub")
        .and_then([](Node& sub) { return sub.try_at("k"); })
        .map([](Node const& n) { return n.value_or(std::string()); });
    EXPECT(k && *k == "v");

    Result<int> fallback = doc.try_at("missing")
        .map([](Node const& n) { return n.value_or(0); })
        .or_else([](ErrorInfo const& error) -> Result<int> {
            if (error.code == ErrorCode::Key)
                return -1;
            return error;
        });
    EXPECT(fallback.value() == -1);

    Result<int> with_context = Result<int>(ErrorInfo(ErrorCode::Io, "m")).map_error([](ErrorInfo e) { e.message += "!"; return e; });
    EXPECT(with_context.error().message == "m!");
    EXPECT(Result<int>(ErrorInfo(ErrorCode::Io, "m")).value_or(3) == 3);

    // Writing through operator[] on objects and missing keys is fine.
    doc["new"]["key"] = 1;
    EXPECT(doc["new"]["key"].value_or(0) == 1);
    doc.add("dup", 1);
    doc.add("dup", 2);
    EXPECT(doc.count("dup") == 2);
}

// Aborting is the expected outcome of the two modes below: the handler ends the
// process with a success code after a marker that the CTest test looks for.
void OnAbort(int)
{
    static const char message[] = "SIGABRT handler reached\n";
    std::fwrite(message, 1, sizeof(message) - 1, stderr);
    std::_Exit(EXIT_SUCCESS);
}

void ExpectAbort()
{
#ifdef _MSC_VER
    // No "abort() has been called" message box or report in debug builds.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::signal(SIGABRT, OnAbort);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "value-on-error") == 0)
    {
        ExpectAbort();
        KeyValue doc("Root");
        Node& node = doc.try_at("missing").value();
        std::fprintf(stderr, "value() did not abort\n");
        return node.has_value() ? 2 : 1;
    }

    if (argc > 1 && std::strcmp(argv[1], "scalar-object") == 0)
    {
        ExpectAbort();
        KeyValue doc("Root", { {"appid", 440} });
        doc["appid"]["x"] = 1;
        std::fprintf(stderr, "operator[] on a scalar did not abort\n");
        return 1;
    }

    TestParse();
    TestBinary();
    TestFiles();
    TestWrite();
    TestResult();

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All checks passed\n");
    return 0;
}
