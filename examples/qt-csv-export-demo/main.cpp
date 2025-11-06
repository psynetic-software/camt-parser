#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
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

    for (const auto& row : camtData)
    {
        auto S = [](const std::string& s){ return QString::fromUtf8(s.c_str()); };

        qDebug()    << "CounterpartyIBAN:"                          <<  S(row[camt::to_index(camt::ExportField::CounterpartyIBAN)].first);
        qDebug()    << "RemittanceLine:"                            <<  S(row[camt::to_index(camt::ExportField::RemittanceLine)].first);
        qDebug()    << "IsCredit:"                                  <<  row[camt::to_index(camt::ExportField::CreditDebit)].first;
        qDebug()    << "Reversal:"                                  <<  row[camt::to_index(camt::ExportField::Reversal)].first;

        qDebug()    << "ValueDate   YYYY-MM-DD:"                    <<  S(row[camt::to_index(camt::ExportField::ValueDate)].first)
                    << "ValueDate   YYYYMMDD:"                      <<  S(row[camt::to_index(camt::ExportField::ValueDate)].second)     << "\n"
                    << "BookingDate YYYY-MM-DD:"                    <<  S(row[camt::to_index(camt::ExportField::BookingDate)].first)
                    << "BookingDate YYYYMMDD:"                      <<  S(row[camt::to_index(camt::ExportField::BookingDate)].second)   << "\n"
                    << "Amount before CreditDebit and Reversal:"    <<  S(row[camt::to_index(camt::ExportField::Amount)].second)
                    << "Final Amount:"                              <<  S(row[camt::to_index(camt::ExportField::Amount)].first)
                    << "\n";
    }

    qInfo("Done.");
    return 0;
}
