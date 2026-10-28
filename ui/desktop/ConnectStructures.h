#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <functional>

// NEEBLES Connect owns only a typed consumer of the authenticated Secrets API.
// KDBX files, master keys, migration and crypto remain exclusively Secrets-owned.
namespace ConnectStructures {

struct Record {
    QString id;
    quint64 revision = 0;
    QJsonObject value;
};

class Store final {
public:
    using Broker = std::function<QJsonObject(const QJsonObject&)>;
    explicit Store(Broker broker);

    // Idempotent: existing identical schemas are preserved by Secrets; an
    // incompatible schema requires an explicit migration, never an overwrite.
    bool ensureSchemas(QString* error = nullptr) const;

    bool list(const QString& schemaId, QVector<Record>& records,
              QString* error = nullptr) const;
    bool get(const QString& schemaId, const QString& id, Record& record,
             QString* error = nullptr) const;
    bool create(const QString& schemaId, const QString& id, const QJsonObject& value,
                Record& created, QString* error = nullptr) const;
    bool replace(const QString& schemaId, const Record& previous,
                 const QJsonObject& value, Record& updated,
                 QString* error = nullptr) const;
    bool erase(const QString& schemaId, const Record& existing,
               QString* error = nullptr) const;

    // One My Connect equipment record plus zero or more VS Code records.
    // SSH, VNC, Sunshine are optional groups in the equipment record.
    static bool validValue(const QString& schemaId, const QJsonObject& value,
                           QString* error = nullptr);
    static QString newId(const QString& schemaId);
    static bool knownSchema(const QString& schemaId);

private:
    bool request(const QJsonObject& body, QJsonObject& data,
                 QString* error) const;
    Broker broker_;
};

} // namespace ConnectStructures
