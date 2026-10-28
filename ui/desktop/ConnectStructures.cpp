#include "ConnectStructures.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <utility>

namespace ConnectStructures {
namespace {

const QString kModule = QStringLiteral("neebles-connect");
const QString kEquipment = QStringLiteral("myconnect");
const QString kVsCode = QStringLiteral("vscode");
const QString kDashboardLayout = QStringLiteral("dashboard");
constexpr double kJsonExactMax = 9007199254740991.0;

void fail(QString* error, const QString& reason) {
    if (error) *error = reason;
}

QJsonObject field(const char* kind, bool required = false, int maxLength = 0) {
    QJsonObject rule{{QStringLiteral("type"), QString::fromLatin1(kind)}};
    if (required) rule.insert(QStringLiteral("required"), true);
    if (maxLength > 0) rule.insert(QStringLiteral("max_length"), maxLength);
    return rule;
}

QJsonObject equipmentSchema() {
    // A newly created machine has no protocol configuration at all.
    // Fixed protocols are optional field groups: none can have >1 instance
    // because one equipment ID has exactly one complete record.
    const QJsonObject fields{
        {QStringLiteral("name"), field("string", true, 128)},
        {QStringLiteral("ip"), field("string", true, 255)},
        {QStringLiteral("user"), field("string", false, 128)},
        {QStringLiteral("ssh-name"), field("string", false, 128)},
        {QStringLiteral("ssh-user"), field("string", false, 128)},
        {QStringLiteral("ssh-port"), field("integer")},
        {QStringLiteral("ssh-pass"), field("secret", false, 2048)},
        {QStringLiteral("vnc-name"), field("string", false, 128)},
        {QStringLiteral("vnc-port"), field("integer")},
        {QStringLiteral("vnc-pass"), field("secret", false, 2048)},
        {QStringLiteral("vnc-monitor"), field("string", false, 128)},
        {QStringLiteral("sunshine-name"), field("string", false, 128)},
        {QStringLiteral("sunshine-pass"), field("secret", false, 2048)}
    };
    return {{QStringLiteral("id"), kEquipment},
            {QStringLiteral("version"), 1},
            {QStringLiteral("require_replica"), true},
            {QStringLiteral("fields"), fields},
            {QStringLiteral("unique"), QJsonArray{}}};
}

QJsonObject vsCodeSchema() {
    // VS Code is the ONLY 0..N protocol per equipment. Secrets enforces
    // both referential integrity and (equipment,port) uniqueness.
    const QJsonObject fields{
        {QStringLiteral("equipment"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("reference")},
            {QStringLiteral("required"), true},
            {QStringLiteral("target_schema"), kEquipment}}},
        {QStringLiteral("name"), field("string", true, 128)},
        {QStringLiteral("port"), field("integer", true)},
        {QStringLiteral("pass"), field("secret", false, 2048)}
    };
    return {{QStringLiteral("id"), kVsCode},
            {QStringLiteral("version"), 1},
            {QStringLiteral("require_replica"), true},
            {QStringLiteral("fields"), fields},
            {QStringLiteral("unique"), QJsonArray{
                QJsonArray{QStringLiteral("equipment"), QStringLiteral("port")}}}};
}

QJsonObject dashboardSchema() {
    // One encrypted JSON layout document. Not a connection protocol.
    // Bounded by the existing Secrets single-value limit.
    return {{QStringLiteral("id"), kDashboardLayout},
            {QStringLiteral("version"), 1},
            {QStringLiteral("require_replica"), false},
            {QStringLiteral("fields"), QJsonObject{
                {QStringLiteral("layout"), field("string", true, 8000)}}},
            {QStringLiteral("unique"), QJsonArray{}}};
}

QJsonObject envelope(const QString& op, const QString& schemaId) {
    return {{QStringLiteral("op"), op},
            {QStringLiteral("module"), kModule},
            {QStringLiteral("schema_id"), schemaId}};
}

bool parseRecord(const QJsonObject& data, Record& record, QString* error) {
    const QString id = data.value(QStringLiteral("id")).toString();
    const QJsonValue rv = data.value(QStringLiteral("revision"));
    const double revision = rv.toDouble();
    if (id.isEmpty() || !rv.isDouble() || revision < 1 ||
        revision > kJsonExactMax ||
        revision != static_cast<double>(static_cast<quint64>(revision)) ||
        !data.value(QStringLiteral("value")).isObject()) {
        fail(error, QStringLiteral("invalid_structure_reply"));
        return false;
    }
    record = Record{id, static_cast<quint64>(revision),
                    data.value(QStringLiteral("value")).toObject()};
    return true;
}

bool validPort(const QJsonValue& v) {
    if (!v.isDouble()) return false;
    const double port = v.toDouble();
    return port >= 1 && port <= 65535 && port == static_cast<int>(port);
}

bool nonEmptyString(const QJsonObject& value, const char* fieldName) {
    const QJsonValue v = value.value(QString::fromLatin1(fieldName));
    return v.isString() && !v.toString().trimmed().isEmpty();
}

bool validateGroup(const QJsonObject& value, const QString& prefix,
                   const QStringList& requiredNames, QString* error) {
    bool any = false;
    for (auto i = value.begin(); i != value.end(); ++i) {
        if (i.key().startsWith(prefix + QLatin1Char('-'))) any = true;
    }
    if (!any) return true;
    for (const QString& name : requiredNames) {
        const QString key = prefix + QLatin1Char('-') + name;
        if (name == QStringLiteral("port")) {
            if (!validPort(value.value(key))) {
                fail(error, QStringLiteral("invalid_%1_port").arg(prefix));
                return false;
            }
        } else if (!value.value(key).isString() ||
                   value.value(key).toString().trimmed().isEmpty()) {
            fail(error, QStringLiteral("incomplete_%1_configuration").arg(prefix));
            return false;
        }
    }
    return true;
}

} // namespace

