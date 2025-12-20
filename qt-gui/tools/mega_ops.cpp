/**
 * Mega Operations CLI Tool
 * Quick CLI for folder creation and copy operations
 * Shares session with MegaCustomGUI
 */

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <megaapi.h>
#include <iostream>
#include <memory>

// OpenSSL for AES-256-GCM decryption
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <QSysInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDate>
#include <QLocale>

/**
 * Get the current year and month folder path component
 * Returns e.g., "2025/December. " for December 2025
 */
static QString getCurrentMonthPath() {
    QDate today = QDate::currentDate();
    QString year = QString::number(today.year());
    QString month = QLocale(QLocale::English).monthName(today.month()) + ". ";
    return year + "/" + month;
}

/**
 * Get the base path for a member's monthly folder (without year/month)
 * Input: "/Alen Sultanic.../2025/November. "
 * Output: "/Alen Sultanic.../"
 */
static QString getBasePath(const QString& fullPath) {
    // Find the year pattern (4 digits followed by /)
    QRegularExpression yearPattern("\\d{4}/[A-Za-z]+\\. ?$");
    return QString(fullPath).remove(yearPattern);
}

/**
 * Get the effective destination path with current month
 * If the path has an old year/month, replace it with current
 */
static QString getEffectiveDestPath(const QString& configuredPath) {
    QString basePath = getBasePath(configuredPath);
    return basePath + getCurrentMonthPath();
}

/**
 * Get path for a specific month offset from current
 * offset=0: current month, offset=-1: previous month, offset=1: next month
 */
static QString getMonthPath(int monthOffset) {
    QDate targetDate = QDate::currentDate().addMonths(monthOffset);
    QString year = QString::number(targetDate.year());
    QString month = QLocale(QLocale::English).monthName(targetDate.month()) + ". ";
    return year + "/" + month;
}

/**
 * Get effective path for a specific month offset
 */
static QString getPathForMonth(const QString& configuredPath, int monthOffset) {
    QString basePath = getBasePath(configuredPath);
    return basePath + getMonthPath(monthOffset);
}

/**
 * Member configuration structure for mega_ops
 * Loaded from ~/.config/MegaCustom/mega_ops_members.json
 */
struct MemberConfig {
    QString id;                    // Member ID (e.g., "Icekkk")
    QString sourcePattern;         // Source folder pattern (e.g., "/latest-wm/Icekkk_*")
    QString novemberFolder;        // November destination folder
    QString theoryCallsPath;       // Theory Calls path for AI Summary fixes

    static MemberConfig fromJson(const QJsonObject& obj) {
        MemberConfig cfg;
        cfg.id = obj["id"].toString();
        cfg.sourcePattern = obj["sourcePattern"].toString();
        cfg.novemberFolder = obj["novemberFolder"].toString();
        cfg.theoryCallsPath = obj["theoryCallsPath"].toString();
        return cfg;
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["sourcePattern"] = sourcePattern;
        obj["novemberFolder"] = novemberFolder;
        obj["theoryCallsPath"] = theoryCallsPath;
        return obj;
    }
};

// Forward declaration
static QList<MemberConfig> getDefaultMemberConfigs();

/**
 * Load member configurations from JSON file
 * Falls back to defaults if file doesn't exist
 */
static QList<MemberConfig> loadMemberConfigs() {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString membersPath = configPath + "/MegaCustom/mega_ops_members.json";

    QFile file(membersPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();

        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonArray members = root["members"].toArray();

            QList<MemberConfig> configs;
            for (const QJsonValue& val : members) {
                configs.append(MemberConfig::fromJson(val.toObject()));
            }

            if (!configs.isEmpty()) {
                qDebug() << "Loaded" << configs.size() << "member configs from" << membersPath;
                return configs;
            }
        }
    }

    // Return default configs (preserves backwards compatibility)
    qDebug() << "Using default member configs (no config file found at" << membersPath << ")";
    return getDefaultMemberConfigs();
}

/**
 * Get default member configurations (hardcoded fallback)
 */
static QList<MemberConfig> getDefaultMemberConfigs() {
    QList<MemberConfig> configs = {
        {"Icekkk", "/latest-wm/Icekkk_*", "/Alen Sultanic - NHB+ - EGBs/3. Icekkk/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/3. Icekkk/Fast Forward⏩/2- Theory Calls"},
        {"nekondarun", "/latest-wm/nekondarun_*", "/Alen Sultanic - NHB+ - EGBs/5. nekondarun/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/5. nekondarun/Fast Forward⏩/2- Theory Calls"},
        {"sp3nc3", "/latest-wm/sp3nc3_*", "/Alen Sultanic - NHB+ - EGBs/7. sp3nc3/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/7. sp3nc3/Fast Forward⏩/2- Theory Calls"},
        {"mehtha", "/latest-wm/mehtha_*", "/Alen Sultanic - NHB+ - EGBs/9. mehulthakkar/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/9. mehulthakkar/Fast Forward⏩/2- Theory Calls"},
        {"maxbooks", "/latest-wm/maxbooks_*", "/Alen Sultanic - NHB+ - EGBs/10. maxbooks/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/10. maxbooks/Fast Forward⏩/2- Theory Calls"},
        {"mars", "/latest-wm/mars_*", "/Alen Sultanic - NHB+ - EGBs/11. mars/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/11. mars/Fast Forward⏩/2- Theory Calls"},
        {"mm2024", "/latest-wm/mm2024_*", "/Alen Sultanic - NHB+ - EGBs/13. alfie - MM2024/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/13. alfie - MM2024/Fast Forward⏩/2- Theory Calls"},
        {"jpegcollector", "/latest-wm/jpegcollector_*", "/Alen Sultanic - NHB+ - EGBs/14. peterpette/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/14. peterpette/Fast Forward⏩/2- Theory Calls"},
        {"danki", "/latest-wm/danki_*", "/Alen Sultanic - NHB+ - EGBs/17. danki/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/17. danki/Fast Forward⏩/2- Theory Calls"},
        {"slayer", "/latest-wm/slayer_*", "/Alen Sultanic - NHB+ - EGBs/20. marvizta/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/20. marvizta/Fast Forward⏩/2- Theory Calls"},
        {"jkalam", "/latest-wm/jkalam_*", "/Alen Sultanic - NHB+ - EGBs/21. jkalam/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/21. jkalam/Fast Forward⏩/2- Theory Calls"},
        {"CMex", "/latest-wm/CMex_*", "/Alen Sultanic - NHB+ - EGBs/23. CMex/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/23. CMex/Fast Forward⏩/2- Theory Calls"},
        {"downdogcatsup", "/latest-wm/downdogcatsup_*", "/Alen Sultanic - NHB+ - EGBs/24. downdogcatsup/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/24. downdogcatsup/Fast Forward⏩/2- Theory Calls"},
        {"boris", "/latest-wm/boris_*", "/Alen Sultanic - NHB+ - EGBs/25. Boris/NHB+ 2021-2024 - Regularly Updated/1. NHB+ Calls & Playlists/2025/November. ", "/Alen Sultanic - NHB+ - EGBs/25. Boris/Fast Forward⏩/2- Theory Calls"}
    };
    return configs;
}

