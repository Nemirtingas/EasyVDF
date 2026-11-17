// EasyVDF - single-header C++17 reader/writer for Valve KeyValues (VDF) files,
// text and binary. See README.md for an overview and examples.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Floating-point std::from_chars/std::to_chars are missing from some standard
// libraries (libc++ before LLVM 20): fall back to classic-locale streams there.
// Define EASYVDF_HAS_FLOAT_CHARCONV to 0 or 1 before including this header to override the detection.
#ifndef EASYVDF_HAS_FLOAT_CHARCONV
    #if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        #define EASYVDF_HAS_FLOAT_CHARCONV 1
    #else
        #define EASYVDF_HAS_FLOAT_CHARCONV 0
    #endif
#endif

// EasyVDF reports its errors by return value (EasyVDF::Result, the try_* functions);
// the throwing API (parse(), load(), as<T>(), at(), save(), ...) and the exception
// classes are a thin layer on top of it. Define EASYVDF_NO_EXCEPTIONS before including
// this header to remove that layer. It is defined automatically when the compiler has
// exceptions disabled (-fno-exceptions, /EHs-c-). See README.md, "Error handling
// without exceptions".
#if !defined(EASYVDF_NO_EXCEPTIONS) && !defined(__cpp_exceptions) && !defined(__EXCEPTIONS) && !defined(_CPPUNWIND)
    #define EASYVDF_NO_EXCEPTIONS
#endif

namespace EasyVDF {

/////////////////////////////////////////////////////////////////////
//
//                          Basic types
//
/////////////////////////////////////////////////////////////////////

/// What a Node holds.
/// Text files only ever produce Object and String; the other types come from
/// binary files or from values assigned in code.
enum class Type : uint8_t
{
    None,    ///< No value (missing key, default constructed or moved-from node).
    Object,  ///< A list of key/value children.
    String,
    Int32,
    Float,   ///< 32 bits float, the only floating point type of the format.
    Pointer,
    Color,
    UInt64,
    Int64,
};

/// File formats.
enum class Format
{
    Auto,   ///< Reading only: detect text / binary / VBKV from the first bytes.
    Text,   ///< Text KeyValues ("key" "value").
    Binary, ///< Binary KeyValues, objects end with 0x08 (Steam appinfo, shortcuts.vdf, ...).
    Vbkv,   ///< Binary KeyValues with a "VBKV" + CRC32 header, objects end with 0x0B.
};

/// A 32 bits "pointer" value of the binary format.
struct Pointer
{
    uint32_t value = 0;
};

/// A color value of the binary format (4 bytes: r, g, b, a). Its text form is "r g b a".
struct Color
{
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

inline bool operator==(Pointer l, Pointer r) noexcept { return l.value == r.value; }
inline bool operator!=(Pointer l, Pointer r) noexcept { return l.value != r.value; }
inline bool operator< (Pointer l, Pointer r) noexcept { return l.value <  r.value; }
inline bool operator<=(Pointer l, Pointer r) noexcept { return l.value <= r.value; }
inline bool operator> (Pointer l, Pointer r) noexcept { return l.value >  r.value; }
inline bool operator>=(Pointer l, Pointer r) noexcept { return l.value >= r.value; }

inline bool operator==(Color l, Color r) noexcept { return std::tie(l.r, l.g, l.b, l.a) == std::tie(r.r, r.g, r.b, r.a); }
inline bool operator!=(Color l, Color r) noexcept { return !(l == r); }
inline bool operator< (Color l, Color r) noexcept { return std::tie(l.r, l.g, l.b, l.a) < std::tie(r.r, r.g, r.b, r.a); }
inline bool operator<=(Color l, Color r) noexcept { return !(r < l); }
inline bool operator> (Color l, Color r) noexcept { return r < l; }
inline bool operator>=(Color l, Color r) noexcept { return !(l < r); }

/////////////////////////////////////////////////////////////////////
//
//                              Errors
//
/////////////////////////////////////////////////////////////////////

/// What went wrong. Every error has one of these codes, whether it is returned in a
/// Result (try_* functions) or thrown (the exception class is given for each group).
enum class ErrorCode
{
    // Malformed input, thrown as ParseError.
    Syntax,              ///< Text: a token that the grammar does not allow there.
    UnexpectedEof,       ///< The input ends before the end of the document (truncated input).
    EmptyInput,          ///< The input holds no document: no byte, or only blanks, comments and directives.
    TrailingData,        ///< Data after the root object (see parse_all() for the files that hold several).
    InvalidRoot,         ///< Binary: the root item is not an object.
    UnknownNodeType,     ///< Binary: a node type that is not part of the format.
    UnsupportedNodeType, ///< Binary: WideString (5) or binary blob (9), not supported.
    UnsupportedEncoding, ///< Text: UTF-16 input.
    ChecksumMismatch,    ///< VBKV: the CRC32 of the data does not match the header.
    DepthExceeded,       ///< Objects nested deeper than ReadOptions::max_depth.

    Io,                  ///< IoError: a file can't be opened, read or written, or a stream can't be read.
    Write,               ///< WriteError: the document can't be written (root not an object, ...).
    Type,                ///< TypeError: no conversion to the requested type, or a scalar used as an object.
    Key,                 ///< KeyError: Node::at() / try_at() did not find the key.
};

/// The name of the code: "Syntax", "UnexpectedEof", ...
inline const char* to_string(ErrorCode code) noexcept
{
    switch (code)
    {
        case ErrorCode::Syntax             : return "Syntax";
        case ErrorCode::UnexpectedEof      : return "UnexpectedEof";
        case ErrorCode::EmptyInput         : return "EmptyInput";
        case ErrorCode::TrailingData       : return "TrailingData";
        case ErrorCode::InvalidRoot        : return "InvalidRoot";
        case ErrorCode::UnknownNodeType    : return "UnknownNodeType";
        case ErrorCode::UnsupportedNodeType: return "UnsupportedNodeType";
        case ErrorCode::UnsupportedEncoding: return "UnsupportedEncoding";
        case ErrorCode::ChecksumMismatch   : return "ChecksumMismatch";
        case ErrorCode::DepthExceeded      : return "DepthExceeded";
        case ErrorCode::Io                 : return "Io";
        case ErrorCode::Write              : return "Write";
        case ErrorCode::Type               : return "Type";
        case ErrorCode::Key                : return "Key";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, ErrorCode code)
{
    return os << to_string(code);
}

/// An error: its code, a readable message (the what() of the matching exception)
/// and, for text input, the 1-based position of the error. line and column are 0
/// when they do not apply (binary input, I/O, ...); column is 0 at the end of the input.
struct ErrorInfo
{
    ErrorCode code;
    std::string message;
    std::size_t line = 0;
    std::size_t column = 0;

    ErrorInfo(ErrorCode error_code, std::string error_message, std::size_t error_line = 0, std::size_t error_column = 0) :
        code(error_code), message(std::move(error_message)), line(error_line), column(error_column)
    {}

    friend bool operator==(ErrorInfo const& l, ErrorInfo const& r) noexcept
    {
        return l.code == r.code && l.line == r.line && l.column == r.column && l.message == r.message;
    }

    friend bool operator!=(ErrorInfo const& l, ErrorInfo const& r) noexcept { return !(l == r); }
};

/// "Code: message", e.g. "UnexpectedEof: Unexpected end of file, ... at line 3".
inline std::ostream& operator<<(std::ostream& os, ErrorInfo const& error)
{
    return os << to_string(error.code) << ": " << error.message;
}

#ifndef EASYVDF_NO_EXCEPTIONS

/// Base class of every exception thrown by EasyVDF. code(), line() and column()
/// give the same information as the ErrorInfo of the try_* functions; info() rebuilds it.
class Error : public std::runtime_error
{
    ErrorCode m_Code;
    std::size_t m_Line;
    std::size_t m_Column;

protected:
    Error(ErrorCode code, std::string const& message, std::size_t line, std::size_t column) :
        std::runtime_error(message), m_Code(code), m_Line(line), m_Column(column)
    {}

public:
    explicit Error(ErrorInfo const& info) :
        Error(info.code, info.message, info.line, info.column)
    {}

    ErrorCode code() const noexcept { return m_Code; }

    /// 1-based position in a text input, 0 when unknown or not applicable.
    std::size_t line() const noexcept { return m_Line; }
    std::size_t column() const noexcept { return m_Column; }

    /// The error as a value, with what() as its message.
    ErrorInfo info() const { return ErrorInfo(m_Code, what(), m_Line, m_Column); }
};

/// Malformed input. line() and column() are 1-based and 0 when unknown (binary input).
/// Its code() is one of the parse codes, Syntax to UnsupportedEncoding.
class ParseError : public Error
{
public:
    explicit ParseError(std::string const& message, std::size_t line = 0, std::size_t column = 0) :
        Error(ErrorCode::Syntax, message, line, column)
    {}

    explicit ParseError(ErrorInfo const& info) : Error(info) {}
};

/// A value has no conversion to the requested type, or a non-object was used as an object.
class TypeError : public Error
{
public:
    explicit TypeError(std::string const& message) : Error(ErrorCode::Type, message, 0, 0) {}
    explicit TypeError(const char* message) : Error(ErrorCode::Type, message, 0, 0) {}
    explicit TypeError(ErrorInfo const& info) : Error(info) {}
};

/// Node::at() did not find the key.
class KeyError : public Error
{
public:
    explicit KeyError(std::string const& message) : Error(ErrorCode::Key, message, 0, 0) {}
    explicit KeyError(const char* message) : Error(ErrorCode::Key, message, 0, 0) {}
    explicit KeyError(ErrorInfo const& info) : Error(info) {}
};

/// The tree can't be written (root without value or not an object, ...).
class WriteError : public Error
{
public:
    explicit WriteError(std::string const& message) : Error(ErrorCode::Write, message, 0, 0) {}
    explicit WriteError(const char* message) : Error(ErrorCode::Write, message, 0, 0) {}
    explicit WriteError(ErrorInfo const& info) : Error(info) {}
};

/// A file could not be opened, read or written.
class IoError : public Error
{
public:
    explicit IoError(std::string const& message) : Error(ErrorCode::Io, message, 0, 0) {}
    explicit IoError(const char* message) : Error(ErrorCode::Io, message, 0, 0) {}
    explicit IoError(ErrorInfo const& info) : Error(info) {}
};

#endif // EASYVDF_NO_EXCEPTIONS

namespace Details {

/// Reports an error that the caller did not handle: Result::value() on an error,
/// a scalar used as an object, ... Throws the exception that matches the code;
/// without exceptions, prints the error on stderr and calls std::abort().
[[noreturn]] inline void Raise(ErrorInfo const& error)
{
#ifndef EASYVDF_NO_EXCEPTIONS
    switch (error.code)
    {
        case ErrorCode::Io   : throw IoError(error);
        case ErrorCode::Write: throw WriteError(error);
        case ErrorCode::Type : throw TypeError(error);
        case ErrorCode::Key  : throw KeyError(error);
        default              : throw ParseError(error);
    }
#else
    std::fprintf(stderr, "EasyVDF: unhandled %s error: %s\n", to_string(error.code), error.message.c_str());
    std::fflush(stderr);
    std::abort();
#endif
}

} // namespace Details

/////////////////////////////////////////////////////////////////////
//
//                              Result
//
/////////////////////////////////////////////////////////////////////

template<typename T> class Result;

namespace Details {

template<typename T> struct IsResult : std::false_type {};
template<typename T> struct IsResult<Result<T>> : std::true_type {};

template<typename T>
using RemoveCvRef = std::remove_cv_t<std::remove_reference_t<T>>;

/// Arguments that construct the value of a Result<T> (not an error, not another Result).
template<typename T, typename U>
constexpr bool IsResultValueArgument =
    std::is_constructible_v<T, U&&>
    && !std::is_same_v<RemoveCvRef<U>, Result<T>>
    && !std::is_same_v<RemoveCvRef<U>, ErrorInfo>
    && !std::is_same_v<RemoveCvRef<U>, std::in_place_t>
    && (!IsResult<RemoveCvRef<U>>::value || std::is_same_v<RemoveCvRef<U>, std::remove_cv_t<T>>);

/// The combinators shared by every kind of Result.
struct ResultOps
{
    // f(value), or f() for Result<void>.
    template<typename Self, typename F>
    static decltype(auto) Call(Self&& self, F&& f)
    {
        if constexpr (std::is_void_v<typename RemoveCvRef<Self>::value_type>)
            return std::invoke(std::forward<F>(f));
        else
            return std::invoke(std::forward<F>(f), *std::forward<Self>(self));
    }

    template<typename Self, typename F>
    static auto Map(Self&& self, F&& f)
    {
        using U = RemoveCvRef<decltype(Call(std::forward<Self>(self), std::forward<F>(f)))>;
        if (!self.has_value())
            return Result<U>(std::forward<Self>(self).error());

        if constexpr (std::is_void_v<U>)
        {
            Call(std::forward<Self>(self), std::forward<F>(f));
            return Result<U>();
        }
        else
        {
            return Result<U>(Call(std::forward<Self>(self), std::forward<F>(f)));
        }
    }

    template<typename Self, typename F>
    static auto AndThen(Self&& self, F&& f)
    {
        using R = RemoveCvRef<decltype(Call(std::forward<Self>(self), std::forward<F>(f)))>;
        static_assert(IsResult<R>::value, "EasyVDF: the function given to and_then() must return a Result");
        if (!self.has_value())
            return R(std::forward<Self>(self).error());
        return R(Call(std::forward<Self>(self), std::forward<F>(f)));
    }

    template<typename Self, typename F>
    static RemoveCvRef<Self> OrElse(Self&& self, F&& f)
    {
        using R = RemoveCvRef<Self>;
        static_assert(std::is_same_v<RemoveCvRef<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>, R>,
            "EasyVDF: the function given to or_else() must return a Result of the same type");
        if (self.has_value())
            return R(std::forward<Self>(self));
        return std::invoke(std::forward<F>(f), std::forward<Self>(self).error());
    }

    template<typename Self, typename F>
    static RemoveCvRef<Self> MapError(Self&& self, F&& f)
    {
        using R = RemoveCvRef<Self>;
        if (self.has_value())
            return R(std::forward<Self>(self));
        return R(ErrorInfo(std::invoke(std::forward<F>(f), std::forward<Self>(self).error())));
    }
};

} // namespace Details

/// A value of type T or an ErrorInfo, like C++23 std::expected<T, ErrorInfo> or
/// Rust's Result<T, E>. Returned by the try_* functions, which never throw
/// (except std::bad_alloc).
///
/// ```
/// EasyVDF::Result<EasyVDF::KeyValue> doc = EasyVDF::try_load(path);
/// if (!doc)
///     std::cerr << doc.error() << '\n';      // "Syntax: Expected a key ... at line 3, column 5"
/// else
///     std::cout << (*doc)["name"].value_or("?") << '\n';
/// ```
///
/// - `*r` and `r->` access the value (has_value() must be true), `r.error()` the
///   error (has_value() must be false).
/// - `r.value()` returns the value, or reports the error like an uncaught one: it
///   throws the matching exception (ParseError, IoError, ...), and without
///   exceptions prints the error on stderr and calls std::abort(), like Rust's unwrap().
/// - `map(f)` transforms the value, `and_then(f)` chains an operation that can fail,
///   `or_else(f)` recovers from an error, `map_error(f)` transforms the error.
///
/// Result<T&> holds a reference (see Node::try_at()), Result<void> only tells
/// success from failure (see KeyValue::try_save()).
template<typename T>
class [[nodiscard]] Result
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>, "EasyVDF: Result<T> holds an object type, an lvalue reference or void");
    static_assert(!std::is_same_v<std::remove_cv_t<T>, ErrorInfo>, "EasyVDF: Result<ErrorInfo> can't tell a value from an error");

