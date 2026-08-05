#ifndef MEGACUSTOM_WATERMARKDIAGNOSTICS_H
#define MEGACUSTOM_WATERMARKDIAGNOSTICS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace MegaCustom {

struct WatermarkDiagnosticRow {
    QString sourcePath;
    QString processingMode;
    QString diagnostic;
};

struct FastSegmentFallbackReason {
    QString reason;
    int rows = 0;
    int sourceFiles = 0;
};

struct FastSegmentDiagnostics {
    bool requested = false;
    int cacheHitRows = 0;
    int cacheBuildRows = 0;
    int fallbackRows = 0;
    int fullEncodeRows = 0;
    QString outcome;
    QString summary;
    QList<FastSegmentFallbackReason> fallbackReasons;

    int attemptedRows() const {
        return cacheHitRows + cacheBuildRows + fallbackRows;
    }

    int acceleratedRows() const {
        return cacheHitRows + cacheBuildRows;
    }
};

QString normalizeFastSegmentFallbackReason(const QString& diagnostic);
FastSegmentDiagnostics summarizeFastSegmentDiagnostics(
    const QList<WatermarkDiagnosticRow>& rows,
    bool requested);
QJsonArray fastSegmentFallbackReasonsToJson(const FastSegmentDiagnostics& diagnostics);
QJsonObject fastSegmentDiagnosticsToJson(const FastSegmentDiagnostics& diagnostics);
void applyFastSegmentDiagnosticsToMetadata(
    QJsonObject& metadata,
    const FastSegmentDiagnostics& diagnostics);

} // namespace MegaCustom

#endif // MEGACUSTOM_WATERMARKDIAGNOSTICS_H
