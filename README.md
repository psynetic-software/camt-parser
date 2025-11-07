# camt-parser

A high-performance C++ library for parsing SEPA CAMT XML banking formats.

Supports:
- **camt.052** Bank-to-Customer Account Report
- **camt.053** Bank-to-Customer Statement
- **camt.054** Bank-to-Customer Debit/Credit Notification

The library provides a structured, domain-friendly data model and an optional
CSV export layer optimized for accounting, reconciliation, and audit workloads.

## Features

- Fully supports **camt.052 / .053 / .054**
- Extracts complete transaction details (counterparty, remittance, references)
- Unicode-aware free-text normalization (optional `USE_UTF8PROC`)
- Bank transaction code → **GVC** mapping included
- Optional **canonical transaction hash** for duplicate detection
- CSV export designed for **accounting systems**
- Zero locale-dependencies (monetary values kept as text until formatted)
- **MIT License** (commercial-friendly)

## Installation / Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Optional dependencies:
- **pugixml** (MIT) for XML parsing
- **utf8proc** (MIT) for Unicode normalization (if `USE_UTF8PROC` is defined)

## High-Level Data Model

```
Document
 └─ Statement(s)
     └─ Entry(s)
         └─ EntryTransaction(s)
```

### `Document`
Represents a full CAMT file (052 / 053 / 054).

### `Statement`
Contains metadata (account, creation time, balances) and `Entry` records.

### `Entry`
Represents a booked transaction line (booking date, value date, amount).

May contain **multiple** `EntryTransaction` elements.

### `EntryTransaction`
Contains full payment details:
- Counterparty (IBAN/BIC/Name)
- Remittance text (structured + unstructured)
- ISO 20022 BankTransactionCode (Domain / Family / SubFamily)
- Proprietary bank code
- Charges, fees, reversal indicator, etc.

## Parsing API

```cpp
#include <camt_parser_pugi.hpp>

camt::Parser p;
camt::Document doc;
std::string err;

if (!p.parse_file("statement.xml", doc, &err)) {
    std::cerr << "Parse error: " << err << "\n";
}
```

## CSV Export

```cpp
#include <camt_csv.hpp>

std::ofstream out("export.csv");

camt::ExportOptions opt;
opt.write_utf8_bom = true;          // Excel compatible
opt.remittance_separator = " | ";   // readable multi-part purpose text
opt.signed_amount = true;
opt.credit_as_bool = true;

camt::export_entries_csv(doc, &out, nullptr, opt);
```

## ExportOptions

| Option | Default | Description |
|--------|---------|-------------|
| `delimiter` | `';'` | CSV separator (`;`, `,`, or `\t`) |
| `include_header` | `true` | Adds header row |
| `write_utf8_bom` | `false` | Necessary for Excel UTF-8 import |
| `signed_amount` | `true` | Credit positive / Debit negative |
| `credit_as_bool` | `true` | `IsCredit = 1/0` instead of `CRDT/DBIT` |
| `remittance_separator` | `""` | Join multiple `Ustrd[]` lines |
| `use_effective_credit` | `false` | Apply reversal indicator |
| `prefer_ultimate_counterparty` | `true` | Prefer `UltmtDbtr` / `UltmtCdtr` |

## Exported Row Format: Display Value (`first`) vs Normalized Value (`second`)

Each exported field consists of a pair:
- **first**: Human-readable display value (formatted, signed, date-formatted)
- **second**: Canonical normalized value used for sorting, comparison, and deterministic hashing.

`second` is **never empty** — if not assigned directly, it is generated via normalization rules.

### Field overview (C1 display naming)

| Field | `first` (display) | `second` (normalized raw) | Influenced by options |
|---|---|---|---|
| Value Date | `YYYY-MM-DD` | `YYYYMMDD` digits | Sorting (`useBookingDate=false`) |
| Booking Date | `YYYY-MM-DD` | `YYYYMMDD` digits | Sorting (`useBookingDate=true`) |
| Amount | Formatted signed or unsigned | Absolute numeric text | `signed_amount`, `use_effective_credit` |
| Is Credit | `1`/`0` or `CRDT`/`DBIT` | `1`/`0` original direction | `credit_as_bool`, `use_effective_credit` |
| Reversal | `1` or `0` | `1` or `0` | Affects sign if `use_effective_credit=true` |
| Currency | `EUR` etc. | Uppercased, trimmed | Normalization only |
| Counterparty Name | Human-friendly chosen party | NFC/casefold/trim normalized | `prefer_ultimate_counterparty`, normalization |
| Counterparty IBAN | Formatted IBAN | Uppercase, no spaces | Normalization |
| Counterparty BIC | Formatted BIC | Uppercase, no spaces | Normalization |
| Remittance Line | Joined text lines | GS-joined normalized tokens | `remittance_separator`, `USE_UTF8PROC` |
| Remittance Structured | Display text | Normalized base | Normalization |
| End-to-End ID | Shown as provided | Uppercase, no spaces | Normalization |
| Mandate ID | Shown as provided | Uppercase, no spaces | Normalization |
| Transaction ID | Shown as provided | Uppercase, no spaces | Normalization |
| Bank Reference | Display reference | Normalized | Normalization |
| Account IBAN | Statement IBAN | Uppercase, no spaces | Normalization |
| Account BIC | Statement BIC | Uppercase, no spaces | Normalization |
| Booking Code | Code as text | Uppercase trimmed | Normalization |
| Status | Display text | Trimmed | Normalization |
| Running Balance | Formatted running total | Same as display | Always signed logically (`CRDT = +`, `DBIT = −`) |
| Charges Amount | Display charges | Same as display | Independent of `signed_amount` |
| Charges Currency | Display currency | Normalized | Normalization |
| Charges Included | `1`/`0` | Same | None |
| Entry Ordinal | Display index | Same | Used as stable tiebreaker |
| Transaction Ordinal | Display index | Same | Used as stable tiebreaker |

