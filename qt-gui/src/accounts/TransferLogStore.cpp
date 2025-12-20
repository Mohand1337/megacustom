#include "TransferLogStore.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

namespace MegaCustom {

TransferLogStore::TransferLogStore(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
{
    // Database path in config directory
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configPath);
    m_dbPath = configPath + "/transfer_history.db";
}

TransferLogStore::~TransferLogStore()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool TransferLogStore::initialize()
{
    if (m_initialized) {
        return true;
    }

    // Use a unique connection name
    QString connectionName = "TransferLogStore_" + QString::number(reinterpret_cast<quintptr>(this));

    m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qWarning() << "TransferLogStore: Failed to open database:" << m_db.lastError().text();
        return false;
    }

    if (!createTables()) {
        qWarning() << "TransferLogStore: Failed to create tables";
        return false;
    }

    m_initialized = true;
    qDebug() << "TransferLogStore: Initialized at" << m_dbPath;
    return true;
}

bool TransferLogStore::createTables()
{
    QSqlQuery query(m_db);

    QString createTable = R"(
        CREATE TABLE IF NOT EXISTS transfers (
            id TEXT PRIMARY KEY,
            timestamp INTEGER NOT NULL,
            source_account_id TEXT NOT NULL,
            source_path TEXT NOT NULL,
            target_account_id TEXT NOT NULL,
            target_path TEXT NOT NULL,
            operation INTEGER NOT NULL,
            status INTEGER NOT NULL,
            bytes_transferred INTEGER DEFAULT 0,
            bytes_total INTEGER DEFAULT 0,
            files_transferred INTEGER DEFAULT 0,
            files_total INTEGER DEFAULT 0,
            error_message TEXT,
            error_code INTEGER DEFAULT 0,
            retry_count INTEGER DEFAULT 0,
            can_retry INTEGER DEFAULT 1,
            updated_at INTEGER
        )
    )";

    if (!query.exec(createTable)) {
        qWarning() << "TransferLogStore: Failed to create transfers table:" << query.lastError().text();
        return false;
    }

    // Create indexes for common queries
    query.exec("CREATE INDEX IF NOT EXISTS idx_transfers_timestamp ON transfers(timestamp DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_transfers_status ON transfers(status)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_transfers_source ON transfers(source_account_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_transfers_target ON transfers(target_account_id)");

    return true;
}

void TransferLogStore::logTransfer(const CrossAccountTransfer& transfer)
{
    if (!m_initialized && !initialize()) {
        return;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO transfers (
            id, timestamp, source_account_id, source_path,
            target_account_id, target_path, operation, status,
            bytes_transferred, bytes_total, files_transferred, files_total,
            error_message, error_code, retry_count, can_retry, updated_at
        ) VALUES (
            :id, :timestamp, :source_account_id, :source_path,
            :target_account_id, :target_path, :operation, :status,
            :bytes_transferred, :bytes_total, :files_transferred, :files_total,
            :error_message, :error_code, :retry_count, :can_retry, :updated_at
        )
    )");

    query.bindValue(":id", transfer.id);
    query.bindValue(":timestamp", transfer.timestamp.toSecsSinceEpoch());
    query.bindValue(":source_account_id", transfer.sourceAccountId);
    query.bindValue(":source_path", transfer.sourcePath);
    query.bindValue(":target_account_id", transfer.targetAccountId);
    query.bindValue(":target_path", transfer.targetPath);
    query.bindValue(":operation", static_cast<int>(transfer.operation));
    query.bindValue(":status", static_cast<int>(transfer.status));
    query.bindValue(":bytes_transferred", transfer.bytesTransferred);
    query.bindValue(":bytes_total", transfer.bytesTotal);
    query.bindValue(":files_transferred", transfer.filesTransferred);
    query.bindValue(":files_total", transfer.filesTotal);
    query.bindValue(":error_message", transfer.errorMessage);
    query.bindValue(":error_code", transfer.errorCode);
    query.bindValue(":retry_count", transfer.retryCount);
    query.bindValue(":can_retry", transfer.canRetry ? 1 : 0);
    query.bindValue(":updated_at", QDateTime::currentSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "TransferLogStore: Failed to log transfer:" << query.lastError().text();
        return;
    }

    emit transferLogged(transfer);
}

