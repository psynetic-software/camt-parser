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
#include <pugixml.hpp>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <cstring>  

namespace camt {

// ---------- Helpers (namespace-agnostic, only classic loops) ----------
inline const char* ln(const pugi::xml_node& n) {
    if (!n) return "";
    const char* full = n.name();
    const char* c = std::strrchr(full, ':');
    return c ? c + 1 : full;
}
inline bool isln(const pugi::xml_node& n, const char* wanted) { return std::strcmp(ln(n), wanted) == 0; }

inline const char* ln(const pugi::xml_attribute& a) {
    if (!a) return "";
    const char* full = a.name(); const char* c = std::strrchr(full, ':');
    return c ? c + 1 : full;
}
inline bool isln(const pugi::xml_attribute& a, const char* wanted) { return std::strcmp(ln(a), wanted) == 0; }

// direct child with local name
inline pugi::xml_node child_any(const pugi::xml_node& p, const char* name) {
    for (pugi::xml_node c = p.first_child(); c; c = c.next_sibling())
        if (isln(c, name)) return c;
    return pugi::xml_node();
}

// depth search (recursive) over all descendants
inline pugi::xml_node desc_any(const pugi::xml_node& p, const char* name) {
    for (pugi::xml_node c = p.first_child(); c; c = c.next_sibling()) {
        if (isln(c, name)) return c;
        pugi::xml_node found = desc_any(c, name);
        if (found) return found;
    }
    return pugi::xml_node();
}

inline std::string txt(const pugi::xml_node& n) {
    std::string s = n.text().as_string(); // UTF-8
    // trim
    auto notsp = [](int ch){ return ch!=' ' && ch!='\t' && ch!='\n' && ch!='\r'; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notsp));
    s.erase(std::find_if(s.rbegin(), s.rend(), notsp).base(), s.end());
    return s;
}
inline std::string child_text(const pugi::xml_node& p, const char* name) {
    pugi::xml_node n = child_any(p, name);
    return n ? txt(n) : std::string();
}
inline std::string desc_text(const pugi::xml_node& p, const char* name) {
    pugi::xml_node n = desc_any(p, name);
    return n ? txt(n) : std::string();
}

// ISO-4217 exponents (short list)
inline int ccy_exp(const std::string& ccy){
    static const std::unordered_map<std::string,int> m{
        {"JPY",0},{"KRW",0},{"VND",0},
        {"BHD",3},{"KWD",3},{"OMR",3},{"TND",3},
        {"CLF",4}
    };
    auto it = m.find(ccy);
    return (it==m.end()) ? 2 : it->second;
}

