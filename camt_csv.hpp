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
#include "camt_model.hpp"
#include "camt_parser_pugi.hpp"
#include "gvc_map.hpp"
#include <ostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <charconv>
#ifdef USE_UTF8PROC
#include <utf8proc.h>
#include <memory>
#endif

namespace camt {

#ifdef USE_UTF8PROC

    // RAII deleter for buffers allocated by utf8proc_map (uses malloc internally)
    struct Utf8ProcDeleter {
        void operator()(utf8proc_uint8_t* p) const noexcept { if (p) free(p); }
    };

    // Check if codepoint is considered whitespace (Unicode separators + ASCII controls)
    inline bool isUnicodeSpaceOrControlWS(utf8proc_int32_t cp) {
        const int cat = utf8proc_category(cp);
        // Unicode categories for space, line, and paragraph separators
        if (cat == UTF8PROC_CATEGORY_ZS ||
            cat == UTF8PROC_CATEGORY_ZL ||
            cat == UTF8PROC_CATEGORY_ZP) {
            return true;
        }
        // Common ASCII control whitespaces
        switch (cp) {
        case 0x09: // \t
        case 0x0A: // \n
        case 0x0B: // \v
        case 0x0C: // \f
        case 0x0D: // \r
            return true;
        default:
            return false;
        }
    }

    inline std::string normalize_freetext(std::string_view in,
        bool do_casefold = true,
        bool strip_zero_width = true)
    {
        utf8proc_uint8_t* raw = nullptr;
        utf8proc_option_t opts = UTF8PROC_COMPOSE; // NFC
        if (do_casefold) opts = (utf8proc_option_t)(opts | UTF8PROC_CASEFOLD);

        const utf8proc_ssize_t nlen = utf8proc_map(
            reinterpret_cast<const utf8proc_uint8_t*>(in.data()),
            static_cast<utf8proc_ssize_t>(in.size()),
            &raw, opts
        );
        if (nlen < 0 || !raw) {
            return std::string(in); // Fallback: return original input
        }
        std::unique_ptr<utf8proc_uint8_t, Utf8ProcDeleter> norm(raw);

        std::string out;
        out.reserve(static_cast<size_t>(nlen));
        const utf8proc_uint8_t* p   = norm.get();
        const utf8proc_uint8_t* end = norm.get() + nlen;

        while (p < end) {
            utf8proc_int32_t cp = 0;
            const utf8proc_ssize_t adv =
                utf8proc_iterate(p, (utf8proc_ssize_t)(end - p), &cp);
            if (adv <= 0) { // Skip invalid byte
                ++p;
                continue;
            }
            p += adv;

            // Remove Unicode and ASCII whitespace
            if (isUnicodeSpaceOrControlWS(cp)) {
                continue;
            }

            // Optionally strip zero-width characters
            if (strip_zero_width) {
                switch (cp) {
                case 0x200B: // ZERO WIDTH SPACE
                case 0x200C: // ZERO WIDTH NON-JOINER
                case 0x200D: // ZERO WIDTH JOINER
                case 0x2060: // WORD JOINER
                case 0xFEFF: // BOM / ZERO WIDTH NO-BREAK SPACE
                    continue;
                }
            }

            // Encode valid codepoint back to UTF-8
            utf8proc_uint8_t buf[4];
            const utf8proc_ssize_t w = utf8proc_encode_char(cp, buf);
            if (w > 0) {
                out.append(reinterpret_cast<char*>(buf), (size_t)w);
            }
        }

        return out;
    }

#else // USE_UTF8PROC not defined

