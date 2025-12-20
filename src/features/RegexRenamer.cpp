/**
 * RegexRenamer.cpp - Advanced regex-based bulk file renaming for Mega
 *
 * Features:
 * - PCRE2 regex support with fallback to std::regex
 * - Preview mode before applying changes
 * - Undo/redo functionality with history
 * - Sequential numbering and date/time patterns
 * - Case conversions and metadata extraction
 * - Conflict resolution and safe mode
 */

#include "features/RegexRenamer.h"
#include <megaapi.h>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <json_simple.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>

// Check for PCRE2 availability
#ifdef __has_include
    #if __has_include(<pcre2.h>)
        #define HAS_PCRE2
        #define PCRE2_CODE_UNIT_WIDTH 8
        #include <pcre2.h>
    #endif
#endif

namespace fs = std::filesystem;

namespace MegaCustom {

// Rename listener for async operations
class RenameListener : public mega::MegaRequestListener {
private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_finished = false;
    int m_errorCode = mega::MegaError::API_OK;
    std::string m_errorString;

public:
    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = false;
        m_errorCode = mega::MegaError::API_OK;
        m_errorString.clear();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_finished; });
    }

    int getError() const { return m_errorCode; }
    std::string getErrorString() const { return m_errorString; }

    void onRequestFinish(mega::MegaApi* api, mega::MegaRequest* request, mega::MegaError* error) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = true;
        if (error) {
            m_errorCode = error->getErrorCode();
            m_errorString = error->getErrorString() ? error->getErrorString() : "";
        }
        m_cv.notify_all();
    }
};

// Constructor
RegexRenamer::RegexRenamer(mega::MegaApi* megaApi)
    : m_megaApi(megaApi), m_safeMode(true) {
    initializeTemplates();
}

// Destructor
RegexRenamer::~RegexRenamer() = default;

// Initialize predefined templates
void RegexRenamer::initializeTemplates() {
    // Photo organization template
    RenamePattern photoOrganize;
    photoOrganize.searchPattern = R"(IMG_(\d{4})(\d{2})(\d{2})_(.+))";
    photoOrganize.replacePattern = "$1-$2-$3_Photo_$4";
    photoOrganize.preserveExtension = true;
    m_templates["photo_organize"] = photoOrganize;

    // Document versioning template
    RenamePattern docVersion;
    docVersion.searchPattern = R"((.+?)(?:_v\d+)?(\.[^.]+)$)";
    docVersion.replacePattern = "$1_v{num:02d}$2";
    docVersion.useSequentialNumbering = true;
    docVersion.numberingStart = 1;
    docVersion.numberingPadding = 2;
    m_templates["doc_version"] = docVersion;

    // Clean spaces template
    RenamePattern cleanSpaces;
    cleanSpaces.searchPattern = R"(\s+)";
    cleanSpaces.replacePattern = "_";
    cleanSpaces.preserveExtension = true;
    m_templates["clean_spaces"] = cleanSpaces;

    // Date prefix template
    RenamePattern datePrefix;
    datePrefix.insertDateTime = true;
    datePrefix.dateTimeFormat = "%Y%m%d_";
    datePrefix.useFileModTime = true;
    m_templates["date_prefix"] = datePrefix;

    // Remove special chars template
    RenamePattern removeSpecial;
    removeSpecial.searchPattern = R"([^\w\s\-_.])";
    removeSpecial.replacePattern = "";
    removeSpecial.preserveExtension = true;
    removeSpecial.sanitizeForFilesystem = true;
    m_templates["remove_special"] = removeSpecial;
}