// "1.234,56" oder "1234.56" -> Minor Units (robust, exception-free, without __int128)
inline std::int64_t dec_to_minor(std::string s, int exp){
    // remove simple ASCII groupings/spaces
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch){
        return ch==' '||ch=='\t'||ch=='\r'||ch=='\n'||ch=='\''||ch=='_'||ch==0xA0;
    }), s.end());

    bool neg = false;
    if (!s.empty() && s.front()=='(' && s.back()==')') {
        neg = true;
        s.erase(s.begin()); s.pop_back();
    }
    if (!s.empty() && (s.front()=='+' || s.front()=='-')) {
        if (s.front()=='-') neg = !neg;
        s.erase(s.begin());
    }

    // determine decimal separator
    size_t lastDot = s.find_last_of('.');
    size_t lastCom = s.find_last_of(',');
    char dec = 0;
    if (lastDot != std::string::npos || lastCom != std::string::npos) {
        if (lastDot == std::string::npos) dec = ',';
        else if (lastCom == std::string::npos) dec = '.';
        else dec = (lastDot > lastCom) ? '.' : ',';
    }

    // splitt
    std::string intp, frac;
    if (dec) {
        size_t pos = s.find_last_of(dec);
        intp = s.substr(0, pos);
        frac = s.substr(pos+1);
    } else {
        intp = s;
    }

    // remove any separator other than grouping
    char other = dec ? (dec=='.' ? ',' : '.') : 0;
    if (other) {
        intp.erase(std::remove(intp.begin(), intp.end(), other), intp.end());
        frac.erase(std::remove(frac.begin(), frac.end(), other), frac.end());
    }

    // allow digits only
    auto only_digits = [](const std::string& t)->bool {
        for (unsigned char c : t) if (c < '0' || c > '9') return false;
        return true;
    };
    if (intp.empty()) intp = "0";
    if (!only_digits(intp) || !only_digits(frac)) return 0;

    // adjust fraction to exponent (truncate or pad)
    if (exp < 0) exp = 0;
    if ((int)frac.size() > exp) frac.resize((size_t)exp);
    else if ((int)frac.size() < exp) frac.append((size_t)(exp - (int)frac.size()), '0');

    // safe parsers: t -> uint64
    auto parse_u64 = [](const std::string& t, std::uint64_t& out)->bool {
        out = 0;
        for (unsigned char c : t) {
            unsigned d = (unsigned)(c - '0');
            if (d > 9) return false;
            if (out > (std::numeric_limits<std::uint64_t>::max() - d) / 10ull) return false;
            out = out * 10ull + d;
        }
        return true;
    };
    std::uint64_t ip=0, fp=0;
    if (!parse_u64(intp, ip)) return 0;
    if (!parse_u64(frac, fp)) return 0;

    // ip must fit in int64
    if (ip > (std::uint64_t)std::numeric_limits<std::int64_t>::max()) return 0;

    // safely compute pow10(exp)
    auto pow10_i64 = [](int e, std::int64_t& out)->bool {
        out = 1;
        for (int i=0;i<e;i++){
            if (out > (std::numeric_limits<std::int64_t>::max() / 10)) return false;
            out *= 10;
        }
        return true;
    };
    std::int64_t scale;
    if (!pow10_i64(exp, scale)) return 0;

    // safe multiplication a*b -> res, with overflow check (signed)
    auto mul_check = [](std::int64_t a, std::int64_t b, std::int64_t& res)->bool {
        // Handle special cases
        if (a == 0 || b == 0) { res = 0; return true; }
        // Check overflow generically
        if (a > 0) {
            if (b > 0) { if (a > (std::numeric_limits<std::int64_t>::max() / b)) return false; }
            else { if (b < (std::numeric_limits<std::int64_t>::min() / a)) return false; }
        } else {
            if (b > 0) { if (a < (std::numeric_limits<std::int64_t>::min() / b)) return false; }
            else { if (a != 0 && b < (std::numeric_limits<std::int64_t>::max() / a)) return false; }
        }
        res = a * b;
        return true;
    };

    std::int64_t ip_s = (std::int64_t)ip;
    std::int64_t scaled_major;
    if (!mul_check(ip_s, scale, scaled_major)) return 0;

    // fp always fits in scale (because tailored to exp), and fp <= 10^exp-1
    if (fp > (std::uint64_t)std::numeric_limits<std::int64_t>::max()) return 0;
    std::int64_t fp_s = (std::int64_t)fp;

    // combine with overflow checks
    std::int64_t v;
    if (neg) {
        // -(scaled_major + fp_s)
        if (scaled_major < (std::numeric_limits<std::int64_t>::min() + fp_s)) return 0;
        v = scaled_major + fp_s;
        if (v > 0) v = -v; // ensure negative
    } else {
        if (scaled_major > (std::numeric_limits<std::int64_t>::max() - fp_s)) return 0;
        v = scaled_major + fp_s;
    }
    return v;
}

inline CurrencyAmount parse_amount(const pugi::xml_node& amt){
    CurrencyAmount a;
    for (pugi::xml_attribute at = amt.first_attribute(); at; at = at.next_attribute())
        if (isln(at,"Ccy")) { a.currency = at.value(); break; }
    a.minor = dec_to_minor(txt(amt), ccy_exp(a.currency));
    return a;
}

inline AccountId parse_account_id(const pugi::xml_node& id){
    AccountId out;
    pugi::xml_node n = desc_any(id,"IBAN");
    if (n) out.iban = txt(n);
    if (out.iban.empty()) {
        pugi::xml_node o = child_any(id,"Othr");
        if (o) {
            pugi::xml_node i = child_any(o,"Id");
            if (i) out.other = txt(i);
        }
    }
    return out;
}

inline Agent parse_agent(const pugi::xml_node& node){
    Agent a;
    pugi::xml_node fi = child_any(node,"FinInstnId");
    if (fi) {
        pugi::xml_node b = child_any(fi,"BIC");
        if (b) a.bic = txt(b);
        if (a.bic.empty()) {
            pugi::xml_node bf = child_any(fi,"BICFI");
            if (bf) a.bic = txt(bf);
        }
        pugi::xml_node nm = child_any(fi,"Nm");
        if (nm) a.name = txt(nm);
    }
    return a;
}

inline Party parse_party(const pugi::xml_node& node){
    Party p;
    pugi::xml_node nm = desc_any(node,"Nm"); if (nm) p.name = txt(nm);
    pugi::xml_node ib = desc_any(node,"IBAN"); if (ib) p.iban = txt(ib);
    pugi::xml_node b = desc_any(node,"BIC"); if (b) p.bic = txt(b);
    if (p.bic.empty()) { pugi::xml_node bf = desc_any(node,"BICFI"); if (bf) p.bic = txt(bf); }
    return p;
}