    // Minimal ASCII-only fallback:
    // - strips ASCII whitespace
    // - lowercases A–Z
    // - leaves all non-ASCII bytes untouched
    inline std::string normalize_freetext(std::string_view in, bool do_casefold = true, bool strip_zero_width = true) {
        std::string out;
        out.reserve(in.size());
        for (unsigned char c : in) {
            if (c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\f' || c=='\v')
                continue;
            if (c < 0x80) {
                out.push_back(static_cast<char>(std::tolower(c)));
            } else {
                out.push_back(static_cast<char>(c)); // keep UTF-8 byte
            }
        }
        return out;
    }

#endif // USE_UTF8PROC


struct ExportOptions {
    char delimiter = ';';          
    bool include_header = true;
    bool write_utf8_bom = false;   // Excel-compatible
    
    // true  => amount with sign (CRDT=+ / DBIT=-)
    // false => amount always positive, sign only in "CreditDebit"
    bool signed_amount = true;

    //CSV column as Bool (1/0) instead of "CRDT"/"DBIT"
    bool credit_as_bool = true;  // true => "IsCredit"; false => "CreditDebit"
    std::string remittance_separator; // = " | ";

    bool use_effective_credit = false;
    bool prefer_ultimate_counterparty = true;
};

inline std::string csv_escape(const std::string& s, char delimiter) {
    bool needQuotes = s.find(delimiter) != std::string::npos ||
                      s.find('"')       != std::string::npos ||
                      s.find('\n')      != std::string::npos ||
                      s.find('\r')      != std::string::npos;
    std::string out = s;
    // double quotes
    for (size_t pos = 0; (pos = out.find('"', pos)) != std::string::npos; pos += 2)
        out.insert(pos, "\"");
    if (needQuotes) {
        out.insert(out.begin(), '"');
        out.push_back('"');
    }
    return out;
}

// CurrencyAmount -> "123,45" / "123.45"
inline std::string fmt_amount(const CurrencyAmount& a, bool use_decimal_comma=false) {
    const int exp = ccy_exp(a.currency);
    int64_t v = a.minor;
    bool neg = v < 0; if (neg) v = -v;
    int64_t pow10 = 1; for (int i=0;i<exp;++i) pow10 *= 10;
    int64_t major = v / pow10;
    int64_t frac  = v % pow10;

    std::ostringstream oss;
    if (neg) oss << '-';
    oss << major;
    if (exp > 0) {
        oss << (use_decimal_comma ? ',' : '.')
            << std::setw(exp) << std::setfill('0') << frac;
    }
    return oss.str();
}

struct ChargesSummary {
    CurrencyAmount total;  // Sum (sign depending on option)
    bool anyIncluded = false; // whether at least one record had ChrgInclInd=true
};

inline int64_t apply_sign(int64_t minor, bool hasInd, bool isCredit) {
    if (!hasInd) return (minor >= 0 ? minor : -minor); // without indicator: treat as positive
    int64_t absM = (minor >= 0 ? minor : -minor);
    return isCredit ? absM : -absM; // CRDT=+, DBIT=-
}


using CAMTRow = std::vector<std::pair<std::string,std::string>>;
using ExportData = std::vector<CAMTRow>;

enum class ExportField { 
    BookingDate,
    ValueDate,
    Amount,
    CreditDebit,       
    Currency,
    CounterpartyName,
    CounterpartyIBAN,
    CounterpartyBIC,
    RemittanceLine,
    RemittanceStructured,
    EndToEndId,
    MandateId,
    TxId,
    BankRef,
    AccountIBAN,
    AccountBIC,
    BkTxCd,
    BookingCode,
    Status,
    Reversal,
    RunningBalance,
    ServicerBankName,
    OpeningBalance,
    ClosingBalance,
    Primanota,
    DTACode,
    GVCCode,
    SWIFTTransactionCode,
    ChargesAmount,
    ChargesCurrency,
    ChargesIncluded,
    EntryOrdinal,       
    TransactionOrdinal, 
    Count // Array size 
};

constexpr std::size_t to_index(ExportField f) noexcept {
    return static_cast<std::size_t>(f);
}

// ----------------------- minimal ASCII utilities (UTF-8 safe)" ------------------
inline std::string ascii_trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b]==' '||s[b]=='\t'||s[b]=='\n'||s[b]=='\r'||s[b]=='\f'||s[b]=='\v')) ++b;
    while (e > b && (s[e-1]==' '||s[e-1]=='\t'||s[e-1]=='\n'||s[e-1]=='\r'||s[e-1]=='\f'||s[e-1]=='\v')) --e;
    return std::string(s.substr(b, e-b));
}
inline std::string ascii_strip_all_spaces(std::string_view s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) {
        if (c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v') continue;
        out.push_back(static_cast<char>(c));
    }
    return out;
}
inline std::string ascii_upper_preserve_utf8(std::string_view s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(c < 0x80 ? std::toupper(c) : c));
    }
    return out;
}
inline std::string ascii_lower_preserve_utf8(std::string_view s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(c < 0x80 ? std::tolower(c) : c));
    }
    return out;
}