    std::variant<T, ErrorInfo> m_Storage;

public:
    using value_type = T;
    using error_type = ErrorInfo;

    /// A value: `Result<int> r = 42;`
    template<typename U = T, std::enable_if_t<Details::IsResultValueArgument<T, U> && std::is_convertible_v<U&&, T>, int> = 0>
    Result(U&& value) :
        m_Storage(std::in_place_index<0>, std::forward<U>(value))
    {}

    template<typename U = T, std::enable_if_t<Details::IsResultValueArgument<T, U> && !std::is_convertible_v<U&&, T>, int> = 0>
    explicit Result(U&& value) :
        m_Storage(std::in_place_index<0>, std::forward<U>(value))
    {}

    /// An error: `return EasyVDF::ErrorInfo(EasyVDF::ErrorCode::Io, "can't open the file");`
    Result(ErrorInfo error) :
        m_Storage(std::in_place_index<1>, std::move(error))
    {}

    bool has_value() const noexcept { return m_Storage.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    /// The value; has_value() must be true.
    T& operator*() & noexcept { assert(has_value()); return *std::get_if<0>(&m_Storage); }
    T const& operator*() const& noexcept { assert(has_value()); return *std::get_if<0>(&m_Storage); }
    T&& operator*() && noexcept { assert(has_value()); return std::move(*std::get_if<0>(&m_Storage)); }
    T const&& operator*() const&& noexcept { assert(has_value()); return std::move(*std::get_if<0>(&m_Storage)); }
    T* operator->() noexcept { assert(has_value()); return std::get_if<0>(&m_Storage); }
    T const* operator->() const noexcept { assert(has_value()); return std::get_if<0>(&m_Storage); }

    /// The value; an error is thrown as the matching exception, or printed before
    /// std::abort() without exceptions.
    T& value() & { Check(); return **this; }
    T const& value() const& { Check(); return **this; }
    T&& value() && { Check(); return std::move(**this); }
    T const&& value() const&& { Check(); return std::move(**this); }

    /// The error; has_value() must be false.
    ErrorInfo& error() & noexcept { assert(!has_value()); return *std::get_if<1>(&m_Storage); }
    ErrorInfo const& error() const& noexcept { assert(!has_value()); return *std::get_if<1>(&m_Storage); }
    ErrorInfo&& error() && noexcept { assert(!has_value()); return std::move(*std::get_if<1>(&m_Storage)); }

    /// The value, or default_value converted to T when this holds an error.
    template<typename U>
    T value_or(U&& default_value) const&
    {
        if (has_value())
            return **this;
        return static_cast<T>(std::forward<U>(default_value));
    }

    template<typename U>
    T value_or(U&& default_value) &&
    {
        if (has_value())
            return std::move(**this);
        return static_cast<T>(std::forward<U>(default_value));
    }

    /// Result<U> holding f(value), or this error. f may return void (Result<void>).
    template<typename F> auto map(F&& f) & { return Details::ResultOps::Map(*this, std::forward<F>(f)); }
    template<typename F> auto map(F&& f) const& { return Details::ResultOps::Map(*this, std::forward<F>(f)); }
    template<typename F> auto map(F&& f) && { return Details::ResultOps::Map(std::move(*this), std::forward<F>(f)); }

    /// f(value), which returns a Result, or this error.
    template<typename F> auto and_then(F&& f) & { return Details::ResultOps::AndThen(*this, std::forward<F>(f)); }
    template<typename F> auto and_then(F&& f) const& { return Details::ResultOps::AndThen(*this, std::forward<F>(f)); }
    template<typename F> auto and_then(F&& f) && { return Details::ResultOps::AndThen(std::move(*this), std::forward<F>(f)); }

    /// This value, or f(error), which returns a Result<T>.
    template<typename F> Result or_else(F&& f) & { return Details::ResultOps::OrElse(*this, std::forward<F>(f)); }
    template<typename F> Result or_else(F&& f) const& { return Details::ResultOps::OrElse(*this, std::forward<F>(f)); }
    template<typename F> Result or_else(F&& f) && { return Details::ResultOps::OrElse(std::move(*this), std::forward<F>(f)); }

    /// This value, or the error f(error) returns (an ErrorInfo).
    template<typename F> Result map_error(F&& f) & { return Details::ResultOps::MapError(*this, std::forward<F>(f)); }
    template<typename F> Result map_error(F&& f) const& { return Details::ResultOps::MapError(*this, std::forward<F>(f)); }
    template<typename F> Result map_error(F&& f) && { return Details::ResultOps::MapError(std::move(*this), std::forward<F>(f)); }

private:
    void Check() const
    {
        if (!has_value())
            Details::Raise(*std::get_if<1>(&m_Storage));
    }
};

/// A reference or an error, see Node::try_at(). Like a pointer, the Result does not
/// own the value: the reference stays valid as long as the node it refers to.
/// value_or() returns a copy.
template<typename T>
class [[nodiscard]] Result<T&>
{
    template<typename> friend class Result;

    std::variant<T*, ErrorInfo> m_Storage;

public:
    using value_type = T&;
    using error_type = ErrorInfo;

    Result(T& value) noexcept :
        m_Storage(std::in_place_index<0>, std::addressof(value))
    {}

    /// No reference to a temporary.
    Result(T&& value) = delete;

    /// Result<Node const&> from Result<Node&>.
    template<typename U, std::enable_if_t<!std::is_same_v<U, T> && std::is_convertible_v<U*, T*>, int> = 0>
    Result(Result<U&> const& other) :
        m_Storage(other.has_value()
            ? std::variant<T*, ErrorInfo>(std::in_place_index<0>, *std::get_if<0>(&other.m_Storage))
            : std::variant<T*, ErrorInfo>(std::in_place_index<1>, *std::get_if<1>(&other.m_Storage)))
    {}

    Result(ErrorInfo error) :
        m_Storage(std::in_place_index<1>, std::move(error))
    {}

    bool has_value() const noexcept { return m_Storage.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    T& operator*() const noexcept { assert(has_value()); return **std::get_if<0>(&m_Storage); }
    T* operator->() const noexcept { assert(has_value()); return *std::get_if<0>(&m_Storage); }

    T& value() const
    {
        if (!has_value())
            Details::Raise(*std::get_if<1>(&m_Storage));
        return **this;
    }

    ErrorInfo& error() & noexcept { assert(!has_value()); return *std::get_if<1>(&m_Storage); }
    ErrorInfo const& error() const& noexcept { assert(!has_value()); return *std::get_if<1>(&m_Storage); }
    ErrorInfo&& error() && noexcept { assert(!has_value()); return std::move(*std::get_if<1>(&m_Storage)); }

    /// A copy of the value, or default_value converted to it.
    template<typename U>
    std::remove_cv_t<T> value_or(U&& default_value) const
    {
        if (has_value())
            return **this;
        return static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
    }

    template<typename F> auto map(F&& f) const { return Details::ResultOps::Map(*this, std::forward<F>(f)); }
    template<typename F> auto and_then(F&& f) const { return Details::ResultOps::AndThen(*this, std::forward<F>(f)); }
    template<typename F> Result or_else(F&& f) const { return Details::ResultOps::OrElse(*this, std::forward<F>(f)); }
    template<typename F> Result map_error(F&& f) const { return Details::ResultOps::MapError(*this, std::forward<F>(f)); }
};

/// Success or an error, see KeyValue::try_save(). The functions given to map()
/// and and_then() take no argument.
template<>
class [[nodiscard]] Result<void>
{
    std::optional<ErrorInfo> m_Error;

public:
    using value_type = void;
    using error_type = ErrorInfo;

    /// Success.
    Result() noexcept = default;

    Result(ErrorInfo error) :
        m_Error(std::move(error))
    {}

    bool has_value() const noexcept { return !m_Error.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }

    /// Reports the error, if any: throws the matching exception, or prints it
    /// before std::abort() without exceptions.
    void value() const
    {
        if (m_Error)
            Details::Raise(*m_Error);
    }

    ErrorInfo& error() & noexcept { assert(!has_value()); return *m_Error; }
    ErrorInfo const& error() const& noexcept { assert(!has_value()); return *m_Error; }
    ErrorInfo&& error() && noexcept { assert(!has_value()); return std::move(*m_Error); }

    template<typename F> auto map(F&& f) const& { return Details::ResultOps::Map(*this, std::forward<F>(f)); }
    template<typename F> auto map(F&& f) && { return Details::ResultOps::Map(std::move(*this), std::forward<F>(f)); }
    template<typename F> auto and_then(F&& f) const& { return Details::ResultOps::AndThen(*this, std::forward<F>(f)); }
    template<typename F> auto and_then(F&& f) && { return Details::ResultOps::AndThen(std::move(*this), std::forward<F>(f)); }
    template<typename F> Result or_else(F&& f) const& { return Details::ResultOps::OrElse(*this, std::forward<F>(f)); }
    template<typename F> Result or_else(F&& f) && { return Details::ResultOps::OrElse(std::move(*this), std::forward<F>(f)); }
    template<typename F> Result map_error(F&& f) const& { return Details::ResultOps::MapError(*this, std::forward<F>(f)); }
    template<typename F> Result map_error(F&& f) && { return Details::ResultOps::MapError(std::move(*this), std::forward<F>(f)); }
};

class Node;
class KeyValue;

/////////////////////////////////////////////////////////////////////
//
//                     Implementation details
//
/////////////////////////////////////////////////////////////////////
namespace Details {

template<bool Const> class ChildIterator;
template<bool Const> class ChildRange;

/// Children of an object node. Every child is allocated on its own, so a
/// reference to a child stays valid until that child is erased.
struct Children
{
    /// Children in document order.
    std::vector<std::unique_ptr<KeyValue>> items;
    /// Children created by the non-const Node::operator[] that have not been
    /// given a value yet. They are invisible until they get one.
    std::vector<std::unique_ptr<KeyValue>> pending;
    /// The object itself was created by Node::operator[] on a node without value:
    /// it only exists while one of its children has a value.
    bool implicit = false;

    Children() noexcept;
    Children(Children const& other);
    Children(Children&& other) noexcept;
    Children& operator=(Children const& other);
    Children& operator=(Children&& other) noexcept;
    ~Children();

