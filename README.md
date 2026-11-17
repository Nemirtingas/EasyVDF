# EasyVDF

A single-header C++17 library to read and write Valve KeyValues (VDF) files:

- **text** files: `appmanifest_*.acf`, `libraryfolders.vdf`, `loginusers.vdf`, `config.vdf`, ...
- **binary** files: `shortcuts.vdf`, `appinfo` sections, ... (objects end with `0x08`)
- **VBKV** binary files: `VBKV` magic + CRC32 header (objects end with `0x0B`)

No dependency, one header: [`EasyVDF.h`](EasyVDF.h).

```cpp
#include <EasyVDF.h>

auto manifest = EasyVDF::load("steamapps/appmanifest_440.acf");

std::string name = manifest["name"].as<std::string>();
int         id   = manifest["appid"].as<int>();
std::string lang = manifest["UserConfig"]["language"].value_or("english");

manifest["UserConfig"]["language"] = "french";
manifest.save("steamapps/appmanifest_440.acf");
```

## Installation

Copy `EasyVDF.h` into your project, or use CMake:

```cmake
# As a subdirectory...
add_subdirectory(third_party/EasyVDF)

# ...or with FetchContent
include(FetchContent)
FetchContent_Declare(EasyVDF
    GIT_REPOSITORY https://github.com/Nemirtingas/EasyVDF.git
    GIT_TAG        main)
FetchContent_MakeAvailable(EasyVDF)

target_link_libraries(my_app PRIVATE EasyVDF::EasyVDF)
```

`EasyVDF::EasyVDF` is a header-only target: it adds the include path and requires C++17.
The tests are only built when EasyVDF is the top-level project (`-DEASYVDF_BUILD_TESTS=ON` to force them).

## The model in one minute

| Type | What it is |
|---|---|
| `EasyVDF::Node` | A value: nothing (`Type::None`), an object (ordered key/value children) or a scalar. |
| `EasyVDF::KeyValue` | A `Node` plus its key. It is the element type of an object **and** the type of a whole document (root key + root object). |

```
"AppState"                  <- KeyValue  (key() == "AppState", an object)
{
    "appid"   "440"         <- KeyValue  (key() == "appid", a String)
    "UserConfig"            <- KeyValue  (an object)
    {
        "language" "english"
    }
}
```

Text files only contain objects and strings; `as<T>()` converts on demand.
Binary files also hold `Int32`, `Float`, `UInt64`, `Int64`, `Color` and `Pointer` values.

## Reading

```cpp
// A file, text or binary: the format is detected.
EasyVDF::KeyValue doc = EasyVDF::load("libraryfolders.vdf");

// Memory, or any std::istream (pipes and std::cin work: the stream is never seeked).
EasyVDF::KeyValue a = EasyVDF::parse(R"("root" { "key" "value" })");
EasyVDF::KeyValue b = EasyVDF::parse(std::cin);
```

A document is one root key and its object. Some Steam files hold several of them, like the signed
`installscript.vdf` whose `"InstallScript"` object is followed by a `"kvsignatures"` one: `load()` refuses
them (it would have to drop data), `load_all()` / `parse_all()` return every root object.

```cpp
std::vector<EasyVDF::KeyValue> roots = EasyVDF::load_all("installscript.vdf");
for (auto& root : roots)
    std::cout << root.key() << '\n';          // InstallScript, kvsignatures
```

### Getting values

```cpp
auto manifest = EasyVDF::load("appmanifest_440.acf");

manifest.key();                                   // "AppState"

// as<T>() converts, and throws EasyVDF::TypeError when it can't.
auto appid = manifest["appid"].as<uint32_t>();    // "440" -> 440
auto name  = manifest["name"].as<std::string>();
auto size  = manifest["SizeOnDisk"].as<uint64_t>();

// value_or() never throws: the default is used when the key is missing or not convertible.
bool        updating = manifest["AutoUpdateBehavior"].value_or(0) != 0;
std::string language = manifest["UserConfig"]["language"].value_or("english");
uint64_t    build    = manifest["buildid"].value_or<uint64_t>(0);

// try_as<T>() gives a std::optional.
if (auto owner = manifest["LastOwner"].try_as<uint64_t>())
    std::cout << "owner: " << *owner << '\n';

// Missing keys chain safely, and are easy to test.
if (manifest["InstalledDepots"]["441"])           // or .has_value()
    std::cout << "depot 441 is installed\n";
if (auto* depot = manifest["InstalledDepots"].find("441"))
    std::cout << depot->key() << ": " << (*depot)["manifest"].as<std::string>() << '\n';

manifest.at("appid");                             // throws EasyVDF::KeyError if missing
manifest.try_at("appid");                         // or a Result, see "Error handling without exceptions"
manifest.contains("appid");                       // true
```

