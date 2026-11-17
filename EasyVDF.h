#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <exception>

namespace EasyVDF {

// VBKV
static constexpr uint32_t BinaryVDFMagic = 0x564B4256;

enum class ObjectType : int8_t
{
    None       = -1,
    Object     =  0,
    String     =  1,
    Int32      =  2,
    Float      =  3,
    Pointer    =  4,
    WideString =  5,
    Color      =  6,
    UInt64     =  7,
    Binary     =  9,
    Int64      =  10,
};

class ValveDataObject;

template<typename T>
class ValveDataObjectRefWrapper;

using ValveDataObjectRef = ValveDataObjectRefWrapper<ValveDataObject>;
using ValveDataObjectConstRef = ValveDataObjectRefWrapper<const ValveDataObject>;

using ValveCollection = std::vector<::EasyVDF::ValveDataObject>;
using ValveCollectionRef = std::vector<::EasyVDF::ValveDataObjectRef>;
using ValveCollectionConstRef = std::vector<::EasyVDF::ValveDataObjectConstRef>;

struct pointer_t
{
    uint32_t value;
};

struct color_t
{
    uint32_t value;
};

namespace Details {

static inline bool is_cr(char c)
{
    return c == '\r';
}

static inline bool is_cr(wchar_t c)
{
    return c == L'\r';
}

static inline bool is_lf(char c)
{
    return c == '\n';
}

static inline bool is_lf(wchar_t c)
{
    return c == L'\n';
}

template<typename CharT, typename Traits, typename Allocator>
static std::basic_istream<CharT, Traits>& getline(std::basic_istream<CharT, Traits>& is, std::basic_string<CharT, Traits, Allocator>& buffer)
{
    CharT c;

    buffer.clear();
    while(is.get(c))
    {
        buffer.push_back(c);
        
        if (is_cr(c))
        {// MacOS EOL.
            // Nope, Windows EOL.
            c = is.peek();
            if (is_lf(c))
                buffer.push_back(is.get());
            
            break;
        }
        else if(is_lf(c))
        {// Linux EOL.
            break;
        }
    }
    
    return is;
}

inline void SkipSpaces(const char*& s, const char*& e)
{
    while (s != e)
    {
        char c = *s;
        if (c != ' ' && c != '\t')
            break;

        ++s;
    }
    while ((e-1) != s)
    {
        char c = *(e-1);
        if (c != '\n' && c != '\r')
            break;

        --e;
    }
}

/// <summary>
/// Appends bytes from [b, e) to buffer until buffer holds max_size bytes or [b, e) is consumed.
/// </summary>
inline void ReadBinaryBytes(const char*& b, const char* e, std::string& buffer, size_t max_size)
{
    const size_t missing = max_size > buffer.length() ? max_size - buffer.length() : 0;
    const size_t available = static_cast<size_t>(e - b);
    const size_t left_count = missing < available ? missing : available;
    buffer.append(b, left_count);
    b += left_count;
}

// Binary VDF values are little-endian, decode them byte by byte (no aliasing, no alignment requirement, host endianness independent).
inline uint32_t LoadLittleEndian32(const char* p)
{
    const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
    return static_cast<uint32_t>(u[0])
         | (static_cast<uint32_t>(u[1]) << 8)
         | (static_cast<uint32_t>(u[2]) << 16)
         | (static_cast<uint32_t>(u[3]) << 24);
}

inline uint64_t LoadLittleEndian64(const char* p)
{
    return static_cast<uint64_t>(LoadLittleEndian32(p)) | (static_cast<uint64_t>(LoadLittleEndian32(p + 4)) << 32);
}

template<typename T, typename U>
inline T BitCast(U value)
{
    static_assert(sizeof(T) == sizeof(U), "BitCast requires types of the same size");
    T result;
    std::memcpy(&result, &value, sizeof(T));
    return result;
}

/// <summary>
/// Appends the bytes in [b, e) up to the null terminator to str.
/// A binary string is an opaque byte sequence ended by '\0' (UTF-8 is not
/// validated, a null byte can never appear inside a multi-byte UTF-8 sequence),
/// so a string split across several chunks is rebuilt by calling this function
/// again with the next chunk and the same str.
/// Returns  0 when the string is read to its end (b points past the null char)
/// Returns -1 when the string was partially read (b == e, all bytes were appended)
/// Returns -2 is reserved for invalid codepoints and is never returned.
/// </summary>
inline int ParseBinaryString(const char*& b, const char* e, std::string& str)
{
    if (b == e)
        return -1;

    const char* string_end = static_cast<const char*>(std::memchr(b, '\0', static_cast<size_t>(e - b)));
    if (string_end == nullptr)
    {
        str.append(b, e);
        b = e;
        return -1;
    }

    str.append(b, string_end);
    b = string_end + 1;
    return 0;
}

/// <summary>
/// Makes sure [b, e) is not empty: when it is, reads the next chunk of is into buffer
/// (buffer must not be empty). Returns false on end of stream.
/// </summary>
inline bool FillBinaryBuffer(std::istream& is, std::string& buffer, const char*& b, const char*& e)
{
    if (b != e)
        return true;

    is.read(&buffer[0], static_cast<std::streamsize>(buffer.length()));
    b = buffer.data();
    e = b + is.gcount();
    return b != e;
}

/// <summary>
/// Reads a whole null terminated binary string into str, reading more chunks as needed.
/// Returns false if the stream ended before the null terminator.
/// </summary>
inline bool ReadFullBinaryString(std::istream& is, std::string& buffer, const char*& b, const char*& e, std::string& str)
{
    str.clear();
    while (FillBinaryBuffer(is, buffer, b, e))
    {
        if (ParseBinaryString(b, e, str) == 0)
            return true;
    }
    return false;
}

/// <summary>
/// Reads exactly count bytes into out, reading more chunks as needed.
/// Returns false if the stream ended before count bytes were read.
/// </summary>
inline bool ReadFullBinaryBytes(std::istream& is, std::string& buffer, const char*& b, const char*& e, std::string& out, size_t count)
{
    out.clear();
    while (out.length() < count)
    {
        if (!FillBinaryBuffer(is, buffer, b, e))
            return false;

        ReadBinaryBytes(b, e, out, count);
    }
    return true;
}

/// <summary>
/// Parses a double quoted text string.
/// Returns  0 when the string was read up to its closing quote
/// Returns -1 when no opening quote was found
/// Returns -2 when no closing quote was found
/// Returns -3 is reserved for invalid codepoints, it is never returned since
///            strings are not UTF-8 validated (see below).
/// </summary>
inline int ParseString(const char*& b, const char* e, std::string& str)
{
    bool has_escape = false;
    const char* string_start = nullptr;
    while (b != e)
    {
        char c = *b++;
        if (has_escape)
        {
            has_escape = false;
        }
        else if (c == '\\')
        {
            has_escape = true;
        }
        else if (c == '"')
        {
            string_start = b;
            break;
        }
    }
    // Strings are opaque byte sequences: UTF-8 is not validated. Only the ASCII
    // bytes '"' and '\\' are significant, and they can never appear inside a
    // multi-byte UTF-8 sequence, so scanning byte by byte is always safe.
    while (b != e)
    {
        char c = *b++;
        if (c == '\\')
        {
            has_escape = true;
            continue;
        }
        if (has_escape)
        {
            has_escape = false;
            continue;
        }
        if (c == '"')
        {
            str.assign(string_start, b - 1);
            return 0;
        }
    }
    if (string_start == nullptr)
        return -1;

    return -2;
}

}

class ParserException : public std::exception
{
    std::string _Msg;
public:
    ParserException(std::string msg) :
        _Msg(std::move(msg))
    {}