    std::size_t slots() const noexcept { return items.size() + pending.size(); }
    KeyValue* slot(std::size_t i) const noexcept;
};

using Value = std::variant<std::monostate, Children, std::string, int32_t, float, Pointer, Color, uint64_t, int64_t>;

template<typename T>
constexpr bool IsCharacter = std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

template<typename T>
constexpr bool IsNumber = std::is_arithmetic_v<T> && !IsCharacter<T>;

template<typename T>
constexpr bool IsStringLike = std::is_convertible_v<T const&, std::string_view> && !std::is_same_v<T, std::nullptr_t>;

/// Types that can be stored in a Node: numbers, bool, strings, Color and Pointer.
template<typename T>
constexpr bool IsScalar = IsNumber<std::decay_t<T>> || IsStringLike<std::decay_t<T>>
    || std::is_same_v<std::decay_t<T>, Color> || std::is_same_v<std::decay_t<T>, Pointer>;

template<typename T>
using ValueOrResult = std::conditional_t<IsStringLike<std::decay_t<T>>, std::string, std::decay_t<T>>;

inline const char* TypeName(Type type) noexcept
{
    switch (type)
    {
        case Type::None   : return "None";
        case Type::Object : return "Object";
        case Type::String : return "String";
        case Type::Int32  : return "Int32";
        case Type::Float  : return "Float";
        case Type::Pointer: return "Pointer";
        case Type::Color  : return "Color";
        case Type::UInt64 : return "UInt64";
        case Type::Int64  : return "Int64";
    }
    return "unknown";
}

template<typename T>
constexpr const char* TargetName() noexcept
{
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>)
        return "a string";
    else if constexpr (std::is_same_v<T, bool>)
        return "a bool";
    else if constexpr (std::is_integral_v<T>)
        return "an integer";
    else if constexpr (std::is_floating_point_v<T>)
        return "a floating point number";
    else if constexpr (std::is_same_v<T, Color>)
        return "a Color";
    else
        return "a Pointer";
}

inline bool IsSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

inline std::string_view Trim(std::string_view s) noexcept
{
    while (!s.empty() && IsSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && IsSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

inline bool EqualsIgnoreCase(std::string_view l, std::string_view r) noexcept
{
    if (l.size() != r.size())
        return false;

    for (std::size_t i = 0; i < l.size(); ++i)
    {
        char a = l[i], b = r[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

/// Integer to integer conversion, false when the value does not fit.
template<typename To, typename From>
bool IntegerCast(From value, To& out) noexcept
{
    if constexpr (std::is_signed_v<From>)
    {
        const int64_t v = static_cast<int64_t>(value);
        if (v < 0)
        {
            if constexpr (std::is_signed_v<To>)
            {
                if (v < static_cast<int64_t>((std::numeric_limits<To>::min)()))
                    return false;
            }
            else
            {
                return false;
            }
        }
        else if (static_cast<uint64_t>(v) > static_cast<uint64_t>((std::numeric_limits<To>::max)()))
        {
            return false;
        }
    }
    else
    {
        if (static_cast<uint64_t>(value) > static_cast<uint64_t>((std::numeric_limits<To>::max)()))
            return false;
    }
    out = static_cast<To>(value);
    return true;
}

/// Parses a whole string as an integer: optional sign, decimal or 0x hexadecimal,
/// surrounding blanks ignored. False if it is not a number or does not fit in T.
template<typename T>
bool ParseInteger(std::string_view s, T& out) noexcept
{
    s = Trim(s);
    bool negative = false;
    if (!s.empty() && (s.front() == '-' || s.front() == '+'))
    {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }

    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s.remove_prefix(2);
    }

    if (s.empty() || s.front() == '-' || s.front() == '+')
        return false;

    uint64_t magnitude = 0;
    const char* last = s.data() + s.size();
    const auto result = std::from_chars(s.data(), last, magnitude, base);
    if (result.ec != std::errc() || result.ptr != last)
        return false;

    if (!negative)
        return IntegerCast(magnitude, out);

    if (magnitude > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) + 1)
        return false;

    // Two's complement negation, valid for -2^63 too.
    return IntegerCast(static_cast<int64_t>(~magnitude + 1), out);
}

/// Parses a whole string as a floating point number, independently of the global
/// locale: optional sign, decimal or scientific notation, surrounding blanks ignored.
/// False if it is not a number or if it is out of the range of T.
template<typename T>
bool ParseFloating(std::string_view s, T& out)
{
    s = Trim(s);
    if (!s.empty() && s.front() == '+')
    {
        s.remove_prefix(1);
        if (!s.empty() && (s.front() == '+' || s.front() == '-'))
            return false;
    }
    // Hexadecimal floats are refused everywhere (some stream implementations take them).
    if (s.empty() || s.find_first_of("xX") != std::string_view::npos)
        return false;

#if EASYVDF_HAS_FLOAT_CHARCONV
    const char* last = s.data() + s.size();
    const auto result = std::from_chars(s.data(), last, out);
    return result.ec == std::errc() && result.ptr == last;
#else
    std::istringstream is{ std::string(s) };
    is.imbue(std::locale::classic());
    is >> out;
    return !is.fail() && is.peek() == std::istringstream::traits_type::eof();
#endif
}

template<typename T>
void AppendInteger(std::string& out, T value)
{
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
}

/// Appends the shortest text that parses back to exactly the same float, in
/// plain decimal notation ("100000", "0.0001") unless the number is very large
/// or very small ("1e+20", "1.5e-30").
inline void AppendFloat(std::string& out, float value)
{
    const float magnitude = std::fabs(value);
    const bool fixed = value == 0 || (magnitude >= 1e-4f && magnitude < 1e16f);

#if EASYVDF_HAS_FLOAT_CHARCONV
    char buffer[64];
    const auto result = fixed
        ? std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed)
        : std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
#else
    // The first precision whose text reads back as the same float.
    std::string text;
    for (int precision = fixed ? 0 : 6; precision <= (fixed ? 20 : 9); ++precision)
    {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        if (fixed)
            os.setf(std::ios_base::fixed, std::ios_base::floatfield);
        os.precision(precision);
        os << value;
        text = os.str();

        float parsed = 0;
        if (ParseFloating(text, parsed) && parsed == value)
            break;
    }
    out += text;
#endif
}

/// Standard CRC-32 (zlib, polynomial 0xEDB88320), the checksum of VBKV files.
inline uint32_t Crc32Update(uint32_t crc, const char* data, std::size_t size) noexcept
{
    struct Table
    {
        std::array<uint32_t, 256> values{};

        constexpr Table() noexcept
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int bit = 0; bit < 8; ++bit)
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                values[i] = c;
            }
        }
    };
    static constexpr Table table{};

    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i)
        crc = table.values[(crc ^ static_cast<unsigned char>(data[i])) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

inline uint32_t Crc32(std::string_view data) noexcept
{
    return Crc32Update(0, data.data(), data.size());
}

} // namespace Details

/////////////////////////////////////////////////////////////////////
//
//                              Node
//
/////////////////////////////////////////////////////////////////////

/// A value of a KeyValues tree: nothing (Type::None), an object (a list of
/// key/value children, duplicate keys allowed) or a scalar.
///
/// - `node["key"]` goes down the tree. On a const node it never throws: a missing
///   key gives a node without value, so `doc["a"]["b"].value_or(0)` is always safe.
///   On a non-const node it prepares the key for writing, like std::map:
///   `doc["a"]["b"] = 1` creates what is missing. Keys that are only read are
///   never created: they stay invisible (not iterated, not counted, not written).
/// - `as<T>()`, `try_as<T>()` and `value_or(default)` convert the value.
/// - `for (auto& [key, value] : node)` iterates the children of an object.
/// - References to children stay valid until the child is erased.
/// - A moved-from Node has no value (Type::None).
class Node
{
public:
    using iterator = Details::ChildIterator<false>;
    using const_iterator = Details::ChildIterator<true>;

    /// A node without value.
    Node() noexcept = default;
    Node(std::nullptr_t) noexcept {}

    /// A scalar: bool and integers up to 32 bits signed are stored as Int32,
    /// wider signed integers as Int64, 32 and 64 bits unsigned integers as UInt64,
    /// float and double as Float, strings as String.
    template<typename T, std::enable_if_t<Details::IsScalar<T>, int> = 0>
    Node(T&& value)
    {
        Emplace(std::forward<T>(value));
    }

    /// An object: `Node n = { {"key", "value"}, {"other", 5}, {"sub", { {"k", 1.5f} }} };`
    Node(std::initializer_list<KeyValue> children);

    Node(Node const& other) = default;

    Node(Node&& other) noexcept :
        m_Value(std::move(other.m_Value))
    {
        other.m_Value.template emplace<std::monostate>();
    }

    ~Node() = default;

    /// An empty object.
    static Node object()
    {
        Node node;
        node.m_Value.template emplace<Details::Children>();
        return node;
    }

    /// Replaces the value. Strong exception guarantee; other may be a child of *this.
    Node& operator=(Node const& other)
    {
        if (this != &other)
        {
            Node copy(other);
            *this = std::move(copy);
        }
        return *this;
    }

    Node& operator=(Node&& other) noexcept
    {
        if (this != &other)
        {
            // Detach other's value before releasing ours: other may be one of our children.
            Details::Value value(std::move(other.m_Value));
            other.m_Value.template emplace<std::monostate>();
            m_Value = std::move(value);
        }
        return *this;
    }

    /// Replaces the value with a scalar (see the scalar constructor).
    template<typename T, std::enable_if_t<Details::IsScalar<T>, int> = 0>
    Node& operator=(T&& value)
    {
        Node node(std::forward<T>(value));
        m_Value = std::move(node.m_Value);
        return *this;
    }

    /// Same as reset().
    Node& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    /// Removes the value: the node becomes Type::None. A child without value is
    /// invisible in its parent, see erase() to really remove it.
    void reset() noexcept
    {
        m_Value.template emplace<std::monostate>();
    }

    Type type() const noexcept
    {
        return has_value() ? static_cast<Type>(m_Value.index()) : Type::None;
    }

    /// True unless the node is Type::None.
    bool has_value() const noexcept;

    /// Same as has_value(): `if (auto& n = doc["key"]) ...`
    explicit operator bool() const noexcept { return has_value(); }

    bool is_none() const noexcept { return !has_value(); }
    bool is_object() const noexcept { return type() == Type::Object; }
    bool is_string() const noexcept { return type() == Type::String; }

#ifndef EASYVDF_NO_EXCEPTIONS
    /// Converts the value to T: std::string, std::string_view (String nodes only),
    /// bool, any integer or floating point type, Color or Pointer.
    /// Strings are parsed ("42", "0x2A", "1.5", "255 0 0 255"), numbers are range
    /// checked, any scalar converts to std::string.
    /// Throws TypeError when there is no value or no lossless conversion.
    /// Not available with EASYVDF_NO_EXCEPTIONS: use try_as<T>() or value_or().
    template<typename T>
    T as() const
    {
        T result{};
        if (!Convert(result))
        {
            throw TypeError(has_value()
                ? std::string("EasyVDF: can't convert a ") + Details::TypeName(type()) + " value to " + Details::TargetName<T>()
                : std::string("EasyVDF: can't convert a missing value (Type::None) to ") + Details::TargetName<T>());
        }
        return result;
    }
#endif

    /// Like as<T>(), but returns std::nullopt instead of throwing.
    template<typename T>
    std::optional<T> try_as() const
    {
        T result{};
        if (Convert(result))
            return result;
        return std::nullopt;
    }

    /// Like as<T>() with T deduced from the default value, which is returned when
    /// there is no value or no conversion. String defaults give a std::string:
    /// `doc["name"].value_or("unknown")`, `doc["size"].value_or<uint64_t>(0)`.
    template<typename T>
    Details::ValueOrResult<T> value_or(T&& default_value) const
    {
        Details::ValueOrResult<T> result{};
        if (Convert(result))
            return result;
        return Details::ValueOrResult<T>(std::forward<T>(default_value));
    }

    /// Number of children (0 if this is not an object). Linear in the number of children.
    std::size_t size() const noexcept;

    /// True if there is no child.
    bool empty() const noexcept;

    /// First child with this key, or a node without value if there is none (or if
    /// this is not an object). Never throws, never modifies the tree.
    Node const& operator[](std::string_view key) const noexcept;

    /// First child with this key, for reading or writing. A missing key is created
    /// lazily: it only becomes part of the tree once it is given a value.
    /// Throws TypeError if this node holds a scalar (without exceptions: prints the
    /// error and calls std::abort(), test is_object() / is_none() first).
    Node& operator[](std::string_view key);

    /// First child with this key, nullptr if there is none.
    KeyValue const* find(std::string_view key) const noexcept;
    KeyValue* find(std::string_view key) noexcept;

#ifndef EASYVDF_NO_EXCEPTIONS
    /// First child with this key, throws KeyError if there is none.
    Node const& at(std::string_view key) const;
    Node& at(std::string_view key);
#endif

    /// First child with this key, or an ErrorCode::Key error if there is none:
    /// `if (auto appid = doc.try_at("appid")) appid->as<int>();`
    /// The reference is valid as long as the child, like the one at() returns.
    Result<Node const&> try_at(std::string_view key) const;
    Result<Node&> try_at(std::string_view key);

    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    /// Number of children with this key.
    std::size_t count(std::string_view key) const noexcept;

    /// Every child with this key, in order: `for (auto& value : node.all("key"))`.
    /// The iterators of the returned range must not outlive it.
    Details::ChildRange<true> all(std::string_view key) const;
    Details::ChildRange<false> all(std::string_view key);

    /// Appends a child, even if the key already exists (duplicate keys are legal).
    /// A node without value becomes an object; a scalar is a TypeError, like for operator[].
    KeyValue& add(std::string key, Node value);

    /// Appends an empty object.
    KeyValue& add(std::string key);

    /// Inserts a child before pos.
    iterator insert(const_iterator pos, KeyValue child);

    /// Removes the child at pos, returns the iterator to the next one.
    iterator erase(const_iterator pos);

    /// Removes every child with this key, returns how many were removed.
    std::size_t erase(std::string_view key);

    /// Removes every child. An object stays an (empty) object.
    void clear() noexcept;

    /// Stable sort of the children: comp(const KeyValue&, const KeyValue&).
    template<typename Compare>
    void sort(Compare comp);

    /// Children of an object, in order; an empty range for anything else.
    /// The elements are KeyValue: `for (auto& [key, value] : node)`.
    iterator begin();
    iterator end() noexcept;
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
    const_iterator cbegin() const noexcept;
    const_iterator cend() const noexcept;

    /// Deep comparison: same type, same value, same children in the same order.
    friend bool operator==(Node const& l, Node const& r) noexcept;
    friend bool operator!=(Node const& l, Node const& r) noexcept { return !(l == r); }

private:
    Details::Value m_Value;

    static Node const& NullNode() noexcept
    {
        static const Node null;
        return null;
    }

    Details::Children const* ChildrenPtr() const noexcept { return std::get_if<Details::Children>(&m_Value); }
    Details::Children* ChildrenPtr() noexcept { return std::get_if<Details::Children>(&m_Value); }

    // The children of this node, making it an object if it has no value.
    // A scalar has none: Details::Raise() reports a TypeError.
    Details::Children& MakeChildren();

    // Moves the pending children that were given a value to the end of the items.
    static void Promote(Details::Children& children);