/**
 * Save default member configs to JSON file (for user to edit)
 */
static void saveDefaultMemberConfigs() {
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString dirPath = configPath + "/MegaCustom";
    QString membersPath = dirPath + "/mega_ops_members.json";

    // Create directory if needed
    QDir().mkpath(dirPath);

    QList<MemberConfig> defaults = getDefaultMemberConfigs();

    QJsonArray membersArray;
    for (const MemberConfig& cfg : defaults) {
        membersArray.append(cfg.toJson());
    }

    QJsonObject root;
    root["version"] = 1;
    root["description"] = "Member configurations for mega_ops CLI. Edit sourcePattern to match current timestamps, or use wildcards (*).";
    root["members"] = membersArray;

    QFile file(membersPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Saved default member configs to" << membersPath;
    }
}

/**
 * Generate machine key matching CredentialStore
 * Uses machine identifiers + per-installation salt
 */
static QString generateMachineKey()
{
    // Get per-installation salt (same location as CredentialStore)
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString saltPath = configPath + "/MegaCustom/.salt.bin";
    QString salt;

    QFile saltFile(saltPath);
    if (saltFile.exists() && saltFile.open(QIODevice::ReadOnly)) {
        QByteArray saltData = saltFile.readAll();
        saltFile.close();
        if (saltData.size() >= 32) {
            salt = QString::fromLatin1(saltData.toBase64());
        }
    }

    // Build machine info string (same as CredentialStore)
    QString machineInfo;
    machineInfo += QSysInfo::machineUniqueId();
    machineInfo += QSysInfo::machineHostName();
    machineInfo += QSysInfo::kernelType();
    machineInfo += QSysInfo::productType();
    machineInfo += salt;

    // Hash to create consistent key
    QByteArray hash = QCryptographicHash::hash(machineInfo.toUtf8(), QCryptographicHash::Sha256);
    return hash.toHex();
}

/**
 * Decrypt session data using AES-256-GCM
 * Format: base64(IV[12] + ciphertext + tag[16])
 * Matches CredentialStore::decrypt()
 */
static QString decryptSessionAES(const QString& ciphertext, const QString& machineKey)
{
    if (ciphertext.isEmpty() || machineKey.isEmpty()) {
        return QString();
    }

    static constexpr int IV_SIZE = 12;
    static constexpr int TAG_SIZE = 16;

    // Decode from base64
    QByteArray combined = QByteArray::fromBase64(ciphertext.toLatin1());

    if (combined.size() < IV_SIZE + TAG_SIZE) {
        qDebug() << "Ciphertext too short";
        return QString();
    }

    // Extract IV, ciphertext, and tag
    QByteArray iv = combined.left(IV_SIZE);
    QByteArray tag = combined.right(TAG_SIZE);
    QByteArray cipher = combined.mid(IV_SIZE, combined.size() - IV_SIZE - TAG_SIZE);

    // Derive key (SHA-256 hash of machine key)
    QByteArray key = QCryptographicHash::hash(machineKey.toUtf8(), QCryptographicHash::Sha256);

    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qDebug() << "Failed to create cipher context";
        return QString();
    }

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qDebug() << "Failed to initialize decryption";
        return QString();
    }

    // Decrypt
    QByteArray plaintext(cipher.size() + EVP_MAX_BLOCK_LENGTH, 0);
    int len = 0;
    int plaintextLen = 0;

    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &len,
                          reinterpret_cast<const unsigned char*>(cipher.constData()),
                          cipher.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qDebug() << "Decryption failed";
        return QString();
    }
    plaintextLen = len;

    // Set auth tag for verification
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                            const_cast<char*>(tag.constData())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qDebug() << "Failed to set auth tag";
        return QString();
    }

    // Finalize and verify tag
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        qDebug() << "Authentication failed - data may be tampered";
        return QString();
    }
    plaintextLen += len;

    EVP_CIPHER_CTX_free(ctx);

    plaintext.resize(plaintextLen);
    return QString::fromUtf8(plaintext);
}

class MegaOpsListener : public mega::MegaRequestListener {
public:
    bool finished = false;
    bool success = false;
    QString errorMsg;
    mega::MegaHandle newHandle = mega::INVALID_HANDLE;

    void onRequestFinish(mega::MegaApi*, mega::MegaRequest* request, mega::MegaError* error) override {
        success = (error->getErrorCode() == mega::MegaError::API_OK);
        if (!success) {
            errorMsg = QString::fromUtf8(error->getErrorString());
        }
        if (request->getNodeHandle()) {
            newHandle = request->getNodeHandle();
        }
        finished = true;
    }

    void reset() {
        finished = false;
        success = false;
        errorMsg.clear();
        newHandle = mega::INVALID_HANDLE;
    }

    bool wait(int timeoutMs = 30000) {
        QElapsedTimer timer;
        timer.start();
        while (!finished && timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents();
            QThread::msleep(50);
        }
        return finished;
    }
};

class MegaOps : public QObject {
    Q_OBJECT
public:
    MegaOps(const QString& apiKey) {
        m_api = new mega::MegaApi(apiKey.toUtf8().constData(), (const char*)nullptr, "MegaOps/1.0");
        m_listener = new MegaOpsListener();
    }

    ~MegaOps() {
        delete m_api;
        delete m_listener;
    }