    virtual char const* what() const noexcept
    {
        return _Msg.c_str();
    }
};

class SerializeException : public std::exception
{
    const char* _Msg;
public:
    SerializeException(const char* msg):
        _Msg(msg)
    {}

    virtual char const* what() const noexcept
    {
        return _Msg;
    }
};

template<typename T>
class ValveDataObjectRefWrapper
{
    T* _Obj;

public:
    ValveDataObjectRefWrapper(T*);

    ValveDataObjectRefWrapper(ValveDataObjectRefWrapper<T> const& o);

    inline std::string const& Name() const;

    inline ObjectType Type() const;

    inline bool Empty() const;

    inline std::string& String();

    inline std::string const& String() const;

    inline ValveCollection& Collection();

    inline ValveCollection const& Collection() const;

    inline bool operator==(std::nullptr_t);

    inline ValveDataObjectRefWrapper& operator=(ValveDataObjectRefWrapper const&);

    inline ValveDataObjectRefWrapper& operator=(ValveDataObjectRefWrapper &&);

    inline ValveDataObjectRefWrapper& operator=(T const&);

    inline ValveDataObjectRefWrapper& operator=(T&&);

    inline ValveDataObjectRefWrapper& operator=(std::nullptr_t);

    inline ValveDataObjectRefWrapper& operator=(std::string const& value);

    inline ValveDataObjectRefWrapper& operator=(std::string&& value);

    inline ValveDataObjectRefWrapper& operator=(int32_t value);

    inline ValveDataObjectRefWrapper& operator=(pointer_t value);

    inline ValveDataObjectRefWrapper& operator=(color_t value);

    inline ValveDataObjectRefWrapper& operator=(float value);

    inline ValveDataObjectRefWrapper& operator=(int64_t value);

    inline ValveDataObjectRefWrapper& operator=(uint64_t value);

    inline int32_t Int32() const;

    inline float Float() const;

    inline pointer_t Pointer() const;

    inline color_t Color() const;

    inline int64_t Int64() const;

    inline uint64_t UInt64() const;

    inline ValveCollectionRef operator[](const char* key);

    inline ValveCollectionRef operator[](std::string const& key);

    inline ValveCollectionConstRef operator[](const char* key) const;

    inline ValveCollectionConstRef operator[](std::string const& key) const;

    inline std::string SerializeAsText() const;

    inline std::string SerializeAsBinary(int version = 0) const;

    inline void SerializeAsText(std::ostream& os) const;

    inline void SerializeAsBinary(std::ostream& os, int version = 0) const;
};

class ValveDataObject
{
private:
    enum class BinaryNodeType : int8_t
    {
        Object         = 0,
        String         = 1,
        Int32          = 2,
        Float          = 3,
        Pointer        = 4,
        WideString     = 5,
        Color          = 6,
        UInt64         = 7,
        ObjectEnd      = 8,
        Binary         = 9,
        Int64          = 10,
        AlternativeEnd = 11,
    };

    class Data_t
    {
        friend class ValveDataObject;

        std::string _Name;
        size_t _NameHash;
        union
        {
            std::string* _String;
            ValveCollection* _Collection;
            int32_t _Int32;
            float _Float;
            pointer_t _Pointer;
            color_t _Color;
            int64_t _Int64;
            uint64_t _UInt64;
        } _U;
        ObjectType _Type;

    public:
        Data_t() :
            _Name(),
            _NameHash(std::hash<std::string>()(_Name)),
            _Type(ObjectType::None)
        {}
    };

