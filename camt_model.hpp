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
#include <cstdint>
#include <optional>

namespace camt {

// --- Basis ---
struct CurrencyAmount {
    std::string currency;     // "EUR"
    std::int64_t minor{0};    // Minor units (Cent e.g.)
};

struct AccountId {
    std::string iban;
    std::string other;        // <Id><Othr><Id>
};

struct Agent {
    std::string bic;          // BIC or BICFI
    std::string name;         // optional (FinInstnId/Nm)
};

struct Account {
    AccountId id;
    std::string name;         // Acct/Nm
    std::string currency;     // Acct/Ccy
    Agent      servicer;      // Svcr/FinInstnId
};

struct Party {
    std::string name;     // Nm
    std::string iban;     // if Party == Acct/Hldr with IBAN (occurs depending on the bank)
    std::string bic;      // BIC/BICFI (if available)
    // Additional address fields could be added here
};

struct Purpose {
    std::string code;     // Purp/Cd
    std::string proprietary; // Purp/Prtry
};

struct References {
    std::string endToEndId;   // EndToEndId
    std::string txId;         // TxId
    std::string acctSvcrRef;  // AcctSvcrRef (Bank-Referenz)
    std::string mandateId;    // MndtId (SEPA-Lastschrift)
    std::string msgId;        // from GrpHdr/MsgId (at statement level)
};

struct BankTransactionCode {
    // BkTxCd/Cd/… Domain/Family/SubFamily plus proprietär
    std::string domain;       // Domn/Cd
    std::string family;       // Domn/Fmly/Cd
    std::string subFamily;    // Domn/Fmly/SubFmlyCd
    std::string proprietary;  // Prtry/Cd oder Prtry
};

struct ProprietaryBankTransactionCode {
    std::string code;         // Reversal/additional codes (optional)
    std::string issuer;
};

// Verwendungszweck/Remittance
struct StructuredRemittance {
    std::string creditorRefType;  // Strd/CdtrRefInf/RefTp/CdOrPrtry/…
    std::string creditorRef;      // Strd/CdtrRefInf/Ref
    std::string additionalInfo;   // Strd/AddtlRmtInf
};
struct RemittanceInformation {
    std::vector<std::string> unstructured;        // Ustrd[]
    std::vector<StructuredRemittance> structured; // Strd[]
};

// Beteiligte
struct RelatedParties {
    Party debtor;            // Dbtr
    AccountId debtorAccount; // DbtrAcct/Id/IBAN|Othr/Id
    Party ultimateDebtor;    // UltmtDbtr
    Party creditor;          // Cdtr
    AccountId creditorAccount;// CdtrAcct/…
    Party ultimateCreditor;  // UltmtCdtr
};

struct RelatedAgents {
    Agent debtorAgent;       // DbtrAgt/FinInstnId
    Agent creditorAgent;     // CdtrAgt/FinInstnId
};

struct ChargesRecord {
    CurrencyAmount amount;     // <Amt Ccy="...">...</Amt>
    Agent          agent;      // <Agt><FinInstnId>...</FinInstnId></Agt>
    bool hasCdtDbtInd = false; // ob <CdtDbtInd> vorhanden
    bool isCredit     = false; // CRDT=true / DBIT=false
    bool included     = false; // <ChrgInclInd>true</ChrgInclInd>
};

struct Charges {
    CurrencyAmount total;               // <TtlChrgsAndTaxAmt> (optional)
    std::vector<ChargesRecord> records; // <Rcrd>[]
};

struct FxRateInfo {
    std::string srcCcy, trgtCcy, unitCcy;
    double      rate = 0.0;
    bool        has  = false;
};

// Einzeltransaktion (Entry->TxDtls)
struct EntryTransaction {
    References refs;
    RelatedParties parties;
    RelatedAgents agents;
    RemittanceInformation remittance;
    Purpose purpose;
    BankTransactionCode bankTxCode;
    ProprietaryBankTransactionCode proprietaryBankTxCode;
    Charges charges;
    std::string additionalInfo;  // AddtlTxInf
	std::optional<CurrencyAmount> txAmount; // AmtDtls/TxAmt/Amt
	std::string dtaCode;  // e.g. "NMSC+201" (Prtry/Cd)
    std::string gvc;  // e.g. "201" (numeric part after '+')
	bool hasCdtDbtInd = false;  // true if CdtDbtInd was present at Tx level
    bool isCredit     = false;  // value from CdtDbtInd ("CRDT" = true, "DBIT" = false)
	std::string codeSwift; 
    FxRateInfo     fx;               // CcyXchg (exchange rate and parties involved)
    CurrencyAmount fxInstdAmt;       // InstdAmt/Amt  (original / instructed currency)
    CurrencyAmount fxTxAmt;          // TxAmt/Amt     (settlement amount in foreign currency)
    CurrencyAmount fxCounterValAmt;  // CntrValAmt/Amt (countervalue in account currency)
    bool           hasFxInstdAmt = false;
    bool           hasFxTxAmt    = false;
    bool           hasFxCntrVal  = false;
    int            importOrdinal{-1}; // Original TxDtls order inside the Entry
};

// Buchungszeile (Ntry)
struct Entry {
    CurrencyAmount amount;
    bool isCredit{false};         // CdtDbtInd == CRDT
    std::string bookingDate;      // BookgDt/Dt | ISO
    std::string valueDate;        // ValDt/Dt | ISO
    int bookingDateInt{0};        // parsed YYYYMMDD
    int valueDateInt{0};          // parsed YYYYMMDD
    std::string entryRef;         // NtryRef
    std::vector<EntryTransaction> transactions; // NtryDtls/TxDtls[]
    bool reversal{false};         // RvslInd
    std::string status;           // Sts
	std::string acctSvcrRef;      // Primanota at Entry level
    int importOrdinal{-1};        // running index within the statement
};

// Salden
struct Balance {
    std::string type;        // Tp/Cd | Tp/Prtry
    CurrencyAmount amount;   // Amt @Ccy
    std::string date;        // Dt/Dt | ISO
	bool hasCdtDbtInd = false; // true if present in the XML
    bool isCredit     = true;  // CRDT=true, DBIT=false (only valid if hasCdtDbtInd==true)
};

// Statement + Header
struct GroupHeader {
    std::string msgId;           // GrpHdr/MsgId
    std::string creationDateTime;// GrpHdr/CreDtTm
    std::string messageRecipient; // GrpHdr/MsgRcpt/Nm (if available)
};

struct Statement {
    std::string id;              // Stmt/Id
    std::string creationDateTime;// Stmt/CreDtTm
    Account account;
    GroupHeader groupHeader;     // true if present in the XML
    std::vector<Balance> balances;
    std::vector<Entry> entries;
};

// Dokument
enum class DocKind { Camt052, Camt053, Camt054, Unknown };

struct Document {
    DocKind kind{DocKind::Unknown};
    std::vector<Statement> statements;
};

} // namespace camt
