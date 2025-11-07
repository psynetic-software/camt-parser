#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <fstream>
#include <camt_parser_pugi.hpp>
#include <camt_csv.hpp>

// FileRemover.h
#pragma once

#include <QString>
#include <QFile>

class FileRemover
{
public:
    explicit FileRemover(const QString& path)
        : m_path(path) {}

    ~FileRemover()
    {
        if (!m_path.isEmpty())
        {
            QFile f(m_path);
            if (f.exists())
            {
                f.remove(); // ignore return value intentionally
            }
        }
    }
private:
    QString m_path;
};


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

    QTemporaryFile tmp(QDir::tempPath() + "/camtXXXXXX.xml");
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        qCritical("Cannot create temp file");
        return 1;
    }
    tmp.write(ba);
    tmp.flush();
    tmp.close();

    FileRemover fm(tmp.fileName());

    /*
    Each exported field consists of a pair:
    - first: Human-readable display value (formatted, signed, date-formatted)
    - second: Canonical normalized value used for sorting, comparison, and deterministic hashing.
    `second` is never empty — if not assigned directly, it is generated via normalization.
    */

    auto L = [](const char* label, int w = 34){
        return QString(label).leftJustified(w, QLatin1Char(' '));
    };

    auto process_doc = [&](const camt::Document& doc, std::ostream* ostrPtr)
    {
        camt::ExportOptions opt;
        opt.include_header = false;
        opt.signed_amount = true;
        opt.credit_as_bool = true;

        camt::ExportData camtData;
        camt::export_entries_csv(doc, ostrPtr, &camtData, opt);
        camt::sortExportData(camtData, opt.include_header, true);

        for (const auto& row : camtData)
        {
            auto S = [](const std::string& s){ return QString::fromUtf8(s.c_str()); };
            auto v = [&](camt::ExportField f){ return row[camt::to_index(f)]; };

            std::string hash = camt::accumulate_hash_row(row);

            qDebug().noquote() << L("HashSHA256")               << QCryptographicHash::hash(hash.c_str(), QCryptographicHash::Sha256).toHex();
            qDebug().noquote() << L("CounterpartyIBAN:")        << S(v(camt::ExportField::CounterpartyIBAN).first);
            qDebug().noquote() << L("RemittanceLine:")          << S(v(camt::ExportField::RemittanceLine).first);
            qDebug().noquote() << L("IsCredit:")                << v(camt::ExportField::CreditDebit).first;
            qDebug().noquote() << L("Reversal:")                << v(camt::ExportField::Reversal).first;

            qDebug().noquote()
                << L("ValueDate   YYYY-MM-DD:")                 << S(v(camt::ExportField::ValueDate).first)
                << "   "
                << L("ValueDate YYYYMMDD:")                     << S(v(camt::ExportField::ValueDate).second);

            qDebug().noquote()
                << L("BookingDate YYYY-MM-DD:")                 << S(v(camt::ExportField::BookingDate).first)
                << "   "
                << L("BookingDate YYYYMMDD:")                   << S(v(camt::ExportField::BookingDate).second);

            qDebug().noquote()
                << L("Amount (normalized):")                    << v(camt::ExportField::Amount).second
                << "   "
                << L("Amount (final):")                         << v(camt::ExportField::Amount).first;

            qDebug().noquote() << L("RunningBalance:")          << v(camt::ExportField::RunningBalance).second;

            qDebug() << "";
        }
    };

    for(int step = 0; step <= 2; step++)
    {
        camt::Document doc;
        std::string err;
        std::unique_ptr<std::ostream> ostrPtr;

        if (step == 0)
        {
            // (1) std::istream
            std::ifstream in(std::filesystem::u8path(tmp.fileName().toUtf8().constData()), std::ios::in | std::ios::binary);
            if(!parser.parse_file(in, doc, &err))
            {
                qCritical("Parse error: %s", err.c_str());
                return 1;
            }
            qDebug() << "[INFO] Parsed from std::istream\n";
            ostrPtr = std::make_unique<std::ofstream>("export.csv", std::ios::binary);
        }
        else if (step == 1)
        {
            // (2) File name
            if (!parser.parse_file(tmp.fileName().toUtf8().constData(), doc, &err))
            {
                qCritical("Parse error: %s", err.c_str());
                return 1;
            }
            qDebug() << "[INFO] Parsed from filename\n";
        }
        else if (step == 2)
        {
            // (3) QByteArray / Memory Buffer
            if (!parser.parse_string(ba.constData(), doc, &err))
            {
                qCritical("Parse error: %s", err.c_str());
                return 1;
            }
            qDebug() << "[INFO] Parsed from memory buffer\n";
        }

        process_doc(doc, ostrPtr.get());
    }

    qInfo("Done.");
    return 0;
}
