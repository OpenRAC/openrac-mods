// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The region table (regions.hpp): the Server's Regions.java codes in its order, with the English
// names and ISO alpha-3 codes of the country codes, then EU and LOCAL.

#include "regions.hpp"

#include <algorithm>
#include <iterator>

namespace mp {
namespace {

// clang-format off
constexpr Region kRegions[] = {
    {"AD", "AND", "Andorra"},
    {"AE", "ARE", "United Arab Emirates"},
    {"AF", "AFG", "Afghanistan"},
    {"AG", "ATG", "Antigua and Barbuda"},
    {"AI", "AIA", "Anguilla"},
    {"AL", "ALB", "Albania"},
    {"AM", "ARM", "Armenia"},
    {"AO", "AGO", "Angola"},
    {"AQ", "ATA", "Antarctica"},
    {"AR", "ARG", "Argentina"},
    {"AS", "ASM", "American Samoa"},
    {"AT", "AUT", "Austria"},
    {"AU", "AUS", "Australia"},
    {"AW", "ABW", "Aruba"},
    {"AX", "ALA", "Åland Islands"},
    {"AZ", "AZE", "Azerbaijan"},
    {"BA", "BIH", "Bosnia and Herzegovina"},
    {"BB", "BRB", "Barbados"},
    {"BD", "BGD", "Bangladesh"},
    {"BE", "BEL", "Belgium"},
    {"BF", "BFA", "Burkina Faso"},
    {"BG", "BGR", "Bulgaria"},
    {"BH", "BHR", "Bahrain"},
    {"BI", "BDI", "Burundi"},
    {"BJ", "BEN", "Benin"},
    {"BL", "BLM", "St. Barthélemy"},
    {"BM", "BMU", "Bermuda"},
    {"BN", "BRN", "Brunei"},
    {"BO", "BOL", "Bolivia"},
    {"BQ", "BES", "Caribbean Netherlands"},
    {"BR", "BRA", "Brazil"},
    {"BS", "BHS", "Bahamas"},
    {"BT", "BTN", "Bhutan"},
    {"BV", "BVT", "Bouvet Island"},
    {"BW", "BWA", "Botswana"},
    {"BY", "BLR", "Belarus"},
    {"BZ", "BLZ", "Belize"},
    {"CA", "CAN", "Canada"},
    {"CC", "CCK", "Cocos (Keeling) Islands"},
    {"CD", "COD", "Congo - Kinshasa"},
    {"CF", "CAF", "Central African Republic"},
    {"CG", "COG", "Congo - Brazzaville"},
    {"CH", "CHE", "Switzerland"},
    {"CI", "CIV", "Côte d'Ivoire"},
    {"CK", "COK", "Cook Islands"},
    {"CL", "CHL", "Chile"},
    {"CM", "CMR", "Cameroon"},
    {"CN", "CHN", "China"},
    {"CO", "COL", "Colombia"},
    {"CR", "CRI", "Costa Rica"},
    {"CU", "CUB", "Cuba"},
    {"CV", "CPV", "Cape Verde"},
    {"CW", "CUW", "Curaçao"},
    {"CX", "CXR", "Christmas Island"},
    {"CY", "CYP", "Cyprus"},
    {"CZ", "CZE", "Czechia"},
    {"DE", "DEU", "Germany"},
    {"DJ", "DJI", "Djibouti"},
    {"DK", "DNK", "Denmark"},
    {"DM", "DMA", "Dominica"},
    {"DO", "DOM", "Dominican Republic"},
    {"DZ", "DZA", "Algeria"},
    {"EC", "ECU", "Ecuador"},
    {"EE", "EST", "Estonia"},
    {"EG", "EGY", "Egypt"},
    {"EH", "ESH", "Western Sahara"},
    {"ER", "ERI", "Eritrea"},
    {"ES", "ESP", "Spain"},
    {"ET", "ETH", "Ethiopia"},
    {"FI", "FIN", "Finland"},
    {"FJ", "FJI", "Fiji"},
    {"FK", "FLK", "Falkland Islands"},
    {"FM", "FSM", "Micronesia"},
    {"FO", "FRO", "Faroe Islands"},
    {"FR", "FRA", "France"},
    {"GA", "GAB", "Gabon"},
    {"GB", "GBR", "United Kingdom"},
    {"GD", "GRD", "Grenada"},
    {"GE", "GEO", "Georgia"},
    {"GF", "GUF", "French Guiana"},
    {"GG", "GGY", "Guernsey"},
    {"GH", "GHA", "Ghana"},
    {"GI", "GIB", "Gibraltar"},
    {"GL", "GRL", "Greenland"},
    {"GM", "GMB", "Gambia"},
    {"GN", "GIN", "Guinea"},
    {"GP", "GLP", "Guadeloupe"},
    {"GQ", "GNQ", "Equatorial Guinea"},
    {"GR", "GRC", "Greece"},
    {"GS", "SGS", "South Georgia and South Sandwich Islands"},
    {"GT", "GTM", "Guatemala"},
    {"GU", "GUM", "Guam"},
    {"GW", "GNB", "Guinea-Bissau"},
    {"GY", "GUY", "Guyana"},
    {"HK", "HKG", "Hong Kong SAR China"},
    {"HM", "HMD", "Heard and McDonald Islands"},
    {"HN", "HND", "Honduras"},
    {"HR", "HRV", "Croatia"},
    {"HT", "HTI", "Haiti"},
    {"HU", "HUN", "Hungary"},
    {"ID", "IDN", "Indonesia"},
    {"IE", "IRL", "Ireland"},
    {"IL", "ISR", "Israel"},
    {"IM", "IMN", "Isle of Man"},
    {"IN", "IND", "India"},
    {"IO", "IOT", "British Indian Ocean Territory"},
    {"IQ", "IRQ", "Iraq"},
    {"IR", "IRN", "Iran"},
    {"IS", "ISL", "Iceland"},
    {"IT", "ITA", "Italy"},
    {"JE", "JEY", "Jersey"},
    {"JM", "JAM", "Jamaica"},
    {"JO", "JOR", "Jordan"},
    {"JP", "JPN", "Japan"},
    {"KE", "KEN", "Kenya"},
    {"KG", "KGZ", "Kyrgyzstan"},
    {"KH", "KHM", "Cambodia"},
    {"KI", "KIR", "Kiribati"},
    {"KM", "COM", "Comoros"},
    {"KN", "KNA", "St. Kitts and Nevis"},
    {"KP", "PRK", "North Korea"},
    {"KR", "KOR", "South Korea"},
    {"KW", "KWT", "Kuwait"},
    {"KY", "CYM", "Cayman Islands"},
    {"KZ", "KAZ", "Kazakhstan"},
    {"LA", "LAO", "Laos"},
    {"LB", "LBN", "Lebanon"},
    {"LC", "LCA", "St. Lucia"},
    {"LI", "LIE", "Liechtenstein"},
    {"LK", "LKA", "Sri Lanka"},
    {"LR", "LBR", "Liberia"},
    {"LS", "LSO", "Lesotho"},
    {"LT", "LTU", "Lithuania"},
    {"LU", "LUX", "Luxembourg"},
    {"LV", "LVA", "Latvia"},
    {"LY", "LBY", "Libya"},
    {"MA", "MAR", "Morocco"},
    {"MC", "MCO", "Monaco"},
    {"MD", "MDA", "Moldova"},
    {"ME", "MNE", "Montenegro"},
    {"MF", "MAF", "St. Martin"},
    {"MG", "MDG", "Madagascar"},
    {"MH", "MHL", "Marshall Islands"},
    {"MK", "MKD", "North Macedonia"},
    {"ML", "MLI", "Mali"},
    {"MM", "MMR", "Myanmar (Burma)"},
    {"MN", "MNG", "Mongolia"},
    {"MO", "MAC", "Macao SAR China"},
    {"MP", "MNP", "Northern Mariana Islands"},
    {"MQ", "MTQ", "Martinique"},
    {"MR", "MRT", "Mauritania"},
    {"MS", "MSR", "Montserrat"},
    {"MT", "MLT", "Malta"},
    {"MU", "MUS", "Mauritius"},
    {"MV", "MDV", "Maldives"},
    {"MW", "MWI", "Malawi"},
    {"MX", "MEX", "Mexico"},
    {"MY", "MYS", "Malaysia"},
    {"MZ", "MOZ", "Mozambique"},
    {"NA", "NAM", "Namibia"},
    {"NC", "NCL", "New Caledonia"},
    {"NE", "NER", "Niger"},
    {"NF", "NFK", "Norfolk Island"},
    {"NG", "NGA", "Nigeria"},
    {"NI", "NIC", "Nicaragua"},
    {"NL", "NLD", "Netherlands"},
    {"NO", "NOR", "Norway"},
    {"NP", "NPL", "Nepal"},
    {"NR", "NRU", "Nauru"},
    {"NU", "NIU", "Niue"},
    {"NZ", "NZL", "New Zealand"},
    {"OM", "OMN", "Oman"},
    {"PA", "PAN", "Panama"},
    {"PE", "PER", "Peru"},
    {"PF", "PYF", "French Polynesia"},
    {"PG", "PNG", "Papua New Guinea"},
    {"PH", "PHL", "Philippines"},
    {"PK", "PAK", "Pakistan"},
    {"PL", "POL", "Poland"},
    {"PM", "SPM", "St. Pierre and Miquelon"},
    {"PN", "PCN", "Pitcairn Islands"},
    {"PR", "PRI", "Puerto Rico"},
    {"PS", "PSE", "Palestinian Territories"},
    {"PT", "PRT", "Portugal"},
    {"PW", "PLW", "Palau"},
    {"PY", "PRY", "Paraguay"},
    {"QA", "QAT", "Qatar"},
    {"RE", "REU", "Réunion"},
    {"RO", "ROU", "Romania"},
    {"RS", "SRB", "Serbia"},
    {"RU", "RUS", "Russia"},
    {"RW", "RWA", "Rwanda"},
    {"SA", "SAU", "Saudi Arabia"},
    {"SB", "SLB", "Solomon Islands"},
    {"SC", "SYC", "Seychelles"},
    {"SD", "SDN", "Sudan"},
    {"SE", "SWE", "Sweden"},
    {"SG", "SGP", "Singapore"},
    {"SH", "SHN", "St. Helena"},
    {"SI", "SVN", "Slovenia"},
    {"SJ", "SJM", "Svalbard and Jan Mayen"},
    {"SK", "SVK", "Slovakia"},
    {"SL", "SLE", "Sierra Leone"},
    {"SM", "SMR", "San Marino"},
    {"SN", "SEN", "Senegal"},
    {"SO", "SOM", "Somalia"},
    {"SR", "SUR", "Suriname"},
    {"SS", "SSD", "South Sudan"},
    {"ST", "STP", "São Tomé and Príncipe"},
    {"SV", "SLV", "El Salvador"},
    {"SX", "SXM", "Sint Maarten"},
    {"SY", "SYR", "Syria"},
    {"SZ", "SWZ", "Eswatini"},
    {"TC", "TCA", "Turks and Caicos Islands"},
    {"TD", "TCD", "Chad"},
    {"TF", "ATF", "French Southern Territories"},
    {"TG", "TGO", "Togo"},
    {"TH", "THA", "Thailand"},
    {"TJ", "TJK", "Tajikistan"},
    {"TK", "TKL", "Tokelau"},
    {"TL", "TLS", "Timor-Leste"},
    {"TM", "TKM", "Turkmenistan"},
    {"TN", "TUN", "Tunisia"},
    {"TO", "TON", "Tonga"},
    {"TR", "TUR", "Türkiye"},
    {"TT", "TTO", "Trinidad and Tobago"},
    {"TV", "TUV", "Tuvalu"},
    {"TW", "TWN", "Taiwan"},
    {"TZ", "TZA", "Tanzania"},
    {"UA", "UKR", "Ukraine"},
    {"UG", "UGA", "Uganda"},
    {"UM", "UMI", "U.S. Outlying Islands"},
    {"US", "USA", "United States"},
    {"UY", "URY", "Uruguay"},
    {"UZ", "UZB", "Uzbekistan"},
    {"VA", "VAT", "Vatican City"},
    {"VC", "VCT", "St. Vincent and Grenadines"},
    {"VE", "VEN", "Venezuela"},
    {"VG", "VGB", "British Virgin Islands"},
    {"VI", "VIR", "U.S. Virgin Islands"},
    {"VN", "VNM", "Vietnam"},
    {"VU", "VUT", "Vanuatu"},
    {"WF", "WLF", "Wallis and Futuna"},
    {"WS", "WSM", "Samoa"},
    {"YE", "YEM", "Yemen"},
    {"YT", "MYT", "Mayotte"},
    {"ZA", "ZAF", "South Africa"},
    {"ZM", "ZMB", "Zambia"},
    {"ZW", "ZWE", "Zimbabwe"},
    {"EU", "EU", "Europe"},
    {"LOCAL", "LAN", "Local network"},
};
// clang-format on

// Returns `text` without the spaces around it, in lower case (ASCII; the table's codes and labels
// are ASCII, and a name's accents only need to match themselves).
std::string folded(const std::string& text) {
    const std::size_t first = text.find_first_not_of(' ');
    const std::size_t last = text.find_last_not_of(' ');

    // Nothing but spaces names nothing.
    if (first == std::string::npos) {
        return "";
    }

    std::string out = text.substr(first, last - first + 1);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return out;
}

}  // namespace

const Region* find_region(const std::string& text) {
    const std::string wanted = folded(text);

    // An empty setting names no region.
    if (wanted.empty()) {
        return nullptr;
    }

    // A code, a label or a name, whichever it is.
    for (const Region& r : kRegions) {
        // Any of the three forms a player could type.
        if (wanted == folded(r.code) || wanted == folded(r.label) || wanted == folded(r.name)) {
            return &r;
        }
    }

    return nullptr;
}

std::string region_text(const std::string& code, bool short_form) {
    // The hub sends the code upper case; an older hub's free text matches no code, and stays.
    for (const Region& r : kRegions) {
        // Only the code is on the wire.
        if (folded(code) == folded(r.code)) {
            return short_form ? r.label : r.name;
        }
    }

    return code;
}

std::string server_text(const std::string& name, const std::string& code, std::size_t max_chars) {
    const std::string region = region_text(code);
    const std::string label = region_text(code, true);
    const auto fits = [max_chars](const std::string& text) {
        return max_chars == 0 || text.size() <= max_chars;
    };

    // An unnamed server is its region: the name, else the label.
    if (name.empty()) {
        return fits(region) ? region : label;
    }

    const std::string full = name + " (" + region + ")";
    const std::string tail = " (" + label + ")";

    // The region's name when there is room, its label otherwise, then the server's name cut.
    if (fits(full)) {
        return full;
    }

    // The label, short enough for most names.
    if (fits(name + tail)) {
        return name + tail;
    }

    return name.substr(0, max_chars > tail.size() ? max_chars - tail.size() : 0) + tail;
}

std::size_t region_count() {
    return std::size(kRegions);
}

const Region& region_at(std::size_t index) {
    return kRegions[index];
}

}  // namespace mp