inline std::string normalize_field(ExportField f, std::string_view v)
{
    switch (f) {
        // Free text: robust normalize (NFC + casefold + strip spaces/zero-width)
    case ExportField::RemittanceLine:
    case ExportField::RemittanceStructured:
    case ExportField::CounterpartyName:
        return normalize_freetext(v, /*casefold=*/true, /*strip_zerowidth=*/true);

        // IDs/References: remove inner spaces, usually uppercase (platform-stable)
    case ExportField::EndToEndId:
    case ExportField::MandateId:
    case ExportField::TxId:
    case ExportField::BankRef:
    case ExportField::Primanota: {
        auto s = ascii_strip_all_spaces(v);
        return ascii_upper_preserve_utf8(s); // conservative: Uppercase
    }

    // IBAN/BIC: remove spaces + uppercase (standard)
    case ExportField::AccountIBAN:
    case ExportField::CounterpartyIBAN: {
        auto s = ascii_strip_all_spaces(v);
        return ascii_upper_preserve_utf8(s);
    }
    case ExportField::AccountBIC:
    case ExportField::CounterpartyBIC: {
        auto s = ascii_strip_all_spaces(v);
        return ascii_upper_preserve_utf8(s);
    }

    // Codes: trim + uppercase (ISO/Bank codes are normatively uppercase)
    case ExportField::Currency:
    case ExportField::ChargesCurrency:
    case ExportField::CreditDebit:
    case ExportField::BkTxCd:
    case ExportField::BookingCode:
    case ExportField::DTACode:
    case ExportField::GVCCode:
    case ExportField::SWIFTTransactionCode: {
        auto s = ascii_trim(v);
        return ascii_upper_preserve_utf8(s);
    }

    // Date/Amount/Status fields: trim only (never modify values!)
    case ExportField::BookingDate:
    case ExportField::ValueDate:
    case ExportField::Amount:
    case ExportField::ChargesAmount:
    case ExportField::RunningBalance:
    case ExportField::OpeningBalance:
    case ExportField::ClosingBalance:
    case ExportField::Status:
    case ExportField::Reversal:
    case ExportField::ServicerBankName:
    case ExportField::ChargesIncluded:
    case ExportField::EntryOrdinal:
    case ExportField::TransactionOrdinal:
        return ascii_trim(v);

    // Default: conservative trimming
    default:
        return ascii_trim(v);
    }
}

// Unified normalize_row
inline void normalize_or_accumulate_row(
    CAMTRow& row,
    std::initializer_list<ExportField> fields = {},
    bool includeMode = false,      // true = whitelist, false = blacklist
    std::string* accumulate = nullptr // !=nullptr → append normalized values only
) {
    const std::size_t n = std::min<std::size_t>(row.size(), static_cast<size_t>(ExportField::Count));

    // Append normalized item as "index=value<US>"
    auto append_item = [](std::string& acc, ExportField f, const std::string& value) {
        acc.append(std::to_string(to_index(f)));
        acc.push_back('=');          // index/value separator
        acc.append(value);
        acc.push_back('\x1F');       // field separator (Unit Separator, U+001F)
    };

    // Helper: contains for initializer_list
    auto contains = [](std::initializer_list<ExportField> list, ExportField f) noexcept {
        for (auto x : list) if (x == f) return true;
        return false;
    };

    for (std::size_t i = 0; i < n; ++i) {
        const auto f = static_cast<ExportField>(i);

        const bool selected = (fields.size() == 0)
            ? true
            : (includeMode ? contains(fields, f)  : !contains(fields, f));

        if (!selected) 
            continue;

        if (accumulate) {
            append_item(*accumulate, f, row[i].second);
        } else {  // in-place normalization
            if (row[i].second.empty()) {
                row[i].second = normalize_field(f, row[i].first);
            }    
        }
    }
}

#if 0
inline bool sortExportData(ExportData& rows, bool hasTitle, bool useBookingDate)
{
    auto to_int_noexcept = [](const std::string& s, int64_t default_val = 0) {
        int64_t value = default_val;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec == std::errc()) {
            return value; // parsed ok
        }
        return default_val; // fallback
    };
    
    const std::size_t n = static_cast<size_t>(ExportField::Count);
    const std::size_t off = hasTitle ? 1 : 0;

    if (rows.size()<1+off)
    {
        return true;
    }
    if (rows[off].size() < n)
    {
        return false;
    }

    std::stable_sort(rows.begin()+off, rows.end(), [&](const auto& a, const auto& b)
    {
            const auto da = to_int_noexcept(a[to_index(useBookingDate ? ExportField::BookingDate : ExportField::ValueDate)].second);
            const auto db = to_int_noexcept(b[to_index(useBookingDate ? ExportField::BookingDate : ExportField::ValueDate)].second);
            if (da != db) 
                return da < db;
            
            const auto& ia = a[to_index(ExportField::AccountIBAN)].second;
            const auto& ib = b[to_index(ExportField::AccountIBAN)].second;
            if (ia != ib) 
                return ia < ib;

            const auto eoA = to_int_noexcept(a[to_index(ExportField::EntryOrdinal)].second);
            const auto eoB = to_int_noexcept(b[to_index(ExportField::EntryOrdinal)].second);
            if (eoA != eoB) 
                return eoA < eoB;

            const auto toA = to_int_noexcept(a[to_index(ExportField::TransactionOrdinal)].second);
            const auto toB = to_int_noexcept(b[to_index(ExportField::TransactionOrdinal)].second);
            if (toA != toB) 
                return toA < toB;

            return false;
    });

    double running = 0.0;
    for (auto &row : rows) {
        double amt = std::strtod(row[camt::to_index(camt::ExportField::Amount)].second.c_str(), nullptr);
        running += amt;
        row[camt::to_index(camt::ExportField::RunningBalance)].second = std::to_string(running);
    }

    return true;
}
#endif

