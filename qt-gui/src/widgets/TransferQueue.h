#ifndef TRANSFER_QUEUE_H
#define TRANSFER_QUEUE_H

#include <QWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMap>
#include <QPropertyAnimation>

namespace MegaCustom {

class TransferController;

class TransferQueue : public QWidget {
    Q_OBJECT

public:
    explicit TransferQueue(QWidget* parent = nullptr);
    void setTransferController(TransferController* controller);

public slots:
    void onTransferAdded(const QString& type, const QString& sourcePath,
                         const QString& destPath, qint64 size);
    void onTransferProgress(const QString& transferId, qint64 transferred,
                           qint64 total, qint64 speed, int timeRemaining);
    void onTransferComplete(const QString& transferId);
    void onTransferFailed(const QString& path, const QString& error);
    void onQueueStatusChanged(int active, int pending, int completed, int failed);

private slots:
    void onCancelAllClicked();
    void onClearCompletedClicked();

private:
    void setupUi();
    void updateStatusLabel();
    QString formatSize(qint64 bytes);
    QString formatSpeed(qint64 bytesPerSecond);
    QString formatTime(int seconds);
    int findRowByTransferId(const QString& transferId);
    int findRowByPath(const QString& path);
    void animateProgressBar(QProgressBar* progressBar, int targetValue);

    TransferController* m_controller = nullptr;
    QTableWidget* m_transferTable = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_activeBadge = nullptr;
    QLabel* m_pendingBadge = nullptr;
    QLabel* m_completedBadge = nullptr;
    QPushButton* m_cancelAllButton = nullptr;
    QPushButton* m_clearCompletedButton = nullptr;

    QLabel* createBadge(const QString& text, const QString& color);
    void updateBadge(QLabel* badge, int count, const QString& label);

    // Track transfers by ID -> row
    QMap<QString, int> m_transferRows;

    // Track active progress animations
    QMap<QProgressBar*, QPropertyAnimation*> m_progressAnimations;

    // Counters
    int m_activeCount = 0;
    int m_pendingCount = 0;
    int m_completedCount = 0;
    int m_failedCount = 0;
};

} // namespace MegaCustom

#endif // TRANSFER_QUEUE_H
