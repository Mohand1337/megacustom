#include "utils/MemberRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool writeBytes(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(bytes) == bytes.size();
}

bool writeJson(const QString& path, const QJsonObject& root) {
    return writeBytes(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QJsonObject readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject member(const QString& id, const QString& name, qint64 updatedAt,
                   const QString& source) {
    QJsonObject object;
    object["id"] = id;
    object["displayName"] = name;
    object["active"] = true;
    object["createdAt"] = updatedAt - 10;
    object["updatedAt"] = updatedAt;
    object["sourceOnlyField"] = source;
    return object;
}

QJsonObject group(const QString& name, const QStringList& ids, qint64 updatedAt) {
    QJsonArray memberIds;
    for (const QString& id : ids) memberIds.append(id);
    QJsonObject object;
    object["name"] = name;
    object["memberIds"] = memberIds;
    object["createdAt"] = updatedAt - 10;
    object["updatedAt"] = updatedAt;
    return object;
}

QJsonObject findMember(const QJsonObject& root, const QString& id) {
    for (const QJsonValue& value : root.value("members").toArray()) {
        if (value.toObject().value("id").toString() == id) return value.toObject();
    }
    return {};
}

bool containsMember(const QJsonObject& root, const QString& id) {
    return !findMember(root, id).isEmpty();
}

int fail(const QString& message) {
    std::cerr << "FAIL: " << message.toStdString() << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("MegaCustom");
    QCoreApplication::setApplicationName("MegaCustom");

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) return fail("could not create a temporary directory");

    const QString activeDirectory = QDir(temporaryDirectory.path()).filePath("active");
    const QString legacyDirectory = QDir(temporaryDirectory.path()).filePath("legacy");
    qputenv("MEGACUSTOM_CONFIG_DIR", activeDirectory.toUtf8());
    qputenv("MEGACUSTOM_LEGACY_CONFIG_DIR", legacyDirectory.toUtf8());

    const QString activePath = QDir(activeDirectory).filePath("members.json");
    const QString legacyPath = QDir(legacyDirectory).filePath("members.json");

    QJsonObject activeRoot;
    activeRoot["schemaVersion"] = 11;
    activeRoot["activeOnlyRootField"] = "preserve-me";
    activeRoot["members"] = QJsonArray{
        member("shared", "Older Name", 100, "active"),
        member("active-only", "Active Only", 110, "active")
    };
    activeRoot["groups"] = QJsonArray{
        group("Combined", {"active-only"}, 100)
    };

    QJsonObject legacyRoot;
    legacyRoot["legacyOnlyRootField"] = "also-preserve-me";
    legacyRoot["members"] = QJsonArray{
        member("shared", "Newer Name", 200, "legacy"),
        member("recent", "Recent Member", 210, "legacy")
    };
    legacyRoot["groups"] = QJsonArray{
        group("Combined", {"active-only", "recent"}, 200),
        group("Recent Group", {"recent"}, 210)
    };

    if (!writeJson(activePath, activeRoot) || !writeJson(legacyPath, legacyRoot)) {
        return fail("could not create split-registry fixtures");
    }

    MegaCustom::MemberRegistry* registry = MegaCustom::MemberRegistry::instance();
    if (!registry->isPersistenceReady()) {
        return fail("registry did not load: " + registry->lastPersistenceError());
    }
    if (registry->getAllMembers().size() != 3
        || registry->getMember("shared").displayName != "Newer Name"
        || !registry->hasMember("active-only")
        || !registry->hasMember("recent")) {
        return fail("one-time migration did not merge active and newer legacy members");
    }
    const QStringList combinedIds = registry->getGroup("Combined").memberIds;
    if (!combinedIds.contains("active-only") || !combinedIds.contains("recent")
        || !registry->hasGroup("Recent Group")) {
        return fail("one-time migration did not preserve groups and memberships");
    }
    if (!QFileInfo::exists(activePath + ".migration-v2.json")) {
        return fail("migration completion marker was not created");
    }
    QDir activeDir(activeDirectory);
    if (activeDir.entryList({"members.json.pre-migration-v2-active-*.bak"}, QDir::Files).isEmpty()
        || activeDir.entryList({"members.json.pre-migration-v2-legacy-*.bak"}, QDir::Files).isEmpty()) {
        return fail("pre-migration safety backups were not created");
    }

    MegaCustom::MemberInfo duplicateMember = registry->getMember("recent");
    duplicateMember.displayName = "Must Not Replace Existing";
    if (registry->addMember(duplicateMember)
        || registry->getMember("recent").displayName != "Recent Member") {
        return fail("addMember accepted a duplicate ID");
    }
    if (registry->addGroup(registry->getGroup("Recent Group"))) {
        return fail("addGroup accepted a duplicate name");
    }

    MegaCustom::MemberInfo added;
    added.id = "after-migration";
    added.displayName = "After Migration";
    added.active = true;
    added.createdAt = 300;
    added.updatedAt = 300;
    if (!registry->addMember(added) || !QFileInfo::exists(activePath + ".bak.1")) {
        return fail("normal mutation or rotating backup creation failed");
    }

    QJsonObject saved = readJson(activePath);
    if (saved.value("schemaVersion").toInt() != 11
        || saved.value("activeOnlyRootField").toString() != "preserve-me"
        || saved.value("legacyOnlyRootField").toString() != "also-preserve-me"
        || findMember(saved, "shared").value("sourceOnlyField").toString() != "legacy") {
        return fail("saving discarded unknown root or member fields");
    }

    QJsonArray externallyChangedMembers = saved.value("members").toArray();
    externallyChangedMembers.append(member("external", "External", 400, "external"));
    saved["members"] = externallyChangedMembers;
    if (!writeJson(activePath, saved)) return fail("could not simulate an external writer");

    MegaCustom::MemberInfo blocked;
    blocked.id = "blocked-stale-write";
    blocked.displayName = "Must Roll Back";
    if (registry->addMember(blocked) || registry->hasMember(blocked.id)
        || !containsMember(readJson(activePath), "external")) {
        return fail("stale writer was not rejected and rolled back");
    }

    if (!registry->load() || !registry->hasMember("external")) {
        return fail("registry could not reload after an external change");
    }
    if (!registry->renameGroup("Recent Group", "Recovered Group")
        || registry->hasGroup("Recent Group") || !registry->hasGroup("Recovered Group")) {
        return fail("group rename was not atomic");
    }

    for (int index = 0; index < 6; ++index) {
        MegaCustom::MemberInfo generated;
        generated.id = QString("backup-%1").arg(index);
        generated.displayName = generated.id;
        generated.updatedAt = 500 + index;
        if (!registry->addMember(generated)) return fail("backup rotation mutation failed");
    }
    if (!QFileInfo::exists(activePath + ".bak.5")
        || QFileInfo::exists(activePath + ".bak.6")) {
        return fail("backup rotation did not retain exactly the configured generations");
    }

    if (!writeBytes(activePath, "{") || registry->load() || registry->isPersistenceReady()) {
        return fail("malformed registry was not rejected");
    }
    MegaCustom::MemberInfo mustNotPersist;
    mustNotPersist.id = "must-not-persist";
    if (registry->addMember(mustNotPersist)) {
        return fail("mutation was accepted after malformed registry load");
    }
    QFile malformed(activePath);
    if (!malformed.open(QIODevice::ReadOnly) || malformed.readAll() != "{") {
        return fail("malformed registry was overwritten");
    }

    std::cout << "Member registry integration tests passed\n";
    return 0;
}