    Data_t *_Obj;

    void _ResetValue();

    static void _ParseTextObject(std::istream& is, std::string& name, std::string& buffer, uint32_t& line_num, ValveDataObject& o);

    static void _ParseBinaryObject(std::istream& is, std::string& name, BinaryNodeType object_end, std::string& buffer, const char*& buffer_start, const char*& buffer_end, ValveDataObject& o);

    void _SerializeAsText(std::ostream& os, size_t depth) const;

    void _SerializeAsBinary(std::ostream& os, BinaryNodeType object_end, uint32_t crc) const;

public:
    ValveDataObject();

    ValveDataObject(ValveDataObject const& other);

    ValveDataObject(ValveDataObject&& other) noexcept;
    
    ValveDataObject(std::string const& key);
    
    ValveDataObject(std::string const& key, ValveDataObject const& other);

    ValveDataObject(std::string const& key, ValveDataObject&& other) noexcept;

    ValveDataObject(std::string const& key, std::string const& value);

    ValveDataObject(std::string const& key, std::string&& value);

    ValveDataObject(std::string const& key, int32_t value);

    ValveDataObject(std::string const& key, float value);

    ValveDataObject(std::string const& key, pointer_t value);

    ValveDataObject(std::string const& key, color_t value);

    ValveDataObject(std::string const& key, int64_t value);

    ValveDataObject(std::string const& key, uint64_t value);

    ~ValveDataObject();

    inline void Name(std::string const& value);

    inline void Name(std::string && value);

    inline std::string const& Name() const;

    inline ObjectType Type() const;

    inline bool Empty() const;

    std::string& String();

    std::string const& String() const;

    ValveCollection& Collection();

    ValveCollection const& Collection() const;

    inline bool operator==(std::nullptr_t);

    ValveDataObject& operator=(ValveDataObject const& other);

    ValveDataObject& operator=(ValveDataObject&& other) noexcept;

    ValveDataObject& operator=(std::nullptr_t);

    ValveDataObject& operator=(std::string const& value);

    ValveDataObject& operator=(std::string&& value);

    ValveDataObject& operator=(int32_t value);

    ValveDataObject& operator=(pointer_t value);

    ValveDataObject& operator=(color_t value);

    ValveDataObject& operator=(float value);

    ValveDataObject& operator=(int64_t value);

    ValveDataObject& operator=(uint64_t value);

    int32_t Int32() const;

    float Float() const;

    pointer_t Pointer() const;

    color_t Color() const;

    int64_t Int64() const;

    uint64_t UInt64() const;

    ValveCollectionRef operator[](const char* key);
    
    ValveCollectionRef operator[](std::string const& key);
    
    ValveCollectionConstRef operator[](const char* key) const;

    ValveCollectionConstRef operator[](std::string const& key) const;

    inline std::string SerializeAsText() const;

    inline std::string SerializeAsBinary(int version = 0) const;

    inline void SerializeAsText(std::ostream& os) const;

    inline void SerializeAsBinary(std::ostream& os, int version = 0) const;