// Preview rename operations
std::vector<RenamePreview> RegexRenamer::previewRename(
    const std::vector<mega::MegaNode*>& nodes,
    const RenamePattern& pattern) {

    std::vector<RenamePreview> previews;
    std::vector<std::string> proposedNames;
    int sequenceIndex = pattern.numberingStart;

    for (auto* node : nodes) {
        if (!node || node->getType() != mega::MegaNode::TYPE_FILE) {
            continue;
        }

        RenamePreview preview;
        preview.node = node;
        preview.originalName = node->getName() ? node->getName() : "";
        preview.fullPath = m_megaApi->getNodePath(node);

        // Apply pattern to get new name
        preview.proposedName = applyPattern(preview.originalName, pattern);

        // Apply sequential numbering if enabled
        if (pattern.useSequentialNumbering) {
            preview.proposedName = applyNumbering(preview.proposedName, sequenceIndex++, pattern);
        }

        // Apply date/time if enabled
        if (pattern.insertDateTime) {
            preview.proposedName = applyDateTime(preview.proposedName, node, pattern);
        }

        // Check for conflicts
        preview.hasConflict = false;
        if (std::find(proposedNames.begin(), proposedNames.end(), preview.proposedName) != proposedNames.end()) {
            preview.hasConflict = true;
            preview.conflictReason = "Duplicate name in batch";
        } else {
            mega::MegaNode* parent = m_megaApi->getParentNode(node);
            if (parent && checkNameConflict(preview.proposedName, parent)) {
                preview.hasConflict = true;
                preview.conflictReason = "File already exists in folder";
            }
            delete parent;
        }

        proposedNames.push_back(preview.proposedName);
        previews.push_back(preview);
    }

    return previews;
}

// Preview rename by path pattern
std::vector<RenamePreview> RegexRenamer::previewRenameByPath(
    const std::string& pathPattern,
    const RenamePattern& pattern,
    bool recursive) {

    std::vector<mega::MegaNode*> nodes;

    // Parse path pattern (e.g., "/photos/*.jpg")
    size_t lastSlash = pathPattern.find_last_of('/');
    std::string dirPath = (lastSlash != std::string::npos) ? pathPattern.substr(0, lastSlash) : "/";
    std::string filePattern = (lastSlash != std::string::npos) ? pathPattern.substr(lastSlash + 1) : pathPattern;

    // Get directory node
    mega::MegaNode* dirNode = m_megaApi->getNodeByPath(dirPath.c_str());
    if (!dirNode) {
        return std::vector<RenamePreview>();
    }

    // Convert file pattern to regex
    std::string regexPattern = filePattern;
    // Replace wildcards with regex equivalents
    size_t pos = 0;
    while ((pos = regexPattern.find("*", pos)) != std::string::npos) {
        regexPattern.replace(pos, 1, ".*");
        pos += 2;
    }
    pos = 0;
    while ((pos = regexPattern.find("?", pos)) != std::string::npos) {
        regexPattern.replace(pos, 1, ".");
        pos += 1;
    }

    std::regex fileRegex(regexPattern);

    // Collect matching nodes
    mega::MegaNodeList* children = m_megaApi->getChildren(dirNode);
    if (children) {
        for (int i = 0; i < children->size(); i++) {
            mega::MegaNode* child = children->get(i);
            if (child && child->getType() == mega::MegaNode::TYPE_FILE) {
                std::string name = child->getName() ? child->getName() : "";
                if (std::regex_match(name, fileRegex)) {
                    nodes.push_back(child->copy());
                }
            }
        }
        delete children;
    }

    // Process subdirectories if recursive
    if (recursive) {
        mega::MegaNodeList* folders = m_megaApi->getChildren(dirNode, mega::MegaApi::ORDER_NONE);
        if (folders) {
            for (int i = 0; i < folders->size(); i++) {
                mega::MegaNode* folder = folders->get(i);
                if (folder && folder->getType() == mega::MegaNode::TYPE_FOLDER) {
                    std::string subPath = dirPath + "/" + folder->getName();
                    auto subPreviews = previewRenameByPath(
                        subPath + "/" + filePattern, pattern, recursive);
                    // Note: subPreviews contains previews, not nodes
                    // We need to collect nodes differently for recursive case
                }
            }
            delete folders;
        }
    }

    delete dirNode;

    // Generate previews
    auto previews = previewRename(nodes, pattern);

    // Clean up copied nodes
    for (auto* node : nodes) {
        delete node;
    }

    return previews;
}

