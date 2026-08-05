#include "WatermarkDiagnostics.h"

#include <QMap>
#include <QSet>

#include <algorithm>

namespace MegaCustom {
namespace {

const QStringList kFullEncodeSuffixes = {
    " Standard full encode completed successfully.",
    " Standard full encode also failed.",
    " Standard full encode was then cancelled by the user."
};

struct FallbackAccumulator {
    int rows = 0;
    QSet<QString> sources;
};

QString sourceIdentity(const WatermarkDiagnosticRow& row, int rowIndex) {
    const QString source = row.sourcePath.trimmed();
    return source.isEmpty() ? QString("row:%1").arg(rowIndex) : source;
}

} // namespace

QString normalizeFastSegmentFallbackReason(const QString& diagnostic) {
    QString reason = diagnostic.trimmed();
    for (const QString& suffix : kFullEncodeSuffixes) {
        if (reason.endsWith(suffix)) {
            reason.chop(suffix.size());
            reason = reason.trimmed();
            break;
        }
    }
    return reason.isEmpty()
        ? QString("Fast Segments fell back without recording a reason.")
        : reason;
}

FastSegmentDiagnostics summarizeFastSegmentDiagnostics(
    const QList<WatermarkDiagnosticRow>& rows,
    bool requested) {
    FastSegmentDiagnostics result;
    result.requested = requested;

    QMap<QString, FallbackAccumulator> fallbacks;
    for (int index = 0; index < rows.size(); ++index) {
        const WatermarkDiagnosticRow& row = rows[index];
        if (row.processingMode == "fast_segment_cache_hit") {
            ++result.cacheHitRows;
        } else if (row.processingMode == "fast_segment_cache_build") {
            ++result.cacheBuildRows;
        } else if (row.processingMode == "full_encode_fallback") {
            ++result.fallbackRows;
            ++result.fullEncodeRows;
            const QString reason = normalizeFastSegmentFallbackReason(row.diagnostic);
            FallbackAccumulator& accumulator = fallbacks[reason];
            ++accumulator.rows;
            accumulator.sources.insert(sourceIdentity(row, index));
        } else if (row.processingMode == "full_encode") {
            ++result.fullEncodeRows;
        }
    }

    for (auto it = fallbacks.cbegin(); it != fallbacks.cend(); ++it) {
        FastSegmentFallbackReason reason;
        reason.reason = it.key();
        reason.rows = it.value().rows;
        reason.sourceFiles = it.value().sources.size();
        result.fallbackReasons.append(reason);
    }
    std::sort(result.fallbackReasons.begin(), result.fallbackReasons.end(),
        [](const FastSegmentFallbackReason& left, const FastSegmentFallbackReason& right) {
            if (left.rows != right.rows) {
                return left.rows > right.rows;
            }
            if (left.sourceFiles != right.sourceFiles) {
                return left.sourceFiles > right.sourceFiles;
            }
            return left.reason < right.reason;
        });

    if (!requested) {
        result.outcome = "not_requested";
        result.summary = "Fast Segments was disabled.";
        return result;
    }

    if (result.attemptedRows() == 0) {
        result.outcome = "no_video_attempts";
        result.summary = "Fast Segments was enabled, but no eligible video rows attempted it.";
        return result;
    }

    if (result.fallbackRows == 0) {
        result.outcome = "accelerated";
        result.summary = QString(
            "Fast Segments accelerated all %1 attempted row(s): %2 cache built, %3 cache reused.")
            .arg(result.acceleratedRows())
            .arg(result.cacheBuildRows)
            .arg(result.cacheHitRows);
        return result;
    }

    result.outcome = result.acceleratedRows() == 0 ? "all_fallback" : "partial_fallback";
    result.summary = QString(
        "Fast Segments accelerated %1 of %2 attempted row(s): %3 cache built, %4 cache reused, %5 full fallback.")
        .arg(result.acceleratedRows())
        .arg(result.attemptedRows())
        .arg(result.cacheBuildRows)
        .arg(result.cacheHitRows)
        .arg(result.fallbackRows);
    if (!result.fallbackReasons.isEmpty()) {
        const FastSegmentFallbackReason& top = result.fallbackReasons.constFirst();
        const QString summaryReason = top.reason.size() > 240
            ? top.reason.left(237) + "..."
            : top.reason;
        result.summary += QString(" Top fallback: %1 (%2 row(s), %3 source file(s)).")
            .arg(summaryReason)
            .arg(top.rows)
            .arg(top.sourceFiles);
    }
    return result;
}

QJsonArray fastSegmentFallbackReasonsToJson(const FastSegmentDiagnostics& diagnostics) {
    QJsonArray reasons;
    for (const FastSegmentFallbackReason& reason : diagnostics.fallbackReasons) {
        QJsonObject object;
        object["reason"] = reason.reason;
        object["rows"] = reason.rows;
        object["sourceFiles"] = reason.sourceFiles;
        reasons.append(object);
    }
    return reasons;
}

QJsonObject fastSegmentDiagnosticsToJson(const FastSegmentDiagnostics& diagnostics) {
    QJsonObject object;
    object["requested"] = diagnostics.requested;
    object["outcome"] = diagnostics.outcome;
    object["summary"] = diagnostics.summary;
    object["attemptedRows"] = diagnostics.attemptedRows();
    object["acceleratedRows"] = diagnostics.acceleratedRows();
    object["cacheBuildRows"] = diagnostics.cacheBuildRows;
    object["cacheHitRows"] = diagnostics.cacheHitRows;
    object["fallbackRows"] = diagnostics.fallbackRows;
    object["fullEncodeRows"] = diagnostics.fullEncodeRows;
    object["fallbackReasons"] = fastSegmentFallbackReasonsToJson(diagnostics);
    return object;
}

void applyFastSegmentDiagnosticsToMetadata(
    QJsonObject& metadata,
    const FastSegmentDiagnostics& diagnostics) {
    metadata["watermarkFastRequested"] = diagnostics.requested;
    metadata["watermarkFastOutcome"] = diagnostics.outcome;
    metadata["watermarkFastSummary"] = diagnostics.summary;
    metadata["watermarkFastAttemptRows"] = diagnostics.attemptedRows();
    metadata["watermarkFastAcceleratedRows"] = diagnostics.acceleratedRows();
    metadata["watermarkFastCacheBuildRows"] = diagnostics.cacheBuildRows;
    metadata["watermarkFastCacheHitRows"] = diagnostics.cacheHitRows;
    metadata["watermarkFastFallbackRows"] = diagnostics.fallbackRows;
    metadata["watermarkFullEncodeRows"] = diagnostics.fullEncodeRows;
    metadata["watermarkFastFallbackReasons"] =
        fastSegmentFallbackReasonsToJson(diagnostics);
}

} // namespace MegaCustom