inline void parse_remittance(const pugi::xml_node& rmt, RemittanceInformation& out){
    for (pugi::xml_node u = rmt.first_child(); u; u = u.next_sibling()){
        if (isln(u,"Ustrd")){
            std::string s = txt(u);
            if (!s.empty()) out.unstructured.push_back(s);
        } else if (isln(u,"Strd")){
            StructuredRemittance sr;
            pugi::xml_node rtp = desc_any(u,"RefTp");
            if (rtp){
                pugi::xml_node cdp = desc_any(rtp,"Cd"); if (cdp) sr.creditorRefType = txt(cdp);
                if (sr.creditorRefType.empty()) {
                    pugi::xml_node pr = desc_any(rtp,"Prtry"); if (pr) sr.creditorRefType = txt(pr);
                }
            }
            pugi::xml_node cri = desc_any(u,"CdtrRefInf");
            if (cri){
                pugi::xml_node rf = child_any(cri,"Ref"); if (rf) sr.creditorRef = txt(rf);
            }
            pugi::xml_node add = child_any(u,"AddtlRmtInf"); if (add) sr.additionalInfo = txt(add);
            out.structured.push_back(sr);
        }
    }
}

inline void parse_related_parties(const pugi::xml_node& rp, RelatedParties& out){
    pugi::xml_node n;
    n = child_any(rp,"Dbtr"); if (n) out.debtor = parse_party(n);
    n = child_any(rp,"DbtrAcct"); if (n) out.debtorAccount = parse_account_id(child_any(n,"Id"));
    n = child_any(rp,"UltmtDbtr"); if (n) out.ultimateDebtor = parse_party(n);
    n = child_any(rp,"Cdtr"); if (n) out.creditor = parse_party(n);
    n = child_any(rp,"CdtrAcct"); if (n) out.creditorAccount = parse_account_id(child_any(n,"Id"));
    n = child_any(rp,"UltmtCdtr"); if (n) out.ultimateCreditor = parse_party(n);
}

inline void parse_related_agents(const pugi::xml_node& ra, RelatedAgents& out){
    pugi::xml_node n;
    n = child_any(ra,"DbtrAgt"); if (n) out.debtorAgent   = parse_agent(n);
    n = child_any(ra,"CdtrAgt"); if (n) out.creditorAgent = parse_agent(n);
}

inline void parse_bktx(const pugi::xml_node& btc, BankTransactionCode& out){
    pugi::xml_node d = child_any(btc,"Domn");
    if (d){
        pugi::xml_node cd = child_any(d,"Cd"); if (cd) out.domain = txt(cd);
        pugi::xml_node fm = child_any(d,"Fmly");
        if (fm){
            pugi::xml_node c = child_any(fm,"Cd"); if (c) out.family = txt(c);
            pugi::xml_node s = child_any(fm,"SubFmlyCd"); if (s) out.subFamily = txt(s);
        }
    }
    pugi::xml_node p = child_any(btc,"Prtry");
    if (p){
        pugi::xml_node cd = child_any(p,"Cd");
        if (cd) out.proprietary = txt(cd);
        if (out.proprietary.empty()) out.proprietary = txt(p);
    }
}

inline void parse_proprietary_bktx(const pugi::xml_node& n, ProprietaryBankTransactionCode& out){
    pugi::xml_node cd = child_any(n,"Cd"); if (cd) out.code = txt(cd);
    pugi::xml_node is = child_any(n,"Issr"); if (is) out.issuer = txt(is);
}

inline void parse_charges(const pugi::xml_node& n, Charges& out){
    // optional: total amount of fees/taxes
    if (pugi::xml_node t = child_any(n,"TtlChrgsAndTaxAmt"))
        out.total = parse_amount(t);

    for (pugi::xml_node r = n.first_child(); r; r = r.next_sibling()){
        if (!isln(r,"Rcrd")) continue;

        ChargesRecord rec;

        if (pugi::xml_node a  = child_any(r,"Amt"))
            rec.amount = parse_amount(a);

        if (pugi::xml_node ag = child_any(r,"Agt"))
            rec.agent = parse_agent(ag);

        if (pugi::xml_node ci = child_any(r,"CdtDbtInd")) {
            const std::string s = txt(ci);
            rec.hasCdtDbtInd = true;
            rec.isCredit     = (s == "CRDT"); // CRDT=+, DBIT=-
        }

        if (pugi::xml_node ii = child_any(r,"ChrgInclInd")) {
            const std::string s = txt(ii);
            rec.included = (s == "true" || s == "1");
        }

        out.records.push_back(std::move(rec));
    }
}

