#include "features/Watermarker.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string quoteShellArgument(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char c : value) {
        quoted += c == '"' ? "\\\"" : std::string(1, c);
    }
    return quoted + "\"";
#else
    std::string quoted = "'";
    for (char c : value) {
        quoted += c == '\'' ? "'\\''" : std::string(1, c);
    }
    return quoted + "'";
#endif
}

std::string pathUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.u8string();
#else
    return path.string();
#endif
}

bool filesEqual(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code ec;
    const auto firstSize = std::filesystem::file_size(first, ec);
    if (ec) return false;
    const auto secondSize = std::filesystem::file_size(second, ec);
    if (ec || firstSize != secondSize) return false;

    std::ifstream a(first, std::ios::binary);
    std::ifstream b(second, std::ios::binary);
    constexpr size_t bufferSize = 64 * 1024;
    char aBuffer[bufferSize];
    char bBuffer[bufferSize];
    while (a && b) {
        a.read(aBuffer, bufferSize);
        b.read(bBuffer, bufferSize);
        if (a.gcount() != b.gcount()
            || !std::equal(aBuffer, aBuffer + a.gcount(), bBuffer)) {
            return false;
        }
    }
    return true;
}

int fail(const std::string& message, const std::filesystem::path& root) {
    std::cerr << "FAIL: " << message << "\n";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    return 1;
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    if (!MegaCustom::Watermarker::isFFmpegAvailable()) {
        std::cout << "SKIP: FFmpeg is not available\n";
        return 0;
    }

    const auto token = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("megacustom-watermarker-test-" + std::to_string(token));
    const fs::path generatedDirectory = root / "generated";
    const fs::path generatedInput = generatedDirectory / "input.mp4";
    const fs::path sourceDirectory = root / fs::u8path(u8"source-unicode-é");
    const fs::path outputDirectory = root / fs::u8path(u8"output-unicode-測試");
    const fs::path cache = root / fs::u8path(u8"cache-unicode-ü");
    const fs::path input = sourceDirectory / "input.mp4";
    const fs::path firstOutput = outputDirectory / "member-one.mp4";
    const fs::path secondOutput = outputDirectory / "member-two.mp4";
    const fs::path concurrentOneOutput = outputDirectory / "concurrent-one.mp4";
    const fs::path concurrentTwoOutput = outputDirectory / "concurrent-two.mp4";
    const fs::path overlapFallbackOutput = outputDirectory / "member" / "month" / "overlap.mp4";
    const fs::path fallbackOutput = outputDirectory / "fallback.mp4";
    const fs::path cancelledOutput = outputDirectory / "cancelled.mp4";
    const fs::path passthroughInput = sourceDirectory / "captions.vtt";
    const fs::path passthroughOutput = outputDirectory / "captions.vtt";
    const fs::path unrelatedCacheFile = cache / "keep-me.txt";
    fs::create_directories(generatedDirectory);
    fs::create_directories(sourceDirectory);
    fs::create_directories(outputDirectory);

    const std::string ffmpeg = MegaCustom::Watermarker::getFFmpegPath();
    const std::string generateCommand = quoteShellArgument(ffmpeg) +
        " -y -v error -f lavfi -i testsrc2=size=640x360:rate=30" +
        " -f lavfi -i sine=frequency=880:sample_rate=48000" +
        " -t 30 -c:v libx264 -pix_fmt yuv420p -g 60 -c:a aac " +
        quoteShellArgument(pathUtf8(generatedInput));
    if (std::system(generateCommand.c_str()) != 0 || !fs::exists(generatedInput)) {
        return fail("could not generate the source fixture", root);
    }
    fs::copy_file(generatedInput, input, fs::copy_options::overwrite_existing);

    MegaCustom::WatermarkConfig config;
    config.primaryText = "CACHE 100% REUSE TEST";
    config.secondaryText = "member@example.com";
    config.intervalSeconds = 10;
    config.durationSeconds = 1;
    config.preset = "ultrafast";
    config.crf = 28;
    config.fastSegmentedEncode = true;
    config.segmentCacheDirectory = pathUtf8(cache);
    config.segmentCacheMaxBytes = 2LL * 1024LL * 1024LL * 1024LL;
    config.segmentCacheMaxAgeDays = 7;

    MegaCustom::Watermarker firstWatermarker;
    firstWatermarker.setConfig(config);
    const auto first = firstWatermarker.watermarkVideo(pathUtf8(input), pathUtf8(firstOutput));
    if (!first.success || first.processingMode != "fast_segment_cache_build"
        || first.segmentCacheHit) {
        return fail("first watermark did not build the shared cache: " + first.error, root);
    }

    MegaCustom::Watermarker secondWatermarker;
    secondWatermarker.setConfig(config);
    const auto second = secondWatermarker.watermarkVideo(pathUtf8(input), pathUtf8(secondOutput));
    if (!second.success || second.processingMode != "fast_segment_cache_hit"
        || !second.segmentCacheHit
        || second.diagnostic.find("validated source plan") == std::string::npos) {
        return fail("second watermark did not reuse the shared cache: " + second.error, root);
    }
    if (!filesEqual(firstOutput, secondOutput)) {
        return fail("identical member settings produced different segmented outputs", root);
    }

    const auto stats = MegaCustom::Watermarker::getSegmentCacheStats(pathUtf8(cache));
    if (!stats.error.empty() || stats.entryCount != 1 || stats.incompleteEntryCount != 0
        || stats.sizeBytes <= 0) {
        return fail("cache statistics are inconsistent after reuse", root);
    }

    std::string cacheKey;
    for (const auto& entry : fs::recursive_directory_iterator(cache)) {
        if (entry.is_directory() && entry.path().parent_path() == cache
            && entry.path().filename().string().size() == 16) {
            cacheKey = entry.path().filename().string();
        }
        if (entry.is_directory()
            && entry.path().filename().string().rfind("work_", 0) == 0) {
            return fail("member-specific work directory remained in the clean cache", root);
        }
    }
    if (cacheKey.empty()) {
        return fail("could not identify the completed cache entry", root);
    }

    {
        std::ofstream unrelated(unrelatedCacheFile);
        unrelated << "This file does not belong to MegaCustom.\n";
    }
    const std::string unrelatedHexName = cacheKey == "0123456789abcdef"
        ? "fedcba9876543210"
        : "0123456789abcdef";
    const fs::path unrelatedHexDirectory = cache / unrelatedHexName;
    fs::create_directory(unrelatedHexDirectory);
    {
        std::ofstream unrelated(unrelatedHexDirectory / "user-data.txt");
        unrelated << "A hexadecimal folder name does not make this a cache entry.\n";
    }
    const fs::path activeLease = cache / (cacheKey + ".use.integration-test");
    fs::create_directory(activeLease);
    std::string clearError;
    if (MegaCustom::Watermarker::clearSegmentCache(pathUtf8(cache), clearError)
        || clearError.find("currently in use") == std::string::npos) {
        return fail("cache clear did not reject an active cache user", root);
    }
    fs::remove_all(activeLease);

    clearError.clear();
    if (!MegaCustom::Watermarker::clearSegmentCache(pathUtf8(cache), clearError)) {
        return fail("cache clear failed: " + clearError, root);
    }
    const auto cleared = MegaCustom::Watermarker::getSegmentCacheStats(pathUtf8(cache));
    if (cleared.entryCount != 0 || cleared.sizeBytes != 0
        || !fs::exists(unrelatedCacheFile)
        || !fs::exists(unrelatedHexDirectory / "user-data.txt")) {
        return fail("cache was not empty after clear", root);
    }

    auto runConcurrent = [&](const fs::path& output) {
        MegaCustom::Watermarker watermarker;
        watermarker.setConfig(config);
        return watermarker.watermarkVideo(pathUtf8(input), pathUtf8(output));
    };
    auto concurrentOneFuture = std::async(std::launch::async, runConcurrent, concurrentOneOutput);
    auto concurrentTwoFuture = std::async(std::launch::async, runConcurrent, concurrentTwoOutput);
    const auto concurrentOne = concurrentOneFuture.get();
    const auto concurrentTwo = concurrentTwoFuture.get();
    if (!concurrentOne.success || !concurrentTwo.success) {
        return fail("concurrent cache users did not both complete", root);
    }
    const int concurrentBuilds =
        (concurrentOne.processingMode == "fast_segment_cache_build" ? 1 : 0)
        + (concurrentTwo.processingMode == "fast_segment_cache_build" ? 1 : 0);
    const int concurrentHits =
        (concurrentOne.processingMode == "fast_segment_cache_hit" ? 1 : 0)
        + (concurrentTwo.processingMode == "fast_segment_cache_hit" ? 1 : 0);
    if (concurrentBuilds != 1 || concurrentHits != 1
        || !filesEqual(concurrentOneOutput, concurrentTwoOutput)) {
        return fail("cache locking did not produce one builder and one deterministic reuser", root);
    }

    clearError.clear();
    if (!MegaCustom::Watermarker::clearSegmentCache(pathUtf8(cache), clearError)) {
        return fail("final cache clear failed: " + clearError, root);
    }

    {
        std::ofstream passthrough(passthroughInput);
        passthrough << "WEBVTT\n\n00:00.000 --> 00:01.000\nTest\n";
    }
    MegaCustom::Watermarker passthroughWatermarker;
    passthroughWatermarker.setConfig(config);
    const auto passthrough = passthroughWatermarker.watermarkFile(
        pathUtf8(passthroughInput), pathUtf8(passthroughOutput));
    if (!passthrough.success || passthrough.processingMode != "passthrough_copy"
        || passthrough.inputSizeBytes <= 0
        || passthrough.outputSizeBytes != passthrough.inputSizeBytes
        || !filesEqual(passthroughInput, passthroughOutput)) {
        return fail("passthrough copy did not preserve the file and report sizes", root);
    }
    MegaCustom::WatermarkConfig noOverwriteConfig = config;
    noOverwriteConfig.overwrite = false;
    passthroughWatermarker.setConfig(noOverwriteConfig);
    const auto noOverwrite = passthroughWatermarker.watermarkFile(
        pathUtf8(passthroughInput), pathUtf8(passthroughOutput));
    if (noOverwrite.success || !filesEqual(passthroughInput, passthroughOutput)) {
        return fail("passthrough overwrite protection did not preserve the existing output", root);
    }

    MegaCustom::WatermarkConfig overlapConfig = config;
    overlapConfig.segmentCacheDirectory = pathUtf8(outputDirectory / "cache");
    overlapConfig.segmentCacheOutputRoot = pathUtf8(outputDirectory);
    MegaCustom::Watermarker overlapWatermarker;
    overlapWatermarker.setConfig(overlapConfig);
    const auto overlapResult = overlapWatermarker.watermarkVideo(
        pathUtf8(input), pathUtf8(overlapFallbackOutput));
    if (!overlapResult.success || overlapResult.processingMode != "full_encode_fallback"
        || fs::exists(outputDirectory / "cache")) {
        return fail("cache/output-tree overlap was not blocked before full fallback", root);
    }

    MegaCustom::WatermarkConfig fallbackConfig = config;
    fallbackConfig.fastSegmentedEncode = true;
    fallbackConfig.segmentCacheDirectory = pathUtf8(input);
    fallbackConfig.preset = "ultrafast";
    MegaCustom::Watermarker fallbackWatermarker;
    fallbackWatermarker.setConfig(fallbackConfig);
    const auto fallbackResult = fallbackWatermarker.watermarkVideo(
        pathUtf8(input), pathUtf8(fallbackOutput));
    if (!fallbackResult.success || fallbackResult.processingMode != "full_encode_fallback"
        || !fs::exists(fallbackOutput)) {
        return fail("cache filesystem exception did not fall back to a full encode", root);
    }

    MegaCustom::WatermarkConfig cancelConfig = config;
    cancelConfig.fastSegmentedEncode = true;
    cancelConfig.segmentCacheDirectory = pathUtf8(input);
    cancelConfig.preset = "veryslow";
    cancelConfig.crf = 18;
    MegaCustom::Watermarker cancellable;
    cancellable.setConfig(cancelConfig);
    auto cancelFuture = std::async(std::launch::async, [&]() {
        return cancellable.watermarkVideo(pathUtf8(input), pathUtf8(cancelledOutput));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cancellable.cancel();
    const auto cancelled = cancelFuture.get();
    if (cancelled.success || cancelled.processingMode != "cancelled"
        || fs::exists(cancelledOutput)) {
        return fail("active FFmpeg cancellation did not stop cleanly", root);
    }

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    std::cout << "PASS: cache reuse/locking/clear safety, deterministic output, fallback cancellation, and passthrough\n";
    return 0;
}
