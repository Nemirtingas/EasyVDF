// EasyVDF compiled with exceptions disabled (-fno-exceptions, /EHs-c- /D_HAS_EXCEPTIONS=0):
// the library does not use them. Catch2 needs exceptions: this test has its own tiny checks.
//
// Usage: test_no_exceptions                 runs the checks, from tests/data
//        test_no_exceptions value-on-error  Result::value() on an error: must abort
//        test_no_exceptions scalar-object   a scalar used as an object: must abort
//        test_no_exceptions error-of-value  Result::error() on a value: must abort

#include <EasyVDF.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifdef _MSC_VER
    #include <stdlib.h>
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
    Result<KeyValue> doc = EasyVDF::parse("\"AppState\"\n{\n\t\"appid\"\t\"440\"\n\t\"UserConfig\" { \"language\" \"english\" }\n}\n");
    EXPECT(doc.has_value());
    if (!doc)
        return;

    EXPECT(doc->key() == "AppState");
    EXPECT((*doc)["appid"].value_or(0) == 440);
    EXPECT((*doc)["UserConfig"]["language"].value_or("") == "english");
    EXPECT((*doc)["missing"]["key"].value_or(7) == 7);
    EXPECT((*doc)["appid"].try_as<uint32_t>() == 440u);
    EXPECT((*doc)["appid"].as<uint32_t>() == 440u);
    EXPECT((*doc)["UserConfig"].as<int>().error().code == ErrorCode::Type);

    // The errors and their positions.
    EXPECT(Where(EasyVDF::parse("").error()) == "EmptyInput 0:0");
    EXPECT(Where(EasyVDF::parse("\"root\"\n{\n\t\"k\"\t\"v\"\n").error()) == "UnexpectedEof 3:0");
    EXPECT(Where(EasyVDF::parse("\"root\"\n{\n\t\"k\"\n}\n").error()) == "Syntax 4:1");
    EXPECT(Where(EasyVDF::parse("\"root\" { } {").error()) == "TrailingData 1:12");
    EXPECT(EasyVDF::parse("\"root\"\n{\n\t\"k\"\n}\n").error().message
        == "Expected a value or an object start ('{') after key \"k\" at line 4, column 1");

    // Streams, by small blocks.
    EasyVDF::ReadOptions options;
    options.chunk_size = 3;
    std::istringstream is("a { b { c d } }");
    Result<KeyValue> from_stream = EasyVDF::parse(is, options);
    EXPECT(from_stream && (*from_stream)["b"]["c"].value_or("") == "d");

    Result<std::vector<KeyValue>> roots = EasyVDF::parse_all("a { }\nb { }\n");
    EXPECT(roots && roots->size() == 2);
}

void TestBinary()
{
    KeyValue doc("Root", { {"int", int32_t(-5)}, {"sub", { {"u64", uint64_t(1) << 40} }} });

    Result<std::string> vbkv = doc.to_binary(Format::Vbkv);
    EXPECT(vbkv.has_value());
    if (!vbkv)
        return;

    Result<KeyValue> back = EasyVDF::parse(*vbkv);
    EXPECT(back && *back == doc);

    std::string corrupted = *vbkv;
    corrupted[4] ^= 1;
    EXPECT(EasyVDF::parse(corrupted).error().code == ErrorCode::ChecksumMismatch);
    EXPECT(EasyVDF::parse(vbkv->substr(0, vbkv->size() / 2)).error().code == ErrorCode::UnexpectedEof);

    std::string nested;
    for (int i = 0; i < 200; ++i)
        nested += std::string("\x00o\x00", 3);
    EXPECT(EasyVDF::parse(nested).error().code == ErrorCode::DepthExceeded);
}

void TestFiles()
{
    Result<KeyValue> binary = EasyVDF::load("binary.vdf");
    EXPECT(binary && binary->key() == "RootObject");

    Result<KeyValue> missing = EasyVDF::load("no_such_file.vdf");
    EXPECT(!missing && missing.error() == ErrorInfo(ErrorCode::Io, "EasyVDF: can't open \"no_such_file.vdf\" for reading"));
    EXPECT(EasyVDF::load_all("no_such_file.vdf").error().code == ErrorCode::Io);

    KeyValue doc("Root", { {"k", "v"} });
    const char* path = "test_no_exceptions.tmp";
    EXPECT(doc.save(path, Format::Binary).has_value());
    Result<KeyValue> reloaded = EasyVDF::load(path);
    EXPECT(reloaded && *reloaded == doc);
    std::remove(path);

    EXPECT(doc.save("no_such_directory/file.vdf").error().code == ErrorCode::Io);
    EXPECT(KeyValue().save(path).error().code == ErrorCode::Write);
}

void TestWrite()
{
    KeyValue doc("Root", { {"k", "v"} });
    Result<std::string> text = doc.to_text();
    EXPECT(text && *text == "\"Root\"\n{\n\t\"k\"\t\t\"v\"\n}\n");

    std::ostringstream os;
    EXPECT(doc.write(os).has_value());
    EXPECT(os.str() == *text);
    std::ostringstream streamed;
    streamed << doc;
    EXPECT(streamed.str() == *text);

    KeyValue with_null("Root");
    with_null.add("k", std::string("a\0b", 3));
    EXPECT(with_null.to_binary().error().code == ErrorCode::Write);
    EXPECT(KeyValue("k", 5).dump().error().code == ErrorCode::Write);
}

void TestResult()
{
    KeyValue doc("Root", { {"appid", 440}, {"sub", { {"k", "v"} }} });

    Result<Node&> appid = doc.at("appid");
    EXPECT(appid && appid->value_or(0) == 440);
    *appid = 441;
    EXPECT(doc["appid"].value_or(0) == 441);
    EXPECT(&appid.value() == &doc["appid"]);

    Result<Node&> missing = doc.at("missing");
    EXPECT(!missing && missing.error().code == ErrorCode::Key);

    Result<std::string> k = doc.at("sub")
        .and_then([](Node& sub) { return sub.at("k"); })
        .map([](Node const& n) { return n.value_or(std::string()); });
    EXPECT(k && *k == "v");

    Result<int> fallback = doc.at("missing")
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
        Node& node = doc.at("missing").value();
        std::fprintf(stderr, "value() did not abort\n");
        return node.has_value() ? 2 : 1;
    }

    if (argc > 1 && std::strcmp(argv[1], "error-of-value") == 0)
    {
        ExpectAbort();
        Result<int> value = 1;
        std::fprintf(stderr, "error() did not abort: %s\n", value.error().message.c_str());
        return 1;
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