This section ensures consistent interpretation when converting to CSV, databases, or accounting systems.

 (`first`) vs Normalized Value (`second`)

Each exported field consists of a pair:
- **first**: Human-readable display value (formatted, signed, date-formatted)
- **second**: Canonical normalized value used for sorting, comparison, and deterministic hashing.

`second` is **never empty** — if not assigned directly, it is generated via normalization.

| Display Name | `first` (human output) | `second` (normalized / for sorting) | Relevant Options |
|---|---|---|---|
| **Value Date** | `YYYY-MM-DD` | `YYYYMMDD` | Affects sorting order |
| **Booking Date** | `YYYY-MM-DD` | `YYYYMMDD` | Sorting if selected |
| **Amount** | Signed or unsigned amount text | Absolute numeric amount | `signed_amount`, `use_effective_credit` |
| **Is Credit** | `1` (credit) / `0` (debit) or `CRDT/DBIT` | Always `1/0` for original direction | `credit_as_bool`, `use_effective_credit` |
| **Reversal** | `1` or `0` | Same | May flip meaning under `use_effective_credit` |
| **Counterparty Name** | Best resolved party name | Case-normalized canonical text | `prefer_ultimate_counterparty` |
| **Counterparty IBAN** | Standard IBAN text | Uppercased, spaces removed | Normalization |
| **Counterparty BIC** | Standard BIC text | Uppercased, spaces removed | Normalization |
| **Remittance** | Joined free text lines | Canonical token-joined, normalized lines | `remittance_separator`, `USE_UTF8PROC` |
| **Structured Reference** | Display reference text | Normalized canonical text | `USE_UTF8PROC` |
| **End-to-End ID** | Displayed as present | Uppercased, spaces removed | Normalization |
| **Mandate ID / Transaction ID / Bank Reference** | Display text | Normalized ID | Normalization |
| **Running Balance** | Accumulated signed balance | Same as display | Determined by sorting order |
| **Opening / Closing Balance** | Displayed balance | Same | Placement depends on statement order |

Normalization rules:
- Text fields are trimmed and unified
- Codes & currency uppercased
- IBAN/BIC spaces removed
- Date `.second` always `YYYYMMDD`
- `Amount.second` is always absolute value
- Advanced Unicode normalization if compiled with `USE_UTF8PROC`

| Option | Default | Description |
|--------|---------|-------------|
| `delimiter` | `';'` | CSV separator (`;`, `,`, or `\t`) |
| `include_header` | `true` | Adds header row |
| `write_utf8_bom` | `false` | Necessary for Excel UTF-8 import |
| `signed_amount` | `true` | Credit positive / Debit negative |
| `credit_as_bool` | `true` | `IsCredit = 1/0` instead of `CRDT/DBIT` |
| `remittance_separator` | `""` | Join multiple `Ustrd[]` lines |
| `use_effective_credit` | `false` | Apply reversal indicator |
| `prefer_ultimate_counterparty` | `true` | Prefer `UltmtDbtr` / `UltmtCdtr` |

## Exported Row Format: first vs second

Every column in `ExportData` is stored as a pair:

- `first` → human-readable, formatted display value
- `second` → normalized, canonical value used for sorting, hashing, and stable processing

If a field does not explicitly assign `second`, it is filled from `first` via normalization.

Normalization rules include:
- Date fields → `YYYYMMDD`
- IBAN/BIC → uppercase, no spaces
- Free-text fields → trimmed, casefolded (with utf8proc if enabled)
- Amounts → `second` stores absolute value

Sorting uses `.second` for date ordering.
Running balance uses signed logic independent of display formatting.

## Optional Unicode Normalization (`USE_UTF8PROC`)

This library supports optional full Unicode normalization of free-text fields
(e.g., remittance lines, counterparty names, references).

If compiled with `-DUSE_UTF8PROC`, the following normalization is applied using
the MIT-licensed `utf8proc` library:

- Normalize text to **NFC**
- Unicode-aware case folding
- Removal of zero-width characters
- Stable whitespace normalization

If `USE_UTF8PROC` is **not** defined:

- A lightweight ASCII-only fallback is used
- No external dependencies are required
- No utf8proc code is linked or distributed


## Canonical Transaction Hashing

```cpp
std::string hash = camt::accumulate_hash_row(row);
```

Stable fingerprint for:
- Duplicate detection
- Ledger synchronization
- Audit trails

## License

Released under the **MIT License**.

```
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2025 Psynetic
```
