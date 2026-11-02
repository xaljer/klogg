//
// Created by omni on 16.12.2019.
//

#ifndef KLOGG_ENCODINGS_H
#define KLOGG_ENCODINGS_H

#include <map>
#include <string>
#include <vector>

// Text encodings offered by klogg, grouped by language family.
// Values are MIB enums (see klogg::Encoding in logdata's encoding.h).
// Pure data shared by the UI encoding menu/options dialog and the Core API.
class Encodings {
public:
    static std::map<std::string, std::vector<int>> supportedEncodings()
    {
        return std::map<std::string, std::vector<int>>{
            { "Unicode", { 106, 1013, 1014, 1018, 1019 } },
            { "Arabic", { 82, 2256 } },
            { "Baltic", { 7, 109, 2257 } },
            { "Celtic", { 110 } },
            { "Cyrillic", { 8, 2084, 2088, 2027, 2086, 2251 } },
            { "Central European", { 2250 } },
            { "Chinese", { 2026, 2025 } },
            { "Eastern European", { 5 } },
            { "Greek", { 10, 2253 } },
            { "Hebrew", { 85, 2255 } },
            { "Japanese", { 17, 18, 39 } },
            { "Korean", { -949, 38 } },
            { "Thai", { 2259 } },
            { "Turkish", { 6, 12, 2254 } },
            { "Western European", { 3, 4, 111, 2009, 2252 } },
            { "Vietnamese", { 2258 } }
        };
    }
};

#endif // KLOGG_ENCODINGS_H