// Apply rename operations
std::vector<RenameResult> RegexRenamer::applyRename(
    const std::vector<RenamePreview>& previews,
    bool dryRun) {

    std::vector<RenameResult> results;
    RenameListener listener;

    // Save operation for undo
    RenameOperation operation;
    operation.operationId = std::to_string(std::time(nullptr));
    operation.timestamp = std::chrono::system_clock::now();

    int current = 0;
    int total = previews.size();

    for (const auto& preview : previews) {
        current++;

        // Progress callback
        if (m_progressCallback) {
            m_progressCallback(current, total, preview.originalName);
        }

        RenameResult result;
        result.originalName = preview.originalName;
        result.newName = preview.proposedName;
        result.fullPath = preview.fullPath;
        result.wasSkipped = false;

        // Skip if conflict and no resolver
        if (preview.hasConflict) {
            if (m_conflictResolver) {
                result.newName = m_conflictResolver(preview.originalName, preview.proposedName);
            } else {
                result.wasSkipped = true;
                result.success = false;
                result.errorMessage = preview.conflictReason;
                results.push_back(result);
                continue;
            }
        }

        if (dryRun) {
            result.success = true;
            result.errorMessage = "Dry run - no actual rename";
        } else {
            // Save backup if safe mode
            if (m_safeMode) {
                saveBackup(preview.node);
            }

            // Perform actual rename
            listener.reset();
            m_megaApi->renameNode(preview.node, result.newName.c_str(), &listener);
            listener.wait();

            if (listener.getError() == mega::MegaError::API_OK) {
                result.success = true;
            } else {
                result.success = false;
                result.errorMessage = listener.getErrorString();
            }
        }

        results.push_back(result);
        operation.results.push_back(result);
    }

    // Add to undo stack if not dry run and had successful operations
    if (!dryRun && !operation.results.empty()) {
        m_undoStack.push_back(operation);
        if (m_undoStack.size() > m_maxHistorySize) {
            m_undoStack.erase(m_undoStack.begin());
        }
        m_redoStack.clear(); // Clear redo stack on new operation
    }

    return results;
}

// Bulk rename with pattern
std::vector<RenameResult> RegexRenamer::bulkRename(
    const std::vector<mega::MegaNode*>& nodes,
    const RenamePattern& pattern,
    bool autoResolveConflicts) {

    // Set conflict resolver if auto-resolve enabled
    std::function<std::string(const std::string&, const std::string&)> oldResolver = m_conflictResolver;

    if (autoResolveConflicts) {
        m_conflictResolver = [this](const std::string& original, const std::string& proposed) {
            std::vector<std::string> existing;
            // Generate unique name by appending number
            return generateUniqueName(proposed, existing);
        };
    }

    auto previews = previewRename(nodes, pattern);
    auto results = applyRename(previews, false);

    // Restore old resolver
    m_conflictResolver = oldResolver;

    return results;
}

// Apply pattern to filename
std::string RegexRenamer::applyPattern(const std::string& input, const RenamePattern& pattern) {
    std::string result = input;
    std::string nameWithoutExt = input;
    std::string extension = "";

    // Handle extension preservation
    if (pattern.preserveExtension && !pattern.applyToExtension) {
        extension = extractExtension(input);
        nameWithoutExt = removeExtension(input);
        result = nameWithoutExt;
    }

    // Apply regex replacement
    if (!pattern.searchPattern.empty()) {
        result = applyRegex(result, pattern);
    }

    // Apply case conversion
    if (pattern.caseConversion != RenamePattern::NONE) {
        result = applyCaseConversion(result, pattern.caseConversion);
    }

    // Apply character replacements
    for (const auto& [search, replace] : pattern.characterReplacements) {
        size_t pos = 0;
        while ((pos = result.find(search, pos)) != std::string::npos) {
            result.replace(pos, search.length(), replace);
            pos += replace.length();
        }
    }

    // Sanitize for filesystem if needed
    if (pattern.sanitizeForFilesystem) {
        result = sanitizeFilename(result);
    }

    // Re-add extension if preserved
    if (pattern.preserveExtension && !pattern.applyToExtension && !extension.empty()) {
        result += extension;
    }

    return result;
}