    template<typename T>
    void Emplace(T&& value)
    {
        using D = std::decay_t<T>;
        if constexpr (std::is_same_v<D, bool>)
            m_Value.template emplace<int32_t>(value ? 1 : 0);
        else if constexpr (std::is_floating_point_v<D>)
        {
            // Out of the range of a float: infinity, not an undefined conversion.
            if constexpr (std::is_same_v<D, float>)
            {
                m_Value.template emplace<float>(value);
            }
            else
            {
                // Only values in range are converted (NaN goes through unchanged).
                const D limit = static_cast<D>((std::numeric_limits<float>::max)());
                const D clamped = value > limit ? limit : (value < -limit ? -limit : value);
                float narrowed = static_cast<float>(clamped);
                if (value > limit)
                    narrowed = std::numeric_limits<float>::infinity();
                else if (value < -limit)
                    narrowed = -std::numeric_limits<float>::infinity();
                m_Value.template emplace<float>(narrowed);
            }
        }
        else if constexpr (std::is_integral_v<D> && std::is_signed_v<D>)
        {
            if constexpr (sizeof(D) <= sizeof(int32_t))
                m_Value.template emplace<int32_t>(static_cast<int32_t>(value));
            else
                m_Value.template emplace<int64_t>(static_cast<int64_t>(value));
        }
        else if constexpr (std::is_integral_v<D>)
        {
            if constexpr (sizeof(D) < sizeof(int32_t))
                m_Value.template emplace<int32_t>(static_cast<int32_t>(value));
            else
                m_Value.template emplace<uint64_t>(static_cast<uint64_t>(value));
        }
        else if constexpr (std::is_same_v<D, Color> || std::is_same_v<D, Pointer>)
            m_Value.template emplace<D>(value);
        else if constexpr (std::is_same_v<D, std::string>)
            m_Value.template emplace<std::string>(std::forward<T>(value));
        else
            m_Value.template emplace<std::string>(std::string_view(value));
    }

    // Text form of a scalar, false for None and Object.
    bool ToString(std::string& out) const
    {
        switch (type())
        {
            case Type::String : out = *std::get_if<std::string>(&m_Value); return true;
            case Type::Int32  : out.clear(); Details::AppendInteger(out, *std::get_if<int32_t>(&m_Value)); return true;
            case Type::Int64  : out.clear(); Details::AppendInteger(out, *std::get_if<int64_t>(&m_Value)); return true;
            case Type::UInt64 : out.clear(); Details::AppendInteger(out, *std::get_if<uint64_t>(&m_Value)); return true;
            case Type::Pointer: out.clear(); Details::AppendInteger(out, std::get_if<Pointer>(&m_Value)->value); return true;
            case Type::Float  : out.clear(); Details::AppendFloat(out, *std::get_if<float>(&m_Value)); return true;
            case Type::Color:
            {
                const Color color = *std::get_if<Color>(&m_Value);
                out.clear();
                Details::AppendInteger(out, static_cast<unsigned>(color.r)); out += ' ';
                Details::AppendInteger(out, static_cast<unsigned>(color.g)); out += ' ';
                Details::AppendInteger(out, static_cast<unsigned>(color.b)); out += ' ';
                Details::AppendInteger(out, static_cast<unsigned>(color.a));
                return true;
            }
            case Type::None:
            case Type::Object:
                break;
        }
        return false;
    }

    template<typename T>
    bool Convert(T& out) const
    {
        static_assert(std::is_same_v<T, std::decay_t<T>>, "EasyVDF: as<T>() needs a value type");

        const Type t = type();
        if constexpr (std::is_same_v<T, std::string>)
        {
            return ToString(out);
        }
        else if constexpr (std::is_same_v<T, std::string_view>)
        {
            if (t != Type::String)
                return false;
            out = *std::get_if<std::string>(&m_Value);
            return true;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            switch (t)
            {
                case Type::Int32 : out = *std::get_if<int32_t>(&m_Value) != 0; return true;
                case Type::Int64 : out = *std::get_if<int64_t>(&m_Value) != 0; return true;
                case Type::UInt64: out = *std::get_if<uint64_t>(&m_Value) != 0; return true;
                case Type::Float : out = *std::get_if<float>(&m_Value) != 0.f; return true;
                case Type::String:
                {
                    const std::string_view s = Details::Trim(*std::get_if<std::string>(&m_Value));
                    int64_t number = 0;
                    if (Details::ParseInteger(s, number))
                    {
                        out = number != 0;
                        return true;
                    }
                    if (Details::EqualsIgnoreCase(s, "true"))
                    {
                        out = true;
                        return true;
                    }
                    if (Details::EqualsIgnoreCase(s, "false"))
                    {
                        out = false;
                        return true;
                    }
                    return false;
                }
                default: return false;
            }
        }
        else if constexpr (std::is_integral_v<T>)
        {
            static_assert(!Details::IsCharacter<T>, "EasyVDF: as<T>() does not support character types, use an integer or a string");
            switch (t)
            {
                case Type::Int32:
                    // The binary format has no unsigned 32 bits type: Steam stores such
                    // values (appid of a shortcut, ...) as Int32, so the 32 bits unsigned
                    // types get the bit pattern instead of a range error.
                    if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(uint32_t))
                    {
                        out = static_cast<T>(static_cast<uint32_t>(*std::get_if<int32_t>(&m_Value)));
                        return true;
                    }
                    else
                    {
                        return Details::IntegerCast(*std::get_if<int32_t>(&m_Value), out);
                    }
                case Type::Int64  : return Details::IntegerCast(*std::get_if<int64_t>(&m_Value), out);
                case Type::UInt64 : return Details::IntegerCast(*std::get_if<uint64_t>(&m_Value), out);
                case Type::Pointer: return Details::IntegerCast(std::get_if<Pointer>(&m_Value)->value, out);
                case Type::String : return Details::ParseInteger(*std::get_if<std::string>(&m_Value), out);
                case Type::Float:
                {
                    // Only whole numbers convert to an integer.
                    const float f = *std::get_if<float>(&m_Value);
                    if (!(f == std::trunc(f)))
                        return false;
                    if (f < 0)
                        return f >= -9223372036854775808.0f && Details::IntegerCast(static_cast<int64_t>(f), out);
                    return f < 18446744073709551616.0f && Details::IntegerCast(static_cast<uint64_t>(f), out);
                }
                default: return false;
            }
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            switch (t)
            {
                case Type::Int32 : out = static_cast<T>(*std::get_if<int32_t>(&m_Value)); return true;
                case Type::Int64 : out = static_cast<T>(*std::get_if<int64_t>(&m_Value)); return true;
                case Type::UInt64: out = static_cast<T>(*std::get_if<uint64_t>(&m_Value)); return true;
                case Type::Float : out = static_cast<T>(*std::get_if<float>(&m_Value)); return true;
                case Type::String:
                {
                    // Parsed directly as T: going through a double could round twice.
                    std::string const& text = *std::get_if<std::string>(&m_Value);
                    if (Details::ParseFloating(text, out))
                        return true;

                    // Too small for T: zero, like any other rounding. Too big: no conversion.
                    double parsed = 0;
                    if (!Details::ParseFloating(text, parsed) || !(std::fabs(parsed) < 1.0))
                        return false;
                    out = static_cast<T>(parsed);
                    return true;
                }
                default: return false;
            }
        }
        else if constexpr (std::is_same_v<T, Color>)
        {
            if (t == Type::Color)
            {
                out = *std::get_if<Color>(&m_Value);
                return true;
            }
            if (t != Type::String)
                return false;

            // "r g b" or "r g b a"
            std::string_view s = Details::Trim(*std::get_if<std::string>(&m_Value));
            uint8_t channels[4] = { 0, 0, 0, 255 };
            std::size_t count = 0;
            while (!s.empty())
            {
                std::size_t length = 0;
                while (length < s.size() && !Details::IsSpace(s[length]))
                    ++length;
                if (count == 4 || !Details::ParseInteger(s.substr(0, length), channels[count]))
                    return false;
                ++count;
                s = Details::Trim(s.substr(length));
            }
            if (count < 3)
                return false;
            out = Color{ channels[0], channels[1], channels[2], channels[3] };
            return true;
        }
        else if constexpr (std::is_same_v<T, Pointer>)
        {
            if (t == Type::Pointer)
            {
                out = *std::get_if<Pointer>(&m_Value);
                return true;
            }
            return t == Type::String && Details::ParseInteger(*std::get_if<std::string>(&m_Value), out.value);
        }
        else
        {
            static_assert(std::is_same_v<T, std::string>,
                "EasyVDF: unsupported type for as<T>() / try_as<T>() / value_or(): use std::string, std::string_view, bool, an integer or floating point type, Color or Pointer");
            return false;
        }
    }
};

/////////////////////////////////////////////////////////////////////
//
//                      Reading and writing options
//
/////////////////////////////////////////////////////////////////////

/// A `#include "file"` or `#base "file"` line of a text file.
struct Directive
{
    enum class Kind { Include, Base };

    Kind kind;
    std::string path;
};

/// Options of parse() and load(). The defaults read any Steam file.
struct ReadOptions
{
    /// Format::Auto detects the format; Format::Text forces text;
    /// Format::Binary and Format::Vbkv both read any binary file.
    Format format = Format::Auto;
    /// Text: decode escape sequences (\n \t \\ \" ...) in quoted strings.
    bool escapes = true;
    /// VBKV: check the CRC32 of the file, ErrorCode::ChecksumMismatch (ParseError) on mismatch.
    bool verify_crc = true;
    /// Maximum object nesting, the root object counts for 1; deeper input is an
    /// ErrorCode::DepthExceeded ParseError. The parsers themselves are not recursive, but writing, copying,
    /// comparing and destroying a tree use stack space proportional to its depth:
    /// keep this limit reasonable for untrusted input.
    std::size_t max_depth = 128;
    /// Streams are read by blocks of this many bytes.
    std::size_t chunk_size = 16 * 1024;
    /// Text: when set, receives the #include / #base directives of the file,
    /// in order. EasyVDF parses them but never opens the files they name.
    std::vector<Directive>* directives = nullptr;
};

/// Options of KeyValue::dump(), write() and save(). A Format converts implicitly:
/// `doc.save("file.vdf", EasyVDF::Format::Binary)`.
struct WriteOptions
{
    Format format = Format::Text;
    /// Text: encode `\`, `"`, new lines and tabs as escape sequences.
    bool escapes = true;

    WriteOptions() = default;
    WriteOptions(Format f) : format(f) {}
};

/////////////////////////////////////////////////////////////////////
//
//                            KeyValue
//
/////////////////////////////////////////////////////////////////////

/// A Node with its key: the element type of an object, and the type of a whole
/// document (the root key and its object).
///
/// Copying or moving a KeyValue transfers the key with the value. Assigning a
/// Node or a scalar to it only replaces the value: `doc["a"] = doc["b"]` never
/// renames "a". A moved-from KeyValue has an empty key and no value.
class KeyValue : public Node
{
    std::string m_Key;
    std::unique_ptr<std::string> m_Condition;

public:
    /// Empty key, no value.
    KeyValue() noexcept = default;

    /// An empty object: `KeyValue doc("AppState"); doc["appid"] = 440;`
    explicit KeyValue(std::string key) :
        Node(Node::object()), m_Key(std::move(key))
    {}

    /// `KeyValue("appid", 440)`, `KeyValue("UserConfig", { {"language", "english"} })`
    KeyValue(std::string key, Node value) :
        Node(std::move(value)), m_Key(std::move(key))
    {}

    KeyValue(KeyValue const& other) :
        Node(other), m_Key(other.m_Key),
        m_Condition(other.m_Condition ? std::make_unique<std::string>(*other.m_Condition) : nullptr)
    {}

    KeyValue(KeyValue&& other) noexcept :
        Node(std::move(static_cast<Node&>(other))), m_Key(std::move(other.m_Key)), m_Condition(std::move(other.m_Condition))
    {
        other.m_Key.clear();
    }

    KeyValue& operator=(KeyValue const& other)
    {
        if (this != &other)
        {
            KeyValue copy(other);
            *this = std::move(copy);
        }
        return *this;
    }

    KeyValue& operator=(KeyValue&& other) noexcept
    {
        if (this != &other)
        {
            // other may be one of our children: take everything out of it
            // before our current value (and other with it) is released.
            std::string key(std::move(other.m_Key));
            other.m_Key.clear();
            std::unique_ptr<std::string> condition(std::move(other.m_Condition));
            Node::operator=(std::move(static_cast<Node&>(other)));
            m_Key = std::move(key);
            m_Condition = std::move(condition);
        }
        return *this;
    }

    /// Assigning a value keeps the key: `kv = 5;`, `kv = "text";`, `kv = other_node;`
    using Node::operator=;

    std::string const& key() const noexcept { return m_Key; }
    void set_key(std::string key) noexcept { m_Key = std::move(key); }

    /// The conditional of a text entry, without brackets: "$WIN32" for
    /// `"key" "value" [$WIN32]`, empty if there is none. Conditionals are kept and
    /// written back in text, never evaluated, and dropped by the binary formats.
    std::string_view condition() const noexcept { return m_Condition ? std::string_view(*m_Condition) : std::string_view(); }

    void set_condition(std::string_view condition)
    {
        if (condition.empty())
            m_Condition.reset();
        else
            m_Condition = std::make_unique<std::string>(condition);
    }

#ifndef EASYVDF_NO_EXCEPTIONS
    /// Serializes the document. The value must be an object, else WriteError is thrown.
    std::string dump(WriteOptions const& options = {}) const;

    /// dump() as text.
    std::string to_text() const { return dump(Format::Text); }

    /// dump() as binary: Format::Binary or Format::Vbkv.
    std::string to_binary(Format format = Format::Binary) const { return dump(format); }

    /// Writes dump() to a stream (open it in binary mode). The state of the stream
    /// is not checked, test it after the call.
    void write(std::ostream& os, WriteOptions const& options = {}) const;

    /// Writes dump() to a file. Throws IoError if the file can't be written.
    void save(std::filesystem::path const& path, WriteOptions const& options = {}) const;
#endif