// from Tx node, search upwards for the related statement container and read Acct/Ccy
static inline std::string find_account_ccy_from_tx(const pugi::xml_node& tx) {
    pugi::xml_node stmt = tx;
    while (stmt && !isln(stmt, "Stmt") && !isln(stmt, "Rpt") && !isln(stmt, "Ntfctn")) {
        stmt = stmt.parent();
    }

    if (!stmt) {
        return std::string{};
    }

    pugi::xml_node acct = child_any(stmt, "Acct");
    if (!acct) {
        return std::string{};
    }

    pugi::xml_node ccy = child_any(acct, "Ccy");
    if (!ccy) {
        return std::string{};
    }

    return txt(ccy);
}

// select the first amount in the desired currency from AmtDtls (priority: TxAmt, InstdAmt, CntrValAmt)
static inline std::optional<CurrencyAmount> pick_amount_in_ccy(const pugi::xml_node& amtDtls,
    const std::string& accountCcy) {
    if (!amtDtls) {
        return std::nullopt;
    }

    auto pick_one = [&](const char* tag) -> std::optional<CurrencyAmount> {
        pugi::xml_node n = child_any(amtDtls, tag);
        if (!n) {
            return std::nullopt;
        }
        pugi::xml_node a = child_any(n, "Amt");
        if (!a) {
            return std::nullopt;
        }
        CurrencyAmount ca = parse_amount(a);
        if (ca.currency == accountCcy) {
            return ca;
        }
        return std::nullopt;
    };

    if (auto ca = pick_one("TxAmt")) {
        return ca;
    }
    if (auto ca = pick_one("InstdAmt")) {
        return ca;
    }
    if (auto ca = pick_one("CntrValAmt")) {
        return ca;
    }
    return std::nullopt;
}

// reconstruct effective FX rate from two amounts; detect inverted Src/Trgt
static inline void reconcile_ccyxchg(FxRateInfo& fx,
    const CurrencyAmount* aSrc,
    const CurrencyAmount* aTrg,
    double* outEffectiveRate,
    bool* outInverted) {
    const double EPS_REL = 1e-6;

    *outEffectiveRate = 0.0;
    *outInverted = false;

    if (!aSrc || !aTrg) {
        return;
    }
    if (aSrc->currency.empty() || aTrg->currency.empty()) {
        return;
    }
    if (aSrc->minor == 0 || aTrg->minor == 0) {
        return;
    }

    auto toMajor = [](const CurrencyAmount& ca) -> double {
        int exp = ccy_exp(ca.currency);
        double denom = 1.0;
        for (int i = 0; i < exp; ++i) {
            denom *= 10.0;
        }
        return static_cast<double>(ca.minor) / denom;
    };

    double srcMaj = toMajor(*aSrc);
    double trgMaj = toMajor(*aTrg);
    if (srcMaj == 0.0) {
        return;
    }

    double derived = trgMaj / srcMaj; // mathematically: src -> trg

    if (fx.has && fx.rate > 0.0) {
        double diffDirect = std::fabs(fx.rate - derived);
        double diffInverse = std::fabs((1.0 / fx.rate) - derived);
        double tol = std::max(1e-9, std::fabs(derived) * EPS_REL);

        if (diffDirect <= tol) {
            *outEffectiveRate = fx.rate;
            *outInverted = false;
        } else if (diffInverse <= tol) {
            *outEffectiveRate = derived; // we use the derived rate
            *outInverted = true;         // supplied rate was effectively inverted
        } else {
            *outEffectiveRate = derived; // implausible supplied → use derived
            *outInverted = false;
        }
    } else {
        *outEffectiveRate = derived; // no supplied rate → use derived
        *outInverted = false;
    }
}