void TransferLogStore::updateTransfer(const CrossAccountTransfer& transfer)
{
    if (!m_initialized && !initialize()) {
        return;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        UPDATE transfers SET
            status = :status,
            bytes_transferred = :bytes_transferred,
            bytes_total = :bytes_total,
            files_transferred = :files_transferred,
            files_total = :files_total,
            error_message = :error_message,
            error_code = :error_code,
            retry_count = :retry_count,
            can_retry = :can_retry,
            updated_at = :updated_at
        WHERE id = :id
    )");

    query.bindValue(":id", transfer.id);
    query.bindValue(":status", static_cast<int>(transfer.status));
    query.bindValue(":bytes_transferred", transfer.bytesTransferred);
    query.bindValue(":bytes_total", transfer.bytesTotal);
    query.bindValue(":files_transferred", transfer.filesTransferred);
    query.bindValue(":files_total", transfer.filesTotal);
    query.bindValue(":error_message", transfer.errorMessage);
    query.bindValue(":error_code", transfer.errorCode);
    query.bindValue(":retry_count", transfer.retryCount);
    query.bindValue(":can_retry", transfer.canRetry ? 1 : 0);
    query.bindValue(":updated_at", QDateTime::currentSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "TransferLogStore: Failed to update transfer:" << query.lastError().text();
        return;
    }

    emit transferUpdated(transfer);
}

CrossAccountTransfer TransferLogStore::getTransfer(const QString& transferId) const
{
    if (!m_initialized) {
        return CrossAccountTransfer();
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM transfers WHERE id = :id");
    query.bindValue(":id", transferId);

    if (query.exec() && query.next()) {
        return transferFromQuery(query);
    }

    return CrossAccountTransfer();
}

QList<CrossAccountTransfer> TransferLogStore::getAll(int limit, int offset) const
{
    QList<CrossAccountTransfer> result;

    if (!m_initialized) {
        return result;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM transfers ORDER BY timestamp DESC LIMIT :limit OFFSET :offset");
    query.bindValue(":limit", limit);
    query.bindValue(":offset", offset);

    if (query.exec()) {
        while (query.next()) {
            result.append(transferFromQuery(query));
        }
    }

    return result;
}

QList<CrossAccountTransfer> TransferLogStore::getByStatus(CrossAccountTransfer::Status status, int limit) const
{
    QList<CrossAccountTransfer> result;

    if (!m_initialized) {
        return result;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM transfers WHERE status = :status ORDER BY timestamp DESC LIMIT :limit");
    query.bindValue(":status", static_cast<int>(status));
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            result.append(transferFromQuery(query));
        }
    }

    return result;
}

QList<CrossAccountTransfer> TransferLogStore::getByAccount(const QString& accountId, int limit) const
{
    QList<CrossAccountTransfer> result;

    if (!m_initialized) {
        return result;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT * FROM transfers
        WHERE source_account_id = :account_id OR target_account_id = :account_id
        ORDER BY timestamp DESC LIMIT :limit
    )");
    query.bindValue(":account_id", accountId);
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            result.append(transferFromQuery(query));
        }
    }

    return result;
}

QList<CrossAccountTransfer> TransferLogStore::getByDateRange(const QDateTime& from, const QDateTime& to, int limit) const
{
    QList<CrossAccountTransfer> result;

    if (!m_initialized) {
        return result;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT * FROM transfers
        WHERE timestamp >= :from AND timestamp <= :to
        ORDER BY timestamp DESC LIMIT :limit
    )");
    query.bindValue(":from", from.toSecsSinceEpoch());
    query.bindValue(":to", to.toSecsSinceEpoch());
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            result.append(transferFromQuery(query));
        }
    }

    return result;
}