    /// The non-throwing versions of dump(), to_text(), to_binary(), write() and save():
    /// ErrorCode::Write when the document can't be written, ErrorCode::Io when the
    /// file can't be. Nothing is written when the document can't be serialized.
    Result<std::string> try_dump(WriteOptions const& options = {}) const;
    Result<std::string> try_to_text() const { return try_dump(Format::Text); }
    Result<std::string> try_to_binary(Format format = Format::Binary) const { return try_dump(format); }
    Result<void> try_write(std::ostream& os, WriteOptions const& options = {}) const;
    Result<void> try_save(std::filesystem::path const& path, WriteOptions const& options = {}) const;

    /// Structured bindings: `auto& [key, value] = kv;` (key is const, value is the Node).
    template<std::size_t I>
    auto& get() & noexcept
    {
        if constexpr (I == 0)
            return std::as_const(m_Key);
        else
            return static_cast<Node&>(*this);
    }

    template<std::size_t I>
    auto& get() const& noexcept
    {
        if constexpr (I == 0)
            return m_Key;
        else
            return static_cast<Node const&>(*this);
    }

    template<std::size_t I>
    auto&& get() && noexcept
    {
        if constexpr (I == 0)
            return std::move(std::as_const(m_Key));
        else
            return static_cast<Node&&>(*this);
    }

    /// Same key, same condition and same value (deep).
    friend bool operator==(KeyValue const& l, KeyValue const& r) noexcept
    {
        return l.m_Key == r.m_Key && l.condition() == r.condition() && static_cast<Node const&>(l) == static_cast<Node const&>(r);
    }

    friend bool operator!=(KeyValue const& l, KeyValue const& r) noexcept { return !(l == r); }
};

#ifndef EASYVDF_NO_EXCEPTIONS
/// Parses a document held in memory (text or binary, see ReadOptions::format).
/// A document is one root key and its object: anything after it is an error,
/// see parse_all() for the files that hold several. Throws ParseError.
KeyValue parse(std::string_view data, ReadOptions const& options = {});

/// Parses a document from a stream, read sequentially to its end: pipes and
/// std::cin work. Open files in binary mode. Throws ParseError, or IoError if
/// the stream reports a read error.
KeyValue parse(std::istream& is, ReadOptions const& options = {});

/// Parses a file. Throws IoError if it can't be opened, ParseError if it is malformed.
KeyValue load(std::filesystem::path const& path, ReadOptions const& options = {});

/// Like parse() and load() for text files that hold several root objects one
/// after the other, such as the signed Steam files (installscript.vdf: the
/// "InstallScript" object is followed by a "kvsignatures" one). Returns every
/// root object, in order, at least one.
std::vector<KeyValue> parse_all(std::string_view data, ReadOptions const& options = {});
std::vector<KeyValue> parse_all(std::istream& is, ReadOptions const& options = {});
std::vector<KeyValue> load_all(std::filesystem::path const& path, ReadOptions const& options = {});
#endif

/// The non-throwing versions of parse(), load(), parse_all() and load_all(): the
/// error that they throw is returned instead, with the same message.
/// `if (auto doc = EasyVDF::try_load(path)) use(*doc); else report(doc.error());`
Result<KeyValue> try_parse(std::string_view data, ReadOptions const& options = {});
Result<KeyValue> try_parse(std::istream& is, ReadOptions const& options = {});
Result<KeyValue> try_load(std::filesystem::path const& path, ReadOptions const& options = {});
Result<std::vector<KeyValue>> try_parse_all(std::string_view data, ReadOptions const& options = {});
Result<std::vector<KeyValue>> try_parse_all(std::istream& is, ReadOptions const& options = {});
Result<std::vector<KeyValue>> try_load_all(std::filesystem::path const& path, ReadOptions const& options = {});

/// Prints a value: the text of a scalar (`std::cout << doc["name"]`), the
/// `{ ... }` block of an object, nothing for a node without value.
std::ostream& operator<<(std::ostream& os, Node const& node);

/// Prints an entry as text: a whole document, or `"key"  "value"` for a scalar.
std::ostream& operator<<(std::ostream& os, KeyValue const& entry);

/////////////////////////////////////////////////////////////////////
//
//                       Iterators and ranges
//
/////////////////////////////////////////////////////////////////////
namespace Details {

inline Children::Children() noexcept = default;
inline Children::Children(Children&&) noexcept = default;
inline Children& Children::operator=(Children&&) noexcept = default;
inline Children::~Children() = default;

inline Children::Children(Children const& other) :
    implicit(false)
{
    // Children without value are not part of the tree: they are not copied.
    std::size_t count = 0;
    for (std::size_t i = 0; i < other.slots(); ++i)
        count += other.slot(i)->has_value() ? 1 : 0;

    items.reserve(count);
    for (std::size_t i = 0; i < other.slots(); ++i)
    {
        if (other.slot(i)->has_value())
            items.push_back(std::make_unique<KeyValue>(*other.slot(i)));
    }
    // An object with children exists for good; one without stays as it was.
    implicit = other.implicit && items.empty();
}

inline Children& Children::operator=(Children const& other)
{
    if (this != &other)
    {
        Children copy(other);
        *this = std::move(copy);
    }
    return *this;
}

inline KeyValue* Children::slot(std::size_t i) const noexcept
{
    return i < items.size() ? items[i].get() : pending[i - items.size()].get();
}

/// Forward iterator over the children of an object that have a value,
/// optionally restricted to one key. Appending children (add(), assigning to a
/// new key) keeps it valid; erasing or inserting children does not, use the
/// iterator returned by Node::erase() / Node::insert().
template<bool Const>
class ChildIterator
{
    static constexpr std::size_t End = static_cast<std::size_t>(-1);

    Children const* m_Children = nullptr;
    std::size_t m_Index = End;
    std::string const* m_Key = nullptr;

    friend class ::EasyVDF::Node;
    template<bool> friend class ChildIterator;
    template<bool> friend class ChildRange;

    ChildIterator(Children const* children, std::size_t index, std::string const* key) noexcept :
        m_Children(children), m_Index(index), m_Key(key)
    {
        Settle();
    }

    // Moves forward to the first visible child at or after m_Index.
    void Settle() noexcept
    {
        if (m_Children == nullptr)
        {
            m_Index = End;
            return;
        }
        for (; m_Index != End; ++m_Index)
        {
            if (m_Index >= m_Children->slots())
            {
                m_Index = End;
                return;
            }
            KeyValue const* child = m_Children->slot(m_Index);
            if (child->has_value() && (m_Key == nullptr || child->key() == *m_Key))
                return;
        }
    }

public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = KeyValue;
    using difference_type = std::ptrdiff_t;
    using reference = std::conditional_t<Const, KeyValue const&, KeyValue&>;
    using pointer = std::conditional_t<Const, KeyValue const*, KeyValue*>;

    ChildIterator() noexcept = default;

    /// iterator converts to const_iterator.
    template<bool OtherConst, std::enable_if_t<Const && !OtherConst, int> = 0>
    ChildIterator(ChildIterator<OtherConst> const& other) noexcept :
        m_Children(other.m_Children), m_Index(other.m_Index), m_Key(other.m_Key)
    {}

    reference operator*() const noexcept { return *m_Children->slot(m_Index); }
    pointer operator->() const noexcept { return m_Children->slot(m_Index); }

    ChildIterator& operator++() noexcept
    {
        ++m_Index;
        Settle();
        return *this;
    }

    ChildIterator operator++(int) noexcept
    {
        ChildIterator previous(*this);
        ++*this;
        return previous;
    }

    friend bool operator==(ChildIterator const& l, ChildIterator const& r) noexcept { return l.m_Index == r.m_Index; }
    friend bool operator!=(ChildIterator const& l, ChildIterator const& r) noexcept { return l.m_Index != r.m_Index; }
};

/// The children of an object that share a key, see Node::all().
template<bool Const>
class ChildRange
{
    Children const* m_Children;
    std::string m_Key;

    friend class ::EasyVDF::Node;

    ChildRange(Children const* children, std::string_view key) :
        m_Children(children), m_Key(key)
    {}

public:
    using iterator = ChildIterator<Const>;

    iterator begin() const noexcept { return iterator(m_Children, 0, &m_Key); }
    iterator end() const noexcept { return iterator(); }
    bool empty() const noexcept { return begin() == end(); }
    std::size_t size() const noexcept { return static_cast<std::size_t>(std::distance(begin(), end())); }

    /// The first element; the range must not be empty.
    typename iterator::reference front() const noexcept { return *begin(); }
};

} // namespace Details

/////////////////////////////////////////////////////////////////////
//
//                      Node implementation
//
/////////////////////////////////////////////////////////////////////

inline Node::Node(std::initializer_list<KeyValue> children)
{
    Details::Children& c = m_Value.template emplace<Details::Children>();
    c.items.reserve(children.size());
    for (KeyValue const& child : children)
        c.items.push_back(std::make_unique<KeyValue>(child));
}

inline bool Node::has_value() const noexcept
{
    switch (m_Value.index())
    {
        case 0:
            return false;

        case 1:
        {
            Details::Children const& c = *ChildrenPtr();
            if (!c.implicit)
                return true;

            for (std::size_t i = 0; i < c.slots(); ++i)
            {
                if (c.slot(i)->has_value())
                    return true;
            }
            return false;
        }

        default:
            return true;
    }
}

inline std::size_t Node::size() const noexcept
{
    return static_cast<std::size_t>(std::distance(begin(), end()));
}

inline bool Node::empty() const noexcept
{
    return begin() == end();
}

inline KeyValue const* Node::find(std::string_view key) const noexcept
{
    if (Details::Children const* c = ChildrenPtr())
    {
        for (std::size_t i = 0; i < c->slots(); ++i)
        {
            KeyValue const* child = c->slot(i);
            if (child->key() == key && child->has_value())
                return child;
        }
    }
    return nullptr;
}

inline KeyValue* Node::find(std::string_view key) noexcept
{
    return const_cast<KeyValue*>(std::as_const(*this).find(key));
}

inline Node const& Node::operator[](std::string_view key) const noexcept
{
    KeyValue const* child = find(key);
    return child != nullptr ? static_cast<Node const&>(*child) : NullNode();
}

inline Details::Children& Node::MakeChildren()
{
    if (m_Value.index() == 0)
    {
        Details::Children& c = m_Value.template emplace<Details::Children>();
        c.implicit = true;
        return c;
    }
    if (Details::Children* c = ChildrenPtr())
        return *c;

    Details::Raise(ErrorInfo(ErrorCode::Type, std::string("EasyVDF: a ") + Details::TypeName(type()) + " value has no children"));
}

inline void Node::Promote(Details::Children& children)
{
    for (auto it = children.pending.begin(); it != children.pending.end();)
    {
        if ((*it)->has_value())
        {
            children.items.push_back(std::move(*it));
            it = children.pending.erase(it);
            children.implicit = false;
        }
        else
        {
            ++it;
        }
    }
}

inline Node& Node::operator[](std::string_view key)
{
    Details::Children& c = MakeChildren();
    Promote(c);

    for (std::size_t i = 0; i < c.slots(); ++i)
    {
        KeyValue* child = c.slot(i);
        if (child->key() == key)
            return *child;
    }

    // Not there: keep a child without value aside. It joins the tree when it is given one.
    auto child = std::make_unique<KeyValue>();
    child->set_key(std::string(key));
    c.pending.push_back(std::move(child));
    return *c.pending.back();
}

namespace Details {

inline ErrorInfo KeyNotFound(std::string_view key)
{
    return ErrorInfo(ErrorCode::Key, "EasyVDF: key \"" + std::string(key) + "\" not found");
}

} // namespace Details

inline Result<Node const&> Node::try_at(std::string_view key) const
{
    if (KeyValue const* child = find(key))
        return static_cast<Node const&>(*child);
    return Details::KeyNotFound(key);
}

inline Result<Node&> Node::try_at(std::string_view key)
{
    if (KeyValue* child = find(key))
        return static_cast<Node&>(*child);
    return Details::KeyNotFound(key);
}

#ifndef EASYVDF_NO_EXCEPTIONS
inline Node const& Node::at(std::string_view key) const
{
    return try_at(key).value();
}

inline Node& Node::at(std::string_view key)
{
    return try_at(key).value();
}
#endif

inline std::size_t Node::count(std::string_view key) const noexcept
{
    std::size_t result = 0;
    if (Details::Children const* c = ChildrenPtr())
    {
        for (std::size_t i = 0; i < c->slots(); ++i)
        {
            KeyValue const* child = c->slot(i);
            result += (child->key() == key && child->has_value()) ? 1 : 0;
        }
    }
    return result;
}

inline Details::ChildRange<true> Node::all(std::string_view key) const
{
    return Details::ChildRange<true>(ChildrenPtr(), key);
}

inline Details::ChildRange<false> Node::all(std::string_view key)
{
    if (Details::Children* c = ChildrenPtr())
        Promote(*c);
    return Details::ChildRange<false>(ChildrenPtr(), key);
}

inline KeyValue& Node::add(std::string key, Node value)
{
    auto child = std::make_unique<KeyValue>(std::move(key), std::move(value));
    Details::Children& c = MakeChildren();
    Promote(c);
    c.items.push_back(std::move(child));
    c.implicit = false;
    return *c.items.back();
}

inline KeyValue& Node::add(std::string key)
{
    return add(std::move(key), Node::object());
}

inline Node::iterator Node::insert(const_iterator pos, KeyValue child)
{
    auto owned = std::make_unique<KeyValue>(std::move(child));
    Details::Children& c = MakeChildren();
    Promote(c);
    const std::size_t index = (std::min)(pos.m_Index, c.items.size());
    c.items.insert(c.items.begin() + static_cast<std::ptrdiff_t>(index), std::move(owned));
    c.implicit = false;
    return iterator(&c, index, nullptr);
}

inline Node::iterator Node::erase(const_iterator pos)
{
    Details::Children* c = ChildrenPtr();
    if (c == nullptr || pos.m_Index >= c->slots())
        return end();

    // An object that exists keeps existing once emptied.
    if (has_value())
        c->implicit = false;

    if (pos.m_Index < c->items.size())
        c->items.erase(c->items.begin() + static_cast<std::ptrdiff_t>(pos.m_Index));
    else
        c->pending.erase(c->pending.begin() + static_cast<std::ptrdiff_t>(pos.m_Index - c->items.size()));

    return iterator(c, pos.m_Index, pos.m_Key);
}

inline std::size_t Node::erase(std::string_view key)
{
    std::size_t erased = 0;
    if (Details::Children* c = ChildrenPtr())
    {
        // key may point into a child that is about to be destroyed.
        const std::string owned_key(key);
        // An object that exists keeps existing once emptied.
        if (has_value())
            c->implicit = false;
        auto matches = [&](std::unique_ptr<KeyValue> const& child)
        {
            if (child->key() != owned_key)
                return false;
            erased += child->has_value() ? 1 : 0;
            return true;
        };
        c->items.erase(std::remove_if(c->items.begin(), c->items.end(), matches), c->items.end());
        c->pending.erase(std::remove_if(c->pending.begin(), c->pending.end(), matches), c->pending.end());
    }
    return erased;
}

inline void Node::clear() noexcept
{
    if (Details::Children* c = ChildrenPtr())
    {
        if (has_value())
            c->implicit = false;
        c->items.clear();
        c->pending.clear();
    }
}

template<typename Compare>
void Node::sort(Compare comp)
{
    if (Details::Children* c = ChildrenPtr())
    {
        Promote(*c);
        std::stable_sort(c->items.begin(), c->items.end(),
            [&comp](std::unique_ptr<KeyValue> const& l, std::unique_ptr<KeyValue> const& r)
            {
                return comp(std::as_const(*l), std::as_const(*r));
            });
    }
}

inline Node::iterator Node::begin()
{
    if (Details::Children* c = ChildrenPtr())
        Promote(*c);
    return iterator(ChildrenPtr(), 0, nullptr);
}

inline Node::iterator Node::end() noexcept { return iterator(); }
inline Node::const_iterator Node::begin() const noexcept { return const_iterator(ChildrenPtr(), 0, nullptr); }
inline Node::const_iterator Node::end() const noexcept { return const_iterator(); }
inline Node::const_iterator Node::cbegin() const noexcept { return begin(); }
inline Node::const_iterator Node::cend() const noexcept { return end(); }

inline bool operator==(Node const& l, Node const& r) noexcept
{
    const Type type = l.type();
    if (type != r.type())
        return false;

    switch (type)
    {
        case Type::None:
            return true;

        case Type::Object:
        {
            auto li = l.begin(), le = l.end();
            auto ri = r.begin(), re = r.end();
            for (; li != le && ri != re; ++li, ++ri)
            {
                if (!(*li == *ri))
                    return false;
            }
            return li == le && ri == re;
        }

        case Type::String : return *std::get_if<std::string>(&l.m_Value) == *std::get_if<std::string>(&r.m_Value);
        case Type::Int32  : return *std::get_if<int32_t>(&l.m_Value) == *std::get_if<int32_t>(&r.m_Value);
        case Type::Float  : return *std::get_if<float>(&l.m_Value) == *std::get_if<float>(&r.m_Value);
        case Type::Pointer: return *std::get_if<Pointer>(&l.m_Value) == *std::get_if<Pointer>(&r.m_Value);
        case Type::Color  : return *std::get_if<Color>(&l.m_Value) == *std::get_if<Color>(&r.m_Value);
        case Type::UInt64 : return *std::get_if<uint64_t>(&l.m_Value) == *std::get_if<uint64_t>(&r.m_Value);
        case Type::Int64  : return *std::get_if<int64_t>(&l.m_Value) == *std::get_if<int64_t>(&r.m_Value);
    }
    return false;
}

/////////////////////////////////////////////////////////////////////
//
//                         Parsers, writers
//
/////////////////////////////////////////////////////////////////////
namespace Details {

inline constexpr char VbkvMagic[4] = { 'V', 'B', 'K', 'V' };

enum BinaryNodeType : unsigned char
{
    BinaryObject       = 0,
    BinaryString       = 1,
    BinaryInt32        = 2,
    BinaryFloat        = 3,
    BinaryPointer      = 4,
    BinaryWideString   = 5,
    BinaryColor        = 6,
    BinaryUInt64       = 7,
    BinaryEnd          = 8,
    BinaryBlob         = 9,
    BinaryInt64        = 10,
    BinaryAlternateEnd = 11,
};

/// Sequential byte source over a memory range or a stream read by chunks.
/// Never seeks, so it works on pipes.
class Reader
{
    std::istream* m_Stream = nullptr;
    std::string m_Buffer;
    const char* m_Current = nullptr;
    const char* m_End = nullptr;