inline EntryTransaction parse_txdtls(const pugi::xml_node& tx) {
    EntryTransaction t;

    // ----- Refs -----
    if (pugi::xml_node refs = child_any(tx, "Refs")) {
        pugi::xml_node n;
        n = child_any(refs, "EndToEndId");
        if (n) {
            t.refs.endToEndId = txt(n);
        }
        n = child_any(refs, "TxId");
        if (n) {
            t.refs.txId = txt(n);
        }
        n = child_any(refs, "AcctSvcrRef");
        if (n) {
            t.refs.acctSvcrRef = txt(n);
        }
        n = child_any(refs, "MndtId");
        if (n) {
            t.refs.mandateId = txt(n);
        }
    }

    // ----- BankTransactionCode (incl. proprietary/GVC) -----
    if (pugi::xml_node btc = child_any(tx, "BkTxCd")) {
        parse_bktx(btc, t.bankTxCode);

        if (pugi::xml_node pr = child_any(btc, "Prtry")) {
            pugi::xml_node cd = child_any(pr, "Cd");
            if (cd) {
                t.proprietaryBankTxCode.code = txt(cd);
            }
            pugi::xml_node is = child_any(pr, "Issr");
            if (is) {
                t.proprietaryBankTxCode.issuer = txt(is);
            }
        }

        t.dtaCode = t.proprietaryBankTxCode.code;

        const std::string& c = t.proprietaryBankTxCode.code;
        size_t p = c.find('+');
        if (p != std::string::npos && (p + 1) < c.size()) {
            t.gvc = c.substr(p + 1);
        }
    }

    // ----- Parties/Agents/Remittance information -----
    if (pugi::xml_node rp = child_any(tx, "RltdPties")) {
        parse_related_parties(rp, t.parties);
    }
    if (pugi::xml_node ra = child_any(tx, "RltdAgts")) {
        parse_related_agents(ra, t.agents);
    }
    if (pugi::xml_node rmt = child_any(tx, "RmtInf")) {
        parse_remittance(rmt, t.remittance);
    }
    if (pugi::xml_node p = child_any(tx, "Purp")) {
        pugi::xml_node c = child_any(p, "Cd");
        if (c) {
            t.purpose.code = txt(c);
        }
        pugi::xml_node pr = child_any(p, "Prtry");
        if (pr) {
            t.purpose.proprietary = txt(pr);
        }
    }
    if (pugi::xml_node pbc = child_any(tx, "PrtryBkTxCd")) {
        parse_proprietary_bktx(pbc, t.proprietaryBankTxCode);
    }
    if (pugi::xml_node ch = child_any(tx, "Chrgs")) {
        parse_charges(ch, t.charges);
    }
    if (pugi::xml_node ai = child_any(tx, "AddtlTxInf")) {
        t.additionalInfo = txt(ai);
    }

    // ----- Amount of single transaction (Tx level) -----
    // primary: TxDtls/Amt
    if (pugi::xml_node a0 = child_any(tx, "Amt")) {
        t.txAmount = parse_amount(a0);
    } else {
        // Fallback: TxDtls/AmtDtls/TxAmt/Amt
        if (pugi::xml_node ad = child_any(tx, "AmtDtls")) {
            if (pugi::xml_node ta = child_any(ad, "TxAmt")) {
                if (pugi::xml_node a = child_any(ta, "Amt")) {
                    t.txAmount = parse_amount(a);
                }
            }
        }
    }

    // sign indicator (Tx level)
    if (pugi::xml_node cdi = child_any(tx, "CdtDbtInd")) {
        t.hasCdtDbtInd = true;
        t.isCredit = (txt(cdi) == "CRDT");
    }

    // ----- Foreign currency details (optional) -----
    if (pugi::xml_node ad = child_any(tx, "AmtDtls")) {
        // 2.1 InstdAmt
        if (pugi::xml_node ia = child_any(ad, "InstdAmt")) {
            if (pugi::xml_node a = child_any(ia, "Amt")) {
                t.fxInstdAmt = parse_amount(a);
                t.hasFxInstdAmt = true;

                if (pugi::xml_node cx = child_any(ia, "CcyXchg")) {
                    pugi::xml_node n;
                    n = child_any(cx, "SrcCcy");
                    if (n) {
                        t.fx.srcCcy = txt(n);
                    }
                    n = child_any(cx, "TrgtCcy");
                    if (n) {
                        t.fx.trgtCcy = txt(n);
                    }
                    n = child_any(cx, "UnitCcy");
                    if (n) {
                        t.fx.unitCcy = txt(n);
                    }
                    n = child_any(cx, "XchgRate");
                    if (n) {
                        std::string s = txt(n);
                        std::replace(s.begin(), s.end(), ',', '.');
                        try {
                            t.fx.rate = s.empty() ? 0.0 : std::stod(s);
                        } catch (...) {
                            t.fx.rate = 0.0;
                        }
                        t.fx.has = true;
                    }
                }
            }
        }

        // 2.2 TxAmt
        if (pugi::xml_node ta = child_any(ad, "TxAmt")) {
            if (pugi::xml_node a = child_any(ta, "Amt")) {
                t.fxTxAmt = parse_amount(a);
                t.hasFxTxAmt = true;
            }
        }

        // 2.3 CntrValAmt
        if (pugi::xml_node cv = child_any(ad, "CntrValAmt")) {
            if (pugi::xml_node a = child_any(cv, "Amt")) {
                t.fxCounterValAmt = parse_amount(a);
                t.hasFxCntrVal = true;
            }
        }
    }

    // ----- NEW: prioritize account-currency amount from AmtDtls (if reasonable) -----
    {
        std::string accountCcy = find_account_ccy_from_tx(tx);

        if (!accountCcy.empty()) {
            if (pugi::xml_node ad = child_any(tx, "AmtDtls")) {
                std::optional<CurrencyAmount> acctAmt = pick_amount_in_ccy(ad, accountCcy);

                // overwrite t.txAmount only if:
                //   - we don’t yet have a Tx amount, OR
                //   - the existing amount is not in account currency
                if (acctAmt.has_value()) {
                    bool replace = false;

                    if (!t.txAmount.has_value()) {
                        replace = true;
                    } else if (t.txAmount->currency != accountCcy) {
                        replace = true;
                    }

                    if (replace) {
                        t.txAmount = acctAmt.value();
                    }
                }
            }
        }
    }

    // ----- NEW: derive FX rate consistently (detect inverted Src/Trgt) -----
    {
        const CurrencyAmount* aSrc = nullptr;
        const CurrencyAmount* aTrg = nullptr;

        // try to map amounts to the provided src/trgt
        if (t.fx.has) {
            if (t.hasFxInstdAmt && t.hasFxTxAmt &&
                !t.fx.srcCcy.empty() && !t.fx.trgtCcy.empty()) {

                if (t.fx.srcCcy == t.fxInstdAmt.currency && t.fx.trgtCcy == t.fxTxAmt.currency) {
                    aSrc = &t.fxInstdAmt;
                    aTrg = &t.fxTxAmt;
                } else if (t.fx.srcCcy == t.fxTxAmt.currency && t.fx.trgtCcy == t.fxInstdAmt.currency) {
                    aSrc = &t.fxTxAmt;
                    aTrg = &t.fxInstdAmt;
                }
            }

            if (!aSrc && t.hasFxCntrVal && t.hasFxInstdAmt &&
                !t.fx.srcCcy.empty() && !t.fx.trgtCcy.empty()) {

                if (t.fx.srcCcy == t.fxCounterValAmt.currency && t.fx.trgtCcy == t.fxInstdAmt.currency) {
                    aSrc = &t.fxCounterValAmt;
                    aTrg = &t.fxInstdAmt;
                } else if (t.fx.srcCcy == t.fxInstdAmt.currency && t.fx.trgtCcy == t.fxCounterValAmt.currency) {
                    aSrc = &t.fxInstdAmt;
                    aTrg = &t.fxCounterValAmt;
                }
            }
        }

        double effRate = 0.0;
        bool inverted = false;

        reconcile_ccyxchg(t.fx, aSrc, aTrg, &effRate, &inverted);

        if (effRate > 0.0) {
            t.fx.rate = effRate;
            t.fx.has = true;
            // optional: extend FxRateInfo with 'bool inverted' and set it here
        }
    }

    return t;
}