`as<T>()`, `try_as<T>()` and `value_or()` accept `std::string`, `std::string_view` (String values only, no copy),
`bool`, every integer and floating point type, `EasyVDF::Color` and `EasyVDF::Pointer`.
Integers are range checked (`"300"` is not a `uint8_t`), `0x` hexadecimal is accepted,
and number parsing never depends on the global locale.

### Iterating

```cpp
auto folders = EasyVDF::load("libraryfolders.vdf");

// Children are KeyValue: structured bindings give the key and the value.
for (auto& [index, folder] : folders)
{
    std::cout << index << ": " << folder["path"].value_or("?") << '\n';

    for (auto& [appid, size] : folder["apps"])
        std::cout << "  app " << appid << " uses " << size.as<uint64_t>() << " bytes\n";
}

// Or without structured bindings:
for (const EasyVDF::KeyValue& folder : folders)
    std::cout << folder.key() << " has " << folder["apps"].size() << " apps\n";
```

Iterating something that is not an object (a string, a missing key) is an empty loop, not an error.

### Duplicate keys

Duplicate keys are legal in VDF. `node["key"]` is the first one; `all()` gives them all:

```cpp
auto doc = EasyVDF::parse(R"("root" { "dll" "a.dll"  "dll" "b.dll"  "other" "x" })");

doc["dll"].as<std::string>();          // "a.dll"
doc.count("dll");                      // 2
for (auto& dll : doc.all("dll"))
    std::cout << dll.as<std::string>() << '\n';

doc.add("dll", "c.dll");               // add() always appends
doc.erase("dll");                      // erases the three of them
```

## Modifying and saving

```cpp
auto manifest = EasyVDF::load("appmanifest_440.acf");

manifest["StateFlags"] = 4;                        // replace a value
manifest["UserConfig"]["language"] = "french";     // objects and keys are created as needed
manifest["MountedDepots"].erase("441");            // remove a key
manifest["InstalledDepots"].add("442", {           // append an object
    {"manifest", "1234567890"},
    {"size", 4096},
});

manifest.save("appmanifest_440.acf");              // text by default
std::string text = manifest.to_text();             // or get the text
std::cout << manifest;                             // or stream it
```

On a non-const node `operator[]` works like `std::map`'s: it returns something you can assign to.
Unlike `std::map`, a key that you only *read* is never added to the document:

```cpp
EasyVDF::KeyValue doc("Settings");
doc["a"]["b"].value_or(0);     // nothing is created
doc.empty();                   // true
doc["a"]["b"] = 1;             // now "a" { "b" "1" } exists
```

References to nodes stay valid when their parent grows: each child is allocated on its own.

```cpp
EasyVDF::Node& config = manifest["UserConfig"];
manifest["NewKey"] = "value";          // config is still valid
config["language"] = "german";
```

### Building a document

```cpp
EasyVDF::KeyValue doc("AppState", {
    {"appid", 440},
    {"name", "Team Fortress 2"},
    {"SizeOnDisk", uint64_t(26843545600)},
    {"UserConfig", {
        {"language", "english"},
    }},
});
doc["InstalledDepots"]["441"]["manifest"] = "7707273133538421307";
doc.save("appmanifest_440.acf");
```

Assigned values keep a type: `bool` and signed integers up to 32 bits are `Int32`, wider signed integers `Int64`,
32/64 bits unsigned integers `UInt64`, `float`/`double` are `Float` (32 bits, the only floating point type of
the format), strings are `String`. The type matters for the binary formats; in text everything is written as a
quoted string (floats with the shortest representation that reads back to the same value).

## Binary files

