#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
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

inline void ReadBinaryBytes(const char*& b, const char* e, std::string& buffer, size_t max_size)
{
    size_t left_count = (b + max_size - buffer.length()) > e ? e - b : max_size - buffer.length();
    buffer.insert(buffer.end(), b, b + left_count);
    b += left_count;
}

/// <summary>
/// Returns  0 when string is read to end (null char)
/// Returns -1 when string was partially read
/// Returns -2 when invalid utf8 codepoint was found
/// </summary>
/// <param name="b"></param>
/// <param name="e"></param>
/// <param name="str"></param>
/// <returns></returns>
inline int ParseBinaryString(const char*& b, const char* e, std::string& str)
{
    const char* string_start = b;
    while (b != e)
    {
        char c = *b++;
        if (c < 0x80u)
        {
            if (c == '\0')
            {
                str.insert(str.end(), string_start, b - 1);
                return 0;
            }
        }
        else if ((c >> 5) == 0x6)
        {// 2 bytes utf8
            if (b == e)
                return -2;

            ++b;
        }
        else if ((c >> 4) == 0xe)
        {// 3 bytes utf8
            if (b == (e - 1))
                return -2;

            b += 2;
        }
        else if ((c >> 3) == 0x1e)
        {// 4 bytes utf8
            if (b == (e - 2))
                return -2;

            b += 3;
        }
    }

    str.insert(str.end(), string_start, b - 1);
    return -1;
}

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
    while (b != e)
    {
        unsigned char c = *b++;
        if (c < 0x80u)
        {
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
        else if ((c >> 5) == 0x6)
        {// 2 bytes utf8
            if (b == e)
                return -3;

            ++b;
        }
        else if ((c >> 4) == 0xe)
        {// 3 bytes utf8
            if (b == (e - 1))
                return -3;

            b += 2;
        }
        else if ((c >> 3) == 0x1e)
        {// 4 bytes utf8
            if (b == (e - 2))
                return -3;

            b += 3;
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
    int error;
    std::string tmp1, item_key;

    BinaryNodeType state = BinaryNodeType::Object;
    bool parsed_item_key = false;
    bool type_read = false;

    o._Obj->_NameHash = std::hash<std::string>()(name);
    o._Obj->_Name = std::move(name);
    o._Obj->_U._Collection = new ValveCollection();
    o._Obj->_Type = ObjectType::Object;

    while (is || buffer_start != buffer_end)
    {
        while (buffer_start != buffer_end)
        {
            if (!type_read)
            {
                state = (BinaryNodeType)buffer_start[0];
                ++buffer_start;
                item_key.clear();
                type_read = true;
            }
            else
            {
                if (state == BinaryNodeType::ObjectEnd || state == BinaryNodeType::AlternativeEnd)
                {
                    if (state != object_end)
                    {
                        // Got object end but I didn't expected this value
                        //SPDLOG_DEBUG("Got object end {:02x} but expected {:02x}", (uint32_t)state, (uint32_t)object_end);
                    }
                    
                    return;
                }

                if (!parsed_item_key)
                {
                    error = Details::ParseBinaryString(buffer_start, buffer_end, item_key);
                    if (error == -2)
                    {
                        throw ParserException("Invalid codepoint while parsing binary string");
                    }
                    if (error == 0)
                    {
                        parsed_item_key = true;
                    }
                }
                else
                {
                    bool clear = false;

                    switch (state)
                    {
                        case BinaryNodeType::Object:
                            o._Obj->_U._Collection->emplace_back();
                            _ParseBinaryObject(is, item_key, object_end, buffer, buffer_start, buffer_end, *o._Obj->_U._Collection->rbegin());
                            clear = true;
                            break;

                        case BinaryNodeType::String:
                            error = Details::ParseBinaryString(buffer_start, buffer_end, tmp1);
                            if (error == -2)
                            {
                                throw ParserException("Invalid codepoint while parsing binary string");
                            }
                            if(error == 0)
                            {// String was fully read
                                o._Obj->_U._Collection->emplace_back(std::move(item_key), std::move(tmp1));

                                clear = true;
                            }
                            break;

                        case BinaryNodeType::Int32:
                            Details::ReadBinaryBytes(buffer_start, buffer_end, tmp1, 4);
                            if (tmp1.length() == 4)
                            {
                                o._Obj->_U._Collection->emplace_back(std::move(item_key), *reinterpret_cast<const int32_t*>(tmp1.data()));
                                clear = true;
                            }
                            break;

                        case BinaryNodeType::Float:
                            Details::ReadBinaryBytes(buffer_start, buffer_end, tmp1, 4);
                            if (tmp1.length() == 4)
                            {
                                o._Obj->_U._Collection->emplace_back(std::move(item_key), *reinterpret_cast<const float*>(tmp1.data()));
                                clear = true;
                            }
                            break;

                        case BinaryNodeType::Pointer:
                            Details::ReadBinaryBytes(buffer_start, buffer_end, tmp1, 4);
                            if (tmp1.length() == 4)
                            {
                                o._Obj->_U._Collection->emplace_back(std::move(item_key), *reinterpret_cast<const pointer_t*>(tmp1.data()));
                                clear = true;
                            }
                            break;

                        case BinaryNodeType::Color:
                            Details::ReadBinaryBytes(buffer_start, buffer_end, tmp1, 4);
                            if (tmp1.length() == 4)
                            {
                                o._Obj->_U._Collection->emplace_back(std::move(item_key), *reinterpret_cast<const color_t*>(tmp1.data()));
                                clear = true;
                            }
                            break;

                        case BinaryNodeType::Int64:
                            Details::ReadBinaryBytes(buffer_start, buffer_end, tmp1, 8);
                            if (tmp1.length() == 8)
                            {
                                o._Obj->_U._Collection->emplace_back(std::move(item_key), *reinterpret_cast<const int64_t*>(tmp1.data()));
                                clear = true;
                            }
                            break;

                        case BinaryNodeType::UInt64:
                            Details::ReadBinaryBytes(buffer_start, buffer_end, tmp1, 8);
                            if (tmp1.length() == 8)
                            {
                                o._Obj->_U._Collection->emplace_back(std::move(item_key), *reinterpret_cast<const uint64_t*>(tmp1.data()));
                                clear = true;
                            }
                            break;

                        default:
                            //SPDLOG_DEBUG("Unhandled item type {:02x}", (uint32_t)state);
                            throw std::runtime_error("Unhandled VDF type");
                    }

                    if (clear)
                    {
                        item_key.clear();
                        tmp1.clear();
                        parsed_item_key = false;
                        type_read = false;
                    }
                }
            }
        }

        is.read(&buffer[0], buffer.length());
        buffer_start = &buffer[0];
        buffer_end = buffer_start + is.gcount();
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

    is.read(&buffer[0], 4);
    if (!is)
        throw ParserException("Failed to read stream.");

    if (*reinterpret_cast<const uint32_t*>(buffer.data()) == BinaryVDFMagic)
    {
        as_binary = true;
        binary_root_end = BinaryNodeType::AlternativeEnd;
        // Skip crc
        is.seekg(4, std::ios::cur);
    }
    else
    {
        if (buffer[0] == '\0')
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
        if (!is.read(&buffer[0], 1) || is.gcount() != 1)
        {
            throw ParserException("Premature end of file while parsing root binary object");
        }
        if (buffer[0] != (int8_t)BinaryNodeType::Object)
        {
            throw ParserException("Binary root item type is not an object");
        }

        while (is.read(&buffer[0], chunk_size) || is.gcount() != 0)
        {
            buffer_start = &buffer[0];
            buffer_end = buffer_start + is.gcount();

            error = Details::ParseBinaryString(buffer_start, buffer_end, object_name);
            if (error == -2)
            {
                throw ParserException("Invalid codepoint while parsing binary string");
            }
            if (error == 0)
            {
                _ParseBinaryObject(is, object_name, binary_root_end, buffer, buffer_start, buffer_end, parsed_object);
            }
        }
    }

    return parsed_object;
}

}

static inline bool operator==(const EasyVDF::pointer_t v1, const EasyVDF::pointer_t v2)
{
    return v1.value == v2.value;
}

static inline bool operator!=(const EasyVDF::pointer_t v1, const EasyVDF::pointer_t v2)
{
    return v1.value != v2.value;
}

static inline bool operator==(const EasyVDF::color_t v1, const EasyVDF::color_t v2)
{
    return v1.value == v2.value;
}

static inline bool operator!=(const EasyVDF::color_t v1, const EasyVDF::color_t v2)
{
    return v1.value != v2.value;
}

static inline bool operator<(const EasyVDF::color_t v1, const EasyVDF::color_t v2)
{
    return v1.value < v2.value;
}

static inline bool operator<=(const EasyVDF::color_t v1, const EasyVDF::color_t v2)
{
    return v1.value <= v2.value;
}

static inline bool operator>(const EasyVDF::color_t v1, const EasyVDF::color_t v2)
{
    return v1.value > v2.value;
}

static inline bool operator>=(const EasyVDF::color_t v1, const EasyVDF::color_t v2)
{
    return v1.value >= v2.value;
}
