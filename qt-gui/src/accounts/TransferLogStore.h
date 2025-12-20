#ifndef MEGACUSTOM_TRANSFERLOGSTORE_H
#define MEGACUSTOM_TRANSFERLOGSTORE_H

#include <QObject>
#include <QSqlDatabase>
#include <QDateTime>
#include <QList>
#include "AccountModels.h"

namespace MegaCustom {

/**
 * @brief SQLite-based persistent storage for cross-account transfer history
 *
 * Stores completed, failed, and in-progress cross-account transfers.
 * Provides query capabilities for viewing history, filtering, and retry.
 */
class TransferLogStore : public QObject
{
    Q_OBJECT

public:
    explicit TransferLogStore(QObject* parent = nullptr);
    ~TransferLogStore();

    /**
     * @brief Initialize the database
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Log a new transfer
     * @param transfer The transfer to log
     */
    void logTransfer(const CrossAccountTransfer& transfer);

    /**
     * @brief Update an existing transfer
     * @param transfer Updated transfer data
     */
    void updateTransfer(const CrossAccountTransfer& transfer);

    /**
     * @brief Get a transfer by ID
     * @param transferId The transfer ID
     * @return The transfer, or invalid transfer if not found
     */
    CrossAccountTransfer getTransfer(const QString& transferId) const;

    /**
     * @brief Get all transfers with pagination
     * @param limit Maximum number of results
     * @param offset Starting offset
     * @return List of transfers, newest first
     */
    QList<CrossAccountTransfer> getAll(int limit = 100, int offset = 0) const;

    /**
     * @brief Get transfers by status
     * @param status The status to filter by
     * @param limit Maximum number of results
     * @return Matching transfers
     */
    QList<CrossAccountTransfer> getByStatus(CrossAccountTransfer::Status status, int limit = 100) const;

    /**
     * @brief Get transfers involving a specific account
     * @param accountId The account ID (source or target)
     * @param limit Maximum number of results
     * @return Matching transfers
     */
    QList<CrossAccountTransfer> getByAccount(const QString& accountId, int limit = 100) const;

    /**
     * @brief Get transfers within a date range
     * @param from Start date
     * @param to End date
     * @param limit Maximum number of results
     * @return Matching transfers
     */
    QList<CrossAccountTransfer> getByDateRange(const QDateTime& from, const QDateTime& to, int limit = 100) const;

    /**
     * @brief Search transfers by path
     * @param query Search string (matches source or target path)
     * @param limit Maximum number of results
     * @return Matching transfers
     */
    QList<CrossAccountTransfer> search(const QString& query, int limit = 100) const;

    /**
     * @brief Get count of transfers by status
     * @return Map of status to count
     */
    QMap<CrossAccountTransfer::Status, int> getStatusCounts() const;

    /**
     * @brief Delete a transfer record
     * @param transferId The transfer ID
     */
    void deleteTransfer(const QString& transferId);

    /**
     * @brief Clear transfers older than a date
     * @param olderThan Delete transfers before this date
     * @return Number of deleted records
     */
    int clearOlderThan(const QDateTime& olderThan);

    /**
     * @brief Clear all completed transfers
     * @return Number of deleted records
     */
    int clearCompleted();

    /**
     * @brief Clear all transfer records
     * @return Number of deleted records
     */
    int clearAll();

    /**
     * @brief Get database file path
     */
    QString databasePath() const;

signals:
    void transferLogged(const CrossAccountTransfer& transfer);
    void transferUpdated(const CrossAccountTransfer& transfer);
    void transferDeleted(const QString& transferId);

private:
    bool createTables();
    CrossAccountTransfer transferFromQuery(const QSqlQuery& query) const;

    QSqlDatabase m_db;
    QString m_dbPath;
    bool m_initialized;
};

} // namespace MegaCustom

#endif // MEGACUSTOM_TRANSFERLOGSTORE_H