    static ValveDataObject ParseObject(std::istream& is, size_t chunk_size = 10 * 1024);
};


/////////////////////////////////////////////////////////////////////
// 
//                        ValveDataObjectRef
// 
/////////////////////////////////////////////////////////////////////

template<typename T>
ValveDataObjectRefWrapper<T>::ValveDataObjectRefWrapper(T* obj):
    _Obj(obj)
{}

template<typename T>
ValveDataObjectRefWrapper<T>::ValveDataObjectRefWrapper(ValveDataObjectRefWrapper<T> const& o):
    _Obj(o._Obj)
{}

template<typename T>
inline std::string const& ValveDataObjectRefWrapper<T>::Name() const
{
    return _Obj->Name();
}

template<typename T>
inline ObjectType ValveDataObjectRefWrapper<T>::Type() const
{
    return _Obj->Type();
}

template<typename T>
inline bool ValveDataObjectRefWrapper<T>::Empty() const
{
    return _Obj->Empty();
}

template<typename T>
inline std::string& ValveDataObjectRefWrapper<T>::String()
{
    return _Obj->String();
}

template<typename T>
inline std::string const& ValveDataObjectRefWrapper<T>::String() const
{
    return _Obj->String();
}

template<typename T>
inline ValveCollection& ValveDataObjectRefWrapper<T>::Collection()
{
    return _Obj->Collection();
}

template<typename T>
inline ValveCollection const& ValveDataObjectRefWrapper<T>::Collection() const
{
    return _Obj->Collection();
}

template<typename T>
inline bool ValveDataObjectRefWrapper<T>::operator==(std::nullptr_t)
{
    return (*_Obj) == nullptr;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(ValveDataObjectRefWrapper<T> const& value)
{
    (*_Obj) = *value._Obj;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(ValveDataObjectRefWrapper<T>&& value)
{
    (*_Obj) = std::move(*value._Obj);
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(T const& value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(T&& value)
{
    (*_Obj) = std::move(value);
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(std::nullptr_t)
{
    (*_Obj) = nullptr;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(std::string const& value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(std::string&& value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(int32_t value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(pointer_t value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(color_t value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(float value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(int64_t value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline ValveDataObjectRefWrapper<T>& ValveDataObjectRefWrapper<T>::operator=(uint64_t value)
{
    (*_Obj) = value;
    return *this;
}

template<typename T>
inline int32_t ValveDataObjectRefWrapper<T>::Int32() const
{
    return _Obj->Int32();
}

template<typename T>
inline float ValveDataObjectRefWrapper<T>::Float() const
{
    return _Obj->Float();
}

template<typename T>
inline pointer_t ValveDataObjectRefWrapper<T>::Pointer() const
{
    return _Obj->Pointer();
}

template<typename T>
inline color_t ValveDataObjectRefWrapper<T>::Color() const
{
    return _Obj->Color();
}

template<typename T>
inline int64_t ValveDataObjectRefWrapper<T>::Int64() const
{
    return _Obj->Int64();
}

template<typename T>
inline uint64_t ValveDataObjectRefWrapper<T>::UInt64() const
{
    return _Obj->UInt64();
}

template<typename T>
inline ValveCollectionRef ValveDataObjectRefWrapper<T>::operator[](const char* key)
{
    return (*_Obj)[key];
}

template<typename T>
inline ValveCollectionRef ValveDataObjectRefWrapper<T>::operator[](std::string const& key)
{
    return (*_Obj)[key];
}

template<typename T>
inline ValveCollectionConstRef ValveDataObjectRefWrapper<T>::operator[](const char* key) const
{
    return static_cast<const ValveDataObject&>(*_Obj)[key];
}

template<typename T>
inline ValveCollectionConstRef ValveDataObjectRefWrapper<T>::operator[](std::string const& key) const
{
    return static_cast<const ValveDataObject&>(*_Obj)[key];
}

template<typename T>
inline std::string ValveDataObjectRefWrapper<T>::SerializeAsText() const
{
    return _Obj->SerializeAsText();
}

template<typename T>
inline std::string ValveDataObjectRefWrapper<T>::SerializeAsBinary(int version) const
{
    return _Obj->SerializeAsBinary(version);
}

template<typename T>
inline void ValveDataObjectRefWrapper<T>::SerializeAsText(std::ostream& os) const
{
    return _Obj->SerializeAsText(os);
}

template<typename T>
inline void ValveDataObjectRefWrapper<T>::SerializeAsBinary(std::ostream& os, int version) const
{
    return _Obj->SerializeAsBinary(os, version);
}

/////////////////////////////////////////////////////////////////////
// 
//                        ValveDataObject
// 
/////////////////////////////////////////////////////////////////////
inline ValveDataObject::ValveDataObject() :
    _Obj(new Data_t())
{
    _Obj->_Type = ObjectType::None;
}

inline ValveDataObject::ValveDataObject(ValveDataObject const& other):
    _Obj(new Data_t())
{
    _Obj->_Name = other._Obj->_Name;
    _Obj->_NameHash = other._Obj->_NameHash;
    _Obj->_Type = other._Obj->_Type;
    switch (_Obj->_Type)
    {   // Copy pointers content
        case ObjectType::String: _Obj->_U._String = new std::string(*other._Obj->_U._String); break;
        case ObjectType::Object: _Obj->_U._Collection = new ValveCollection(*other._Obj->_U._Collection); break;
        // Copy biggest possible value
        default: _Obj->_U = other._Obj->_U;
    }
}

inline ValveDataObject::ValveDataObject(ValveDataObject && other) noexcept :
    _Obj(new Data_t())
{
    // Copy object name on creation
    _Obj->_Name = other._Obj->_Name;
    _Obj->_NameHash = other._Obj->_NameHash;
    // But move content
    (*this) = std::move(other);
}

inline ValveDataObject::ValveDataObject(std::string const& key) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_Type = ObjectType::Object;
    _Obj->_U._Collection = new ValveCollection();
}

inline ValveDataObject::ValveDataObject(std::string const& key, ValveDataObject const& other) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_Type = ObjectType::Object;
    _Obj->_U._Collection = new ValveCollection();
    _Obj->_U._Collection->emplace_back(other);
}

inline ValveDataObject::ValveDataObject(std::string const& key, ValveDataObject&& other) noexcept :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_Type = ObjectType::Object;
    _Obj->_U._Collection = new ValveCollection();
    _Obj->_U._Collection->emplace_back(std::move(other));
}

inline ValveDataObject::ValveDataObject(std::string const& key, std::string const& value) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._String = new std::string(value);
    _Obj->_Type = ObjectType::String;
}

inline ValveDataObject::ValveDataObject(std::string const& key, std::string && value) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._String = new std::string(std::move(value));
    _Obj->_Type = ObjectType::String;
}

inline ValveDataObject::ValveDataObject(std::string const& key, int32_t value) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._Int32 = value;
    _Obj->_Type = ObjectType::Int32;
}

inline ValveDataObject::ValveDataObject(std::string const& key, float value) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._Float = value;
    _Obj->_Type = ObjectType::Float;
}

inline ValveDataObject::ValveDataObject(std::string const& key, pointer_t value):
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._Pointer = value;
    _Obj->_Type = ObjectType::Pointer;
}

inline ValveDataObject::ValveDataObject(std::string const& key, color_t value) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._Color = value;
    _Obj->_Type = ObjectType::Color;
}

inline ValveDataObject::ValveDataObject(std::string const& key, int64_t value) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._Int64 = value;
    _Obj->_Type = ObjectType::Int64;
}

inline ValveDataObject::ValveDataObject(std::string const& key, uint64_t value) :
    _Obj(new Data_t())
{
    _Obj->_Name = key;
    _Obj->_NameHash = std::hash<std::string>()(key);
    _Obj->_U._UInt64 = value;
    _Obj->_Type = ObjectType::UInt64;
}

inline ValveDataObject::~ValveDataObject()
{
    _ResetValue();
    delete _Obj;
}

inline void ValveDataObject::Name(std::string const& value)
{
    _Obj->_NameHash = std::hash<std::string>()(value);
    _Obj->_Name = value;
}

inline void ValveDataObject::Name(std::string&& value)
{
    _Obj->_NameHash = std::hash<std::string>()(value);
    _Obj->_Name = std::move(value);
}

inline std::string const& ValveDataObject::Name() const
{
    return _Obj->_Name;
}

inline ObjectType ValveDataObject::Type() const
{
    return _Obj->_Type;
}

inline bool ValveDataObject::Empty() const
{
    return _Obj->_Type == ObjectType::None;
}

inline std::string& ValveDataObject::String()
{
    if (_Obj->_Type != ObjectType::String)
    {
        throw std::invalid_argument("Attempted to read a String from a non String type.");
    }

    return *_Obj->_U._String;
}

inline std::string const& ValveDataObject::String() const
{
    if (_Obj->_Type != ObjectType::String)
    {
        throw std::invalid_argument("Attempted to read a String from a non String type.");
    }
    
    return *_Obj->_U._String;
}

inline ValveCollection& ValveDataObject::Collection()
{
    if (_Obj->_Type != ObjectType::Object)
    {
        throw std::invalid_argument("Attempted to get a Collection from non Collection type.");
    }

    return *_Obj->_U._Collection;
}

inline ValveCollection const& ValveDataObject::Collection() const
{
    if (_Obj->_Type != ObjectType::Object)
    {
        throw std::invalid_argument("Attempted to get a Collection from non Collection type.");
    }

    return *_Obj->_U._Collection;
}

inline bool ValveDataObject::operator==(std::nullptr_t)
{
    return Empty();
}

inline ValveDataObject& ValveDataObject::operator=(ValveDataObject const& value)
{
    std::cout << "Copy" << std::endl;
    return (*this = ValveDataObject(value));
}

inline ValveDataObject& ValveDataObject::operator=(ValveDataObject&& value) noexcept
{
    auto type = value._Obj->_Type;
    auto v = value._Obj->_U;
    
    value._Obj->_Type = _Obj->_Type;
    value._Obj->_U = _Obj->_U;

    _Obj->_Type = type;
    _Obj->_U = v;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(std::nullptr_t)
{
    _ResetValue();
    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(std::string const& value)
{
    std::string* v = new std::string(value);
    _ResetValue();
    _Obj->_U._String = v;
    _Obj->_Type = ObjectType::String;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(std::string&& value)
{
    std::string* v = new std::string(std::move(value));
    _ResetValue();
    _Obj->_U._String = v;
    _Obj->_Type = ObjectType::String;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(int32_t value)
{
    _ResetValue();
    _Obj->_U._Int32 = value;
    _Obj->_Type = ObjectType::Int32;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(pointer_t value)
{
    _ResetValue();
    _Obj->_U._Pointer = value;
    _Obj->_Type = ObjectType::Int32;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(color_t value)
{
    _ResetValue();
    _Obj->_U._Color = value;
    _Obj->_Type = ObjectType::Int32;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(float value)
{
    _ResetValue();
    _Obj->_U._Float = value;
    _Obj->_Type = ObjectType::Float;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(int64_t value)
{
    _ResetValue();
    _Obj->_U._Int64 = value;
    _Obj->_Type = ObjectType::Int64;

    return *this;
}

inline ValveDataObject& ValveDataObject::operator=(uint64_t value)
{
    _ResetValue();
    _Obj->_U._UInt64 = value;
    _Obj->_Type = ObjectType::UInt64;

    return *this;
}

inline int32_t ValveDataObject::Int32() const
{
    if (_Obj->_Type != ObjectType::Int32)
    {
        throw std::invalid_argument("Attempted to get an Int32 from non Int32 type.");
    }

    return _Obj->_U._Int32;
}

inline float ValveDataObject::Float() const
{
    if (_Obj->_Type != ObjectType::Float)
    {
        throw std::invalid_argument("Attempted to get a Float from non Float type.");
    }

    return _Obj->_U._Float;
}

inline pointer_t ValveDataObject::Pointer() const
{
    if (_Obj->_Type != ObjectType::Pointer)
    {
        throw std::invalid_argument("Attempted to get a Pointer from non Pointer type.");
    }

    return _Obj->_U._Pointer;
}

inline color_t ValveDataObject::Color() const
{
    if (_Obj->_Type != ObjectType::Color)
    {
        throw std::invalid_argument("Attempted to get a Pointer from non Pointer type.");
    }

    return _Obj->_U._Color;
}

inline int64_t ValveDataObject::Int64() const
{
    if (_Obj->_Type != ObjectType::Int64)
    {
        throw std::invalid_argument("Attempted to get an Int64 from non Int64 type.");
    }

    return _Obj->_U._Int64;
}

inline uint64_t ValveDataObject::UInt64() const
{
    if (_Obj->_Type != ObjectType::UInt64)
    {
        throw std::invalid_argument("Attempted to get an UInt64 from non UInt64 type.");
    }

    return _Obj->_U._UInt64;
}

inline ValveCollectionRef ValveDataObject::operator[](const char* key)
{
    return (*this)[std::string(key)];
}

inline ValveCollectionRef ValveDataObject::operator[](std::string const& key)
{
    auto& c = Collection();

    ValveCollectionRef r;
    size_t key_hash = std::hash<std::string>()(key);

    for (auto& item : c)
    {
        if (item._Obj->_NameHash == key_hash)
            r.emplace_back(ValveDataObjectRef(&item));
    }

    return r;
}

inline ValveCollectionConstRef ValveDataObject::operator[](const char* key) const
{
    return (*this)[std::string(key)];
}

inline ValveCollectionConstRef ValveDataObject::operator[](std::string const& key) const
{
    auto& c = Collection();

    ValveCollectionConstRef r;
    size_t key_hash = std::hash<std::string>()(key);

    for (auto& item : c)
    {
        if (item._Obj->_NameHash == key_hash)
            r.emplace_back(ValveDataObjectConstRef(&item));
    }

    return r;
}

inline void ValveDataObject::_ResetValue()
{
    if (_Obj == nullptr)
        return;

    switch (_Obj->_Type)
    {
        case ObjectType::String: delete _Obj->_U._String; break;
        case ObjectType::Object: delete _Obj->_U._Collection; break;
        default: break; // Warning fix.
    }
    _Obj->_Type = ObjectType::None;
}

inline void ValveDataObject::_ParseTextObject(std::istream& is, std::string& name, std::string& buffer, uint32_t& line_num, ValveDataObject& o)
{
    const char* line_start;
    const char* line_end;

    std::string object_name;
    std::string tmp;
    int error;
    bool is_object = false;

    o._Obj->_NameHash = std::hash<std::string>()(name);
    o._Obj->_Name = std::move(name);
    o._Obj->_U._Collection = new ValveCollection();
    o._Obj->_Type = ObjectType::Object;

    while (EasyVDF::Details::getline(is, buffer))
    {
        ++line_num;
        line_start = &buffer[0];
        line_end = line_start + buffer.length();
        Details::SkipSpaces(line_start, line_end);
            
        // Skip empty line
        if (line_start == line_end)
            continue;
            
        if (*line_start == '}')
            break;

        if (!is_object)
        {
            error = Details::ParseString(line_start, line_end, object_name);
            if (error == -1)
            {
                throw ParserException("Expected item key start at line " + std::to_string(line_num));
            }
            if (error == -2)
            {
                throw ParserException("Expected item key end at line " + std::to_string(line_num));
            }
            if (error == -3)
            {
                throw ParserException("Invalid codepoint at line " + std::to_string(line_num));
            }
            Details::SkipSpaces(line_start, line_end);

            if (*line_start == '"')
            {// Parsing item value
                error = Details::ParseString(line_start, line_end, tmp);
                if (error == -1)
                {
                    throw ParserException("Expected item value start at line " + std::to_string(line_num));
                }
                if (error == -2)
                {
                    throw ParserException("Expected item value end at line " + std::to_string(line_num));
                }
                if (error == -3)
                {
                    throw ParserException("Invalid codepoint at line " + std::to_string(line_num));
                }
                Details::SkipSpaces(line_start, line_end);
                if (line_start != line_end)
                {
                    throw ParserException("Got datas after item value at line " + std::to_string(line_num));
                }

                o._Obj->_U._Collection->emplace_back(std::move(object_name), tmp);
            }
            else if (line_start != line_end)
            {
                throw ParserException("Got datas after item key at line " + std::to_string(line_num));
            }
            else
            {
                is_object = true;
            }
        }
        else
        {
            if (*line_start++ != '{')
            {
                throw ParserException("Expected object start at line " + std::to_string(line_num));
            }
            Details::SkipSpaces(line_start, line_end);
            if (line_start != line_end)
            {
                throw ParserException("Got datas after object start at line " + std::to_string(line_num));
            }

            o._Obj->_U._Collection->emplace_back();
            _ParseTextObject(is, object_name, buffer, line_num, *o._Obj->_U._Collection->rbegin());
            is_object = false;
        }
    }
}
    

inline void ValveDataObject::_ParseBinaryObject(std::istream& is, std::string& name, BinaryNodeType object_end, std::string& buffer, const char*& buffer_start, const char*& buffer_end, ValveDataObject& o)
{
    // Release any previous value first, so parsing into an existing object can never leak.
    o._ResetValue();
    o._Obj->_NameHash = std::hash<std::string>()(name);
    o._Obj->_Name = std::move(name);
    o._Obj->_U._Collection = new ValveCollection();
    o._Obj->_Type = ObjectType::Object;

    // Items are parsed into the collection owned by o: if an exception is thrown,
    // everything parsed so far is released with o.
    ValveCollection& collection = *o._Obj->_U._Collection;
    std::string tmp1, item_key;

    for (;;)
    {
        if (!Details::FillBinaryBuffer(is, buffer, buffer_start, buffer_end))
            throw ParserException("Premature end of file: missing end marker of binary object \"" + o._Obj->_Name + "\"");

        BinaryNodeType state = static_cast<BinaryNodeType>(*buffer_start++);
        switch (state)
        {
            case BinaryNodeType::ObjectEnd:
            case BinaryNodeType::AlternativeEnd:
                // Both end markers are accepted whatever object_end is, as before.
                return;

            case BinaryNodeType::Object:
            case BinaryNodeType::String:
            case BinaryNodeType::Int32:
            case BinaryNodeType::Float:
            case BinaryNodeType::Pointer:
            case BinaryNodeType::Color:
            case BinaryNodeType::UInt64:
            case BinaryNodeType::Int64:
                break;

            default:
                throw ParserException("Unhandled binary VDF node type " + std::to_string(static_cast<unsigned int>(static_cast<uint8_t>(state))) + " in binary object \"" + o._Obj->_Name + "\"");
        }

        if (!Details::ReadFullBinaryString(is, buffer, buffer_start, buffer_end, item_key))
            throw ParserException("Premature end of file while parsing a key of binary object \"" + o._Obj->_Name + "\"");

        switch (state)
        {
            case BinaryNodeType::Object:
                collection.emplace_back();
                _ParseBinaryObject(is, item_key, object_end, buffer, buffer_start, buffer_end, collection.back());
                break;

            case BinaryNodeType::String:
                if (!Details::ReadFullBinaryString(is, buffer, buffer_start, buffer_end, tmp1))
                    throw ParserException("Premature end of file while parsing the value of key \"" + item_key + "\"");

                collection.emplace_back(std::move(item_key), std::move(tmp1));
                break;

            default:
            {
                const size_t value_size = (state == BinaryNodeType::Int64 || state == BinaryNodeType::UInt64) ? 8 : 4;
                if (!Details::ReadFullBinaryBytes(is, buffer, buffer_start, buffer_end, tmp1, value_size))
                    throw ParserException("Premature end of file while parsing the value of key \"" + item_key + "\"");

                const char* value = tmp1.data();
                switch (state)
                {
                    case BinaryNodeType::Int32  : collection.emplace_back(std::move(item_key), Details::BitCast<int32_t>(Details::LoadLittleEndian32(value))); break;
                    case BinaryNodeType::Float  : collection.emplace_back(std::move(item_key), Details::BitCast<float>(Details::LoadLittleEndian32(value))); break;
                    case BinaryNodeType::Pointer: collection.emplace_back(std::move(item_key), pointer_t{ Details::LoadLittleEndian32(value) }); break;
                    case BinaryNodeType::Color  : collection.emplace_back(std::move(item_key), color_t{ Details::LoadLittleEndian32(value) }); break;
                    case BinaryNodeType::Int64  : collection.emplace_back(std::move(item_key), Details::BitCast<int64_t>(Details::LoadLittleEndian64(value))); break;
                    case BinaryNodeType::UInt64 : collection.emplace_back(std::move(item_key), Details::LoadLittleEndian64(value)); break;
                    default: break; // Not reachable, filtered above.
                }
            }
        }
    }
}

inline void ValveDataObject::_SerializeAsText(std::ostream& os, size_t depth) const
{
    std::string indent(depth, '\t');

    os << indent << '"' << _Obj->_Name << '"';
    switch (_Obj->_Type)
    {
        case ObjectType::Object:
            os << '\n' << indent << "{\n";
            for (auto const& item : *_Obj->_U._Collection)
            {
                item._SerializeAsText(os, depth + 1);
            }
            os << indent << "}\n";
            break;

        case ObjectType::Pointer: os << "\t\t\"" << _Obj->_U._Pointer.value << "\"\n"; break;
        case ObjectType::Color  : os << "\t\t\"" << _Obj->_U._Color.value   << "\"\n"; break;
        case ObjectType::Float  : os << "\t\t\"" << _Obj->_U._Float         << "\"\n"; break;
        case ObjectType::Int32  : os << "\t\t\"" << _Obj->_U._Int32         << "\"\n"; break;
        case ObjectType::Int64  : os << "\t\t\"" << _Obj->_U._Int64         << "\"\n"; break;
        case ObjectType::UInt64 : os << "\t\t\"" << _Obj->_U._UInt64        << "\"\n"; break;
        case ObjectType::String : os << "\t\t\"" << *_Obj->_U._String       << "\"\n"; break;
        
        //case ObjectType::WideString: TODO;
        //case ObjectType::Binary    : TODO;
        
        case ObjectType::None: break; // Warning fix.
    }
}

inline void ValveDataObject::_SerializeAsBinary(std::ostream& os, BinaryNodeType object_end, uint32_t crc) const
{
    os.write((const char*)&_Obj->_Type, 1);
    os.write(_Obj->_Name.c_str(), _Obj->_Name.length() + 1);

    switch (_Obj->_Type)
    {
        case ObjectType::Object:
            for (auto const& item : *_Obj->_U._Collection)
            {
                item._SerializeAsBinary(os, object_end, crc);
            }
            os.write((const char*)&object_end, 1);
            break;
    
        case ObjectType::Pointer: os.write((const char*)&_Obj->_U._Pointer, 4); break;
        case ObjectType::Color  : os.write((const char*)&_Obj->_U._Color, 4); break;
        case ObjectType::Float  : os.write((const char*)&_Obj->_U._Float, 4); break;
        case ObjectType::Int32  : os.write((const char*)&_Obj->_U._Int32, 4); break;
        case ObjectType::Int64  : os.write((const char*)&_Obj->_U._Int64, 8); break;
        case ObjectType::UInt64 : os.write((const char*)&_Obj->_U._UInt64, 8); break;
        case ObjectType::String : os.write(_Obj->_U._String->c_str(), _Obj->_U._String->length() + 1); break;
        
        //case ObjectType::WideString: TODO;
        //case ObjectType::Binary    : TODO;
        
        case ObjectType::None: break; // Warning fix.
    }
}

inline std::string ValveDataObject::SerializeAsBinary(int version) const
{
    std::stringstream sstr;
    SerializeAsBinary(sstr, version);
    return sstr.str();
}

inline void ValveDataObject::SerializeAsBinary(std::ostream& os, int version) const
{
    if (_Obj->_Type != ObjectType::Object)
        throw SerializeException("Can't serialize ValveDataObject, it needs to be an Object type.");

    uint32_t crc = 0x00000000;
    //size_t spos = os.tellp();

    BinaryNodeType object_end = version <= 1 ? BinaryNodeType::ObjectEnd : BinaryNodeType::AlternativeEnd;
    if (version > 1)
    {
        os.write((const char*)&BinaryVDFMagic, 4);
        os.write((const char*)&crc, 4);
    }

    _SerializeAsBinary(os, object_end, crc);

    if (version > 1)
    {// TODO: Update crc
        //os.seekp(spos + 4, std::ios::beg);
        //os.write((const char*)&crc, sizeof(crc));
        //os.seekp(spos, std::ios::beg);
    }
}

inline std::string ValveDataObject::SerializeAsText() const
{
    std::stringstream sstr;
    SerializeAsText(sstr);
    return sstr.str();
}

inline void ValveDataObject::SerializeAsText(std::ostream& os) const
{
    if (_Obj->_Type != ObjectType::Object)
        throw SerializeException("Can't serialize ValveDataObject, it needs to be an Object type.");

    _SerializeAsText(os, 0);
}

inline ValveDataObject ValveDataObject::ParseObject(std::istream& is, size_t chunk_size)
{
    uint32_t line_num = 0;

    bool as_binary = false;
    const char* buffer_start, *buffer_end;

    ValveDataObject parsed_object;

    std::string buffer(chunk_size, '\0');
    std::string object_name;
    int error;
    BinaryNodeType binary_root_end = BinaryNodeType::ObjectEnd;

    char header[4];
    is.read(header, 4);
    if (!is)
        throw ParserException("Failed to read stream.");

    if (Details::LoadLittleEndian32(header) == BinaryVDFMagic)
    {
        as_binary = true;
        binary_root_end = BinaryNodeType::AlternativeEnd;
        // Skip crc
        is.seekg(4, std::ios::cur);
    }
    else
    {
        if (header[0] == '\0')
        {// Likely, This is the root object as binary
            as_binary = true;
        }

        is.seekg(0, std::ios::beg);
    }

    if (!as_binary)
    {// Parse as text VDF
        while (EasyVDF::Details::getline(is, buffer))
        {
            ++line_num;
            buffer_start = &buffer[0];
            buffer_end = buffer_start + buffer.length();
            Details::SkipSpaces(buffer_start, buffer_end);

            // Skip empty line
            if (buffer_start == buffer_end)
                continue;

            if (object_name.empty())
            {
                error = Details::ParseString(buffer_start, buffer_end, object_name);
                if (error == -1)
                {
                    throw ParserException("Expected object key start at line " + std::to_string(line_num));
                }
                if (error == -2)
                {
                    throw ParserException("Expected object key end at line " + std::to_string(line_num));
                }
                if (error == -3)
                {
                    throw ParserException("Invalid codepoint at line " + std::to_string(line_num));
                }
                Details::SkipSpaces(buffer_start, buffer_end);
                if (buffer_start != buffer_end)
                {
                    throw ParserException("Got datas after object key at line " + std::to_string(line_num));
                }
            }
            else
            {
                if (*buffer_start++ != '{')
                {
                    throw ParserException("Expected object start at line " + std::to_string(line_num));
                }
                Details::SkipSpaces(buffer_start, buffer_end);
                if (buffer_start != buffer_end)
                {
                    throw ParserException("Got datas after object start at line " + std::to_string(line_num));
                }

                _ParseTextObject(is, object_name, buffer, line_num, parsed_object);
            }
        }
    }
    else
    {// Parse as binary VDF
        if (buffer.empty())
            buffer.resize(1);

        buffer_start = buffer_end = buffer.data();
        if (!Details::FillBinaryBuffer(is, buffer, buffer_start, buffer_end))
        {
            throw ParserException("Premature end of file while parsing root binary object");
        }
        if (*buffer_start++ != static_cast<char>(BinaryNodeType::Object))
        {
            throw ParserException("Binary root item type is not an object");
        }
        if (!Details::ReadFullBinaryString(is, buffer, buffer_start, buffer_end, object_name))
        {
            throw ParserException("Premature end of file while parsing root binary object name");
        }

        _ParseBinaryObject(is, object_name, binary_root_end, buffer, buffer_start, buffer_end, parsed_object);

        // Only one root object is allowed. Valve binary files commonly end with one extra
        // end marker after the root object end (0x08, or the root end marker itself):
        // accept a single one, reject anything else.
        if (Details::FillBinaryBuffer(is, buffer, buffer_start, buffer_end))
        {
            const char trailing = *buffer_start++;
            if ((trailing != static_cast<char>(BinaryNodeType::ObjectEnd) && trailing != static_cast<char>(binary_root_end)) ||
                Details::FillBinaryBuffer(is, buffer, buffer_start, buffer_end))
            {
                throw ParserException("Unexpected data after the binary root object");
            }
        }
    }

    return parsed_object;
}

}

static inline bool operator==(EasyVDF::pointer_t v1, EasyVDF::pointer_t v2)
{
    return v1.value == v2.value;
}

static inline bool operator!=(EasyVDF::pointer_t v1, EasyVDF::pointer_t v2)
{
    return v1.value != v2.value;
}

static inline bool operator==(EasyVDF::color_t v1, EasyVDF::color_t v2)
{
    return v1.value == v2.value;
}

static inline bool operator!=(EasyVDF::color_t v1, EasyVDF::color_t v2)
{
    return v1.value != v2.value;
}

static inline bool operator<(EasyVDF::color_t v1, EasyVDF::color_t v2)
{
    return v1.value < v2.value;
}

static inline bool operator<=(EasyVDF::color_t v1, EasyVDF::color_t v2)
{
    return v1.value <= v2.value;
}

static inline bool operator>(EasyVDF::color_t v1, EasyVDF::color_t v2)
{
    return v1.value > v2.value;
}

static inline bool operator>=(EasyVDF::color_t v1, EasyVDF::color_t v2)
{
    return v1.value >= v2.value;
}