Store::Store(Broker broker) : broker_(std::move(broker)) {}

bool Store::knownSchema(const QString& schemaId) {
    return schemaId == kEquipment || schemaId == kVsCode || schemaId == kDashboardLayout;
}

QString Store::newId(const QString& schemaId) {
    if (!knownSchema(schemaId)) return {};
    const QString prefix = schemaId == kEquipment ? QStringLiteral("m-") :
                                                   QStringLiteral("c-");
    // Secrets structured IDs are restricted to <=28 lower-case ASCII bytes.
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces)
                        .remove(QLatin1Char('-')).left(24);
}

bool Store::validValue(const QString& schemaId, const QJsonObject& value,
                       QString* error) {
    if (schemaId == kEquipment) {
        if (!nonEmptyString(value, "name") || !nonEmptyString(value, "ip")) {
            fail(error, QStringLiteral("equipment_name_and_host_required"));
            return false;
        }
        if (!validateGroup(value, QStringLiteral("ssh"),
                           {QStringLiteral("name"), QStringLiteral("user"),
                            QStringLiteral("port")}, error)) return false;
        if (!validateGroup(value, QStringLiteral("vnc"),
                           {QStringLiteral("name"), QStringLiteral("port")}, error)) return false;
        if (!validateGroup(value, QStringLiteral("sunshine"),
                           {QStringLiteral("name")}, error)) return false;
        return true;
    }
    if (schemaId == kVsCode) {
        if (!nonEmptyString(value, "equipment") || !nonEmptyString(value, "name") ||
            !validPort(value.value(QStringLiteral("port")))) {
            fail(error, QStringLiteral("incomplete_vscode_configuration"));
            return false;
        }
        return true;
    }
    if (schemaId == kDashboardLayout) {
        if (!nonEmptyString(value, "layout") ||
            value.value(QStringLiteral("layout")).toString().toUtf8().size() > 8000) {
            fail(error, QStringLiteral("invalid_dashboard_layout"));
            return false;
        }
        return true;
    }
    fail(error, QStringLiteral("unknown_connect_schema"));
    return false;
}

bool Store::request(const QJsonObject& body, QJsonObject& data,
                    QString* error) const {
    if (!broker_) {
        fail(error, QStringLiteral("broker_unavailable"));
        return false;
    }
    const QJsonObject reply = broker_(body);
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        // Never copy sensitive requests or secret-bearing replies into logs.
        const QString reason = reply.value(QStringLiteral("error")).toString();
        fail(error, reason.isEmpty() ? QStringLiteral("secrets_unavailable") : reason);
        return false;
    }
    if (!reply.value(QStringLiteral("data")).isObject()) {
        fail(error, QStringLiteral("invalid_structure_reply"));
        return false;
    }
    data = reply.value(QStringLiteral("data")).toObject();
    return true;
}

bool Store::ensureSchemas(QString* error) const {
    // Only the actual My Connect UI will invoke this after confirming
    // an existing, unlocked module vault. Never call on module update
    // without that precondition; never recreate a present KDBX.
    for (const QJsonObject& schema : {equipmentSchema(), vsCodeSchema(), dashboardSchema()}) {
        QJsonObject data;
        const QJsonObject body{
            {QStringLiteral("op"), QStringLiteral("structure.schema_define")},
            {QStringLiteral("module"), kModule},
            {QStringLiteral("schema"), schema}};
        if (!request(body, data, error)) return false;
        if (data.value(QStringLiteral("id")) != schema.value(QStringLiteral("id")) ||
            data.value(QStringLiteral("version")).toInt() != 1) {
            fail(error, QStringLiteral("schema_definition_mismatch"));
            return false;
        }
    }
    return true;
}

