// EasyVDF - single-header C++17 reader/writer for Valve KeyValues (VDF) files,
// text and binary. See README.md for an overview and examples.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
//                          Exceptions
//
/////////////////////////////////////////////////////////////////////

/// Base class of every exception thrown by EasyVDF.
class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// Malformed input. line() and column() are 1-based and 0 when unknown (binary input).
class ParseError : public Error
{
    std::size_t m_Line;
    std::size_t m_Column;

public:
    explicit ParseError(std::string const& message, std::size_t line = 0, std::size_t column = 0) :
        Error(message), m_Line(line), m_Column(column)
    {}

    std::size_t line() const noexcept { return m_Line; }
    std::size_t column() const noexcept { return m_Column; }
};

/// A value has no conversion to the requested type, or a non-object was used as an object.
class TypeError : public Error
{
public:
    using Error::Error;
};

/// Node::at() did not find the key.
class KeyError : public Error
{
public:
    using Error::Error;
};

/// The tree can't be written (root without value or not an object, ...).
class WriteError : public Error
{
public:
    using Error::Error;
};

/// A file could not be opened, read or written.
class IoError : public Error
{
public:
    using Error::Error;
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

    /// Converts the value to T: std::string, std::string_view (String nodes only),
    /// bool, any integer or floating point type, Color or Pointer.
    /// Strings are parsed ("42", "0x2A", "1.5", "255 0 0 255"), numbers are range
    /// checked, any scalar converts to std::string.
    /// Throws TypeError when there is no value or no lossless conversion.
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
    /// Throws TypeError if this node holds a scalar.
    Node& operator[](std::string_view key);

    /// First child with this key, nullptr if there is none.
    KeyValue const* find(std::string_view key) const noexcept;
    KeyValue* find(std::string_view key) noexcept;

    /// First child with this key, throws KeyError if there is none.
    Node const& at(std::string_view key) const;
    Node& at(std::string_view key);

    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    /// Number of children with this key.
    std::size_t count(std::string_view key) const noexcept;

    /// Every child with this key, in order: `for (auto& value : node.all("key"))`.
    /// The iterators of the returned range must not outlive it.
    Details::ChildRange<true> all(std::string_view key) const;
    Details::ChildRange<false> all(std::string_view key);

    /// Appends a child, even if the key already exists (duplicate keys are legal).
    /// A node without value becomes an object; throws TypeError on a scalar.
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
    /// VBKV: check the CRC32 of the file, throw ParseError on mismatch.
    bool verify_crc = true;
    /// Maximum object nesting, the root object counts for 1; deeper input is a
    /// ParseError. The parsers themselves are not recursive, but writing, copying,
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

    /// Serializes the document. The value must be an object, else WriteError is thrown.
    std::string dump(WriteOptions const& options = {}) const;

    /// dump() as text.
    std::string to_text() const { return dump(Format::Text); }

    /// dump() as binary: Format::Binary or Format::Vbkv.
    std::string to_binary(Format format = Format::Binary) const { return dump(format); }

    /// Writes dump() to a stream (open it in binary mode).
    void write(std::ostream& os, WriteOptions const& options = {}) const;

    /// Writes dump() to a file. Throws IoError if the file can't be written.
    void save(std::filesystem::path const& path, WriteOptions const& options = {}) const;

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

/// Parses a document held in memory (text or binary, see ReadOptions::format).
/// A document is one root key and its object: anything after it is an error,
/// see parse_all() for the files that hold several. Throws ParseError.
KeyValue parse(std::string_view data, ReadOptions const& options = {});

/// Parses a document from a stream, read sequentially to its end: pipes and
/// std::cin work. Open files in binary mode. Throws ParseError.
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

