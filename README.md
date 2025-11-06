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

## Unicode Normalization (`USE_UTF8PROC`)

If compiled with `USE_UTF8PROC`, the following normalization is applied:
- Convert to **NFC**
- Case folding
- Remove zero-width artifacts
- Stable whitespace normalization

If `USE_UTF8PROC` is **not** defined:
- A minimal ASCII fallback is used.

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