bool Store::get(const QString& schemaId, const QString& id,
                Record& record, QString* error) const {
    if (!knownSchema(schemaId) || id.isEmpty()) {
        fail(error, QStringLiteral("invalid_connect_record"));
        return false;
    }
    QJsonObject body = envelope(QStringLiteral("structure.get"), schemaId);
    body.insert(QStringLiteral("id"), id);
    QJsonObject data;
    if (!request(body, data, error)) return false;
    if (!parseRecord(data, record, error) || record.id != id ||
        data.value(QStringLiteral("schema_id")).toString() != schemaId) {
        fail(error, QStringLiteral("invalid_structure_reply"));
        return false;
    }
    return true;
}

bool Store::list(const QString& schemaId, QVector<Record>& records,
                 QString* error) const {
    if (!knownSchema(schemaId)) {
        fail(error, QStringLiteral("unknown_connect_schema"));
        return false;
    }
    QVector<Record> loaded;
    QSet<QString> seen;
    QString cursor;
    for (;;) {
        QJsonObject body = envelope(QStringLiteral("structure.list"), schemaId);
        body.insert(QStringLiteral("limit"), 100);
        if (!cursor.isEmpty()) body.insert(QStringLiteral("cursor"), cursor);
        QJsonObject page;
        if (!request(body, page, error)) return false;
        if (!page.value(QStringLiteral("ids")).isArray() ||
            !page.value(QStringLiteral("has_more")).isBool()) {
            fail(error, QStringLiteral("invalid_structure_page"));
            return false;
        }
        for (const QJsonValue& value : page.value(QStringLiteral("ids")).toArray()) {
            if (!value.isString() || value.toString().isEmpty() || seen.contains(value.toString())) {
                fail(error, QStringLiteral("invalid_structure_page"));
                return false;
            }
            const QString id = value.toString();
            seen.insert(id);
            Record record;
            if (!get(schemaId, id, record, error)) return false;
            loaded.append(std::move(record));
        }
        if (!page.value(QStringLiteral("has_more")).toBool()) {
            records = std::move(loaded);
            return true;
        }
        const QString next = page.value(QStringLiteral("next_cursor")).toString();
        if (next.isEmpty() || next == cursor || !seen.contains(next)) {
            fail(error, QStringLiteral("invalid_structure_cursor"));
            return false;
        }
        cursor = next;
    }
}

bool Store::create(const QString& schemaId, const QString& id,
                   const QJsonObject& value, Record& created,
                   QString* error) const {
    if (id.isEmpty() || !validValue(schemaId, value, error)) return false;
    QJsonObject body = envelope(QStringLiteral("structure.put"), schemaId);
    body.insert(QStringLiteral("id"), id);
    body.insert(QStringLiteral("value"), value);
    QJsonObject data;
    if (!request(body, data, error)) return false;
    if (data.value(QStringLiteral("id")).toString() != id ||
        data.value(QStringLiteral("revision")).toDouble() != 1 ||
        !data.value(QStringLiteral("created")).toBool()) {
        fail(error, QStringLiteral("invalid_structure_reply"));
        return false;
    }
    created = Record{id, 1, value};
    return true;
}

bool Store::replace(const QString& schemaId, const Record& previous,
                    const QJsonObject& value, Record& updated,
                    QString* error) const {
    if (previous.id.isEmpty() || previous.revision < 1 ||
        previous.revision >= static_cast<quint64>(kJsonExactMax) ||
        !validValue(schemaId, value, error)) {
        if (error && error->isEmpty()) fail(error, QStringLiteral("invalid_connect_revision"));
        return false;
    }
    QJsonObject body = envelope(QStringLiteral("structure.put"), schemaId);
    body.insert(QStringLiteral("id"), previous.id);
    body.insert(QStringLiteral("expected_revision"),
                static_cast<double>(previous.revision));
    body.insert(QStringLiteral("value"), value);
    QJsonObject data;
    if (!request(body, data, error)) return false;
    if (data.value(QStringLiteral("id")).toString() != previous.id ||
        data.value(QStringLiteral("revision")).toDouble() !=
            static_cast<double>(previous.revision + 1) ||
        data.value(QStringLiteral("created")).toBool(true)) {
        fail(error, QStringLiteral("invalid_structure_reply"));
        return false;
    }
    updated = Record{previous.id, previous.revision + 1, value};
    return true;
}

bool Store::erase(const QString& schemaId, const Record& existing,
                  QString* error) const {
    if (!knownSchema(schemaId) || existing.id.isEmpty() || existing.revision < 1 ||
        existing.revision > static_cast<quint64>(kJsonExactMax)) {
        fail(error, QStringLiteral("invalid_connect_revision"));
        return false;
    }
    QJsonObject body = envelope(QStringLiteral("structure.delete"), schemaId);
    body.insert(QStringLiteral("id"), existing.id);
    body.insert(QStringLiteral("expected_revision"),
                static_cast<double>(existing.revision));
    QJsonObject data;
    if (!request(body, data, error)) return false;
    if (data.value(QStringLiteral("id")).toString() != existing.id) {
        fail(error, QStringLiteral("invalid_structure_reply"));
        return false;
    }
    return true;
}

} // namespace ConnectStructures
