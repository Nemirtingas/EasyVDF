// Checks VDF files and prints a summary of each one, or its error the way a compiler
// prints a diagnostic. Built with exceptions disabled (-fno-exceptions, /EHs-c-):
// errors are values (EasyVDF::Result), nothing is ever thrown.
//
// Usage: no_exceptions <file>...
//   no_exceptions appmanifest_440.acf broken.vdf
//   appmanifest_440.acf: "AppState", 12 keys, name "Team Fortress 2", language english, appid 440
//   broken.vdf:7:5: error [Syntax]: Expected a key or an object end ('}') at line 7, column 5
//
// Exit code: 0 when every file is valid, 1 otherwise.

#include <EasyVDF.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

// "file:line:column: error [Code]: message", without the position when there is none.
void PrintError(std::string const& path, EasyVDF::ErrorInfo const& error)
{
    std::cerr << path;
    if (error.line != 0)
    {
        std::cerr << ':' << error.line;
        if (error.column != 0)
            std::cerr << ':' << error.column;
    }
    std::cerr << ": error [" << error.code << "]: " << error.message << '\n';
}

void PrintSummary(std::string const& path, EasyVDF::KeyValue const& root)
{
    std::cout << path << ": \"" << root.key() << "\", " << root.size() << " keys";

    // try_at() returns a Result: a missing key is a value to test, not an exception.
    // map() turns the node into what we want when it is there.
    EasyVDF::Result<std::string> name = root.try_at("name").map([](EasyVDF::Node const& n) { return n.value_or(std::string()); });
    if (name)
        std::cout << ", name \"" << *name << '"';

    // and_then() chains lookups that can each fail; the first error is kept.
    EasyVDF::Result<std::string> language = root.try_at("UserConfig")
        .and_then([](EasyVDF::Node const& config) { return config.try_at("language"); })
        .map([](EasyVDF::Node const& n) { return n.value_or(std::string()); });
    if (language)
        std::cout << ", language " << *language;

    // operator[] and value_or() never fail on a const node: the simplest way to get a default.
    std::cout << ", appid " << root["appid"].value_or(0) << '\n';
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <file>...\n";
        return 2;
    }

    int status = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string path = argv[i];

        // load_all() accepts the files that hold several root objects (signed Steam files).
        EasyVDF::Result<std::vector<EasyVDF::KeyValue>> roots = EasyVDF::try_load_all(path);
        if (!roots)
        {
            PrintError(path, roots.error());
            status = 1;
            continue;
        }

        for (EasyVDF::KeyValue const& root : *roots)
            PrintSummary(path, root);
    }

    // Writing reports its errors the same way: a document must be an object.
    EasyVDF::KeyValue not_a_document("key", 42);
    if (EasyVDF::Result<std::string> text = not_a_document.try_to_text(); !text)
        std::cout << "as expected, " << text.error() << '\n';

    return status;
}