// Apply regex replacement
std::string RegexRenamer::applyRegex(const std::string& input, const RenamePattern& pattern) {
#ifdef HAS_PCRE2
    if (pattern.useExtendedRegex) {
        // Use PCRE2 for extended regex features
        int errorcode;
        PCRE2_SIZE erroroffset;

        uint32_t options = PCRE2_UTF;
        if (!pattern.caseSensitive) {
            options |= PCRE2_CASELESS;
        }

        pcre2_code* re = pcre2_compile(
            (PCRE2_SPTR)pattern.searchPattern.c_str(),
            PCRE2_ZERO_TERMINATED,
            options,
            &errorcode,
            &erroroffset,
            nullptr
        );

        if (!re) {
            // Fall back to std::regex on PCRE2 compile error
            goto use_std_regex;
        }

        pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, nullptr);

        std::string result;
        PCRE2_SIZE start_offset = 0;
        int replacements = 0;

        while (true) {
            int rc = pcre2_match(
                re,
                (PCRE2_SPTR)input.c_str(),
                input.length(),
                start_offset,
                0,
                match_data,
                nullptr
            );

            if (rc < 0) {
                // No more matches, append rest of string
                result += input.substr(start_offset);
                break;
            }

            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);

            // Append text before match
            result += input.substr(start_offset, ovector[0] - start_offset);

            // Process replacement with backreferences
            std::string replacement = pattern.replacePattern;
            for (int i = 1; i < rc; i++) {
                std::string placeholder = "$" + std::to_string(i);
                size_t pos = 0;
                while ((pos = replacement.find(placeholder, pos)) != std::string::npos) {
                    std::string capture = input.substr(ovector[2*i], ovector[2*i+1] - ovector[2*i]);
                    replacement.replace(pos, placeholder.length(), capture);
                    pos += capture.length();
                }
            }
            result += replacement;

            replacements++;
            if (pattern.maxReplacements > 0 && replacements >= pattern.maxReplacements) {
                result += input.substr(ovector[1]);
                break;
            }

            start_offset = ovector[1];
        }

        pcre2_match_data_free(match_data);
        pcre2_code_free(re);

        return result;
    }

    use_std_regex:
#endif

    // Use std::regex as fallback
    try {
        std::regex::flag_type flags = std::regex::ECMAScript;
        if (!pattern.caseSensitive) {
            flags |= std::regex::icase;
        }

        std::regex re(pattern.searchPattern, flags);

        if (pattern.maxReplacements == -1) {
            return std::regex_replace(input, re, pattern.replacePattern);
        } else {
            // Limited replacements
            std::string result = input;
            int replacements = 0;
            std::smatch match;
            std::string::const_iterator searchStart(result.cbegin());

            while (replacements < pattern.maxReplacements &&
                   std::regex_search(searchStart, result.cend(), match, re)) {
                result = match.prefix().str() +
                        std::regex_replace(match.str(), re, pattern.replacePattern) +
                        match.suffix().str();
                replacements++;
                searchStart = result.cbegin() + match.prefix().length() +
                             match.str().length();
            }
            return result;
        }
    } catch (const std::regex_error& e) {
        std::cerr << "Regex error: " << e.what() << std::endl;
        return input;
    }
}

