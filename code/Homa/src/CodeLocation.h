/* Copyright (c) 2011-2018, Stanford University
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HOMA_CODELOCATION_H
#define HOMA_CODELOCATION_H

#include <cstdint>
#include <string>

namespace Homa {

/**
 * Describes the location of a line of code.
 * You can get one of these with #HERE.
 */
// This was mostly taken from the RAMCloud project.
struct CodeLocation {
    /// Called by #HERE only.
    CodeLocation(const char* file, const uint32_t line, const char* function,
                 const char* prettyFunction)
        : file(file)
        , line(line)
        , function(function)
        , prettyFunction(prettyFunction)
    {}

    std::string str() const;
    const char* baseFileName() const;
    std::string relativeFile() const;
    std::string qualifiedFunction() const;

    /// __FILE__
    const char* file;
    /// __LINE__
    uint32_t line;
    /// __func__
    const char* function;
    /// __PRETTY_FUNCTION__
    const char* prettyFunction;
};

/**
 * \def HERE
 * Constructs a #CodeLocation describing the line from where it is used.
 */
#define HERE \
    Homa::CodeLocation(__FILE__, __LINE__, __func__, __PRETTY_FUNCTION__)

/**
 * Constructs a string representation of #CodeLocation destrcibing the line
 * from where this macro is invoked.
 */
#define HERE_STR \
    Homa::CodeLocation(__FILE__, __LINE__, __func__, __PRETTY_FUNCTION__).str()

}  // namespace Homa

#endif  // HOMA_CODELOCATION_H