    // Bytes given back with Unread(), served before the rest of the input.
    std::string m_Pushback;
    const char* m_SavedCurrent = nullptr;
    const char* m_SavedEnd = nullptr;
    bool m_InPushback = false;

    bool m_CrcEnabled = false;
    uint32_t m_Crc = 0;
    const char* m_CrcMark = nullptr;

    bool m_ReadFailed = false;

    bool Fill()
    {
        if (m_InPushback)
        {
            m_InPushback = false;
            m_Current = m_SavedCurrent;
            m_End = m_SavedEnd;
            if (m_Current != m_End)
                return true;
        }

        if (m_Stream == nullptr)
            return false;

        if (m_CrcEnabled)
            m_Crc = Crc32Update(m_Crc, m_CrcMark, static_cast<std::size_t>(m_Current - m_CrcMark));

        m_Stream->read(&m_Buffer[0], static_cast<std::streamsize>(m_Buffer.size()));
        // The end of the stream is expected, a read error is not: it ends the input
        // here, and ReadFailed() tells the caller to report it instead of the result.
        if (m_Stream->bad())
        {
            m_ReadFailed = true;
            m_Stream = nullptr;
            m_Current = m_End = m_Buffer.data();
            m_CrcMark = m_Current;
            return false;
        }

        m_Current = m_Buffer.data();
        m_End = m_Current + m_Stream->gcount();
        m_CrcMark = m_Current;
        return m_Current != m_End;
    }

public:
    explicit Reader(std::string_view data) noexcept :
        m_Current(data.data()), m_End(data.data() + data.size())
    {}

    Reader(std::istream& is, std::size_t chunk_size) :
        m_Stream(&is), m_Buffer((std::max)(chunk_size, std::size_t(1)), '\0')
    {
        m_Current = m_End = m_Buffer.data();
    }

    Reader(Reader const&) = delete;
    Reader& operator=(Reader const&) = delete;

    /// True once the stream reported a read error; the input looks ended from then on.
    bool ReadFailed() const noexcept { return m_ReadFailed; }

    /// Next byte without consuming it, -1 at the end of the input.
    int Peek()
    {
        if (m_Current == m_End && !Fill())
            return -1;
        return static_cast<unsigned char>(*m_Current);
    }

    /// Next byte, -1 at the end of the input.
    int Get()
    {
        const int c = Peek();
        if (c >= 0)
            ++m_Current;
        return c;
    }

    /// Bytes available without reading more; call Peek() first to refill.
    const char* Data() const noexcept { return m_Current; }
    std::size_t Available() const noexcept { return static_cast<std::size_t>(m_End - m_Current); }
    void Advance(std::size_t count) noexcept { m_Current += count; }

    /// Gives back bytes that were consumed: they are the next ones read.
    void Unread(std::string_view bytes)
    {
        if (bytes.empty())
            return;

        m_Pushback.assign(bytes);
        m_SavedCurrent = m_Current;
        m_SavedEnd = m_End;
        m_Current = m_Pushback.data();
        m_End = m_Current + m_Pushback.size();
        m_InPushback = true;
    }

    /// Starts computing the CRC32 of every byte consumed from now on.
    void StartCrc() noexcept
    {
        m_CrcEnabled = true;
        m_Crc = 0;
        m_CrcMark = m_Current;
    }

    uint32_t Crc() const noexcept
    {
        return Crc32Update(m_Crc, m_CrcMark, static_cast<std::size_t>(m_Current - m_CrcMark));
    }
};

inline uint32_t LoadLittleEndian32(const char* p) noexcept
{
    const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
    return static_cast<uint32_t>(u[0])
         | (static_cast<uint32_t>(u[1]) << 8)
         | (static_cast<uint32_t>(u[2]) << 16)
         | (static_cast<uint32_t>(u[3]) << 24);
}

inline uint64_t LoadLittleEndian64(const char* p) noexcept
{
    return static_cast<uint64_t>(LoadLittleEndian32(p)) | (static_cast<uint64_t>(LoadLittleEndian32(p + 4)) << 32);
}

inline void AppendLittleEndian32(std::string& out, uint32_t value)
{
    const char bytes[4] = {
        static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu), static_cast<char>((value >> 24) & 0xFFu),
    };
    out.append(bytes, sizeof(bytes));
}

inline void AppendLittleEndian64(std::string& out, uint64_t value)
{
    AppendLittleEndian32(out, static_cast<uint32_t>(value & 0xFFFFFFFFu));
    AppendLittleEndian32(out, static_cast<uint32_t>(value >> 32));
}

template<typename To, typename From>
To BitCast(From value) noexcept
{
    static_assert(sizeof(To) == sizeof(From), "BitCast requires types of the same size");
    To result;
    std::memcpy(&result, &value, sizeof(To));
    return result;
}

inline std::string Hex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4)
        text += digits[(value >> shift) & 0xFu];
    return text;
}

/////////////////////////////////////////////////////////////////////
// Binary parser
/////////////////////////////////////////////////////////////////////
class BinaryParser
{
    Reader& m_Reader;
    ReadOptions const& m_Options;
    std::string m_Scratch;
    std::optional<ErrorInfo> m_Error;

    // Records the error, returns false: `return Fail(...);`
    bool Fail(ErrorCode code, std::string message)
    {
        m_Error.emplace(code, std::move(message));
        return false;
    }

    // Reads a null terminated string; a binary string is an opaque byte sequence.
    bool ReadString(std::string& out)
    {
        out.clear();
        while (m_Reader.Peek() >= 0)
        {
            const char* data = m_Reader.Data();
            const std::size_t available = m_Reader.Available();
            const char* terminator = static_cast<const char*>(std::memchr(data, '\0', available));
            if (terminator != nullptr)
            {
                out.append(data, terminator);
                m_Reader.Advance(static_cast<std::size_t>(terminator - data) + 1);
                return true;
            }
            out.append(data, available);
            m_Reader.Advance(available);
        }
        return false;
    }

    bool ReadBytes(char* out, std::size_t count)
    {
        while (count > 0)
        {
            if (m_Reader.Peek() < 0)
                return false;

            const std::size_t n = (std::min)(count, m_Reader.Available());
            std::memcpy(out, m_Reader.Data(), n);
            m_Reader.Advance(n);
            out += n;
            count -= n;
        }
        return true;
    }

    // Parses the children of root, down to the end marker of root. Not recursive:
    // the objects being parsed are kept in a stack, nested input can't overflow ours.
    bool ParseObjects(KeyValue& root)
    {
        std::vector<KeyValue*> open{ &root };
        std::string key;
        while (!open.empty())
        {
            KeyValue& object = *open.back();
            std::string const& name = object.key();

            const int type = m_Reader.Get();
            if (type < 0)
                return Fail(ErrorCode::UnexpectedEof, "Premature end of file: missing end marker of binary object \"" + name + "\"");

            switch (type)
            {
                case BinaryEnd:
                case BinaryAlternateEnd:
                    // Steam writes 0x08 and Source 2 writes 0x0B; neither is a
                    // node type of the other, so both always end an object.
                    open.pop_back();
                    continue;

                case BinaryObject:
                case BinaryString:
                case BinaryInt32:
                case BinaryFloat:
                case BinaryPointer:
                case BinaryColor:
                case BinaryUInt64:
                case BinaryInt64:
                    break;

                case BinaryWideString:
                case BinaryBlob:
                    // The known implementations disagree on the encoding of these
                    // two types and Valve's public code does not read them.
                    return Fail(ErrorCode::UnsupportedNodeType, "Unsupported binary VDF node type " + std::to_string(type)
                        + (type == BinaryWideString ? " (WideString)" : " (binary blob)") + " in binary object \"" + name + "\"");

                default:
                    return Fail(ErrorCode::UnknownNodeType, "Unknown binary VDF node type " + std::to_string(type) + " in binary object \"" + name + "\"");
            }

            if (!ReadString(key))
                return Fail(ErrorCode::UnexpectedEof, "Premature end of file while parsing a key of binary object \"" + name + "\"");

            char raw[8];
            switch (type)
            {
                case BinaryObject:
                    if (open.size() >= m_Options.max_depth)
                        return Fail(ErrorCode::DepthExceeded, "Binary object \"" + key + "\" is nested deeper than the limit of " + std::to_string(m_Options.max_depth) + " levels");

                    open.push_back(&object.add(std::move(key)));
                    break;

                case BinaryString:
                    if (!ReadString(m_Scratch))
                        return Fail(ErrorCode::UnexpectedEof, "Premature end of file while parsing the value of key \"" + key + "\"");

                    object.add(std::move(key), Node(m_Scratch));
                    break;

                default:
                {
                    const std::size_t size = (type == BinaryInt64 || type == BinaryUInt64) ? 8 : 4;
                    if (!ReadBytes(raw, size))
                        return Fail(ErrorCode::UnexpectedEof, "Premature end of file while parsing the value of key \"" + key + "\"");

                    switch (type)
                    {
                        case BinaryInt32  : object.add(std::move(key), Node(BitCast<int32_t>(LoadLittleEndian32(raw)))); break;
                        case BinaryFloat  : object.add(std::move(key), Node(BitCast<float>(LoadLittleEndian32(raw)))); break;
                        case BinaryPointer: object.add(std::move(key), Node(Pointer{ LoadLittleEndian32(raw) })); break;
                        case BinaryColor:
                            object.add(std::move(key), Node(Color{ static_cast<uint8_t>(raw[0]), static_cast<uint8_t>(raw[1]), static_cast<uint8_t>(raw[2]), static_cast<uint8_t>(raw[3]) }));
                            break;
                        case BinaryInt64  : object.add(std::move(key), Node(BitCast<int64_t>(LoadLittleEndian64(raw)))); break;
                        default           : object.add(std::move(key), Node(LoadLittleEndian64(raw))); break;
                    }
                }
            }
        }
        return true;
    }

public:
    BinaryParser(Reader& reader, ReadOptions const& options) :
        m_Reader(reader), m_Options(options)
    {}

