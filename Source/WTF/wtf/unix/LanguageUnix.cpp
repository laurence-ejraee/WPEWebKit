/*
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2016, 2017 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <wtf/Language.h>

#include <locale.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

#include <wtf/ASCIICType.h>
#include <fstream>
#include <iostream>

#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>

#define gettid() syscall(SYS_gettid)
#define fprintLog(fmt, ...) \
do { \
pid_t tid = gettid(); \
fprintf(stdout, "\n tid=%ld %s:%s:%d << " fmt "\n", (long)tid, __FILE__,__FUNCTION__,__LINE__ , ##__VA_ARGS__); \
fflush(stdout); \
} while (0)


namespace WTF {

namespace // anonymous
{

std::string convertToIETFLanguageTag(std::string lang)
{
//    English             eng   en_US
//    Bahasa Malaysia     may   ms
//    Chinese             chi   zh
//    Tamil               tam   ta

    std::string result{"en_US"};
    if (lang == "eng") {
        result = "en_US";
    } else if (lang == "may") {
        result = "ms";
    } else if (lang == "chi") {
        result = "zh";
    } else if (lang == "tam") {
        result = "ta";
    } else {
        fprintLog("WARNING: unknown language %s, using default %s", lang.c_str(),  result.c_str());
    }
    return result;
}

// Trim whitespace from left and right side of string
void trimWhitespace(std::string &str)
{
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
        return !isASCIISpace(ch);
    }));
    str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
        return !isASCIISpace(ch);
    }).base(), str.end());
}


std::string getLanguageFromIniFile()
{
    std::string language{"en_US"};
    const std::string iniFilePath{"/applications/settings/wpebrowser/EpgSettings.ini"};

    do {
        std::fstream iniFile(iniFilePath);

        if (!iniFile.is_open()){
            fprintLog("ERROR %s opening file %s", iniFilePath.c_str(),  std::strerror(errno));
            break;
        }
        std::string readLine;
        std::string readLanguage;

        while (std::getline(iniFile, readLine)) {

            // Trim whitespace from left and right ends
            trimWhitespace(readLine);

            // ignore blank lines
            if (readLine.length() == 0) {
                continue;
            }

            // new section header?
            if (readLine.front() == '[' && readLine.back() == ']') {
                continue;
            }

            // parameter assignment: break it into parameter and value
            auto eqpos = readLine.find('=');
            if (eqpos == std::string::npos) {
                continue;
            }

            std::string param = readLine.substr(0, eqpos);
            std::string value = readLine.substr(eqpos+1, std::string::npos);

            // parse parameters...
            if (param == "langCode") {
                language = value;
                break;
            }
        }

    } while(false);

    auto languageTag{convertToIETFLanguageTag(language)};
    return languageTag;
}

} // namespace anonymous


// Using pango_language_get_default() here is not an option, because
// it doesn't support changing the locale in runtime, so it returns
// always the same value.
static String platformLanguage()
{
    String localeDefault(setlocale(LC_CTYPE, nullptr));
    if (localeDefault.isEmpty() || equalIgnoringASCIICase(localeDefault, "C") || equalIgnoringASCIICase(localeDefault, "POSIX"))
        return "en-US"_s;

    String normalizedDefault = localeDefault;
    normalizedDefault.replace('_', '-');
    normalizedDefault.truncate(normalizedDefault.find('.'));
    return normalizedDefault;
}

Vector<String> platformUserPreferredLanguages()
{
//    return { platformLanguage() };
    return { String{getLanguageFromIniFile().c_str()} };
}

} // namespace WTF