// Apply sequential numbering
std::string RegexRenamer::applyNumbering(const std::string& input, int index, const RenamePattern& pattern) {
    std::string result = input;

    if (!pattern.numberingFormat.empty()) {
        // Use custom format
        std::stringstream ss;
        ss << std::setw(pattern.numberingPadding) << std::setfill('0') << index;
        std::string numStr = ss.str();

        // Replace {num} or {num:04d} style placeholders
        std::regex numRegex(R"(\{num(?::\d+d)?\})");
        result = std::regex_replace(result, numRegex, numStr);
    } else {
        // Simple append
        std::stringstream ss;
        ss << std::setw(pattern.numberingPadding) << std::setfill('0') << index;

        std::string nameWithoutExt = removeExtension(result);
        std::string extension = extractExtension(result);
        result = nameWithoutExt + "_" + ss.str() + extension;
    }

    return result;
}

// Apply date/time to filename
std::string RegexRenamer::applyDateTime(const std::string& input, mega::MegaNode* node, const RenamePattern& pattern) {
    std::time_t timestamp;

    if (pattern.useFileModTime && node) {
        timestamp = node->getModificationTime();
    } else {
        timestamp = std::time(nullptr);
    }

    std::tm* timeinfo = std::localtime(&timestamp);
    char buffer[256];
    std::strftime(buffer, sizeof(buffer), pattern.dateTimeFormat.c_str(), timeinfo);

    std::string nameWithoutExt = removeExtension(input);
    std::string extension = extractExtension(input);

    return std::string(buffer) + nameWithoutExt + extension;
}

// Apply case conversion
std::string RegexRenamer::applyCaseConversion(const std::string& input, RenamePattern::CaseConversion conversion) {
    std::string result = input;

    switch (conversion) {
        case RenamePattern::LOWERCASE:
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            break;

        case RenamePattern::UPPERCASE:
            std::transform(result.begin(), result.end(), result.begin(), ::toupper);
            break;

        case RenamePattern::TITLE_CASE: {
            bool newWord = true;
            for (char& c : result) {
                if (std::isalpha(c)) {
                    if (newWord) {
                        c = std::toupper(c);
                        newWord = false;
                    } else {
                        c = std::tolower(c);
                    }
                } else if (std::isspace(c) || c == '_' || c == '-') {
                    newWord = true;
                }
            }
            break;
        }

        case RenamePattern::SENTENCE_CASE: {
            bool newSentence = true;
            for (char& c : result) {
                if (std::isalpha(c)) {
                    if (newSentence) {
                        c = std::toupper(c);
                        newSentence = false;
                    } else {
                        c = std::tolower(c);
                    }
                } else if (c == '.' || c == '!' || c == '?') {
                    newSentence = true;
                }
            }
            break;
        }

        case RenamePattern::CAMEL_CASE: {
            std::string temp;
            bool capitalizeNext = false;
            for (char c : result) {
                if (std::isalnum(c)) {
                    if (capitalizeNext) {
                        temp += std::toupper(c);
                        capitalizeNext = false;
                    } else {
                        temp += std::tolower(c);
                    }
                } else {
                    capitalizeNext = true;
                }
            }
            result = temp;
            break;
        }

        case RenamePattern::SNAKE_CASE: {
            std::string temp;
            for (char c : result) {
                if (std::isalnum(c)) {
                    temp += std::tolower(c);
                } else if (std::isspace(c) || c == '-') {
                    temp += '_';
                }
            }
            result = temp;
            break;
        }

        case RenamePattern::KEBAB_CASE: {
            std::string temp;
            for (char c : result) {
                if (std::isalnum(c)) {
                    temp += std::tolower(c);
                } else if (std::isspace(c) || c == '_') {
                    temp += '-';
                }
            }
            result = temp;
            break;
        }

        default:
            break;
    }

    return result;
}