    /// vbkv: the "VBKV" magic was already consumed.
    Result<KeyValue> Parse(bool vbkv)
    {
        uint32_t stored_crc = 0;
        if (vbkv)
        {
            char raw[4];
            if (!ReadBytes(raw, 4))
                return ErrorInfo(ErrorCode::UnexpectedEof, "Premature end of file while reading the VBKV header");

            stored_crc = LoadLittleEndian32(raw);
            m_Reader.StartCrc();
        }

        const int root_type = m_Reader.Get();
        if (root_type < 0)
            return ErrorInfo(ErrorCode::UnexpectedEof, "Premature end of file while parsing root binary object");
        if (root_type != BinaryObject)
            return ErrorInfo(ErrorCode::InvalidRoot, "Binary root item type is not an object");

        std::string name;
        if (!ReadString(name))
            return ErrorInfo(ErrorCode::UnexpectedEof, "Premature end of file while parsing root binary object name");

        KeyValue root(std::move(name));
        if (!ParseObjects(root))
            return std::move(*m_Error);

        // Only one root object is allowed. Valve binary files commonly end with one
        // extra end marker after the root object end: accept a single one.
        const int root_end = vbkv ? BinaryAlternateEnd : BinaryEnd;
        const int trailing = m_Reader.Get();
        if (trailing >= 0 && ((trailing != BinaryEnd && trailing != root_end) || m_Reader.Peek() >= 0))
            return ErrorInfo(ErrorCode::TrailingData, "Unexpected data after the binary root object");

        if (vbkv && m_Options.verify_crc)
        {
            const uint32_t computed_crc = m_Reader.Crc();
            if (computed_crc != stored_crc)
                return ErrorInfo(ErrorCode::ChecksumMismatch, "VBKV checksum mismatch: the header says " + Hex32(stored_crc) + ", the data gives " + Hex32(computed_crc));
        }

        return root;
    }
};

/////////////////////////////////////////////////////////////////////
// Text parser
/////////////////////////////////////////////////////////////////////
class TextParser
{
    enum class TokenKind { End, Open, Close, String, Condition };

    struct Token
    {
        TokenKind kind = TokenKind::End;
        std::string text;
        std::size_t line = 0;
        std::size_t column = 0;
    };

    Reader& m_Reader;
    ReadOptions const& m_Options;
    std::size_t m_Line = 1;
    std::size_t m_Column = 1;
    bool m_LastWasCr = false;
    Token m_Ahead;
    bool m_HasAhead = false;
    std::optional<ErrorInfo> m_Error;

    // Consumes one byte and tracks the position. LF, CRLF and CR all end a line.
    int Get()
    {
        const int c = m_Reader.Get();
        if (c == '\n')
        {
            if (!m_LastWasCr)
            {
                ++m_Line;
                m_Column = 1;
            }
            m_LastWasCr = false;
        }
        else if (c == '\r')
        {
            ++m_Line;
            m_Column = 1;
            m_LastWasCr = true;
        }
        else if (c >= 0)
        {
            ++m_Column;
            m_LastWasCr = false;
        }
        return c;
    }

    // Record the error and return false: `return Fail(...);`
    bool Fail(ErrorCode code, std::string const& message, std::size_t line, std::size_t column)
    {
        m_Error.emplace(code, message + " at line " + std::to_string(line) + ", column " + std::to_string(column), line, column);
        return false;
    }

    bool Fail(ErrorCode code, std::string const& message, Token const& token)
    {
        return Fail(code, message, token.line, token.column);
    }

    // The end of file is reported on the last line that holds at least one character.
    bool FailAtEnd(ErrorCode code, std::string const& message)
    {
        const std::size_t line = (m_Column == 1 && m_Line > 1) ? m_Line - 1 : m_Line;
        m_Error.emplace(code, "Unexpected end of file, " + message + " at line " + std::to_string(line), line, 0);
        return false;
    }

    static bool EndsUnquoted(int c) noexcept
    {
        return c < 0 || c == '"' || c == '{' || c == '}' || IsSpace(static_cast<char>(c));
    }

    bool ReadQuoted(Token& token)
    {
        for (;;)
        {
            int c = Get();
            if (c < 0)
                return Fail(ErrorCode::UnexpectedEof, "Unexpected end of file in the quoted string started", token);

            if (c == '"')
                return true;

            if (c == '\\' && m_Options.escapes)
            {
                c = Get();
                if (c < 0)
                    return Fail(ErrorCode::UnexpectedEof, "Unexpected end of file in the quoted string started", token);

                switch (c)
                {
                    case 'n' : token.text += '\n'; break;
                    case 't' : token.text += '\t'; break;
                    case 'v' : token.text += '\v'; break;
                    case 'b' : token.text += '\b'; break;
                    case 'r' : token.text += '\r'; break;
                    case 'f' : token.text += '\f'; break;
                    case 'a' : token.text += '\a'; break;
                    case '\\': token.text += '\\'; break;
                    case '?' : token.text += '?'; break;
                    case '\'': token.text += '\''; break;
                    case '"' : token.text += '"'; break;
                    default:
                        // Not an escape sequence (unescaped Windows path, ...): keep it verbatim.
                        token.text += '\\';
                        token.text += static_cast<char>(c);
                        break;
                }
                continue;
            }

            // Strings are opaque bytes: UTF-8 is not validated, new lines are kept.
            token.text += static_cast<char>(c);
        }
    }

    bool Next(Token& token)
    {
        if (m_HasAhead)
        {
            token = std::move(m_Ahead);
            m_HasAhead = false;
            return true;
        }

        token.text.clear();
        for (;;)
        {
            int c = m_Reader.Peek();
            if (c >= 0 && IsSpace(static_cast<char>(c)))
            {
                Get();
                continue;
            }

            token.line = m_Line;
            token.column = m_Column;

            if (c < 0)
            {
                token.kind = TokenKind::End;
                return true;
            }

            Get();
            switch (c)
            {
                case '{': token.kind = TokenKind::Open; return true;
                case '}': token.kind = TokenKind::Close; return true;

                case '"':
                    token.kind = TokenKind::String;
                    return ReadQuoted(token);

                case '[':
                    token.kind = TokenKind::Condition;
                    for (;;)
                    {
                        c = m_Reader.Peek();
                        if (c < 0 || c == '\n' || c == '\r')
                            return Fail(c < 0 ? ErrorCode::UnexpectedEof : ErrorCode::Syntax,
                                "Unterminated conditional, expected ']' on the same line as the '['", token);

                        Get();
                        if (c == ']')
                            return true;
                        token.text += static_cast<char>(c);
                    }

                case '/':
                    if (m_Reader.Peek() == '/')
                    {// Comment: skip to the end of the line.
                        for (c = m_Reader.Peek(); c >= 0 && c != '\n' && c != '\r'; c = m_Reader.Peek())
                            Get();
                        continue;
                    }
                    break;

                default:
                    break;
            }

            // Unquoted token: ends at a blank, a quote or a brace. No escape sequences.
            token.kind = TokenKind::String;
            token.text += static_cast<char>(c);
            while (!EndsUnquoted(m_Reader.Peek()))
                token.text += static_cast<char>(Get());
            return true;
        }
    }

    void PutBack(Token&& token)
    {
        m_Ahead = std::move(token);
        m_HasAhead = true;
    }

    // Parses the members of root, down to its closing brace. Not recursive: the
    // objects being parsed are kept in a stack, nested input can't overflow ours.
    bool ParseMembers(KeyValue& root)
    {
        std::vector<KeyValue*> open{ &root };
        Token key, value;
        while (!open.empty())
        {
            KeyValue& object = *open.back();

            if (!Next(key))
                return false;
            if (key.kind == TokenKind::End)
                return FailAtEnd(ErrorCode::UnexpectedEof, "expected object end ('}')");
            if (key.kind == TokenKind::Close)
            {
                open.pop_back();
                continue;
            }
            if (key.kind != TokenKind::String)
                return Fail(ErrorCode::Syntax, "Expected a key or an object end ('}')", key);

            if (!Next(value))
                return false;
            std::string condition;
            const bool has_condition = value.kind == TokenKind::Condition;
            if (has_condition)
            {// "key" [$COND] { ... }
                condition = std::move(value.text);
                if (!Next(value))
                    return false;
            }

            switch (value.kind)
            {
                case TokenKind::Open:
                {
                    if (open.size() >= m_Options.max_depth)
                        return Fail(ErrorCode::DepthExceeded, "Objects are nested deeper than the limit of " + std::to_string(m_Options.max_depth) + " levels", value);

                    KeyValue& child = object.add(std::move(key.text));
                    child.set_condition(condition);
                    open.push_back(&child);
                    break;
                }

                case TokenKind::String:
                {
                    KeyValue& child = object.add(std::move(key.text), Node(std::move(value.text)));
                    if (has_condition)
                    {
                        child.set_condition(condition);
                    }
                    else
                    {// "key" "value" [$COND]
                        if (!Next(value))
                            return false;
                        if (value.kind == TokenKind::Condition)
                            child.set_condition(value.text);
                        else
                            PutBack(std::move(value));
                    }
                    break;
                }

                case TokenKind::End:
                    return FailAtEnd(ErrorCode::UnexpectedEof, "expected a value or an object start ('{') after key \"" + key.text + "\"");

                default:
                    return Fail(ErrorCode::Syntax, "Expected a value or an object start ('{') after key \"" + key.text + "\"", value);
            }
        }
        return true;
    }

    static bool IsDirective(Token const& token, Directive::Kind& kind) noexcept
    {
        if (token.kind != TokenKind::String)
            return false;
        if (EqualsIgnoreCase(token.text, "#include"))
        {
            kind = Directive::Kind::Include;
            return true;
        }
        if (EqualsIgnoreCase(token.text, "#base"))
        {
            kind = Directive::Kind::Base;
            return true;
        }
        return false;
    }

    bool ParseRoots(bool multiple, std::vector<KeyValue>& roots)
    {
        Token token, next;
        for (;;)
        {
            if (!Next(token))
                return false;
            if (token.kind == TokenKind::End)
                break;

            if (!Next(next))
                return false;

            // #include "file" / #base "file": only meaningful next to the root object.
            Directive::Kind directive_kind = Directive::Kind::Include;
            if (IsDirective(token, directive_kind) && next.kind == TokenKind::String)
            {
                if (m_Options.directives != nullptr)
                    m_Options.directives->push_back(Directive{ directive_kind, std::move(next.text) });
                continue;
            }

            if (!roots.empty() && !multiple)
            {
                m_Error.emplace(ErrorCode::TrailingData,
                    "Unexpected data after root object at line " + std::to_string(token.line) + ", column " + std::to_string(token.column)
                    + (token.kind == TokenKind::String && next.kind == TokenKind::Open ? ": the input holds several root objects, parse_all() / load_all() read them" : ""),
                    token.line, token.column);
                return false;
            }
            if (token.kind != TokenKind::String)
                return Fail(ErrorCode::Syntax, "Expected the root key", token);

            roots.emplace_back(std::move(token.text));
            KeyValue& root = roots.back();
            if (next.kind == TokenKind::Condition)
            {
                root.set_condition(next.text);
                if (!Next(next))
                    return false;
            }
            if (next.kind == TokenKind::End)
                return FailAtEnd(ErrorCode::UnexpectedEof, "expected object start ('{') after the root key");
            if (next.kind != TokenKind::Open)
                return Fail(ErrorCode::Syntax, "Expected object start ('{') after the root key", next);

            if (!ParseMembers(root))
                return false;
        }

        if (roots.empty())
            return FailAtEnd(ErrorCode::EmptyInput, "expected the root key (the input holds no object)");

        return true;
    }

public:
    TextParser(Reader& reader, ReadOptions const& options) :
        m_Reader(reader), m_Options(options)
    {}

    /// Returns the root objects: exactly one, or one and more when multiple is set.
    Result<std::vector<KeyValue>> Parse(bool multiple)
    {
        std::vector<KeyValue> roots;
        if (!ParseRoots(multiple, roots))
            return std::move(*m_Error);
        return roots;
    }
};

