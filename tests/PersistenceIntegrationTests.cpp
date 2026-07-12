#include "integrations/MemberDatabase.h"
#include "integrations/WordPressSync.h"
#include "core/ConfigManager.h"

#ifdef USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#else
#include "json_simple.hpp"
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

std::string pathUtf8(const fs::path& path) {
    return path.u8string();
}

bool writeText(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
    return file.good();
}

std::string readText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

json readJson(const fs::path& path) {
    return json::parse(readText(path));
}

std::string stringValue(const json& object, const std::string& key) {
    return object.contains(key) && object[key].is_string()
        ? object[key].get<std::string>() : std::string();
}

json memberById(const json& root, const std::string& id) {
    const json& members = root["members"];
    if (!members.is_array()) {
        return json();
    }
    for (size_t i = 0; i < members.size(); ++i) {
        if (stringValue(members[i], "id") == id) {
            return members[i];
        }
    }
    return json();
}

int fail(const std::string& message, const fs::path& root) {
    std::cerr << "FAIL: " << message << '\n';
    std::error_code ec;
    fs::remove_all(root, ec);
    return 1;
}
} // namespace

int main() {
    const auto token = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    const fs::path root = fs::temp_directory_path()
        / fs::u8path(u8"megacustom-persistence-\u6e2c\u8a66-")
        / std::to_string(token);
    const fs::path memberPath = root / fs::u8path(u8"members-\u00fc.json");
    const fs::path invalidMemberPath = root / "invalid-members.json";
    const fs::path wordpressPath = root / fs::u8path(u8"wordpress-\u00e9.json");
    const fs::path invalidWordpressPath = root / "invalid-wordpress.json";
    const fs::path configPath = root / "nested" / "config.json";
    const fs::path invalidConfigPath = root / "invalid-config.json";

    json fixture = json::object();
    fixture["version"] = 7;
    fixture["groups"] = json::array();
    fixture["groups"].push_back(json::object());
    fixture["groups"][0]["id"] = "vip";
    fixture["template"] = json::object();
    fixture["template"]["primary"] = "{brand} - {member_name}";
    fixture["members"] = json::array();
    json existingFixture = json::object();
    existingFixture["id"] = "existing";
    existingFixture["displayName"] = "Before";
    existingFixture["name"] = "Before";
    existingFixture["email"] = "before@example.test";
    existingFixture["notes"] = "Preserve this Qt-only note";
    existingFixture["paths"] = json::object();
    existingFixture["paths"]["manual"] = "C:\\Members\\Existing";
    existingFixture["watermarkFields"] = json::array();
    existingFixture["watermarkFields"].push_back("email");
    existingFixture["customFields"] = json::object();
    existingFixture["customFields"]["tier"] = "gold";
    fixture["members"].push_back(existingFixture);

    if (!writeText(memberPath, fixture.dump(2) + "\n")) {
        return fail("could not create the member registry fixture", root);
    }

    MegaCustom::MemberDatabase members(pathUtf8(memberPath));
    auto existing = members.getMember("existing");
    if (!existing.success || !existing.member
        || existing.member->customFields["tier"] != "gold") {
        return fail("member fixture did not load through the structured parser", root);
    }

    MegaCustom::Member updated = *existing.member;
    updated.name = u8"Jos\u00e9 \"Ops\" \\ QA";
    updated.email = "updated+member@example.test";
    updated.customFields["quoted"] = "value with \"quotes\" and \\slashes";
    if (!members.updateMember(updated).success) {
        return fail("could not persist an existing member update", root);
    }

    MegaCustom::Member added;
    added.id = "new-member";
    added.name = u8"M\u00fcller \u6e2c\u8a66";
    added.email = "new@example.test";
    added.megaFolderPath = u8"/Members/\u6e2c\u8a66";
    added.customFields["line"] = "one\ntwo";
    if (!members.addMember(added).success) {
        return fail("could not persist a new member", root);
    }

    json saved = readJson(memberPath);
    json savedExisting = memberById(saved, "existing");
    json savedAdded = memberById(saved, "new-member");
    if (!savedExisting.is_object() || !savedAdded.is_object()
        || saved["version"].get<int>() != 7
        || stringValue(saved["groups"][0], "id") != "vip"
        || stringValue(saved["template"], "primary") != "{brand} - {member_name}"
        || stringValue(savedExisting, "notes") != "Preserve this Qt-only note"
        || stringValue(savedExisting["paths"], "manual") != "C:\\Members\\Existing"
        || stringValue(savedExisting, "displayName") != updated.name
        || stringValue(savedExisting["customFields"], "quoted")
            != updated.customFields["quoted"]
        || stringValue(savedAdded, "displayName") != added.name) {
        return fail("member save lost data or failed to preserve Qt-only fields", root);
    }

    MegaCustom::MemberDatabase reloaded(pathUtf8(memberPath));
    auto reloadedAdded = reloaded.getMember("new-member");
    if (!reloadedAdded.success || !reloadedAdded.member
        || reloadedAdded.member->name != added.name
        || reloadedAdded.member->customFields["line"] != "one\ntwo") {
        return fail("member special characters did not round-trip", root);
    }

    if (!reloaded.removeMember("existing").success) {
        return fail("member removal did not persist", root);
    }
    saved = readJson(memberPath);
    if (memberById(saved, "existing").is_object()
        || stringValue(saved["groups"][0], "id") != "vip") {
        return fail("member removal damaged preserved registry fields", root);
    }

    if (!writeText(invalidMemberPath, "{")) {
        return fail("could not create malformed member fixture", root);
    }
    MegaCustom::MemberDatabase invalidMembers(pathUtf8(invalidMemberPath));
    MegaCustom::Member rejected;
    rejected.id = "must-not-overwrite";
    if (invalidMembers.addMember(rejected).success || readText(invalidMemberPath) != "{") {
        return fail("a mutation overwrote a malformed member registry", root);
    }

    MegaCustom::WordPressConfig wordpress;
    wordpress.siteUrl = "https://example.test/site";
    wordpress.username = u8"admin-\u6e2c\u8a66";
    wordpress.applicationPassword = "secret with spaces and \"quotes\"";
    wordpress.usersEndpoint = "/custom/users";
    wordpress.customEndpoint = "/custom/members?mode=full";
    wordpress.syncAllFields = false;
    wordpress.createNewMembers = false;
    wordpress.updateExisting = false;
    wordpress.perPage = 37;
    wordpress.timeout = 91;
    wordpress.roleFilter = "customer";
    wordpress.fieldMappings["meta.member_code"] = "id";
    wordpress.fieldMappings["display_name"] = "name";

    MegaCustom::WordPressSync wordpressWriter;
    wordpressWriter.setConfig(wordpress);
    if (!wordpressWriter.saveConfig(pathUtf8(wordpressPath))) {
        return fail("could not save WordPress configuration: "
                    + wordpressWriter.getLastError(), root);
    }

    json rawWordpress = readJson(wordpressPath);
    if (!rawWordpress["encrypted"].get<bool>()
        || stringValue(rawWordpress, "applicationPassword")
            == wordpress.applicationPassword) {
        return fail("WordPress password was not encrypted on disk", root);
    }

    MegaCustom::WordPressSync wordpressReader;
    MegaCustom::WordPressConfig stale;
    stale.customEndpoint = "/stale";
    stale.fieldMappings["stale"] = "stale";
    wordpressReader.setConfig(stale);
    if (!wordpressReader.loadConfig(pathUtf8(wordpressPath))) {
        return fail("could not reload WordPress configuration: "
                    + wordpressReader.getLastError(), root);
    }
    const MegaCustom::WordPressConfig loaded = wordpressReader.getConfig();
    if (loaded.siteUrl != wordpress.siteUrl
        || loaded.username != wordpress.username
        || loaded.applicationPassword != wordpress.applicationPassword
        || loaded.usersEndpoint != wordpress.usersEndpoint
        || loaded.customEndpoint != wordpress.customEndpoint
        || loaded.syncAllFields != wordpress.syncAllFields
        || loaded.createNewMembers != wordpress.createNewMembers
        || loaded.updateExisting != wordpress.updateExisting
        || loaded.perPage != wordpress.perPage
        || loaded.timeout != wordpress.timeout
        || loaded.roleFilter != wordpress.roleFilter
        || loaded.fieldMappings != wordpress.fieldMappings) {
        return fail("WordPress configuration fields did not round-trip", root);
    }

    if (!writeText(invalidWordpressPath, "[")) {
        return fail("could not create malformed WordPress fixture", root);
    }
    if (wordpressReader.loadConfig(pathUtf8(invalidWordpressPath))
        || wordpressReader.getConfig().siteUrl != wordpress.siteUrl
        || readText(invalidWordpressPath) != "[") {
        return fail("malformed WordPress configuration changed live settings", root);
    }

    MegaCustom::ConfigManager& config = MegaCustom::ConfigManager::getInstance();
    config.resetToDefaults();
    const std::vector<std::string> expectedArray = {
        "plain", "quoted \"value\"", u8"unicode-\u6e2c\u8a66"
    };
    config.setArray("test.values", expectedArray);
    if (config.getArray("test.values") != expectedArray) {
        return fail("ConfigManager array getter did not return the stored values", root);
    }
    if (!config.saveConfig(pathUtf8(configPath))) {
        return fail("ConfigManager atomic save failed", root);
    }
    config.setArray("test.values", {"changed"});
    if (!config.loadConfig(pathUtf8(configPath))
        || config.getArray("test.values") != expectedArray) {
        return fail("ConfigManager array values did not round-trip", root);
    }
    if (!writeText(invalidConfigPath, "{")
        || config.loadConfig(pathUtf8(invalidConfigPath))
        || config.getArray("test.values") != expectedArray) {
        return fail("malformed core config changed the active configuration", root);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::cout << "Persistence integration tests passed\n";
    return 0;
}