inline Entry parse_entry(const pugi::xml_node& ntry){
    Entry e;
    pugi::xml_node a = child_any(ntry,"Amt"); if (a) e.amount = parse_amount(a);
    pugi::xml_node c = child_any(ntry,"CdtDbtInd"); if (c) e.isCredit = (txt(c)=="CRDT");
    
    auto read_date_choice = [&](const pugi::xml_node& parent, const char* name) -> std::string {
        pugi::xml_node n = child_any(parent, name);
        if (!n) return {};
        std::string d = desc_text(n, "Dt");
        if (d.empty()) {
            std::string dtm = desc_text(n, "DtTm");
            if (!dtm.empty()) d = dtm.substr(0, 10); // "YYYY-MM-DD"
        }
        if (d.empty()) d = child_text(parent, name); // rare
        return d;
    };

    auto parse_iso_date = [](const std::string& s) {
        // expects YYYY-MM-DD (at least 10 chars)
        if (s.size() < 10) return 0;
        int y = std::stoi(s.substr(0, 4));
        int m = std::stoi(s.substr(5, 2));
        int d = std::stoi(s.substr(8, 2));
        return y * 10000 + m * 100 + d;  // e.g. 20251008
    };

    // in parse_entry(...)
    e.bookingDate = read_date_choice(ntry, "BookgDt");
    e.bookingDateInt= parse_iso_date(e.bookingDate);

    e.valueDate   = read_date_choice(ntry, "ValDt");
    e.valueDateInt  = parse_iso_date(e.valueDate);
    
    pugi::xml_node r = child_any(ntry,"NtryRef"); if (r) e.entryRef = txt(r);
    pugi::xml_node st= child_any(ntry,"Sts"); if (st) e.status = txt(st);
    pugi::xml_node rv= child_any(ntry,"RvslInd"); if (rv) { std::string s = txt(rv); e.reversal = (s=="true"||s=="1"); }
	
	pugi::xml_node asr = child_any(ntry, "AcctSvcrRef");
    if (asr) e.acctSvcrRef = txt(asr);

    pugi::xml_node nd = child_any(ntry,"NtryDtls");
    if (nd) {
        int txOrdinal = 0; // local counter per Entry
        for (pugi::xml_node td = nd.first_child(); td; td = td.next_sibling()) {
            if (!isln(td,"TxDtls")) continue;
            EntryTransaction tx = parse_txdtls(td);
            tx.importOrdinal = txOrdinal++;     // preserve TxDtls order
            e.transactions.push_back(std::move(tx));
        }
    }
    return e;
}