/// Detects the format and parses the input. multiple: accept several root
/// objects (text only, a binary document always holds one).
inline Result<std::vector<KeyValue>> ParseInput(Reader& reader, ReadOptions const& options, bool multiple)
{
    auto single = [](Result<KeyValue>&& root) -> Result<std::vector<KeyValue>>
    {
        if (!root)
            return std::move(root).error();

        std::vector<KeyValue> roots;
        roots.push_back(std::move(*root));
        return roots;
    };

    const int first = reader.Peek();
    if (first < 0)
        return ErrorInfo(ErrorCode::EmptyInput, "Empty input: expected a text or binary VDF document");

    if (options.format != Format::Text)
    {
        // "VBKV" header? The bytes read while checking are given back if it is not.
        std::string seen;
        while (seen.size() < sizeof(VbkvMagic) && reader.Peek() == static_cast<unsigned char>(VbkvMagic[seen.size()]))
            seen += static_cast<char>(reader.Get());

        if (seen.size() == sizeof(VbkvMagic))
            return single(BinaryParser(reader, options).Parse(true));

        reader.Unread(seen);
        // A binary document starts with the type of its root: an object (0x00).
        if (first == BinaryObject || options.format != Format::Auto)
            return single(BinaryParser(reader, options).Parse(false));
    }

    // Text: skip an UTF-8 byte order mark, reject UTF-16.
    if (first == 0xEF)
    {
        static constexpr unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        std::string seen;
        while (seen.size() < sizeof(bom) && reader.Peek() == bom[seen.size()])
            seen += static_cast<char>(reader.Get());

        if (seen.size() != sizeof(bom))
            reader.Unread(seen);
    }
    else if (first == 0xFF || first == 0xFE)
    {
        const int a = reader.Get();
        const int b = reader.Peek();
        if ((a == 0xFF && b == 0xFE) || (a == 0xFE && b == 0xFF))
            return ErrorInfo(ErrorCode::UnsupportedEncoding, "UTF-16 text is not supported, convert the file to UTF-8", 1, 1);

        reader.Unread(std::string(1, static_cast<char>(a)));
    }

    return TextParser(reader, options).Parse(multiple);
}

/// ParseInput(), with the read errors of a stream: the input ends where the
/// stream failed, whatever was parsed up to there is replaced by an Io error.
inline Result<std::vector<KeyValue>> ParseDocument(Reader& reader, ReadOptions const& options, bool multiple)
{
    Result<std::vector<KeyValue>> result = ParseInput(reader, options, multiple);
    if (reader.ReadFailed())
        return ErrorInfo(ErrorCode::Io, "EasyVDF: failed to read the input stream");
    return result;
}

/// One root object: the single root of a document.
inline Result<KeyValue> ParseSingle(Reader& reader, ReadOptions const& options)
{
    Result<std::vector<KeyValue>> roots = ParseDocument(reader, options, false);
    if (!roots)
        return std::move(roots).error();
    return std::move(roots->front());
}

/////////////////////////////////////////////////////////////////////
// Writers
/////////////////////////////////////////////////////////////////////
// The writers return false when the tree can't be written: a '"' in a text
// string with escape sequences disabled, a null character in a binary string.
inline constexpr const char* QuoteWithoutEscapesError = "EasyVDF: can't write a string containing '\"' with escape sequences disabled";
inline constexpr const char* NullInBinaryError = "EasyVDF: can't write a key or a string containing a null character in binary";

[[nodiscard]] inline bool AppendQuoted(std::string& out, std::string_view text, bool escapes)
{
    out += '"';
    if (!escapes)
    {
        if (text.find('"') != std::string_view::npos)
            return false;
        out += text;
    }
    else
    {
        for (char c : text)
        {
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"' : out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                default  : out += c; break;
            }
        }
    }
    out += '"';
    return true;
}

[[nodiscard]] inline bool WriteText(std::string& out, KeyValue const& item, std::size_t depth, bool escapes);

[[nodiscard]] inline bool WriteTextBlock(std::string& out, Node const& object, std::size_t depth, bool escapes)
{
    out.append(depth, '\t');
    out += "{\n";
    for (KeyValue const& child : object)
    {
        if (!WriteText(out, child, depth + 1, escapes))
            return false;
    }
    out.append(depth, '\t');
    out += "}\n";
    return true;
}

inline bool WriteText(std::string& out, KeyValue const& item, std::size_t depth, bool escapes)
{
    out.append(depth, '\t');
    if (!AppendQuoted(out, item.key(), escapes))
        return false;

    if (item.is_object())
    {
        if (!item.condition().empty())
        {
            out += "\t\t[";
            out += item.condition();
            out += ']';
        }
        out += '\n';
        return WriteTextBlock(out, item, depth, escapes);
    }

    out += "\t\t";
    // Every scalar converts to a string.
    if (!AppendQuoted(out, item.value_or(std::string()), escapes))
        return false;
    if (!item.condition().empty())
    {
        out += "\t\t[";
        out += item.condition();
        out += ']';
    }
    out += '\n';
    return true;
}

[[nodiscard]] inline bool AppendBinaryString(std::string& out, std::string_view text)
{
    if (text.find('\0') != std::string_view::npos)
        return false;

    out += text;
    out += '\0';
    return true;
}

// The values are read with try_as<T>() on a node of the matching type: they always convert.
[[nodiscard]] inline bool WriteBinary(std::string& out, KeyValue const& item, char object_end)
{
    switch (item.type())
    {
        case Type::Object:
            out += static_cast<char>(BinaryObject);
            if (!AppendBinaryString(out, item.key()))
                return false;
            for (KeyValue const& child : item)
            {
                if (!WriteBinary(out, child, object_end))
                    return false;
            }
            out += object_end;
            return true;

        case Type::String:
            out += static_cast<char>(BinaryString);
            return AppendBinaryString(out, item.key())
                && AppendBinaryString(out, item.try_as<std::string_view>().value_or(std::string_view()));

        case Type::Int32:
            out += static_cast<char>(BinaryInt32);
            if (!AppendBinaryString(out, item.key()))
                return false;
            AppendLittleEndian32(out, BitCast<uint32_t>(item.try_as<int32_t>().value_or(0)));
            return true;

        case Type::Float:
            out += static_cast<char>(BinaryFloat);
            if (!AppendBinaryString(out, item.key()))
                return false;
            AppendLittleEndian32(out, BitCast<uint32_t>(item.try_as<float>().value_or(0.f)));
            return true;

        case Type::Pointer:
            out += static_cast<char>(BinaryPointer);
            if (!AppendBinaryString(out, item.key()))
                return false;
            AppendLittleEndian32(out, item.try_as<Pointer>().value_or(Pointer()).value);
            return true;

        case Type::Color:
        {
            const Color color = item.try_as<Color>().value_or(Color());
            out += static_cast<char>(BinaryColor);
            if (!AppendBinaryString(out, item.key()))
                return false;
            out += static_cast<char>(color.r);
            out += static_cast<char>(color.g);
            out += static_cast<char>(color.b);
            out += static_cast<char>(color.a);
            return true;
        }

        case Type::UInt64:
            out += static_cast<char>(BinaryUInt64);
            if (!AppendBinaryString(out, item.key()))
                return false;
            AppendLittleEndian64(out, item.try_as<uint64_t>().value_or(0));
            return true;

        case Type::Int64:
            out += static_cast<char>(BinaryInt64);
            if (!AppendBinaryString(out, item.key()))
                return false;
            AppendLittleEndian64(out, BitCast<uint64_t>(item.try_as<int64_t>().value_or(0)));
            return true;

        case Type::None:
            // Children without value are never iterated; only a root can get here.
            break;
    }
    return true;
}

} // namespace Details

/////////////////////////////////////////////////////////////////////
//
//                    KeyValue and free functions
//
/////////////////////////////////////////////////////////////////////

namespace Details {

/// A path for an error message. Never fails for a path that the narrow
/// character set can't represent (Windows).
inline std::string PathText(std::filesystem::path const& path)
{
#ifndef EASYVDF_NO_EXCEPTIONS
    try
    {
        return path.string();
    }
    catch (std::exception const&)
    {
        return "<path>";
    }
#else
    // path.string() can't report a conversion error without exceptions: UTF-8 always converts.
    std::string text;
    for (auto c : path.u8string())
        text += static_cast<char>(c);
    return text;
#endif
}

inline ErrorInfo OpenForReadingError(std::filesystem::path const& path)
{
    return ErrorInfo(ErrorCode::Io, "EasyVDF: can't open \"" + PathText(path) + "\" for reading");
}

} // namespace Details

inline Result<std::string> KeyValue::try_dump(WriteOptions const& options) const
{
    // VDF has no representation for a key without a value, and a document is an object.
    if (!has_value())
        return ErrorInfo(ErrorCode::Write, "EasyVDF: can't write a document without value (Type::None)");
    if (!is_object())
        return ErrorInfo(ErrorCode::Write, std::string("EasyVDF: can't write a document whose root is a ") + Details::TypeName(type()) + ", it must be an object");

    std::string out;
    switch (options.format)
    {
        case Format::Auto:
        case Format::Text:
            if (!Details::WriteText(out, *this, 0, options.escapes))
                return ErrorInfo(ErrorCode::Write, Details::QuoteWithoutEscapesError);
            break;

        // Valve writes the root as a list of peer objects that is closed by one more end
        // marker: Steam files end with 08 08 (stats schemas, shortcuts.vdf) and VBKV files
        // with 0B 0B (Dota 2 remote cfg). Write the same so files round-trip byte for byte.
        case Format::Binary:
            if (!Details::WriteBinary(out, *this, static_cast<char>(Details::BinaryEnd)))
                return ErrorInfo(ErrorCode::Write, Details::NullInBinaryError);
            out.push_back(static_cast<char>(Details::BinaryEnd));
            break;

        case Format::Vbkv:
        {
            out.append(Details::VbkvMagic, sizeof(Details::VbkvMagic));
            out.append(4, '\0');
            if (!Details::WriteBinary(out, *this, static_cast<char>(Details::BinaryAlternateEnd)))
                return ErrorInfo(ErrorCode::Write, Details::NullInBinaryError);
            out.push_back(static_cast<char>(Details::BinaryAlternateEnd));

            // CRC32 of everything after the 8 bytes header (end markers included), little-endian.
            std::string crc;
            Details::AppendLittleEndian32(crc, Details::Crc32(std::string_view(out).substr(8)));
            out.replace(4, 4, crc);
            break;
        }
    }
    return out;
}

inline Result<void> KeyValue::try_write(std::ostream& os, WriteOptions const& options) const
{
    Result<std::string> data = try_dump(options);
    if (!data)
        return std::move(data).error();

    os.write(data->data(), static_cast<std::streamsize>(data->size()));
    return {};
}

inline Result<void> KeyValue::try_save(std::filesystem::path const& path, WriteOptions const& options) const
{
    // Serialize first: nothing is created or truncated if the tree can't be written.
    Result<std::string> data = try_dump(options);
    if (!data)
        return std::move(data).error();

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return ErrorInfo(ErrorCode::Io, "EasyVDF: can't open \"" + Details::PathText(path) + "\" for writing");

    file.write(data->data(), static_cast<std::streamsize>(data->size()));
    file.close();
    if (!file)
        return ErrorInfo(ErrorCode::Io, "EasyVDF: failed to write \"" + Details::PathText(path) + "\"");
    return {};
}

inline Result<KeyValue> try_parse(std::string_view data, ReadOptions const& options)
{
    Details::Reader reader(data);
    return Details::ParseSingle(reader, options);
}

inline Result<KeyValue> try_parse(std::istream& is, ReadOptions const& options)
{
    Details::Reader reader(is, options.chunk_size);
    return Details::ParseSingle(reader, options);
}

inline Result<KeyValue> try_load(std::filesystem::path const& path, ReadOptions const& options)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return Details::OpenForReadingError(path);

    return try_parse(file, options);
}

inline Result<std::vector<KeyValue>> try_parse_all(std::string_view data, ReadOptions const& options)
{
    Details::Reader reader(data);
    return Details::ParseDocument(reader, options, true);
}

inline Result<std::vector<KeyValue>> try_parse_all(std::istream& is, ReadOptions const& options)
{
    Details::Reader reader(is, options.chunk_size);
    return Details::ParseDocument(reader, options, true);
}

inline Result<std::vector<KeyValue>> try_load_all(std::filesystem::path const& path, ReadOptions const& options)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return Details::OpenForReadingError(path);

    return try_parse_all(file, options);
}

#ifndef EASYVDF_NO_EXCEPTIONS
// The throwing API: the try_* functions, with the error thrown by Result::value().

inline std::string KeyValue::dump(WriteOptions const& options) const
{
    return try_dump(options).value();
}

inline void KeyValue::write(std::ostream& os, WriteOptions const& options) const
{
    try_write(os, options).value();
}

inline void KeyValue::save(std::filesystem::path const& path, WriteOptions const& options) const
{
    try_save(path, options).value();
}

inline KeyValue parse(std::string_view data, ReadOptions const& options)
{
    return try_parse(data, options).value();
}

inline KeyValue parse(std::istream& is, ReadOptions const& options)
{
    return try_parse(is, options).value();
}

inline KeyValue load(std::filesystem::path const& path, ReadOptions const& options)
{
    return try_load(path, options).value();
}

inline std::vector<KeyValue> parse_all(std::string_view data, ReadOptions const& options)
{
    return try_parse_all(data, options).value();
}

inline std::vector<KeyValue> parse_all(std::istream& is, ReadOptions const& options)
{
    return try_parse_all(is, options).value();
}

inline std::vector<KeyValue> load_all(std::filesystem::path const& path, ReadOptions const& options)
{
    return try_load_all(path, options).value();
}
#endif // EASYVDF_NO_EXCEPTIONS

inline std::ostream& operator<<(std::ostream& os, Node const& node)
{
    // Escape sequences are on: writing can't fail.
    std::string text;
    if (node.is_object())
        static_cast<void>(Details::WriteTextBlock(text, node, 0, true));
    else
        text = node.value_or(std::string());

    return os.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline std::ostream& operator<<(std::ostream& os, KeyValue const& entry)
{
    std::string text;
    if (entry.has_value())
        static_cast<void>(Details::WriteText(text, entry, 0, true));

    return os.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace EasyVDF

/// Structured bindings support: `auto& [key, value] = key_value;`
namespace std {

template<>
struct tuple_size<::EasyVDF::KeyValue> : std::integral_constant<std::size_t, 2> {};

template<>
struct tuple_element<0, ::EasyVDF::KeyValue> { using type = const std::string; };

template<>
struct tuple_element<1, ::EasyVDF::KeyValue> { using type = ::EasyVDF::Node; };

} // namespace std
