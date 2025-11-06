/**
 * camt parser - version 1.00
 * --------------------------------------------------------
 * Report bugs and download new versions at https://github.com/psynetic-software/camt-parser
 *
 * SPDX-FileCopyrightText: 2025 psynectic
 * SPDX-License-Identifier: MIT
 * 
 */
 
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <sstream>
#include <algorithm>
#include <map>


namespace camt {

// ----------------------------- Embedded CSV -----------------------------
// Format:
// GVC;DC;Domain;Family;SubFamily;DomDesc;FamDesc;SubDesc;Comment
extern const char* kGvcCsvEmbedded;

inline std::string trim_copy(std::string_view sv) {
    auto is_space = [](unsigned char c){ return std::isspace(c) != 0; };
    size_t b = 0, e = sv.size();
    while (b < e && is_space(static_cast<unsigned char>(sv[b]))) ++b;
    while (e > b && is_space(static_cast<unsigned char>(sv[e-1]))) --e;
    return std::string(sv.substr(b, e - b));
}

inline std::string upper_trim(std::string_view s) {
    std::string r = trim_copy(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return r;
}

inline std::vector<std::string> split_semicolon(std::string_view line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= line.size()) {
        size_t pos = line.find(';', start);
        if (pos == std::string_view::npos) {
            out.emplace_back(trim_copy(line.substr(start)));
            break;
        }
        out.emplace_back(trim_copy(line.substr(start, pos - start)));
        start = pos + 1;
    }
    return out;
}

// ----------------------------- Minimal Map-Type ---------------------------
// Key = "PMNT;RCDT;VCOM;C"   (Domain;Family;SubFamily;C|D)
// Val = "058"                (three-digit GVC/ISO-Code)

using GvcKey = std::string;
using GvcMap = std::multimap<GvcKey, std::string>;

// Builds the map from the embedded CSV.
inline GvcMap build_gvc_map_from_embedded() {
    GvcMap map;
    std::istringstream iss(std::string{camt::kGvcCsvEmbedded});
    std::string line;
    int num_lines = 0;
    // First line may be a header -> just read everything and filter
    while (std::getline(iss, line)) {
        auto cols = split_semicolon(line);
        if (cols.size() < 5) 
            continue;
        if (!cols[0].empty() && cols[0] == "GVC") continue; // Header

        const std::string iso     = trim_copy(cols[0]);            // "058"
        const char        crFlag  = cols[1].empty() ? '\0' : cols[1][0]; // 'C'/'D'
        const std::string domain  = upper_trim(cols[2]);           // "PMNT"
        const std::string family  = upper_trim(cols[3]);           // "RCDT"
        const std::string variant = upper_trim(cols[4]);           // "VCOM"

        if (iso.empty() || (crFlag!='C' && crFlag!='D') ||
            domain.empty() || family.empty() || variant.empty())
            continue;

        // Build key: "PMNT;RCDT;VCOM;C"
        GvcKey key = domain + ";" + family + ";" + variant + ";" + std::string(1, crFlag);
        map.insert({ key,iso }); // ISO/GVC Number
        num_lines++;
    }
    return map;
}

// Singleton access (build once, then reuse)
inline const GvcMap& get_gvc_map() {
    static GvcMap M = build_gvc_map_from_embedded();
    return M;
}

// Lookup: build key from 3 codes + C/D
inline std::string lookup_gvc(const GvcMap& m,
                              std::string_view domain,
                              std::string_view family,
                              std::string_view variant,
                              char crFlag)
{
    GvcKey key = upper_trim(domain) + ";" +
                 upper_trim(family) + ";" +
                 upper_trim(variant) + ";" +
                 std::string(1, crFlag);
    if (auto it = m.find(key); it != m.end()) return it->second;
    return {};
}


} // namespace camt