inline Balance parse_balance(const pugi::xml_node& bal) {
    Balance b;

    // --- Type (OPBD, PRCD, CLBD, ...) sicher extrahieren ---
    if (pugi::xml_node tp = child_any(bal, "Tp")) {
        // 1) Common case: <Tp><CdOrPrtry><Cd>CLBD</Cd></CdOrPrtry></Tp>
        if (pugi::xml_node cop = child_any(tp, "CdOrPrtry")) {
            if (pugi::xml_node cd = child_any(cop, "Cd"))      b.type = txt(cd);
            if (b.type.empty()) {
                if (pugi::xml_node pr = child_any(cop, "Prtry")) b.type = txt(pr);
            }
        }
        // 2) Fallback: <Tp><Cd>CLBD</Cd></Tp> bzw. <Tp><Prtry>...</Prtry></Tp>
        if (b.type.empty()) {
            if (pugi::xml_node cd = child_any(tp, "Cd"))      b.type = txt(cd);
            if (b.type.empty()) {
                if (pugi::xml_node pr = child_any(tp, "Prtry")) b.type = txt(pr);
            }
        }
        // 3) Final fallback: recursive search (desc_any)
        if (b.type.empty()) {
            if (pugi::xml_node cd = desc_any(tp, "Cd"))       b.type = txt(cd);
            if (b.type.empty()) {
                if (pugi::xml_node pr = desc_any(tp, "Prtry"))  b.type = txt(pr);
            }
        }
    }

    // --- Amount ---
    if (pugi::xml_node a = child_any(bal, "Amt"))
        b.amount = parse_amount(a);  // set currency + minor

    // --- CdtDbtInd (optional; set by many banks) ---
    if (pugi::xml_node cdi = child_any(bal, "CdtDbtInd")) {
        const std::string v = txt(cdi);
        b.hasCdtDbtInd = true;
        b.isCredit     = (v == "CRDT");
    }

    // --- Date ---
    if (pugi::xml_node d = child_any(bal, "Dt"))
        b.date = desc_text(d, "Dt");
    if (b.date.empty())
        b.date = child_text(bal, "Dt");

    return b;
}

inline Account parse_account(const pugi::xml_node& acct){
    Account a;
    pugi::xml_node id = child_any(acct,"Id"); if (id) a.id = parse_account_id(id);
    pugi::xml_node nm = child_any(acct,"Nm"); if (nm) a.name = txt(nm);
    pugi::xml_node cy = child_any(acct,"Ccy"); if (cy) a.currency = txt(cy);
    pugi::xml_node sv = child_any(acct,"Svcr"); if (sv) a.servicer = parse_agent(sv);
    return a;
}

inline GroupHeader parse_group_header(const pugi::xml_node& gh){
    GroupHeader g;
    pugi::xml_node m = child_any(gh,"MsgId"); if (m) g.msgId = txt(m);
    pugi::xml_node c = child_any(gh,"CreDtTm"); if (c) g.creationDateTime = txt(c);
    pugi::xml_node r = child_any(gh,"MsgRcpt");
    if (r) { pugi::xml_node nm = child_any(r,"Nm"); if (nm) g.messageRecipient = txt(nm); }
    return g;
}