inline bool sortExportData(ExportData& rows, bool hasTitle, bool useBookingDate)
{
    auto to_i64 = [](const std::string& s, int64_t d=0){
        int64_t v=d; auto [p,ec]=std::from_chars(s.data(), s.data()+s.size(), v);
        return ec==std::errc() ? v : d;
    };

    const size_t n   = static_cast<size_t>(ExportField::Count);
    const size_t off = hasTitle ? 1 : 0;
    if (rows.size() < 1 + off) { return true; }
    if (rows[off].size() < n)  { return false; }

    // 1) Sortierung: Datum (YYYYMMDD second), IBAN, EntryOrdinal, TransactionOrdinal
    std::stable_sort(rows.begin()+static_cast<std::ptrdiff_t>(off), rows.end(),
        [&](const auto& a, const auto& b){
            auto keyDate = useBookingDate ? ExportField::BookingDate : ExportField::ValueDate;
            auto da = to_i64(a[to_index(keyDate)].second);
            auto db = to_i64(b[to_index(keyDate)].second);
            if (da != db) return da < db;

            const auto& ia = a[to_index(ExportField::AccountIBAN)].second;
            const auto& ib = b[to_index(ExportField::AccountIBAN)].second;
            if (ia != ib) return ia < ib;

            auto eoA = to_i64(a[to_index(ExportField::EntryOrdinal)].second);
            auto eoB = to_i64(b[to_index(ExportField::EntryOrdinal)].second);
            if (eoA != eoB) return eoA < eoB;

            auto toA = to_i64(a[to_index(ExportField::TransactionOrdinal)].second);
            auto toB = to_i64(b[to_index(ExportField::TransactionOrdinal)].second);
            return toA < toB;
        }
    );

    // 2) Running Balance pro IBAN, mit Sign aus CreditDebit.second XOR Reversal.second
    // Amount.second ist absoluter Betrag (Text, Punkt als Dezimaltrennzeichen).
    auto frac = [](const std::string& s){ auto p=s.find('.'); return p==std::string::npos?0:int(s.size()-p-1); };
    auto parse_scaled = [&](const std::string& s, int scale){
        std::string ip=s, fp; auto p=s.find('.'); if(p!=std::string::npos){ ip=s.substr(0,p); fp=s.substr(p+1); }
        if ((int)fp.size() < scale) fp.append(size_t(scale - (int)fp.size()), '0'); else if ((int)fp.size() > scale) fp.resize(size_t(scale));
        if (ip.empty()) ip = "0";
        std::string all = ip + (scale?fp:"");
        int64_t v=0; if(!all.empty()){ auto [_,ec]=std::from_chars(all.data(), all.data()+all.size(), v); if(ec!=std::errc()) v=0; }
        return v;
    };
    auto fmt_scaled = [](int64_t v, int scale){
        bool neg = v<0; uint64_t u = neg? (uint64_t)(-v) : (uint64_t)v;
        std::string s = std::to_string(u);
        if (scale==0) { if(neg) s.insert(s.begin(), '-'); return s; }
        if (s.size() <= (size_t)scale) s.insert(s.begin(), size_t(scale+1 - s.size()), '0');
        size_t dot = s.size() - (size_t)scale; s.insert(s.begin()+std::ptrdiff_t(dot), '.');
        while (!s.empty() && s.back()=='0') s.pop_back();
        if (!s.empty() && s.back()=='.') s.pop_back();
        if (s.empty()) s="0";
        if (neg) s.insert(s.begin(), '-');
        return s;
    };

    struct Bal { int64_t v=0; int scale=0; };
    std::unordered_map<std::string, Bal> bal;

    for (size_t i=off; i<rows.size(); ++i)
    {
        auto& row = rows[i];
        const auto& iban = row[to_index(ExportField::AccountIBAN)].second;
        const auto& asec = row[to_index(ExportField::Amount)].second;       // absolut
        const bool credit = row[to_index(ExportField::CreditDebit)].second == "1";
        const bool reversal = row[to_index(ExportField::Reversal)].second == "1";

        int sign = credit ? +1 : -1;
        if (reversal) { sign = -sign; }

        Bal& b = bal[iban];
        int sScale = frac(asec);
        if (sScale > b.scale) { for(int k=0;k<sScale-b.scale;++k) { b.v *= 10; } b.scale = sScale; }

        int64_t delta = parse_scaled(asec, b.scale);
        b.v += sign * delta;

        row[to_index(ExportField::RunningBalance)].first=row[to_index(ExportField::RunningBalance)].second = fmt_scaled(b.v, b.scale);
        // optional: mirror .first:
        // row[to_index(ExportField::RunningBalance)].first = row[to_index(ExportField::RunningBalance)].second;
    }

    return true;
}