```cpp
// Reading: same functions, the format is detected.
auto shortcuts = EasyVDF::load("userdata/12345/config/shortcuts.vdf");
for (auto& [index, shortcut] : shortcuts)
    std::cout << shortcut["AppName"].value_or("") << " -> " << shortcut["Exe"].value_or("") << '\n';

if (shortcuts["0"]["appid"].type() == EasyVDF::Type::Int32) { /* binary files have typed values */ }

// Writing
shortcuts["0"]["IsHidden"] = 1;                                   // Int32
shortcuts["0"]["LastPlayTime"] = int32_t(1700000000);
shortcuts.save("shortcuts.vdf", EasyVDF::Format::Binary);        // 0x08 end markers
shortcuts.save("shortcuts.vbkv", EasyVDF::Format::Vbkv);         // "VBKV" + CRC32 header

std::string bytes = shortcuts.to_binary();                        // or Format::Vbkv
```

- Everything is read and written little-endian, whatever the host is.
- **VBKV**: the header CRC is the standard CRC-32 (zlib) of every byte that follows the 8 bytes header.
  It is written, and checked when reading (`ParseError`, `ErrorCode::ChecksumMismatch`). Files written by tools that leave the
  CRC at zero can be read with `ReadOptions::verify_crc = false`.
- Both end markers (`0x08`, `0x0B`) end an object in both formats. Like Valve, the writer closes the root with one
  more end marker (files end with `08 08`, or `0B 0B` for VBKV), so Steam and Dota 2 files round-trip byte for byte.
  On read that extra marker is optional (`0x08`, or `0x0B` in a VBKV file); any other trailing data is a `ParseError`.
- The format has no unsigned 32 bits type: Steam stores such values as `Int32` (the `appid` of a non-Steam
  shortcut is a negative `Int32`). `as<uint32_t>()` on an `Int32` returns the same bits; every other integer
  conversion is range checked. To write one, assign `static_cast<int32_t>(appid)` (a `uint32_t` is stored as `UInt64`).
- `WideString` (type 5) and binary blobs (type 9) are rejected with a `ParseError`: the existing
  implementations disagree on their encoding and Valve's public code does not read them.

## Text format details

```
// comments
#base "defaults.vdf"                    // directives: parsed, see below
"root"
{
    unquoted_key   unquoted_value       // quotes are optional
    "one line" { "a" "1" "b" "2" }      // layout is free
    "path"   "C:\\Games\\Steam"         // escapes: \\ \" \n \t (and \r \v \b \f \a \? \')
    "multi"  "line 1
line 2"                                 // values may span lines
    "music"  "win.mp3"   [$WIN32]       // conditionals
    "console" [$X360] { "k" "v" }
}
```

- **Escape sequences** are decoded when reading and encoded when writing, so any string round-trips:
  `doc["path"] = R"(C:\Games)"` is written `"C:\\Games"` and read back as `C:\Games`. A backslash that
  does not start a known sequence is kept as is. Disable with `ReadOptions::escapes` / `WriteOptions::escapes`.
- **Conditionals** are kept, not evaluated: every entry is loaded, `KeyValue::condition()` returns `"$WIN32"`
  (empty when there is none) and it is written back in text. Filter them yourself if you need to.
- **`#include` / `#base`** lines are parsed and never followed (EasyVDF does not open other files).
  Pass `ReadOptions::directives` to get them:

  ```cpp
  std::vector<EasyVDF::Directive> directives;
  EasyVDF::ReadOptions options;
  options.directives = &directives;
  auto doc = EasyVDF::load("gameinfo.txt", options);
  for (auto& d : directives)
      std::cout << (d.kind == EasyVDF::Directive::Kind::Base ? "#base " : "#include ") << d.path << '\n';
  ```
- LF, CRLF and CR line endings and an UTF-8 byte order mark are accepted. Strings are opaque bytes (UTF-8 is not validated).
- A document is exactly one root key and its object. Anything else is a `ParseError`, which carries `line()` and `column()`.
- Keys are case sensitive.

## Errors