// Check for naming conflict
bool RegexRenamer::checkNameConflict(const std::string& name, mega::MegaNode* parent) {
    if (!parent) return false;

    mega::MegaNodeList* children = m_megaApi->getChildren(parent);
    if (!children) return false;

    bool hasConflict = false;
    for (int i = 0; i < children->size(); i++) {
        mega::MegaNode* child = children->get(i);
        if (child && child->getName()) {
            if (name == child->getName()) {
                hasConflict = true;
                break;
            }
        }
    }

    delete children;
    return hasConflict;
}

// Save backup of node name
void RegexRenamer::saveBackup(mega::MegaNode* node) {
    if (!node) return;

    std::string handle = std::to_string(node->getHandle());
    std::string name = node->getName() ? node->getName() : "";
    m_backupNames[handle] = name;
}

// Undo last rename operation
bool RegexRenamer::undoLastRename() {
    if (m_undoStack.empty()) {
        return false;
    }

    RenameOperation operation = m_undoStack.back();
    m_undoStack.pop_back();

    RenameListener listener;
    bool success = true;

    // Reverse all renames in the operation
    for (const auto& result : operation.results) {
        if (result.success && !result.wasSkipped) {
            mega::MegaNode* node = m_megaApi->getNodeByPath(result.fullPath.c_str());
            if (node) {
                listener.reset();
                m_megaApi->renameNode(node, result.originalName.c_str(), &listener);
                listener.wait();

                if (listener.getError() != mega::MegaError::API_OK) {
                    success = false;
                }
                delete node;
            }
        }
    }

    if (success) {
        m_redoStack.push_back(operation);
    }

    return success;
}

// Redo previously undone operation
bool RegexRenamer::redoRename() {
    if (m_redoStack.empty()) {
        return false;
    }

    RenameOperation operation = m_redoStack.back();
    m_redoStack.pop_back();

    RenameListener listener;
    bool success = true;

    // Re-apply all renames in the operation
    for (const auto& result : operation.results) {
        if (result.success && !result.wasSkipped) {
            mega::MegaNode* node = m_megaApi->getNodeByPath(result.fullPath.c_str());
            if (node) {
                // Check if original name matches
                std::string currentName = node->getName() ? node->getName() : "";
                if (currentName == result.originalName) {
                    listener.reset();
                    m_megaApi->renameNode(node, result.newName.c_str(), &listener);
                    listener.wait();

                    if (listener.getError() != mega::MegaError::API_OK) {
                        success = false;
                    }
                }
                delete node;
            }
        }
    }

    if (success) {
        m_undoStack.push_back(operation);
    }

    return success;
}

// Get undo history
std::vector<RenameOperation> RegexRenamer::getUndoHistory(int limit) const {
    std::vector<RenameOperation> history;
    int start = std::max(0, static_cast<int>(m_undoStack.size()) - limit);

    for (int i = start; i < m_undoStack.size(); i++) {
        history.push_back(m_undoStack[i]);
    }

    return history;
}

// Clear history
void RegexRenamer::clearHistory() {
    m_undoStack.clear();
    m_redoStack.clear();
    m_backupNames.clear();
}

// Extract extension from filename
std::string RegexRenamer::extractExtension(const std::string& filename) {
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        return filename.substr(dotPos);
    }
    return "";
}

// Remove extension from filename
std::string RegexRenamer::removeExtension(const std::string& filename) {
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        return filename.substr(0, dotPos);
    }
    return filename;
}

// Add custom rule
void RegexRenamer::addCustomRule(const std::string& name, const RenamePattern& pattern) {
    m_customRules[name] = pattern;
}

// Apply predefined rule
std::vector<RenameResult> RegexRenamer::applyRule(
    const std::string& ruleName,
    const std::vector<mega::MegaNode*>& nodes) {

    // Check custom rules first
    auto customIt = m_customRules.find(ruleName);
    if (customIt != m_customRules.end()) {
        return bulkRename(nodes, customIt->second, false);
    }

    // Check templates
    auto templateIt = m_templates.find(ruleName);
    if (templateIt != m_templates.end()) {
        return bulkRename(nodes, templateIt->second, false);
    }

    // Rule not found
    return std::vector<RenameResult>();
}