    bool restoreSession() {
        // Read from .sessions.enc (JSON format) - matches CredentialStore
        QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        QString sessionPath = configPath + "/MegaCustom/.sessions.enc";

        QFile file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "No session file found at:" << sessionPath;
            return false;
        }

        QByteArray jsonData = file.readAll();
        file.close();

        // Parse JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qDebug() << "JSON parse error:" << parseError.errorString();
            return false;
        }

        QJsonObject root = doc.object();
        if (root.isEmpty()) {
            qDebug() << "No sessions stored";
            return false;
        }

        // Generate machine key (same as CredentialStore)
        QString machineKey = generateMachineKey();
        qDebug() << "Using machine-based encryption key";

        // Try to get active account ID from accounts.json (GUI's active account)
        QString preferredAccountId;
        QString accountsPath = configPath + "/MegaCustom/accounts.json";
        QFile accountsFile(accountsPath);
        if (accountsFile.open(QIODevice::ReadOnly)) {
            QJsonDocument accountsDoc = QJsonDocument::fromJson(accountsFile.readAll());
            accountsFile.close();
            if (!accountsDoc.isNull() && accountsDoc.isObject()) {
                preferredAccountId = accountsDoc.object()["activeAccountId"].toString();
                if (!preferredAccountId.isEmpty()) {
                    qDebug() << "Using GUI's active account:" << preferredAccountId;
                }
            }
        }

        // Fallback to settings.ini lastEmail if accounts.json not available
        if (preferredAccountId.isEmpty()) {
            QString settingsPath = configPath + "/MegaCustom/settings.ini";
            QFile settingsFile(settingsPath);
            if (settingsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!settingsFile.atEnd()) {
                    QString line = settingsFile.readLine().trimmed();
                    if (line.startsWith("lastEmail=")) {
                        preferredAccountId = line.mid(10);
                        break;
                    }
                }
                settingsFile.close();
            }
        }

        // Find session to use - prefer last email, otherwise use first available
        QString encryptedSession;
        QString accountId;

        if (!preferredAccountId.isEmpty() && root.contains(preferredAccountId)) {
            accountId = preferredAccountId;
            encryptedSession = root[accountId].toString();
            qDebug() << "Using preferred account:" << accountId;
        } else {
            // Use first available session
            accountId = root.keys().first();
            encryptedSession = root[accountId].toString();
            qDebug() << "Using first available account:" << accountId;
        }

        if (encryptedSession.isEmpty()) {
            qDebug() << "No session data for account:" << accountId;
            return false;
        }

        // Decrypt session using AES-256-GCM (matches CredentialStore)
        QString sessionToken = decryptSessionAES(encryptedSession, machineKey);
        if (sessionToken.isEmpty()) {
            qDebug() << "Failed to decrypt session data";
            return false;
        }

        m_listener->reset();
        m_api->fastLogin(sessionToken.toUtf8().constData(), m_listener);
        if (!m_listener->wait(60000) || !m_listener->success) {
            qDebug() << "Login failed:" << m_listener->errorMsg;
            return false;
        }

        // Fetch nodes
        m_listener->reset();
        m_api->fetchNodes(m_listener);
        if (!m_listener->wait(120000) || !m_listener->success) {
            qDebug() << "Fetch nodes failed:" << m_listener->errorMsg;
            return false;
        }

        qDebug() << "Session restored successfully for:" << accountId;
        return true;
    }

    mega::MegaNode* getNodeByPath(const QString& path) {
        return m_api->getNodeByPath(path.toUtf8().constData());
    }

    /**
     * Find a folder matching a pattern with wildcard (*) support
     * Pattern like "/latest-wm/Icekkk_*" matches "/latest-wm/Icekkk_20251125_015429"
     */
    mega::MegaNode* findFolderByPattern(const QString& pattern) {
        // If no wildcard, use exact path
        if (!pattern.contains('*')) {
            return getNodeByPath(pattern);
        }

        // Split pattern to get parent folder and name pattern
        int lastSlash = pattern.lastIndexOf('/');
        if (lastSlash < 0) return nullptr;

        QString parentPath = pattern.left(lastSlash);
        QString namePattern = pattern.mid(lastSlash + 1);

        if (parentPath.isEmpty()) parentPath = "/";

        std::unique_ptr<mega::MegaNode> parent(getNodeByPath(parentPath));
        if (!parent || !parent->isFolder()) {
            qDebug() << "Parent folder not found:" << parentPath;
            return nullptr;
        }

        // Convert glob pattern to regex (simple: * -> .*)
        QString regexPattern = QRegularExpression::escape(namePattern);
        regexPattern.replace("\\*", ".*");
        QRegularExpression regex("^" + regexPattern + "$");

        std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(parent.get()));
        if (!children) return nullptr;

        // Find most recent matching folder (by name - timestamps sort alphabetically)
        mega::MegaNode* bestMatch = nullptr;
        QString bestName;

        for (int i = 0; i < children->size(); i++) {
            mega::MegaNode* child = children->get(i);
            if (!child->isFolder()) continue;

            QString name = QString::fromUtf8(child->getName());
            if (regex.match(name).hasMatch()) {
                // Keep the "largest" name (most recent timestamp)
                if (!bestMatch || name > bestName) {
                    bestMatch = child;
                    bestName = name;
                }
            }
        }

        if (bestMatch) {
            qDebug() << "Pattern" << pattern << "matched:" << bestName;
            return m_api->getNodeByHandle(bestMatch->getHandle());
        }

        return nullptr;
    }

    bool createFolder(const QString& parentPath, const QString& folderName) {
        std::unique_ptr<mega::MegaNode> parent(getNodeByPath(parentPath));
        if (!parent) {
            qDebug() << "Parent folder not found:" << parentPath;
            return false;
        }

        m_listener->reset();
        m_api->createFolder(folderName.toUtf8().constData(), parent.get(), m_listener);
        if (!m_listener->wait() || !m_listener->success) {
            qDebug() << "Create folder failed:" << m_listener->errorMsg;
            return false;
        }

        qDebug() << "Created folder:" << folderName << "in" << parentPath;
        return true;
    }

    bool copyNode(const QString& sourcePath, const QString& destPath) {
        std::unique_ptr<mega::MegaNode> source(getNodeByPath(sourcePath));
        if (!source) {
            qDebug() << "Source not found:" << sourcePath;
            return false;
        }

        std::unique_ptr<mega::MegaNode> dest(getNodeByPath(destPath));
        if (!dest) {
            qDebug() << "Destination not found:" << destPath;
            return false;
        }

        m_listener->reset();
        m_api->copyNode(source.get(), dest.get(), m_listener);
        if (!m_listener->wait(120000) || !m_listener->success) {
            qDebug() << "Copy failed:" << m_listener->errorMsg;
            return false;
        }

        qDebug() << "Copied:" << sourcePath << "->" << destPath;
        return true;
    }

    bool removeNode(const QString& path) {
        std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
        if (!node) {
            qDebug() << "Node not found:" << path;
            return false;
        }

        m_listener->reset();
        m_api->remove(node.get(), m_listener);
        if (!m_listener->wait(60000) || !m_listener->success) {
            qDebug() << "Remove failed:" << m_listener->errorMsg;
            return false;
        }

        qDebug() << "Removed:" << path;
        return true;
    }

    bool renameNode(const QString& path, const QString& newName) {
        std::unique_ptr<mega::MegaNode> node(getNodeByPath(path));
        if (!node) {
            qDebug() << "Node not found:" << path;
            return false;
        }

        m_listener->reset();
        m_api->renameNode(node.get(), newName.toUtf8().constData(), m_listener);
        if (!m_listener->wait(60000) || !m_listener->success) {
            qDebug() << "Rename failed:" << m_listener->errorMsg;
            return false;
        }

        qDebug() << "Renamed:" << path << "->" << newName;
        return true;
    }

    /**
     * Bulk rename: remove a substring from all file names in a folder
     */
    int bulkRenameRemove(const QString& folderPath, const QString& removeStr) {
        std::unique_ptr<mega::MegaNode> folder(getNodeByPath(folderPath));
        if (!folder || !folder->isFolder()) {
            qDebug() << "Folder not found:" << folderPath;
            return -1;
        }

        std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(folder.get()));
        if (!children) {
            qDebug() << "Failed to get children";
            return -1;
        }

        int renamed = 0;
        for (int i = 0; i < children->size(); i++) {
            mega::MegaNode* child = children->get(i);
            QString name = QString::fromUtf8(child->getName());

            if (name.contains(removeStr)) {
                QString newName = name;
                newName.replace(removeStr, "");

                m_listener->reset();
                m_api->renameNode(child, newName.toUtf8().constData(), m_listener);
                if (m_listener->wait(60000) && m_listener->success) {
                    qDebug() << "Renamed:" << name << "->" << newName;
                    renamed++;
                } else {
                    qDebug() << "Failed to rename:" << name << "-" << m_listener->errorMsg;
                }
            }
        }

        return renamed;
    }

    /**
     * List contents of a folder
     */
    QStringList listFolder(const QString& folderPath) {
        QStringList result;
        std::unique_ptr<mega::MegaNode> folder(getNodeByPath(folderPath));
        if (!folder || !folder->isFolder()) {
            qDebug() << "Folder not found:" << folderPath;
            return result;
        }

        std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(folder.get()));
        if (!children) {
            return result;
        }

        for (int i = 0; i < children->size(); i++) {
            mega::MegaNode* child = children->get(i);
            QString name = QString::fromUtf8(child->getName());
            QString type = child->isFolder() ? "[DIR]" : "[FILE]";
            qint64 size = child->getSize();
            result << QString("%1 %2 (%3 bytes)").arg(type).arg(name).arg(size);
        }
        return result;
    }

    /**
     * Copy all files from timestamped folders to November folders for each member
     * Each member gets their OWN watermarked files copied to their November folder
     * Uses member configs from JSON file (supports wildcard patterns)
     */
    int copyToNovemberFolders(const QList<MemberConfig>& memberConfigs) {
        int totalCopied = 0;
        for (const MemberConfig& cfg : memberConfigs) {
            if (cfg.sourcePattern.isEmpty() || cfg.novemberFolder.isEmpty()) {
                qDebug() << "Skipping member" << cfg.id << "- missing source or destination";
                continue;
            }

            // Use pattern matching to find source folder (supports wildcards)
            std::unique_ptr<mega::MegaNode> srcFolder(findFolderByPattern(cfg.sourcePattern));
            if (!srcFolder || !srcFolder->isFolder()) {
                qDebug() << "Source folder not found for pattern:" << cfg.sourcePattern;
                continue;
            }

            QString effectiveDestPath = getEffectiveDestPath(cfg.novemberFolder);
            std::unique_ptr<mega::MegaNode> dstFolder(getNodeByPath(effectiveDestPath));
            if (!dstFolder || !dstFolder->isFolder()) {
                qDebug() << "Destination folder not found:" << effectiveDestPath;
                continue;
            }

            std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(srcFolder.get()));
            if (!children) continue;

            int memberCopied = 0;
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                if (child->isFolder()) continue;  // Skip folders, only copy files

                QString name = QString::fromUtf8(child->getName());
                if (name == "member_info.txt") continue;  // Skip member info file

                m_listener->reset();
                m_api->copyNode(child, dstFolder.get(), m_listener);
                if (m_listener->wait(120000) && m_listener->success) {
                    memberCopied++;
                    totalCopied++;
                } else {
                    qDebug() << "Failed to copy:" << name << "-" << m_listener->errorMsg;
                }
            }
            qDebug() << "Copied" << memberCopied << "files for" << cfg.id << "to" << effectiveDestPath;
        }
        return totalCopied;
    }

    /**
     * Remove duplicate files from November folders for members that had duplicates
     * MEGA allows files with same name, so we keep first and delete rest
     * Uses member configs from JSON file
     */
    int cleanupNovemberDuplicates(const QList<MemberConfig>& memberConfigs) {
        int totalDeleted = 0;
        for (const MemberConfig& cfg : memberConfigs) {
            if (cfg.novemberFolder.isEmpty()) continue;
            QString folderPath = getEffectiveDestPath(cfg.novemberFolder);
            std::unique_ptr<mega::MegaNode> folder(getNodeByPath(folderPath));
            if (!folder || !folder->isFolder()) {
                qDebug() << "Folder not found:" << folderPath;
                continue;
            }

            std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(folder.get()));
            if (!children) continue;

            // Group files by name, keep track of duplicates
            QMap<QString, QList<mega::MegaHandle>> filesByName;
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                if (child->isFolder()) continue;
                QString name = QString::fromUtf8(child->getName());
                filesByName[name].append(child->getHandle());
            }

            // Delete duplicates (keep first, delete rest)
            int folderDeleted = 0;
            for (auto it = filesByName.begin(); it != filesByName.end(); ++it) {
                const QList<mega::MegaHandle>& handles = it.value();
                if (handles.size() > 1) {
                    // Skip first (keep it), delete the rest
                    for (int i = 1; i < handles.size(); i++) {
                        std::unique_ptr<mega::MegaNode> node(m_api->getNodeByHandle(handles[i]));
                        if (node) {
                            m_listener->reset();
                            m_api->remove(node.get(), m_listener);
                            if (m_listener->wait(60000) && m_listener->success) {
                                folderDeleted++;
                                totalDeleted++;
                            }
                        }
                    }
                }
            }
            if (folderDeleted > 0) {
                qDebug() << "Deleted" << folderDeleted << "duplicates from" << folderPath;
            }
        }
        return totalDeleted;
    }

    /**
     * Move all content from previous month folders to current month folders for each member
     * Creates current month folder if it doesn't exist, then moves all files
     * (Renamed from moveNovemberToDecember - now works for any month transition)
     */
    int movePreviousToCurrentMonth(const QList<MemberConfig>& memberConfigs) {
        int totalMoved = 0;
        for (const MemberConfig& cfg : memberConfigs) {
            if (cfg.novemberFolder.isEmpty()) continue;

            // Build dynamic paths based on current date
            QString prevMonthPath = getPathForMonth(cfg.novemberFolder, -1);  // Previous month
            QString currMonthPath = getPathForMonth(cfg.novemberFolder, 0);   // Current month

            qDebug() << "Moving content for" << cfg.id << ":";
            qDebug() << "  From:" << prevMonthPath;
            qDebug() << "  To:" << currMonthPath;

            // Get previous month folder (source)
            std::unique_ptr<mega::MegaNode> srcFolder(getNodeByPath(prevMonthPath));
            if (!srcFolder || !srcFolder->isFolder()) {
                qDebug() << "  Previous month folder not found, skipping";
                continue;
            }

            // Get or create current month folder (destination)
            std::unique_ptr<mega::MegaNode> dstFolder(getNodeByPath(currMonthPath));
            if (!dstFolder) {
                // Create destination folder - get parent path
                QString parentPath = currMonthPath.left(currMonthPath.lastIndexOf('/'));
                QString folderName = currMonthPath.mid(currMonthPath.lastIndexOf('/') + 1);

                std::unique_ptr<mega::MegaNode> parentNode(getNodeByPath(parentPath));
                if (!parentNode) {
                    qDebug() << "  Parent folder not found:" << parentPath;
                    continue;
                }

                qDebug() << "  Creating current month folder:" << folderName;
                m_listener->reset();
                m_api->createFolder(folderName.toUtf8().constData(), parentNode.get(), m_listener);
                if (!m_listener->wait(60000) || !m_listener->success) {
                    qDebug() << "  Failed to create destination folder";
                    continue;
                }

                // Wait a moment for folder to be available
                QThread::msleep(500);
                dstFolder.reset(getNodeByPath(currMonthPath));
                if (!dstFolder) {
                    qDebug() << "  Destination folder created but not found";
                    continue;
                }
            }

            // Get current month number prefix (e.g., "12-" for December)
            QDate today = QDate::currentDate();
            QString monthPrefix = QString("%1-").arg(today.month(), 2, 10, QChar('0'));

            // Get all children in previous month folder
            std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(srcFolder.get()));
            if (!children || children->size() == 0) {
                qDebug() << "  No files in previous month folder";
                continue;
            }

            int memberMoved = 0;
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                QString childName = QString::fromUtf8(child->getName());

                // Only move files for the current month (starting with month prefix)
                if (!childName.startsWith(monthPrefix)) {
                    continue;  // Skip files from other months
                }

                // Move file to current month folder
                m_listener->reset();
                m_api->moveNode(child, dstFolder.get(), m_listener);
                if (m_listener->wait(60000) && m_listener->success) {
                    qDebug() << "  Moved:" << childName;
                    memberMoved++;
                    totalMoved++;
                } else {
                    qDebug() << "  Failed to move:" << childName;
                }
            }

            qDebug() << "  Moved" << memberMoved << "files for" << cfg.id;
        }
        return totalMoved;
    }

    /**
     * Move previous month files back from current month folders to previous month folders
     * This fixes the mistake of moving all files instead of just current month files
     * Only moves files with previous month prefix (e.g., "11-" for November)
     * (Renamed from moveDecemberToNovember - now works for any month transition)
     */
    int moveCurrentToPreviousMonth(const QList<MemberConfig>& memberConfigs) {
        int totalMoved = 0;
        for (const MemberConfig& cfg : memberConfigs) {
            if (cfg.novemberFolder.isEmpty()) continue;

            // Build dynamic paths
            QString prevMonthPath = getPathForMonth(cfg.novemberFolder, -1);  // Previous month
            QString currMonthPath = getPathForMonth(cfg.novemberFolder, 0);   // Current month

            // Get previous month number prefix (e.g., "11-" for November)
            QDate prevDate = QDate::currentDate().addMonths(-1);
            QString prevMonthPrefix = QString("%1-").arg(prevDate.month(), 2, 10, QChar('0'));

            qDebug() << "Moving previous month files back for" << cfg.id << ":";
            qDebug() << "  From:" << currMonthPath;
            qDebug() << "  To:" << prevMonthPath;

            // Get current month folder (source for rollback)
            std::unique_ptr<mega::MegaNode> currFolder(getNodeByPath(currMonthPath));
            if (!currFolder || !currFolder->isFolder()) {
                qDebug() << "  Current month folder not found, skipping";
                continue;
            }

            // Get previous month folder (destination for rollback)
            std::unique_ptr<mega::MegaNode> prevFolder(getNodeByPath(prevMonthPath));
            if (!prevFolder || !prevFolder->isFolder()) {
                qDebug() << "  Previous month folder not found, skipping";
                continue;
            }

            // Get all children in current month folder
            std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(currFolder.get()));
            if (!children || children->size() == 0) {
                qDebug() << "  No files in current month folder";
                continue;
            }

            int memberMoved = 0;
            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                QString childName = QString::fromUtf8(child->getName());

                // Only move previous month files (e.g., "11-" prefix) back
                if (!childName.startsWith(prevMonthPrefix)) {
                    continue;  // Skip current month files, they belong here
                }

                // Move file back to previous month folder
                m_listener->reset();
                m_api->moveNode(child, prevFolder.get(), m_listener);
                if (m_listener->wait(60000) && m_listener->success) {
                    qDebug() << "  Moved back:" << childName;
                    memberMoved++;
                    totalMoved++;
                } else {
                    qDebug() << "  Failed to move:" << childName;
                }
            }

            qDebug() << "  Moved" << memberMoved << "files back for" << cfg.id;
        }
        return totalMoved;
    }

    /**
     * Cleanup theory call source files from timestamped folders
     * Removes files matching pattern from all source folders
     * Uses member configs from JSON file (supports wildcard patterns)
     */
    int cleanupTheoryCallSources(const QString& pattern, const QList<MemberConfig>& memberConfigs) {
        int deleted = 0;
        for (const MemberConfig& cfg : memberConfigs) {
            if (cfg.sourcePattern.isEmpty()) continue;

            // Use pattern matching to find source folder (supports wildcards)
            std::unique_ptr<mega::MegaNode> folder(findFolderByPattern(cfg.sourcePattern));
            if (!folder || !folder->isFolder()) {
                qDebug() << "Folder not found for pattern:" << cfg.sourcePattern;
                continue;
            }

            std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(folder.get()));
            if (!children) continue;

            for (int i = 0; i < children->size(); i++) {
                mega::MegaNode* child = children->get(i);
                if (child->isFolder()) continue;

                QString name = QString::fromUtf8(child->getName());
                if (name.contains(pattern)) {
                    m_listener->reset();
                    m_api->remove(child, m_listener);
                    if (m_listener->wait(60000) && m_listener->success) {
                        qDebug() << "Deleted:" << cfg.sourcePattern << "/" << name;
                        deleted++;
                    }
                }
            }
        }
        return deleted;
    }

    /**
     * Fix AI Summary folders: move file from AI Summary folder to main folder, delete AI Summary folder
     * Works across multiple member folders
     * Uses member configs from JSON file
     */
    int fixAISummaryFolders(const QString& theoryCallBase, const QList<MemberConfig>& memberConfigs) {
        int fixed = 0;
        QString aiSummaryFolder = theoryCallBase + " AI Summary";
        QString mainFolder = theoryCallBase;

        for (const MemberConfig& cfg : memberConfigs) {
            if (cfg.theoryCallsPath.isEmpty()) continue;
            QString aiSummaryPath = cfg.theoryCallsPath + "/" + aiSummaryFolder;
            QString mainFolderPath = cfg.theoryCallsPath + "/" + mainFolder;

            // Get AI Summary folder
            std::unique_ptr<mega::MegaNode> aiFolder(getNodeByPath(aiSummaryPath));
            if (!aiFolder || !aiFolder->isFolder()) {
                qDebug() << "AI Summary folder not found:" << aiSummaryPath;
                continue;
            }

            // Get main folder
            std::unique_ptr<mega::MegaNode> mainFolderNode(getNodeByPath(mainFolderPath));
            if (!mainFolderNode || !mainFolderNode->isFolder()) {
                qDebug() << "Main folder not found:" << mainFolderPath;
                continue;
            }

            // Get children of AI Summary folder and move them
            std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(aiFolder.get()));
            if (children) {
                for (int i = 0; i < children->size(); i++) {
                    mega::MegaNode* child = children->get(i);
                    m_listener->reset();
                    m_api->moveNode(child, mainFolderNode.get(), m_listener);
                    if (m_listener->wait(60000) && m_listener->success) {
                        qDebug() << "Moved:" << child->getName() << "to" << mainFolderPath;
                    }
                }
            }

            // Delete the AI Summary folder
            m_listener->reset();
            m_api->remove(aiFolder.get(), m_listener);
            if (m_listener->wait(60000) && m_listener->success) {
                qDebug() << "Deleted:" << aiSummaryPath;
                fixed++;
            }
        }

        return fixed;
    }

    /**
     * Package multiple files matching a pattern into folders
     * For each file: creates folder with same name (minus extension), copies file into it
     */
    int packageFilesMatching(const QString& sourceFolderPath, const QString& pattern, const QString& destParentPath) {
        std::unique_ptr<mega::MegaNode> sourceFolder(getNodeByPath(sourceFolderPath));
        if (!sourceFolder || !sourceFolder->isFolder()) {
            qDebug() << "Source folder not found:" << sourceFolderPath;
            return -1;
        }

        std::unique_ptr<mega::MegaNodeList> children(m_api->getChildren(sourceFolder.get()));
        if (!children) {
            qDebug() << "Failed to get children";
            return -1;
        }

        // Ensure destination parent exists
        std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(destParentPath));
        if (!destParent) {
            qDebug() << "Cannot access destination:" << destParentPath;
            return -1;
        }

        int packaged = 0;
        for (int i = 0; i < children->size(); i++) {
            mega::MegaNode* child = children->get(i);
            if (child->isFolder()) continue;

            QString name = QString::fromUtf8(child->getName());
            if (!name.contains(pattern)) continue;

            // Get folder name (remove extension)
            QString folderName = name;
            int dotPos = folderName.lastIndexOf('.');
            if (dotPos > 0) {
                folderName = folderName.left(dotPos);
            }

            // Create the new folder
            QString newFolderPath = destParentPath;
            if (!newFolderPath.endsWith('/')) newFolderPath += "/";
            newFolderPath += folderName;

            std::unique_ptr<mega::MegaNode> newFolder(ensureFolderExists(newFolderPath));
            if (!newFolder) {
                qDebug() << "Failed to create folder:" << newFolderPath;
                continue;
            }

            // Copy the file into the new folder
            m_listener->reset();
            m_api->copyNode(child, newFolder.get(), m_listener);
            if (m_listener->wait(60000) && m_listener->success) {
                qDebug() << "Packaged:" << name << "->" << newFolderPath;
                packaged++;
            } else {
                qDebug() << "Failed to copy:" << name << "-" << m_listener->errorMsg;
            }
        }

        return packaged;
    }

    /**
     * Package a file into a folder with the same name (minus extension)
     * Creates: /destParent/filename (folder)/filename.ext (file)
     * Returns the folder path on success
     */
    QString packageFile(const QString& sourceFilePath, const QString& destParentPath) {
        // Get source file
        std::unique_ptr<mega::MegaNode> sourceNode(getNodeByPath(sourceFilePath));
        if (!sourceNode || sourceNode->isFolder()) {
            qDebug() << "Source not found or is a folder:" << sourceFilePath;
            return QString();
        }

        // Get filename and create folder name (remove extension)
        QString fileName = sourceNode->getName() ? QString::fromUtf8(sourceNode->getName()) : "";
        if (fileName.isEmpty()) {
            return QString();
        }

        QString folderName = fileName;
        int dotPos = folderName.lastIndexOf('.');
        if (dotPos > 0) {
            folderName = folderName.left(dotPos);
        }

        // Ensure destination parent exists
        std::unique_ptr<mega::MegaNode> destParent(ensureFolderExists(destParentPath));
        if (!destParent) {
            qDebug() << "Cannot access destination:" << destParentPath;
            return QString();
        }

        // Create the new folder
        QString newFolderPath = destParentPath;
        if (!newFolderPath.endsWith('/')) newFolderPath += "/";
        newFolderPath += folderName;

        std::unique_ptr<mega::MegaNode> newFolder(ensureFolderExists(newFolderPath));
        if (!newFolder) {
            qDebug() << "Failed to create folder:" << newFolderPath;
            return QString();
        }

        // Copy the file into the new folder
        m_listener->reset();
        m_api->copyNode(sourceNode.get(), newFolder.get(), m_listener);
        if (!m_listener->wait(60000) || !m_listener->success) {
            qDebug() << "Copy failed:" << m_listener->errorMsg;
            return QString();
        }

        qDebug() << "Packaged:" << sourceFilePath << "->" << newFolderPath << "/" << fileName;
        return newFolderPath;
    }

    mega::MegaNode* ensureFolderExists(const QString& path) {
        if (!m_api || path.isEmpty()) {
            return nullptr;
        }

        // Try to get existing node
        mega::MegaNode* node = m_api->getNodeByPath(path.toUtf8().constData());
        if (node) {
            return node;
        }

        // Split path and create folders
        QStringList components = path.split('/', Qt::SkipEmptyParts);
        std::unique_ptr<mega::MegaNode> currentNode(m_api->getRootNode());
        if (!currentNode) {
            return nullptr;
        }

        for (const QString& component : components) {
            std::unique_ptr<mega::MegaNode> childNode(
                m_api->getChildNode(currentNode.get(), component.toUtf8().constData())
            );

            if (childNode && childNode->isFolder()) {
                currentNode = std::move(childNode);
            } else if (!childNode) {
                // Create folder
                m_listener->reset();
                m_api->createFolder(component.toUtf8().constData(), currentNode.get(), m_listener);

                if (!m_listener->wait(30000)) {
                    return nullptr;
                }

                if (!m_listener->success) {
                    return nullptr;
                }

                // Get the newly created folder
                currentNode.reset(m_api->getChildNode(currentNode.get(), component.toUtf8().constData()));
                if (!currentNode) {
                    return nullptr;
                }
            } else {
                // Component exists but is not a folder
                return nullptr;
            }
        }

        return currentNode.release();
    }