    throw TypeError(std::string("EasyVDF: a ") + Details::TypeName(type()) + " value has no children");
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

inline Node const& Node::at(std::string_view key) const
{
    if (KeyValue const* child = find(key))
        return *child;

    throw KeyError("EasyVDF: key \"" + std::string(key) + "\" not found");
}

inline Node& Node::at(std::string_view key)
{
    return const_cast<Node&>(std::as_const(*this).at(key));
}

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
        // The end of the stream is expected, a read error is not.
        if (m_Stream->bad())
            throw IoError("EasyVDF: failed to read the input stream");

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
    void ParseObjects(KeyValue& root)
    {
        std::vector<KeyValue*> open{ &root };
        std::string key;
        while (!open.empty())
        {
            KeyValue& object = *open.back();
            std::string const& name = object.key();

            const int type = m_Reader.Get();
            if (type < 0)
                throw ParseError("Premature end of file: missing end marker of binary object \"" + name + "\"");

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
                    throw ParseError("Unsupported binary VDF node type " + std::to_string(type)
                        + (type == BinaryWideString ? " (WideString)" : " (binary blob)") + " in binary object \"" + name + "\"");

                default:
                    throw ParseError("Unknown binary VDF node type " + std::to_string(type) + " in binary object \"" + name + "\"");
            }

            if (!ReadString(key))
                throw ParseError("Premature end of file while parsing a key of binary object \"" + name + "\"");

            char raw[8];
            switch (type)
            {
                case BinaryObject:
                    if (open.size() >= m_Options.max_depth)
                        throw ParseError("Binary object \"" + key + "\" is nested deeper than the limit of " + std::to_string(m_Options.max_depth) + " levels");

                    open.push_back(&object.add(std::move(key)));
                    break;

                case BinaryString:
                    if (!ReadString(m_Scratch))
                        throw ParseError("Premature end of file while parsing the value of key \"" + key + "\"");

                    object.add(std::move(key), Node(m_Scratch));
                    break;

                default:
                {
                    const std::size_t size = (type == BinaryInt64 || type == BinaryUInt64) ? 8 : 4;
                    if (!ReadBytes(raw, size))
                        throw ParseError("Premature end of file while parsing the value of key \"" + key + "\"");

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
    }

public:
    BinaryParser(Reader& reader, ReadOptions const& options) :
        m_Reader(reader), m_Options(options)
    {}

    /// vbkv: the "VBKV" magic was already consumed.
    KeyValue Parse(bool vbkv)
    {
        uint32_t stored_crc = 0;
        if (vbkv)
        {
            char raw[4];
            if (!ReadBytes(raw, 4))
                throw ParseError("Premature end of file while reading the VBKV header");

            stored_crc = LoadLittleEndian32(raw);
            m_Reader.StartCrc();
        }

        const int root_type = m_Reader.Get();
        if (root_type < 0)
            throw ParseError("Premature end of file while parsing root binary object");
        if (root_type != BinaryObject)
            throw ParseError("Binary root item type is not an object");

        std::string name;
        if (!ReadString(name))
            throw ParseError("Premature end of file while parsing root binary object name");

        KeyValue root(std::move(name));
        ParseObjects(root);

        // Only one root object is allowed. Valve binary files commonly end with one
        // extra end marker after the root object end: accept a single one.
        const int root_end = vbkv ? BinaryAlternateEnd : BinaryEnd;
        const int trailing = m_Reader.Get();
        if (trailing >= 0 && ((trailing != BinaryEnd && trailing != root_end) || m_Reader.Peek() >= 0))
            throw ParseError("Unexpected data after the binary root object");

        if (vbkv && m_Options.verify_crc)
        {
            const uint32_t computed_crc = m_Reader.Crc();
            if (computed_crc != stored_crc)
                throw ParseError("VBKV checksum mismatch: the header says " + Hex32(stored_crc) + ", the data gives " + Hex32(computed_crc));
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

    [[noreturn]] void Fail(std::string const& message, std::size_t line, std::size_t column) const
    {
        throw ParseError(message + " at line " + std::to_string(line) + ", column " + std::to_string(column), line, column);
    }

    [[noreturn]] void Fail(std::string const& message, Token const& token) const
    {
        Fail(message, token.line, token.column);
    }

    // The end of file is reported on the last line that holds at least one character.
    [[noreturn]] void FailAtEnd(std::string const& message) const
    {
        const std::size_t line = (m_Column == 1 && m_Line > 1) ? m_Line - 1 : m_Line;
        throw ParseError("Unexpected end of file, " + message + " at line " + std::to_string(line), line, 0);
    }

    static bool EndsUnquoted(int c) noexcept
    {
        return c < 0 || c == '"' || c == '{' || c == '}' || IsSpace(static_cast<char>(c));
    }

    void ReadQuoted(Token& token)
    {
        for (;;)
        {
            int c = Get();
            if (c < 0)
                Fail("Unexpected end of file in the quoted string started", token);

            if (c == '"')
                return;

            if (c == '\\' && m_Options.escapes)
            {
                c = Get();
                if (c < 0)
                    Fail("Unexpected end of file in the quoted string started", token);

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

    void Next(Token& token)
    {
        if (m_HasAhead)
        {
            token = std::move(m_Ahead);
            m_HasAhead = false;
            return;
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
                return;
            }

            Get();
            switch (c)
            {
                case '{': token.kind = TokenKind::Open; return;
                case '}': token.kind = TokenKind::Close; return;

                case '"':
                    token.kind = TokenKind::String;
                    ReadQuoted(token);
                    return;

                case '[':
                    token.kind = TokenKind::Condition;
                    for (;;)
                    {
                        c = m_Reader.Peek();
                        if (c < 0 || c == '\n' || c == '\r')
                            Fail("Unterminated conditional, expected ']' on the same line as the '['", token);

                        Get();
                        if (c == ']')
                            return;
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
            return;
        }
    }

    void PutBack(Token&& token)
    {
        m_Ahead = std::move(token);
        m_HasAhead = true;
    }

    // Parses the members of root, down to its closing brace. Not recursive: the
    // objects being parsed are kept in a stack, nested input can't overflow ours.
    void ParseMembers(KeyValue& root)
    {
        std::vector<KeyValue*> open{ &root };
        Token key, value;
        while (!open.empty())
        {
            KeyValue& object = *open.back();

            Next(key);
            if (key.kind == TokenKind::End)
                FailAtEnd("expected object end ('}')");
            if (key.kind == TokenKind::Close)
            {
                open.pop_back();
                continue;
            }
            if (key.kind != TokenKind::String)
                Fail("Expected a key or an object end ('}')", key);

            Next(value);
            std::string condition;
            const bool has_condition = value.kind == TokenKind::Condition;
            if (has_condition)
            {// "key" [$COND] { ... }
                condition = std::move(value.text);
                Next(value);
            }

            switch (value.kind)
            {
                case TokenKind::Open:
                {
                    if (open.size() >= m_Options.max_depth)
                        Fail("Objects are nested deeper than the limit of " + std::to_string(m_Options.max_depth) + " levels", value);

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
                        Next(value);
                        if (value.kind == TokenKind::Condition)
                            child.set_condition(value.text);
                        else
                            PutBack(std::move(value));
                    }
                    break;
                }

                case TokenKind::End:
                    FailAtEnd("expected a value or an object start ('{') after key \"" + key.text + "\"");

                default:
                    Fail("Expected a value or an object start ('{') after key \"" + key.text + "\"", value);
            }
        }
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

public:
    TextParser(Reader& reader, ReadOptions const& options) :
        m_Reader(reader), m_Options(options)
    {}

    /// Returns the root objects: exactly one, or one and more when multiple is set.
    std::vector<KeyValue> Parse(bool multiple)
    {
        std::vector<KeyValue> roots;
        Token token, next;

        for (;;)
        {
            Next(token);
            if (token.kind == TokenKind::End)
                break;

            Next(next);

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
                throw ParseError("Unexpected data after root object at line " + std::to_string(token.line) + ", column " + std::to_string(token.column)
                    + (token.kind == TokenKind::String && next.kind == TokenKind::Open ? ": the input holds several root objects, parse_all() / load_all() read them" : ""),
                    token.line, token.column);
            }
            if (token.kind != TokenKind::String)
                Fail("Expected the root key", token);

            roots.emplace_back(std::move(token.text));
            KeyValue& root = roots.back();
            if (next.kind == TokenKind::Condition)
            {
                root.set_condition(next.text);
                Next(next);
            }
            if (next.kind == TokenKind::End)
                FailAtEnd("expected object start ('{') after the root key");
            if (next.kind != TokenKind::Open)
                Fail("Expected object start ('{') after the root key", next);

            ParseMembers(root);
        }

        if (roots.empty())
            FailAtEnd("expected the root key (the input holds no object)");

        return roots;
    }
};

/// Detects the format and parses the input. multiple: accept several root
/// objects (text only, a binary document always holds one).
inline std::vector<KeyValue> ParseDocument(Reader& reader, ReadOptions const& options, bool multiple)
{
    auto single = [](KeyValue&& root)
    {
        std::vector<KeyValue> roots;
        roots.push_back(std::move(root));
        return roots;
    };

    const int first = reader.Peek();
    if (first < 0)
        throw ParseError("Empty input: expected a text or binary VDF document");

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
            throw ParseError("UTF-16 text is not supported, convert the file to UTF-8", 1, 1);

        reader.Unread(std::string(1, static_cast<char>(a)));
    }

    return TextParser(reader, options).Parse(multiple);
}

/////////////////////////////////////////////////////////////////////
// Writers
/////////////////////////////////////////////////////////////////////
inline void AppendQuoted(std::string& out, std::string_view text, bool escapes)
{
    out += '"';
    if (!escapes)
    {
        if (text.find('"') != std::string_view::npos)
            throw WriteError("EasyVDF: can't write a string containing '\"' with escape sequences disabled");
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
}

inline void WriteText(std::string& out, KeyValue const& item, std::size_t depth, bool escapes);

inline void WriteTextBlock(std::string& out, Node const& object, std::size_t depth, bool escapes)
{
    out.append(depth, '\t');
    out += "{\n";
    for (KeyValue const& child : object)
        WriteText(out, child, depth + 1, escapes);
    out.append(depth, '\t');
    out += "}\n";
}

inline void WriteText(std::string& out, KeyValue const& item, std::size_t depth, bool escapes)
{
    out.append(depth, '\t');
    AppendQuoted(out, item.key(), escapes);

    if (item.is_object())
    {
        if (!item.condition().empty())
        {
            out += "\t\t[";
            out += item.condition();
            out += ']';
        }
        out += '\n';
        WriteTextBlock(out, item, depth, escapes);
        return;
    }

    out += "\t\t";
    AppendQuoted(out, item.as<std::string>(), escapes);
    if (!item.condition().empty())
    {
        out += "\t\t[";
        out += item.condition();
        out += ']';
    }
    out += '\n';
}

inline void AppendBinaryString(std::string& out, std::string_view text)
{
    if (text.find('\0') != std::string_view::npos)
        throw WriteError("EasyVDF: can't write a key or a string containing a null character in binary");

    out += text;
    out += '\0';
}

inline void WriteBinary(std::string& out, KeyValue const& item, char object_end)
{
    switch (item.type())
    {
        case Type::Object:
            out += static_cast<char>(BinaryObject);
            AppendBinaryString(out, item.key());
            for (KeyValue const& child : item)
                WriteBinary(out, child, object_end);
            out += object_end;
            break;

        case Type::String:
            out += static_cast<char>(BinaryString);
            AppendBinaryString(out, item.key());
            AppendBinaryString(out, item.as<std::string_view>());
            break;

        case Type::Int32:
            out += static_cast<char>(BinaryInt32);
            AppendBinaryString(out, item.key());
            AppendLittleEndian32(out, BitCast<uint32_t>(item.as<int32_t>()));
            break;

        case Type::Float:
            out += static_cast<char>(BinaryFloat);
            AppendBinaryString(out, item.key());
            AppendLittleEndian32(out, BitCast<uint32_t>(item.as<float>()));
            break;

        case Type::Pointer:
            out += static_cast<char>(BinaryPointer);
            AppendBinaryString(out, item.key());
            AppendLittleEndian32(out, item.as<Pointer>().value);
            break;

        case Type::Color:
        {
            const Color color = item.as<Color>();
            out += static_cast<char>(BinaryColor);
            AppendBinaryString(out, item.key());
            out += static_cast<char>(color.r);
            out += static_cast<char>(color.g);
            out += static_cast<char>(color.b);
            out += static_cast<char>(color.a);
            break;
        }

        case Type::UInt64:
            out += static_cast<char>(BinaryUInt64);
            AppendBinaryString(out, item.key());
            AppendLittleEndian64(out, item.as<uint64_t>());
            break;

        case Type::Int64:
            out += static_cast<char>(BinaryInt64);
            AppendBinaryString(out, item.key());
            AppendLittleEndian64(out, BitCast<uint64_t>(item.as<int64_t>()));
            break;

        case Type::None:
            // Children without value are never iterated; only a root can get here.
            break;
    }
}

} // namespace Details

/////////////////////////////////////////////////////////////////////
//
//                    KeyValue and free functions
//
/////////////////////////////////////////////////////////////////////

namespace Details {

/// A path for an error message. Never throws for a path that the narrow
/// character set can't represent (Windows).
inline std::string PathText(std::filesystem::path const& path)
{
    try
    {
        return path.string();
    }
    catch (std::exception const&)
    {
        return "<path>";
    }
}

} // namespace Details

inline std::string KeyValue::dump(WriteOptions const& options) const
{
    // VDF has no representation for a key without a value, and a document is an object.
    if (!has_value())
        throw WriteError("EasyVDF: can't write a document without value (Type::None)");
    if (!is_object())
        throw WriteError(std::string("EasyVDF: can't write a document whose root is a ") + Details::TypeName(type()) + ", it must be an object");

    std::string out;
    switch (options.format)
    {
        case Format::Auto:
        case Format::Text:
            Details::WriteText(out, *this, 0, options.escapes);
            break;

        case Format::Binary:
            Details::WriteBinary(out, *this, static_cast<char>(Details::BinaryEnd));
            break;

        case Format::Vbkv:
        {
            out.append(Details::VbkvMagic, sizeof(Details::VbkvMagic));
            out.append(4, '\0');
            Details::WriteBinary(out, *this, static_cast<char>(Details::BinaryAlternateEnd));

            // CRC32 of everything after the 8 bytes header, little-endian.
            std::string crc;
            Details::AppendLittleEndian32(crc, Details::Crc32(std::string_view(out).substr(8)));
            out.replace(4, 4, crc);
            break;
        }
    }
    return out;
}

inline void KeyValue::write(std::ostream& os, WriteOptions const& options) const
{
    const std::string data = dump(options);
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
}

inline void KeyValue::save(std::filesystem::path const& path, WriteOptions const& options) const
{
    // Serialize first: nothing is created or truncated if the tree can't be written.
    const std::string data = dump(options);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw IoError("EasyVDF: can't open \"" + Details::PathText(path) + "\" for writing");

    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();
    if (!file)
        throw IoError("EasyVDF: failed to write \"" + Details::PathText(path) + "\"");
}

inline KeyValue parse(std::string_view data, ReadOptions const& options)
{
    Details::Reader reader(data);
    return std::move(Details::ParseDocument(reader, options, false).front());
}

inline KeyValue parse(std::istream& is, ReadOptions const& options)
{
    Details::Reader reader(is, options.chunk_size);
    return std::move(Details::ParseDocument(reader, options, false).front());
}

inline KeyValue load(std::filesystem::path const& path, ReadOptions const& options)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw IoError("EasyVDF: can't open \"" + Details::PathText(path) + "\" for reading");

    return parse(file, options);
}

inline std::vector<KeyValue> parse_all(std::string_view data, ReadOptions const& options)
{
    Details::Reader reader(data);
    return Details::ParseDocument(reader, options, true);
}

inline std::vector<KeyValue> parse_all(std::istream& is, ReadOptions const& options)
{
    Details::Reader reader(is, options.chunk_size);
    return Details::ParseDocument(reader, options, true);
}

inline std::vector<KeyValue> load_all(std::filesystem::path const& path, ReadOptions const& options)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw IoError("EasyVDF: can't open \"" + Details::PathText(path) + "\" for reading");

    return parse_all(file, options);
}

inline std::ostream& operator<<(std::ostream& os, Node const& node)
{
    std::string text;
    if (node.is_object())
        Details::WriteTextBlock(text, node, 0, true);
    else if (node.has_value())
        text = node.as<std::string>();

    return os.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline std::ostream& operator<<(std::ostream& os, KeyValue const& entry)
{
    std::string text;
    if (entry.has_value())
        Details::WriteText(text, entry, 0, true);

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