// Get available rules
std::map<std::string, std::string> RegexRenamer::getAvailableRules() const {
    std::map<std::string, std::string> rules;

    // Add template descriptions
    rules["photo_organize"] = "Organize photos by date (IMG_YYYYMMDD format)";
    rules["doc_version"] = "Add version numbers to documents";
    rules["clean_spaces"] = "Replace spaces with underscores";
    rules["date_prefix"] = "Add date prefix to filenames";
    rules["remove_special"] = "Remove special characters";

    // Add custom rules
    for (const auto& [name, pattern] : m_customRules) {
        rules[name] = "Custom rule";
    }

    return rules;
}

// Create pattern from template
std::optional<RenamePattern> RegexRenamer::createFromTemplate(const std::string& templateName) {
    auto it = m_templates.find(templateName);
    if (it != m_templates.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Validate regex pattern
bool RegexRenamer::validateRegexPattern(const std::string& pattern, std::string& errorMsg) {
    try {
        std::regex test(pattern);
        errorMsg.clear();
        return true;
    } catch (const std::regex_error& e) {
        errorMsg = e.what();
        return false;
    }
}

// Test pattern on sample text
std::string RegexRenamer::testPattern(
    const std::string& pattern,
    const std::string& sampleText,
    const std::string& replacement) {

    try {
        std::regex re(pattern);
        return std::regex_replace(sampleText, re, replacement);
    } catch (const std::regex_error& e) {
        return "Error: " + std::string(e.what());
    }
}

// Generate unique name
std::string RegexRenamer::generateUniqueName(
    const std::string& baseName,
    const std::vector<std::string>& existingNames) {

    // Extract extension manually for static method
    size_t dotPos = baseName.find_last_of('.');
    std::string nameWithoutExt = baseName;
    std::string extension = "";

    if (dotPos != std::string::npos && dotPos > 0) {
        extension = baseName.substr(dotPos);
        nameWithoutExt = baseName.substr(0, dotPos);
    }

    // Check if base name already exists
    if (std::find(existingNames.begin(), existingNames.end(), baseName) == existingNames.end()) {
        return baseName;
    }

    // Try appending numbers
    for (int i = 1; i < 1000; i++) {
        std::string candidate = nameWithoutExt + "_" + std::to_string(i) + extension;
        if (std::find(existingNames.begin(), existingNames.end(), candidate) == existingNames.end()) {
            return candidate;
        }
    }

    // Use timestamp as last resort
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return nameWithoutExt + "_" + std::to_string(timestamp) + extension;
}

// Sanitize filename
std::string RegexRenamer::sanitizeFilename(const std::string& filename, char replacementChar) {
    std::string result = filename;

    // Characters not allowed in filenames
    const std::string illegalChars = R"(<>:"/\|?*)"
                                     "\x00\x01\x02\x03\x04\x05\x06\x07"
                                     "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
                                     "\x10\x11\x12\x13\x14\x15\x16\x17"
                                     "\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f";

    for (char& c : result) {
        if (illegalChars.find(c) != std::string::npos) {
            c = replacementChar;
        }
    }

    // Remove trailing dots and spaces (Windows requirement)
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) {
        result.pop_back();
    }

    // Remove leading dots and spaces
    size_t start = 0;
    while (start < result.length() && (result[start] == '.' || result[start] == ' ')) {
        start++;
    }
    if (start > 0) {
        result = result.substr(start);
    }

    // Ensure filename is not empty
    if (result.empty()) {
        result = "renamed_file";
    }

    return result;
}

// Set progress callback
void RegexRenamer::setProgressCallback(
    std::function<void(int current, int total, const std::string& currentFile)> callback) {
    m_progressCallback = callback;
}

