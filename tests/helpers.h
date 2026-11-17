// Checks on EasyVDF::Result that never abort: a Result that does not hold what a
// check expects fails the check, it does not end the test program.
#pragma once

#include "../EasyVDF.h"
#include "catch.hpp"

#include <string>
#include <type_traits>

// "OK", or the name of the error code: CHECK(Outcome(EasyVDF::parse(text)) == "UnexpectedEof").
template<typename R>
std::string Outcome(R const& r)
{
    return r ? std::string("OK") : std::string(EasyVDF::to_string(r.error().code));
}

// The error message, "OK" on success: CHECK_THAT(Message(r), Contains("line 3")).
template<typename R>
std::string Message(R const& r)
{
    return r ? std::string("OK") : r.error().message;
}

// True when r holds the error of a malformed input.
template<typename R>
bool IsParseError(R const& r)
{
    return !r && EasyVDF::is_parse_error(r.error().code);
}

// Prints a Result in the messages of failed checks.
namespace Catch {

template<typename T>
struct StringMaker<EasyVDF::Result<T>>
{
    static std::string convert(EasyVDF::Result<T> const& r)
    {
        if (!r)
            return "error " + std::string(EasyVDF::to_string(r.error().code)) + ": " + r.error().message;
        if constexpr (std::is_void_v<T>)
            return "OK";
        else
            return ::Catch::Detail::stringify(*r);
    }
};

} // namespace Catch