QList<CrossAccountTransfer> TransferLogStore::search(const QString& query, int limit) const
{
    QList<CrossAccountTransfer> result;

    if (!m_initialized || query.isEmpty()) {
        return result;
    }

    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare(R"(
        SELECT * FROM transfers
        WHERE source_path LIKE :query OR target_path LIKE :query
        ORDER BY timestamp DESC LIMIT :limit
    )");
    sqlQuery.bindValue(":query", "%" + query + "%");
    sqlQuery.bindValue(":limit", limit);

    if (sqlQuery.exec()) {
        while (sqlQuery.next()) {
            result.append(transferFromQuery(sqlQuery));
        }
    }

    return result;
}

QMap<CrossAccountTransfer::Status, int> TransferLogStore::getStatusCounts() const
{
    QMap<CrossAccountTransfer::Status, int> counts;

    if (!m_initialized) {
        return counts;
    }

    QSqlQuery query(m_db);
    if (query.exec("SELECT status, COUNT(*) FROM transfers GROUP BY status")) {
        while (query.next()) {
            auto status = static_cast<CrossAccountTransfer::Status>(query.value(0).toInt());
            counts[status] = query.value(1).toInt();
        }
    }

    return counts;
}

void TransferLogStore::deleteTransfer(const QString& transferId)
{
    if (!m_initialized) {
        return;
    }

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM transfers WHERE id = :id");
    query.bindValue(":id", transferId);

    if (query.exec()) {
        emit transferDeleted(transferId);
    }
}

int TransferLogStore::clearOlderThan(const QDateTime& olderThan)
{
    if (!m_initialized) {
        return 0;
    }

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM transfers WHERE timestamp < :timestamp");
    query.bindValue(":timestamp", olderThan.toSecsSinceEpoch());

    if (query.exec()) {
        return query.numRowsAffected();
    }

    return 0;
}

int TransferLogStore::clearCompleted()
{
    if (!m_initialized) {
        return 0;
    }

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM transfers WHERE status = :status");
    query.bindValue(":status", static_cast<int>(CrossAccountTransfer::Completed));

    if (query.exec()) {
        return query.numRowsAffected();
    }

    return 0;
}

int TransferLogStore::clearAll()
{
    if (!m_initialized) {
        return 0;
    }

    QSqlQuery query(m_db);
    if (query.exec("DELETE FROM transfers")) {
        return query.numRowsAffected();
    }

    return 0;
}

QString TransferLogStore::databasePath() const
{
    return m_dbPath;
}

CrossAccountTransfer TransferLogStore::transferFromQuery(const QSqlQuery& query) const
{
    CrossAccountTransfer transfer;

    transfer.id = query.value("id").toString();
    transfer.timestamp = QDateTime::fromSecsSinceEpoch(query.value("timestamp").toLongLong());
    transfer.sourceAccountId = query.value("source_account_id").toString();
    transfer.sourcePath = query.value("source_path").toString();
    transfer.targetAccountId = query.value("target_account_id").toString();
    transfer.targetPath = query.value("target_path").toString();
    transfer.operation = static_cast<CrossAccountTransfer::Operation>(query.value("operation").toInt());
    transfer.status = static_cast<CrossAccountTransfer::Status>(query.value("status").toInt());
    transfer.bytesTransferred = query.value("bytes_transferred").toLongLong();
    transfer.bytesTotal = query.value("bytes_total").toLongLong();
    transfer.filesTransferred = query.value("files_transferred").toInt();
    transfer.filesTotal = query.value("files_total").toInt();
    transfer.errorMessage = query.value("error_message").toString();
    transfer.errorCode = query.value("error_code").toInt();
    transfer.retryCount = query.value("retry_count").toInt();
    transfer.canRetry = query.value("can_retry").toInt() != 0;

    return transfer;
}

} // namespace MegaCustom