// Set conflict resolver
void RegexRenamer::setConflictResolver(
    std::function<std::string(const std::string& original, const std::string& proposed)> callback) {
    m_conflictResolver = callback;
}

// Set safe mode
void RegexRenamer::setSafeMode(bool enable) {
    m_safeMode = enable;
    if (!enable) {
        m_backupNames.clear();
    }
}

// Export rules to file
bool RegexRenamer::exportRules(const std::string& filePath) const {
    try {
        nlohmann::json rulesJson = nlohmann::json::object();
        nlohmann::json customRulesJson = nlohmann::json::object();

        for (const auto& [name, pattern] : m_customRules) {
            nlohmann::json patternJson = nlohmann::json::object();
            patternJson["searchPattern"] = pattern.searchPattern;
            patternJson["replacePattern"] = pattern.replacePattern;
            patternJson["caseSensitive"] = pattern.caseSensitive;
            patternJson["useExtendedRegex"] = pattern.useExtendedRegex;
            patternJson["preserveExtension"] = pattern.preserveExtension;
            patternJson["useSequentialNumbering"] = pattern.useSequentialNumbering;
            patternJson["numberingStart"] = pattern.numberingStart;
            patternJson["numberingPadding"] = pattern.numberingPadding;
            customRulesJson[name] = patternJson;
        }

        rulesJson["customRules"] = customRulesJson;
        rulesJson["version"] = "1.0";

        std::ofstream file(filePath);
        if (!file.is_open()) {
            return false;
        }

        file << rulesJson.dump(2);
        file.close();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error exporting rules: " << e.what() << std::endl;
        return false;
    }
}

// Import rules from file
bool RegexRenamer::importRules(const std::string& filePath) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        auto rulesJson = nlohmann::json::parse(buffer.str());

        if (rulesJson.contains("customRules")) {
            const auto& customRulesJson = rulesJson["customRules"];
            // json_simple doesn't support direct iteration, so we'll skip import for now
            // This would need a more complete JSON library
            std::cerr << "Warning: Rule import not fully implemented with json_simple" << std::endl;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error importing rules: " << e.what() << std::endl;
        return false;
    }
}

// Extract metadata (stub implementations for now)
std::map<std::string, std::string> RegexRenamer::extractMetadata(mega::MegaNode* node) {
    std::map<std::string, std::string> metadata;

    if (!node) return metadata;

    // Basic metadata
    metadata["size"] = std::to_string(node->getSize());
    metadata["modTime"] = std::to_string(node->getModificationTime());
    metadata["createTime"] = std::to_string(node->getCreationTime());

    // File type specific metadata would require additional libraries
    std::string name = node->getName() ? node->getName() : "";
    std::string ext = extractExtension(name);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
        auto imgMeta = extractImageMetadata(node);
        metadata.insert(imgMeta.begin(), imgMeta.end());
    } else if (ext == ".mp3" || ext == ".wav" || ext == ".flac") {
        auto audioMeta = extractAudioMetadata(node);
        metadata.insert(audioMeta.begin(), audioMeta.end());
    } else if (ext == ".mp4" || ext == ".avi" || ext == ".mkv") {
        auto videoMeta = extractVideoMetadata(node);
        metadata.insert(videoMeta.begin(), videoMeta.end());
    }

    return metadata;
}

std::map<std::string, std::string> RegexRenamer::extractImageMetadata(mega::MegaNode* node) {
    // Would need image processing library like libexif
    return std::map<std::string, std::string>();
}

std::map<std::string, std::string> RegexRenamer::extractAudioMetadata(mega::MegaNode* node) {
    // Would need audio metadata library like taglib
    return std::map<std::string, std::string>();
}

std::map<std::string, std::string> RegexRenamer::extractVideoMetadata(mega::MegaNode* node) {
    // Would need video metadata library like ffmpeg
    return std::map<std::string, std::string>();
}

} // namespace MegaCustom