private:
    mega::MegaApi* m_api;
    MegaOpsListener* m_listener;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("mega_ops");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("MEGA Operations CLI");
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument("command", "Command: mkdir, cp, package, rm");
    parser.addPositionalArgument("args", "Command arguments");

    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(1);
    }

    QString apiKey = qgetenv("MEGA_API_KEY");
    if (apiKey.isEmpty()) {
        apiKey = "9gETCbhB";
    }

    MegaOps ops(apiKey);
    if (!ops.restoreSession()) {
        std::cerr << "Failed to restore session. Please login via GUI first." << std::endl;
        return 1;
    }

    QString cmd = args[0];

    if (cmd == "mkdir" && args.size() >= 2) {
        QString fullPath = args[1];
        int lastSlash = fullPath.lastIndexOf('/');
        QString parentPath = fullPath.left(lastSlash);
        QString folderName = fullPath.mid(lastSlash + 1);

        if (parentPath.isEmpty()) parentPath = "/";

        return ops.createFolder(parentPath, folderName) ? 0 : 1;

    } else if (cmd == "cp" && args.size() >= 3) {
        QString source = args[1];
        QString dest = args[2];
        return ops.copyNode(source, dest) ? 0 : 1;

    } else if (cmd == "package" && args.size() >= 3) {
        // Package file into folder: package /source/file.mp4 /dest/parent/
        // Creates: /dest/parent/file/file.mp4
        QString sourceFile = args[1];
        QString destParent = args[2];
        QString result = ops.packageFile(sourceFile, destParent);
        if (result.isEmpty()) {
            std::cerr << "Package failed" << std::endl;
            return 1;
        }
        std::cout << "Created folder: " << result.toStdString() << std::endl;
        return 0;

    } else if (cmd == "rm" && args.size() >= 2) {
        QString path = args[1];
        return ops.removeNode(path) ? 0 : 1;

    } else if (cmd == "mv" && args.size() >= 3) {
        QString path = args[1];
        QString newName = args[2];
        return ops.renameNode(path, newName) ? 0 : 1;

    } else if (cmd == "bulk-rename" && args.size() >= 3) {
        // bulk-rename /folder/path "string_to_remove"
        QString folderPath = args[1];
        QString removeStr = args[2];
        int count = ops.bulkRenameRemove(folderPath, removeStr);
        if (count < 0) {
            std::cerr << "Bulk rename failed" << std::endl;
            return 1;
        }
        std::cout << "Renamed " << count << " files" << std::endl;
        return 0;

    } else if (cmd == "bulk-package" && args.size() >= 4) {
        // bulk-package /source/folder "pattern" /dest/parent
        // Packages all files matching pattern into folders at destination
        QString sourceFolder = args[1];
        QString pattern = args[2];
        QString destParent = args[3];
        int count = ops.packageFilesMatching(sourceFolder, pattern, destParent);
        if (count < 0) {
            std::cerr << "Bulk package failed" << std::endl;
            return 1;
        }
        std::cout << "Packaged " << count << " files" << std::endl;
        return 0;

    } else if (cmd == "fix-ai-summary" && args.size() >= 2) {
        // fix-ai-summary "Theory Call Base Name"
        // Fixes AI Summary folders for all members - moves file to main folder, deletes AI Summary folder
        QString theoryCallBase = args[1];

        // Load member configs from JSON file
        QList<MemberConfig> memberConfigs = loadMemberConfigs();

        int fixed = ops.fixAISummaryFolders(theoryCallBase, memberConfigs);
        std::cout << "Fixed " << fixed << " AI Summary folders" << std::endl;
        return 0;

    } else if (cmd == "ls" && args.size() >= 2) {
        // ls /path/to/folder
        QString folderPath = args[1];
        QStringList contents = ops.listFolder(folderPath);
        if (contents.isEmpty()) {
            std::cout << "Folder is empty or not found" << std::endl;
        } else {
            for (const QString& item : contents) {
                std::cout << item.toStdString() << std::endl;
            }
        }
        return 0;

    } else if (cmd == "cleanup-theory-sources" && args.size() >= 2) {
        // cleanup-theory-sources "pattern"
        // Removes files matching pattern from all timestamped source folders
        QString pattern = args[1];

        // Load member configs from JSON file
        QList<MemberConfig> memberConfigs = loadMemberConfigs();

        int deleted = ops.cleanupTheoryCallSources(pattern, memberConfigs);
        std::cout << "Deleted " << deleted << " files" << std::endl;
        return 0;

    } else if (cmd == "copy-to-november") {
        // copy-to-november
        // Copies all files from each member's timestamped folder to their November folder
        std::cout << "Copying files from timestamped folders to November folders..." << std::endl;

        // Load member configs from JSON file
        QList<MemberConfig> memberConfigs = loadMemberConfigs();

        int copied = ops.copyToNovemberFolders(memberConfigs);
        std::cout << "Total files copied: " << copied << std::endl;
        return 0;

    } else if (cmd == "cleanup-november-duplicates") {
        // cleanup-november-duplicates
        // Removes duplicate files from November folders (keeps first, deletes rest)
        std::cout << "Cleaning up duplicate files from November folders..." << std::endl;

        // Load member configs from JSON file
        QList<MemberConfig> memberConfigs = loadMemberConfigs();

        int deleted = ops.cleanupNovemberDuplicates(memberConfigs);
        std::cout << "Total duplicates deleted: " << deleted << std::endl;
        return 0;

    } else if (cmd == "move-november-to-december") {
        // move-november-to-december (legacy) or move-prev-to-curr
        // Moves current month files from previous month folders to current month folders
        QDate today = QDate::currentDate();
        QString currMonth = QLocale(QLocale::English).monthName(today.month());
        QString prevMonth = QLocale(QLocale::English).monthName(today.addMonths(-1).month());
        std::cout << "Moving " << currMonth.toStdString() << " files from "
                  << prevMonth.toStdString() << " to " << currMonth.toStdString() << " folders..." << std::endl;

        // Load member configs from JSON file
        QList<MemberConfig> memberConfigs = loadMemberConfigs();

        int moved = ops.movePreviousToCurrentMonth(memberConfigs);
        std::cout << "Total files moved: " << moved << std::endl;
        return 0;

    } else if (cmd == "move-december-to-november" || cmd == "move-curr-to-prev") {
        // move-december-to-november (legacy) or move-curr-to-prev (REVERSE/FIX)
        // Moves previous month files back from current month folders to previous month folders
        QDate today = QDate::currentDate();
        QString currMonth = QLocale(QLocale::English).monthName(today.month());
        QString prevMonth = QLocale(QLocale::English).monthName(today.addMonths(-1).month());
        std::cout << "Moving " << prevMonth.toStdString() << " files back from "
                  << currMonth.toStdString() << " to " << prevMonth.toStdString() << " folders..." << std::endl;

        // Load member configs from JSON file
        QList<MemberConfig> memberConfigs = loadMemberConfigs();

        int moved = ops.moveCurrentToPreviousMonth(memberConfigs);
        std::cout << "Total files moved back: " << moved << std::endl;
        return 0;

    } else if (cmd == "init-config") {
        // init-config
        // Creates the default member config file for editing
        saveDefaultMemberConfigs();
        QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        std::cout << "Created default config at: " << (configPath + "/MegaCustom/mega_ops_members.json").toStdString() << std::endl;
        std::cout << "Edit this file to customize member paths and patterns." << std::endl;
        return 0;

    } else {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  mega_ops mkdir /path/to/new/folder" << std::endl;
        std::cerr << "  mega_ops ls /path/to/folder" << std::endl;
        std::cerr << "  mega_ops cp /source/path /dest/folder" << std::endl;
        std::cerr << "  mega_ops package /source/file.mp4 /dest/parent/" << std::endl;
        std::cerr << "  mega_ops rm /path/to/delete" << std::endl;
        std::cerr << "  mega_ops mv /path/to/file newname.ext" << std::endl;
        std::cerr << "  mega_ops bulk-rename /folder/path \"_watermarked\"" << std::endl;
        std::cerr << "  mega_ops bulk-package /source/folder \"pattern\" /dest/parent" << std::endl;
        std::cerr << "  mega_ops fix-ai-summary \"Theory Call Base Name\"" << std::endl;
        std::cerr << "  mega_ops cleanup-theory-sources \"pattern\"" << std::endl;
        std::cerr << "  mega_ops copy-to-november" << std::endl;
        std::cerr << "  mega_ops cleanup-november-duplicates" << std::endl;
        std::cerr << "  mega_ops move-november-to-december     # Move Dec files (12-*) from Nov to Dec" << std::endl;
        std::cerr << "  mega_ops move-december-to-november     # Move Nov files (11-*) back to Nov (fix)" << std::endl;
        std::cerr << "  mega_ops init-config                   # Create editable config file" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Config: ~/.config/MegaCustom/mega_ops_members.json" << std::endl;
        std::cerr << "  Member operations use this config file. Run 'init-config' to create it." << std::endl;
        std::cerr << "  Patterns support wildcards (*) e.g. /latest-wm/Icekkk_*" << std::endl;
        return 1;
    }

    return 0;
}

#include "mega_ops.moc"