inline std::string accumulate_hash_row(const CAMTRow& row, const std::initializer_list<ExportField> fields = {})
{
    const std::initializer_list<ExportField> kHashCoreFields
    {
        ExportField::BookingDate,
        ExportField::Amount,
        ExportField::CreditDebit,
        ExportField::Currency,
        ExportField::CounterpartyIBAN,
        ExportField::CounterpartyBIC,
        ExportField::RemittanceLine,
        ExportField::EndToEndId,
        ExportField::TxId,
        ExportField::BankRef,
        ExportField::AccountIBAN,
        ExportField::BkTxCd,
        ExportField::Reversal,
        ExportField::Primanota,
        ExportField::DTACode
    };
    
    std::string sum;
    sum.reserve(512);
    CAMTRow _row=row;
    normalize_or_accumulate_row(_row, fields.size()==0 ? kHashCoreFields : fields, true, &sum);
    return sum;
}

// === Actual export function ===========================================
inline void export_entries_csv(const Document& doc, std::ostream* osPtr=nullptr, ExportData* vPtr=nullptr, const ExportOptions& opt = {}) {
	
    if (osPtr && opt.write_utf8_bom) {
        const unsigned char bom[3] = {0xEF,0xBB,0xBF};
        osPtr->write(reinterpret_cast<const char*>(bom), 3);
    }
    const char D = opt.delimiter;

    auto find_first_of = [&](const Statement& st, std::initializer_list<const char*> codes) -> const Balance* {
        for (const auto& b : st.balances)
            for (const char* c : codes)
                if (b.type == c) return &b;
        return nullptr;
    };
    auto find_last_of = [&](const Statement& st, std::initializer_list<const char*> codes) -> const Balance* {
        const Balance* out = nullptr;
        for (const auto& b : st.balances)
            for (const char* c : codes)
                if (b.type == c) out = &b;
        return out;
    };

    // number WITHOUT currency; sign may be derived from CdtDbtInd (CRDT=+, DBIT=-)
    auto balance_number_str = [&](const Statement& st, const Balance* bal, bool use_decimal_comma=false) -> std::string {
        if (!bal) return std::string();
        CurrencyAmount a = bal->amount;
        if (bal->hasCdtDbtInd) {
            int64_t abs = (a.minor < 0 ? -a.minor : a.minor);
            a.minor = bal->isCredit ? abs : -abs;
        }
        if (a.currency.empty()) a.currency = st.account.currency; // only for decimal places
        return fmt_amount(a, use_decimal_comma); // KEIN CCY-Suffix
    };

    // Interim per transaction (ITBD/ITAV) based on date (BookingDate or ValueDate)
    auto interim_for_entry = [&](const Statement& st, const Entry& e) -> const Balance* {
        const std::string& d1 = e.bookingDate;
        const std::string& d2 = e.valueDate;
        for (const auto& b : st.balances) {
            if (b.type == "ITBD" || b.type == "ITAV") {
                if ((!d1.empty() && b.date == d1) || (!d2.empty() && b.date == d2))
                    return &b;
            }
        }
        return nullptr;
    };

    auto find_balance = [&](const Statement& st, const std::string& code) -> const Balance* {
        for (const auto& b : st.balances) if (b.type == code) return &b;
        return nullptr;
    };

    auto isProvided = [](const std::string& s) {
        return !s.empty() && s != "NOTPROVIDED";
    };
        
    if (opt.include_header) {
        CAMTRow header = {
            { "BookingDate",        "" },
            { "ValueDate",          "" },
            { "Amount",             "" },
            { (opt.credit_as_bool ? "IsCredit" : "CreditDebit"), "" },
            { "Currency",           "" },
            { "CounterpartyName",   "" }, 
            { "CounterpartyIBAN",   "" }, 
            { "CounterpartyBIC",    "" },
            { "RemittanceLine",     "" },
            { "RemittanceStructured","" },
            { "EndToEndId",         "" },
            { "MandateId",          "" },
            { "TxId",               "" },
            { "BankRef",            "" },
            { "AccountIBAN",        "" },
            { "AccountBIC",         "" },
            { "BkTxCd",             "" },
            { "BookingCode",        "" },
            { "Status",             "" },
            { "Reversal",           "" },
            { "RunningBalance",     "" },
            { "ServicerBankName",   "" },
            { "OpeningBalance",     "" },
            { "ClosingBalance",     "" },
            { "Primanota",          "" },
            { "DTACode",            "" },
            { "GVCCode",            "" },
            { "SWIFTTransactionCode","" },
            { "ChargesAmount",      "" },
            { "ChargesCurrency",    "" },
            { "ChargesIncluded",    "" },
            { "EntryOrdinal",       "" },
            { "TxOrdinal",          "" }
        };

        if (osPtr) {
            std::ostream& os = *osPtr;
            for (size_t i = 0; i < header.size(); ++i) {
                os << header[i].first; // always write only the original part
                if (i + 1 < header.size()) {
                    os << D;
                }
            }
            os << "\n";
        }
        if (vPtr) {
            vPtr->push_back(header);
        }
    }
    
    for (const auto& st : doc.statements) {
        int64_t runningMinor = 0;
        std::string runCcy = st.account.currency;

        // Use global balances only once per statement (first/last row)
        /*
        PRCD = Previously Closed Booked Balance
        OPBD = Opening Booked Balance
        OPAV = Opening Available Balance
        CLBD = Closing Booked Balance
        CLAV = Closing Available Balance
        */
        const Balance* globalOpen  = find_first_of(st, {"OPBD","PRCD"});
        const Balance* globalClose = find_last_of (st, {"CLBD"/*,"CLAV"*/});

        const std::string openGlobalStr  = balance_number_str(st, globalOpen);
        const std::string closeGlobalStr = balance_number_str(st, globalClose);

        // Determine number of transaction rows to output in this statement
        size_t totalRows = 0;
        for (const auto& e : st.entries)
            totalRows += e.transactions.empty() ? 1 : e.transactions.size();

        // Sum of fees per transaction (always correct sign, incl. reversal flip)
        auto sum_charges_view = [&](const Entry& e, const EntryTransaction* tx) -> std::pair<CurrencyAmount, bool>
        {
            CurrencyAmount out;
            bool anyIncluded = false;

            if (tx == nullptr)
            {
                return { out, anyIncluded };
            }

            for (const auto& rec : tx->charges.records)
            {
                if (rec.amount.currency.empty())
                {
                    continue;
                }

                if (out.currency.empty())
                {
                    out.currency = rec.amount.currency;
                }

                int64_t m = rec.amount.minor;
                int64_t absM = (m >= 0 ? m : -m);

                // 1) Determine base direction: Priority Rcrd (highest), then Tx, then Entry
                bool creditBase = e.isCredit;

                if (tx->hasCdtDbtInd)
                {
                    creditBase = tx->isCredit;
                }

                if (rec.hasCdtDbtInd)
                {
                    creditBase = rec.isCredit;
                }

                // 2) Apply reversal from Entry (flip)
                bool effectiveCredit = creditBase;

                if (e.reversal)
                {
                    effectiveCredit = !effectiveCredit;
                }

                // 3) Determine signed minor
                int64_t signedM;

                if (effectiveCredit)
                {
                    signedM = absM;   // CRDT → Haben (+)
                }
                else
                {
                    signedM = -absM;  // DBIT → Soll (−)
                }

                // 4) Accumulate (always with sign, regardless of export options)
                out.minor += signedM;

                if (rec.included)
                {
                    anyIncluded = true;
                }
            }

            return { out, anyIncluded };
        };

        size_t rowIndex = 0;
        auto write_row = [&](const Entry& e, const EntryTransaction* tx) {
            // 1) Sign source: first Tx, otherwise Entry
            bool credit = e.isCredit;
            if (tx && tx->hasCdtDbtInd) credit = tx->isCredit;

            bool effectiveCredit = credit;
            if (e.reversal) {
                    effectiveCredit = !effectiveCredit;
                }

            // 2) Counterparty (with option to prefer ultimate parties)
            std::string cpName, cpIban, cpBic;
            if (tx)
            {
                if (effectiveCredit)
                {
                    // Incoming (CRDT): counterparty is the debtor side
                    if (opt.prefer_ultimate_counterparty)
                    {
                        cpName = isProvided(tx->parties.ultimateDebtor.name)
                            ? tx->parties.ultimateDebtor.name
                            : tx->parties.debtor.name;
                    }
                    else
                    {
                        cpName = isProvided(tx->parties.debtor.name)
                            ? tx->parties.debtor.name
                            : tx->parties.ultimateDebtor.name;
                    }

                    if (!tx->parties.debtorAccount.iban.empty())
                        cpIban = tx->parties.debtorAccount.iban;
                    cpBic = tx->agents.debtorAgent.bic;
                }
                else
                {
                    // Outgoing (DBIT): counterparty is the creditor side
                    if (opt.prefer_ultimate_counterparty)
                    {
                        cpName = isProvided(tx->parties.ultimateCreditor.name)
                            ? tx->parties.ultimateCreditor.name
                            : tx->parties.creditor.name;
                    }
                    else
                    {
                        cpName = isProvided(tx->parties.creditor.name)
                            ? tx->parties.creditor.name
                            : tx->parties.ultimateCreditor.name;
                    }

                    if (!tx->parties.creditorAccount.iban.empty())
                        cpIban = tx->parties.creditorAccount.iban;
                    cpBic = tx->agents.creditorAgent.bic;
                }
            }

            // 3) Remittance
            std::string remitU_first, remitU_second;
            std::string remitS_first, remitS_second;

            // Display separator from options
            const std::string disp_sep = opt.remittance_separator;
            // Canonical separator: ASCII GS (0x1D)
            static constexpr char GS = '\x1D';

            if (tx) {
                // --- Unstructured remittance lines ---
                for (size_t i = 0; i < tx->remittance.unstructured.size(); ++i) {
                    const std::string& part = tx->remittance.unstructured[i];

                    // Display (.first)
                    if (i) remitU_first += disp_sep;
                    remitU_first += part;

                    // Canonical (.second)
                    if (i) remitU_second.push_back(GS);
                    remitU_second += normalize_field(ExportField::RemittanceLine, part);
                }

                // --- Structured remittance: take either creditorRef or additionalInfo ---
                if (!tx->remittance.structured.empty()) {
                    const auto& sr = tx->remittance.structured.front();
                    const std::string& base = !sr.creditorRef.empty() ? sr.creditorRef : sr.additionalInfo;

                    remitS_first  = base;
                    remitS_second = normalize_field(ExportField::RemittanceStructured, base);
                }
            }


            // 4) Codes
            std::string bk, pBk;
            if (tx) {
                if (!tx->bankTxCode.domain.empty() || !tx->bankTxCode.family.empty() || !tx->bankTxCode.subFamily.empty())
                    bk = tx->bankTxCode.domain + ":" + tx->bankTxCode.family + ":" + tx->bankTxCode.subFamily;
                pBk = tx->proprietaryBankTxCode.code;
            }
			
			std::string swiftTxCode;
			if (!pBk.empty())
				swiftTxCode = pBk.substr(0, std::min<size_t>(4, pBk.size()));

            // 5) Amount: prefer TxAmt, otherwise Entry.Amt
            CurrencyAmount amt = e.amount;
            if (tx && tx->txAmount.has_value()) amt = *tx->txAmount;

            // Currency for RunningBalance
            if (runCcy.empty())
                runCcy = !amt.currency.empty() ? amt.currency : e.amount.currency;

            // 6) Sign amount + update RunningBalance
            const int64_t absMinor    = (amt.minor < 0 ? -amt.minor : amt.minor);
            //const int64_t signedMinor = credit ? absMinor : -absMinor;
            const int64_t signedMinor = effectiveCredit ? absMinor : -absMinor;

            amt.minor = opt.signed_amount ? signedMinor : absMinor;   // Amount column signed or positive
            runningMinor += signedMinor;                               // Balance: CRDT=+ / DBIT=-
            CurrencyAmount amt_abs = amt;
            amt_abs.minor = absMinor;

            // 7) Additional fields
            const std::string servicerName = st.account.servicer.name;

            // Primanota
            std::string primanota; 

            // DTA_Code & GVC
            std::string dta_code, gvc;
            if (tx) {
                dta_code = !tx->proprietaryBankTxCode.code.empty() ? tx->proprietaryBankTxCode.code : dta_code;
                const std::string& c = dta_code;
                size_t p = c.find('+');
                if (p != std::string::npos)
                {
                    gvc= c.substr(p + 1);
                    p = gvc.find('+');
                    if (p != std::string::npos)
                    {
                        primanota = gvc.substr(p+1);
                        gvc= gvc.substr(0,p);
                    }
                }
            }
			
			// Fallback via ISO mapping if gvc is empty
			// Fallback via minimal map: PMNT;RCDT;SubFmly;C|D -> ISO ("058")
			if (gvc.empty() && tx) {
				const char dc = (credit ? 'C' : 'D');
				gvc = camt::lookup_gvc(
					camt::get_gvc_map(),
					tx->bankTxCode.domain,   // PMNT
					tx->bankTxCode.family,   // RCDT
					tx->bankTxCode.subFamily,// VCOM / FICT / ATXN
					dc
				);
			}

            // 8) Opening/Closing:
            //    - If global value exists -> Opening only in 1st row, Closing only in last row
            //    - Otherwise: per transaction Interim (ITBD/ITAV) with matching date, else " "
            std::string openingStr = " ";
            std::string closingStr = " ";

            if (!openGlobalStr.empty()) {
                if (rowIndex == 0) openingStr = openGlobalStr;
            } else {
                if (const Balance* it = interim_for_entry(st, e))
                    openingStr = balance_number_str(st, it);
            }

            if (!closeGlobalStr.empty()) {
                if (rowIndex + 1 == totalRows) closingStr = closeGlobalStr;
            } else {
                if (const Balance* it = interim_for_entry(st, e))
                    closingStr = balance_number_str(st, it);
            }

            CurrencyAmount chargesAmt; 
            bool chargesIncluded = false;
            if (tx) {
                std::tie(chargesAmt, chargesIncluded) = sum_charges_view(e, tx);
            }

            const bool isCredit = (opt.use_effective_credit ? effectiveCredit : credit);

            const std::string currency = st.account.currency.empty() ? (amt.currency.empty() ? runCcy : amt.currency) : st.account.currency;

            //const std::string acctSvcrRef = !e.acctSvcrRef.empty() ? e.acctSvcrRef : (tx ? tx->refs.acctSvcrRef : std::string());
            const std::string acctSvcrRef = (tx && !tx->refs.acctSvcrRef.empty()) ? tx->refs.acctSvcrRef : e.acctSvcrRef;

            const std::string accountIban = !st.account.id.iban.empty() ? st.account.id.iban : st.account.id.other;

            const std::string reversal = (e.reversal ? "1" : "0");

            const std::string st_chargesIncluded = (chargesIncluded ? "1" : "0");

            const std::string importOrdinalEntry = e.importOrdinal >= 0 ? std::to_string(e.importOrdinal) : "";

            const std::string importOrdinalTx = tx ? std::to_string(tx->importOrdinal) : "";

            CAMTRow row =
            {
                {e.bookingDate, std::to_string(e.bookingDateInt)},
                {e.valueDate, std::to_string(e.valueDateInt)},
                {fmt_amount(amt), fmt_amount(amt_abs)},
                {(opt.credit_as_bool ? (isCredit ? "1" : "0") : (isCredit ? "CRDT" : "DBIT")), credit ? "1" : "0"},
                {currency, ""},
                {cpName, ""},
                {cpIban, ""},
                {cpBic, ""},
                {remitU_first, remitU_second},
                {remitS_first, remitS_second},
                {tx ? tx->refs.endToEndId : std::string(), ""},
                {tx ? tx->refs.mandateId : std::string(), ""},
                {tx ? tx->refs.txId : std::string(), ""},
                {acctSvcrRef, ""},
                {accountIban, ""},
                {st.account.servicer.bic, ""},
                {bk, ""},
                {pBk, ""},
                {e.status, ""}, 
                {reversal, reversal},
                {fmt_amount(CurrencyAmount{ runCcy, runningMinor }), fmt_amount(CurrencyAmount{ runCcy, runningMinor })},
                {servicerName, ""},
                {openingStr, openingStr},
                {closingStr, closingStr},
                {primanota, ""},
                {dta_code, ""},
                {gvc, ""},
                {swiftTxCode, ""},
                {fmt_amount(chargesAmt), fmt_amount(chargesAmt)},
                {chargesAmt.currency, ""},
                {st_chargesIncluded, st_chargesIncluded},
                {importOrdinalEntry, importOrdinalEntry},
                {importOrdinalTx, importOrdinalTx}
            };
            
            if (osPtr) {
                int cnt_field = 0;
                std::ostream& os = *osPtr;
                for (const auto& item : row) {
                    if (++cnt_field == row.size())
                    {
                        os << csv_escape(item.first, D);
                        os << "\n";
                    }
                    else
                    {
                        os << csv_escape(item.first, D) << D;
                    }
                }
            }
            if(vPtr)
            {
                std::initializer_list<ExportField> norm_fields = {
                    ExportField::Currency,
                    ExportField::CounterpartyName,
                    ExportField::CounterpartyIBAN,
                    ExportField::CounterpartyBIC,
                    //ExportField::RemittanceLine,
                    //ExportField::RemittanceStructured,
                    ExportField::EndToEndId,
                    ExportField::MandateId,
                    ExportField::TxId,
                    ExportField::BankRef,
                    ExportField::AccountIBAN,
                    ExportField::AccountBIC,
                    ExportField::BkTxCd,
                    ExportField::BookingCode,
                    ExportField::Status,
                    ExportField::ServicerBankName,
                    ExportField::Primanota,
                    ExportField::DTACode,
                    ExportField::GVCCode,
                    ExportField::SWIFTTransactionCode,
                    ExportField::ChargesCurrency
                };

                normalize_or_accumulate_row(row, norm_fields, true);
                vPtr->push_back(row);
            }

            ++rowIndex;
        };

        for (const auto& e : st.entries) {
            if (!e.transactions.empty()) {
                for (const auto& tx : e.transactions) write_row(e, &tx);
            } else {
                write_row(e, nullptr);
            }
        }
    }
}

} // namespace camt
