#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QCryptographicHash>
#include <camt_parser_pugi.hpp>
#include <camt_csv.hpp>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    camt::Parser parser;
    camt::Document doc;
    std::string err;

    QFile rsc(":/example.camt053.xml");
    if (!rsc.open(QIODevice::ReadOnly)) {
        qCritical() << "Internal error: resource missing";
        return 1;
    }

    QByteArray ba=rsc.readAll();

    if (!parser.parse_string(ba.constData(), doc, &err)) {
        qCritical("Parse error: %s", err.c_str());
        return 1;
    }

    camt::ExportOptions opt;
    opt.include_header = false;
    opt.signed_amount = true;
    opt.credit_as_bool = true;

    camt::ExportData camtData;
    camt::export_entries_csv(doc, nullptr, &camtData, opt);
    camt::sortExportData(camtData, opt.include_header, true);

    /*
    Each exported field consists of a pair:
    - first: Human-readable display value (formatted, signed, date-formatted)
    - second: Canonical normalized value used for sorting, comparison, and deterministic hashing.
    `second` is never empty — if not assigned directly, it is generated via normalization.
    */

    auto L = [](const char* label, int w = 34){
        return QString(label).leftJustified(w, QLatin1Char(' '));
    };

    for (const auto& row : camtData)
    {
        auto S = [](const std::string& s){ return QString::fromUtf8(s.c_str()); };

        auto v = [&](camt::ExportField f){
            return row[camt::to_index(f)];
        };

        std::string hash = camt::accumulate_hash_row(row);

        qDebug().noquote() << L("HashSHA256")        << QCryptographicHash::hash(hash.c_str(), QCryptographicHash::Sha256).toHex();
        qDebug().noquote() << L("CounterpartyIBAN:") << S(v(camt::ExportField::CounterpartyIBAN).first);
        qDebug().noquote() << L("RemittanceLine:")   << S(v(camt::ExportField::RemittanceLine).first);
        qDebug().noquote() << L("IsCredit:")         << v(camt::ExportField::CreditDebit).first;
        qDebug().noquote() << L("Reversal:")         << v(camt::ExportField::Reversal).first;

        qDebug().noquote()
            << L("ValueDate   YYYY-MM-DD:") << S(v(camt::ExportField::ValueDate).first)
            << "   "
            << L("ValueDate YYYYMMDD:")     << S(v(camt::ExportField::ValueDate).second);

        qDebug().noquote()
            << L("BookingDate YYYY-MM-DD:") << S(v(camt::ExportField::BookingDate).first)
            << "   "
            << L("BookingDate YYYYMMDD:")   << S(v(camt::ExportField::BookingDate).second);

        qDebug().noquote()
            << L("Amount (normalized):")    << S(v(camt::ExportField::Amount).second)
            << "   "
            << L("Amount (final):")         << S(v(camt::ExportField::Amount).first);

        qDebug() << "";
    }


    qInfo("Done.");
    return 0;
}