Every error has an `EasyVDF::ErrorCode`, a message and, for text input, a position. Each function that can
fail comes in two forms: `parse()`, `load()`, `save()`, `at()`, ... throw an exception, and their `try_*`
versions return an `EasyVDF::Result` that holds the value or the error
(see [Error handling without exceptions](#error-handling-without-exceptions)).

The exceptions derive from `EasyVDF::Error` (a `std::runtime_error`):

| Exception | Thrown when | `code()` |
|---|---|---|
| `ParseError` | The input is malformed (`line()`, `column()` for text). | One of the parse codes below. |
| `TypeError` | `as<T>()` has no conversion; a scalar is used as an object. | `Type` |
| `KeyError` | `at()` did not find the key. | `Key` |
| `WriteError` | The document can't be written (the root is not an object, ...). | `Write` |
| `IoError` | A file can't be opened, read or written; a stream can't be read. | `Io` |

| Parse code | The input... |
|---|---|
| `Syntax` | has a token that the grammar does not allow there (text). |
| `UnexpectedEof` | ends before the end of the document: a truncated file. |
| `EmptyInput` | holds no document: no byte, or only blanks, comments and `#include` / `#base`. |
| `TrailingData` | goes on after the root object (`parse_all()` reads text files that hold several). |
| `InvalidRoot` | is binary and its root is not an object. |
| `UnknownNodeType` | is binary and holds a node type that is not part of the format. |
| `UnsupportedNodeType` | is binary and holds a `WideString` (5) or a binary blob (9). |
| `UnsupportedEncoding` | is UTF-16 text. |
| `ChecksumMismatch` | is VBKV and its CRC32 does not match its header. |
| `DepthExceeded` | nests objects deeper than `ReadOptions::max_depth`. |

```cpp
try
{
    auto doc = EasyVDF::load(path);
}
catch (const EasyVDF::ParseError& e)
{
    std::cerr << path << ':' << e.line() << ':' << e.column() << ": " << e.what() << '\n';
}
catch (const EasyVDF::Error& e)
{
    std::cerr << e.what() << '\n';                  // e.code(): EasyVDF::ErrorCode::Io, ...
}
```

`line()` and `column()` are 1-based, and 0 when they don't apply (binary input, I/O); at the end of a text input
only the line is known. `e.info()` gives the error as the `EasyVDF::ErrorInfo` that the `try_*` functions return.

## Error handling without exceptions

The `try_*` functions never throw (except `std::bad_alloc`): they return an `EasyVDF::Result<T>`, which holds
either a `T` or an `EasyVDF::ErrorInfo`, like Rust's `Result<T, E>` or C++23's `std::expected<T, E>`.
The parsers, writers and file functions report their errors this way; the throwing API is a thin layer on top.

```cpp
EasyVDF::Result<EasyVDF::KeyValue> doc = EasyVDF::try_load(path);
if (!doc)
{
    const EasyVDF::ErrorInfo& error = doc.error();   // code, message, line, column
    std::cerr << path << ':' << error.line << ':' << error.column << ": " << error.message << '\n';
    return;
}
std::cout << (*doc)["name"].value_or("?") << '\n';   // *doc is the KeyValue, doc-> works too
```

| Throws | Returns |
|---|---|
| `parse(data)`, `parse(stream)`, `load(path)` | `try_parse(...)`, `try_load(path)`: `Result<KeyValue>` |
| `parse_all(...)`, `load_all(path)` | `try_parse_all(...)`, `try_load_all(path)`: `Result<std::vector<KeyValue>>` |
| `doc.dump()`, `doc.to_text()`, `doc.to_binary()` | `doc.try_dump()`, `doc.try_to_text()`, `doc.try_to_binary()`: `Result<std::string>` |
| `doc.write(stream)`, `doc.save(path)` | `doc.try_write(stream)`, `doc.try_save(path)`: `Result<void>` |
| `node.at(key)` | `node.try_at(key)`: `Result<Node&>` (`Result<Node const&>` on a const node) |
| `node.as<T>()` | `node.try_as<T>()`: `std::optional<T>`; `node.value_or(default)` |

The error is the one that the throwing function would throw: same code, message, line and column.

### Result

```cpp
r.has_value();  if (r) ...          // a value?
*r;  r->key();                      // the value (has_value() must be true)
r.error();                          // the ErrorInfo (has_value() must be false)
r.value();                          // the value, or the error thrown as its exception: Rust's unwrap()
r.value_or(default_value);          // the value, or the default
r.map(f);                           // Result<U> of f(value), or the same error
r.and_then(f);                      // f(value), which returns a Result, or the same error
r.or_else(f);                       // the value, or f(error), which returns a Result<T>
r.map_error(f);                     // the value, or the ErrorInfo returned by f(error)

return value;                       // a function returning Result<T> returns a T...
return EasyVDF::ErrorInfo(EasyVDF::ErrorCode::Io, "message");   // ...or an error
```

Errors propagate like Rust's `?` with an early return, and lookups that can each fail chain with `and_then()`:

```cpp
EasyVDF::Result<uint32_t> ReadAppId(const std::filesystem::path& path)
{
    EasyVDF::Result<EasyVDF::KeyValue> manifest = EasyVDF::try_load(path);
    if (!manifest)
        return manifest.error();                       // pass the error up

    if (auto appid = (*manifest)["appid"].try_as<uint32_t>())
        return *appid;
    return EasyVDF::ErrorInfo(EasyVDF::ErrorCode::Type, "appid is not a number");
}

EasyVDF::Result<EasyVDF::KeyValue> manifest = EasyVDF::try_load("appmanifest_440.acf");
std::string language = manifest
    .and_then([](const EasyVDF::KeyValue& doc) { return doc.try_at("UserConfig"); })
    .and_then([](const EasyVDF::Node& config) { return config.try_at("language"); })
    .map([](const EasyVDF::Node& node) { return node.value_or(std::string()); })
    .value_or("english");                              // missing file, key or value: "english"

// Recover from some errors only, add context to the others.
EasyVDF::Result<EasyVDF::KeyValue> config = EasyVDF::try_load("config.vdf")
    .or_else([](const EasyVDF::ErrorInfo& error) -> EasyVDF::Result<EasyVDF::KeyValue> {
        if (error.code == EasyVDF::ErrorCode::Io)
            return EasyVDF::KeyValue("Config");        // no file yet: start empty
        return error;
    })
    .map_error([](EasyVDF::ErrorInfo error) {
        error.message = "config.vdf: " + error.message;
        return error;
    });

// and_then() also chains to a Result<void>: the error is the one of the load, or of the save.
EasyVDF::Result<void> saved = config.and_then([](const EasyVDF::KeyValue& doc) { return doc.try_save("config.vdf"); });
if (!saved)
    std::cerr << saved.error() << '\n';                // "Syntax: config.vdf: Expected a key ..."
```

- `Result<void>` (from `try_write()`, `try_save()`) only tells success from failure; its `map()` and `and_then()`
  take functions without parameter.
- `Result<Node&>` (from `try_at()`) holds a reference, like a pointer: it is valid as long as the node it refers
  to, exactly like the reference that `at()` returns. Assigning another `Result` rebinds it; `value_or()` returns
  a copy. A `Result<Node&>` converts to a `Result<Node const&>`.
- `map()` stores what the function returns by value; `and_then()` with `try_at()` stays on references.
- `operator*`, `operator->` and `error()` check that the `Result` holds what they return with `assert()` only.
  `value()` always checks.
- `ErrorCode` and `ErrorInfo` print with `<<` (`"Syntax: Expected a key ... at line 3, column 5"`),
  `EasyVDF::to_string(code)` gives the name of a code.
- `Result` is EasyVDF's own type, also in C++23: `std::expected` has no reference specialization, its
  `value()` throws `std::bad_expected_access` instead of `ParseError` / `IoError`..., and its `map()` is called
  `transform()`.

### Compiling without exceptions

EasyVDF compiles with exceptions disabled (`-fno-exceptions` with GCC and clang, `/EHs-c- /D_HAS_EXCEPTIONS=0` with
MSVC; clang with the Microsoft standard library needs `-D_HAS_EXCEPTIONS=0` too). The header detects it
(`__cpp_exceptions`, `_CPPUNWIND`) and defines `EASYVDF_NO_EXCEPTIONS`. You can also define `EASYVDF_NO_EXCEPTIONS`
before including it to get the same API in a build with exceptions, to make sure that some code never throws.
In this mode:

- **The throwing API is not declared**: `parse()`, `parse_all()`, `load()`, `load_all()`, `KeyValue::dump()`,
  `to_text()`, `to_binary()`, `write()`, `save()`, `Node::as<T>()`, `Node::at()` and the exception classes.
  Calling one is a compile error; use the `try_*` functions, `try_as<T>()` and `value_or()`.
- **Everything else is available.** Reading never fails (`operator[]` on a const node, `find()`, `value_or()`,
  iteration, ...), and the operations that can fail return a `Result`.
- **Two mistakes have no error value**: `Result::value()` on an error, and a scalar used as an object (the
  non-const `operator[]`, `add()` or `insert()` on a string or a number). They print the error on stderr and call
  `std::abort()`, which is what an uncaught exception would do, or Rust's `unwrap()` and `panic!`. Test first
  with `has_value()` or `is_object()` / `is_none()`.
- An allocation failure (`std::bad_alloc` with exceptions) ends the program, as everywhere without exceptions.

```cpp
// g++ -std=c++17 -fno-exceptions ...
EasyVDF::KeyValue doc("Settings");
doc["window"]["width"] = 1280;                         // OK: objects are created as needed
if (auto saved = doc.try_save("settings.vdf"); !saved)
    std::cerr << saved.error() << '\n';

doc["window"]["width"]["x"] = 1;                       // a number has no children: prints the error, aborts
```

## Options

```cpp
EasyVDF::ReadOptions options;
options.format     = EasyVDF::Format::Auto;  // or force Text / Binary
options.escapes    = true;                   // decode escape sequences (text)
options.verify_crc = true;                   // check the VBKV checksum
options.max_depth  = 128;                    // nesting limit, protects against malicious input
options.chunk_size = 16 * 1024;              // streams are read by blocks
options.directives = nullptr;                // collect #include / #base

EasyVDF::WriteOptions write_options;         // a Format converts implicitly to WriteOptions
write_options.format  = EasyVDF::Format::Text;
write_options.escapes = true;
```

## Good to know

- A `Node` without value (`Type::None`) is what a missing key, a default constructed or a moved-from node is.
  A child without value is not part of its parent: it is not iterated, counted or written.
- `Node::size()` is linear in the number of children.
- Braces build objects: `Node copy(other)` copies, `Node wrapped{key_value}` is an object holding `key_value`.
  `{}` is "no value"; an empty object is `Node::object()` (or `node.add("key")`).
- A key requested through a non-const `operator[]` and never assigned stays allocated (invisible) until its
  parent is cleared or destroyed, so that the reference you got stays valid. Use `find()` or a const reference
  to probe a long-lived document for many different keys.
- Floats are written in plain decimal notation (`100000`, `0.0001`), in scientific notation only when very
  large or very small. A `double` is stored as a 32 bits float (infinity when out of range).
- Text files do not store types: a document built with typed values and saved as text reads back as strings
  (`as<T>()` gives the values back).
- Requires C++17 (`std::string_view`, `std::optional`, `std::variant`, structured bindings, `<charconv>`, `<filesystem>`).

## Examples

Complete programs in [`examples/`](examples), built with the tests (`-DEASYVDF_BUILD_EXAMPLES=ON` to force them):

| Example | What it shows |
|---|---|
| [`quick_tour.cpp`](examples/quick_tour.cpp) | Build a document, read typed values with defaults, iterate, duplicate keys, modify, save as text and VBKV, reload, handle errors. |
| [`achievements.cpp`](examples/achievements.cpp) | List the achievements and stats of a Steam stats schema (binary `UserGameStatsSchema_<appid>.bin`), in a given language. |
| [`vdf_convert.cpp`](examples/vdf_convert.cpp) | Convert any VDF file between text, binary and VBKV. |
| [`no_exceptions.cpp`](examples/no_exceptions.cpp) | Check VDF files and print their errors like a compiler, built with exceptions disabled: `Result`, `try_load_all()`, `try_at()` chains. |

```sh
achievements UserGameStatsSchema_480.bin french
vdf_convert shortcuts.vdf shortcuts.txt          # binary -> text
vdf_convert shortcuts.txt shortcuts.vdf binary   # text -> binary
no_exceptions appmanifest_440.acf broken.vdf     # broken.vdf:7:5: error [Syntax]: ...
```

## Tests

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## License

See [LICENSE](LICENSE).