inline Statement parse_statement(const pugi::xml_node& stmt, const GroupHeader* optHdr){
    Statement s; 
    if (optHdr) s.groupHeader = *optHdr;

    if (pugi::xml_node id = child_any(stmt,"Id"))      s.id = txt(id);
    if (pugi::xml_node cd = child_any(stmt,"CreDtTm")) s.creationDateTime = txt(cd);
    if (pugi::xml_node ac = child_any(stmt,"Acct"))    s.account = parse_account(ac);

    // helper: compare local element name (without namespace prefix)
    auto local_name_eq = [](const pugi::xml_node& n, const char* want)->bool {
        if (n.type() != pugi::node_element) 
		{
			return false;
		}
        const char* full = n.name();
        const char* colon = std::strchr(full, ':');
        const char* local = colon ? colon + 1 : full;
        return std::strcmp(local, want) == 0;
    };
    
    // --- Balances: directly under <Stmt> ---
	for (pugi::xml_node n = stmt.first_child(); n; n = n.next_sibling()){
		if (!isln(n, "Bal")) continue;
		s.balances.push_back(parse_balance(n));
	}

    // --- Entries: directly under <Stmt> ---
    int ordinal = 0;
	for (pugi::xml_node n = stmt.first_child(); n; n = n.next_sibling()){
		if (!isln(n, "Ntry")) continue;
		
        Entry e = parse_entry(n);
        e.importOrdinal = ordinal++;      // assign ordinal in original XML order
        s.entries.push_back(std::move(e));
	}

    return s;
}


inline pugi::xml_node find_payload(const pugi::xml_node& root){
    if (isln(root,"BkToCstmrStmt")||isln(root,"BkToCstmrDbtCdtNtfctn")||isln(root,"BkToCstmrAcctRpt"))
        return root;
    if (isln(root,"Document")){
        for (pugi::xml_node c = root.first_child(); c; c = c.next_sibling())
            if (isln(c,"BkToCstmrStmt")||isln(c,"BkToCstmrDbtCdtNtfctn")||isln(c,"BkToCstmrAcctRpt"))
                return c;
    }
    // generic depth-first search
    return desc_any(root, "BkToCstmrStmt") ?
           desc_any(root, "BkToCstmrStmt") :
           (desc_any(root, "BkToCstmrDbtCdtNtfctn") ?
                desc_any(root, "BkToCstmrDbtCdtNtfctn") :
                desc_any(root, "BkToCstmrAcctRpt"));
}

inline DocKind detect_kind(const pugi::xml_node& payload){
    if (isln(payload,"BkToCstmrStmt")) return DocKind::Camt053;
    if (isln(payload,"BkToCstmrDbtCdtNtfctn")) return DocKind::Camt054;
    if (isln(payload,"BkToCstmrAcctRpt")) return DocKind::Camt052;
    return DocKind::Unknown;
}

// ---------- Parser-Class ----------
class Parser {
public:
    
    bool parse_file(const std::string& path, Document& out, std::string* error=nullptr) const {
        pugi::xml_document doc;
        pugi::xml_parse_result ok = doc.load_file(path.c_str(), pugi::parse_default | pugi::parse_declaration);
        if (!ok){ if(error)*error="XML file parse error"; return false; }
        return parse_doc(doc, out, error);
    }

    bool parse_file(std::istream& is, Document& out, std::string* error=nullptr) const {
        pugi::xml_document doc;
        pugi::xml_parse_result ok = doc.load(is, pugi::parse_default | pugi::parse_declaration);
        if (!ok){ if(error)*error="XML file parse error"; return false; }
        return parse_doc(doc, out, error);
    }

    bool parse_string(const std::string& xml_utf8, Document& out, std::string* error=nullptr) const {
        pugi::xml_document doc;
        pugi::xml_parse_result ok = doc.load_buffer(xml_utf8.data(), xml_utf8.size(), pugi::parse_default | pugi::parse_declaration);
        if (!ok){ if(error)*error="XML parse error"; return false; }
        return parse_doc(doc, out, error);
    }

private:
    bool parse_doc(const pugi::xml_document& doc, Document& out, std::string* error) const {
        pugi::xml_node root = doc.document_element();
        if (!root){ if(error)*error="Empty document"; return false; }
        pugi::xml_node payload = find_payload(root);
        out.kind = detect_kind(payload);
        if (out.kind==DocKind::Unknown){ if(error)*error="Unsupported CAMT root"; return false; }

        // optional GrpHdr above the statements
        std::optional<GroupHeader> gh;
        pugi::xml_node g = child_any(payload,"GrpHdr");
        if (g) gh = parse_group_header(g);

        for (pugi::xml_node n = payload.first_child(); n; n = n.next_sibling()){
            if (isln(n,"Stmt") || isln(n,"Ntfctn") || isln(n,"Rpt"))
                out.statements.push_back(parse_statement(n, gh ? &*gh : nullptr));
        }
        return true;
    }
};

} // namespace camt
