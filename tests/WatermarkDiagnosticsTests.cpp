#include "utils/WatermarkDiagnostics.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>

namespace {

int fail(const QString& message) {
    std::cerr << "FAIL: " << message.toStdString() << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    using MegaCustom::FastSegmentDiagnostics;
    using MegaCustom::WatermarkDiagnosticRow;

    const QString fallback =
        "Fast segmented encode skipped: ffprobe could not read stream layout.";
    QList<WatermarkDiagnosticRow> rows = {
        {"source-a.mp4", "full_encode_fallback",
         fallback + " Standard full encode completed successfully."},
        {"source-a.mp4", "full_encode_fallback",
         fallback + " Standard full encode completed successfully."},
        {"source-b.mp4", "full_encode_fallback",
         fallback + " Standard full encode also failed."},
        {"source-c.mp4", "fast_segment_cache_build", "built"},
        {"source-c.mp4", "fast_segment_cache_hit", "reused"},
        {"source-d.mp4", "full_encode", "standard"}
    };

    const FastSegmentDiagnostics summary =
        MegaCustom::summarizeFastSegmentDiagnostics(rows, true);
    if (summary.outcome != "partial_fallback"
        || summary.attemptedRows() != 5
        || summary.acceleratedRows() != 2
        || summary.cacheBuildRows != 1
        || summary.cacheHitRows != 1
        || summary.fallbackRows != 3
        || summary.fullEncodeRows != 4) {
        return fail("Fast Segment outcome counters are incorrect");
    }
    if (summary.fallbackReasons.size() != 1
        || summary.fallbackReasons.constFirst().reason != fallback
        || summary.fallbackReasons.constFirst().rows != 3
        || summary.fallbackReasons.constFirst().sourceFiles != 2) {
        return fail("fallback reasons were not normalized and grouped by source");
    }

    QJsonObject metadata;
    MegaCustom::applyFastSegmentDiagnosticsToMetadata(metadata, summary);
    if (metadata.value("watermarkFastOutcome").toString() != "partial_fallback"
        || metadata.value("watermarkFastAttemptRows").toInt() != 5
        || metadata.value("watermarkFastAcceleratedRows").toInt() != 2
        || metadata.value("watermarkFastFallbackReasons").toArray().size() != 1) {
        return fail("Fast Segment metadata is incomplete");
    }

    const FastSegmentDiagnostics allFallback =
        MegaCustom::summarizeFastSegmentDiagnostics(rows.mid(0, 3), true);
    if (allFallback.outcome != "all_fallback"
        || !allFallback.summary.contains("accelerated 0 of 3")) {
        return fail("all-fallback jobs are not identified explicitly");
    }

    const FastSegmentDiagnostics disabled =
        MegaCustom::summarizeFastSegmentDiagnostics({}, false);
    if (disabled.outcome != "not_requested") {
        return fail("disabled Fast Segments state is incorrect");
    }

    const FastSegmentDiagnostics noAttempts =
        MegaCustom::summarizeFastSegmentDiagnostics({}, true);
    if (noAttempts.outcome != "no_video_attempts") {
        return fail("enabled jobs without video attempts are not distinguished");
    }

    std::cout << "Watermark diagnostics tests passed.\n";
    return 0;
}
