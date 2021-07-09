/* Copyright (c) 2011-2018 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sstream>
#include <string>
#include <vector>

#ifndef HOMA_STRINGUTIL_H
#define HOMA_STRINGUTIL_H

namespace Homa {
namespace StringUtil {

std::string flags(int value,
                  std::initializer_list<std::pair<int, const char*>> flags);
std::string format(const char* format, ...)
    __attribute__((format(printf, 1, 2)));
bool isPrintable(const char* str);
bool isPrintable(const void* data, size_t length);
std::string join(const std::vector<std::string>& components,
                 const std::string& glue);
void replaceAll(std::string& haystack, const std::string& needle,
                const std::string& replacement);
std::vector<std::string> split(const std::string& subject, char delimiter);
bool startsWith(const std::string& haystack, const std::string& needle);
bool endsWith(const std::string& haystack, const std::string& needle);

/**
 * Return a string returned from the given object's stream operator.
 * This is useful when you're dealing with strings, but the object you want to
 * print only has a stream operator.
 */
template <typename T>
std::string
toString(const T& t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

std::string trim(const std::string& s);

}  // namespace StringUtil
}  // namespace Homa

#endif  // HOMA_STRINGUTIL_H
