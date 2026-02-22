#include "SessionPool.h"
#include "CredentialStore.h"
#include "../utils/Constants.h"
#include <megaapi.h>
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QStandardPaths>
#include <QDir>
#include <QMutexLocker>
#include <QMetaObject>
#include <QTimer>
#include <algorithm>

namespace MegaCustom {

// User agent for MEGA SDK
static const char* MEGA_USER_AGENT = "MegaCustomApp/1.0";

/**
 * Async login handler - chains fastLogin → fetchNodes without blocking.
 * Lives as a QObject so signals from the MEGA SDK thread are safely
 * delivered to the main thread via queued connections.
 */
class AsyncLoginHandler : public QObject, public mega::MegaRequestListener {
    Q_OBJECT
public:
    enum Phase { PhaseLogin, PhaseFetchNodes };

    AsyncLoginHandler(const QString& accountId, mega::MegaApi* api, QObject* parent)
        : QObject(parent), m_accountId(accountId), m_api(api), m_phase(PhaseLogin)
    {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, &AsyncLoginHandler::onTimeout);
    }

    void startLogin(const QString& sessionToken) {
        qDebug() << "AsyncLoginHandler: Starting fastLogin for" << m_accountId;
        m_timer.start(120000); // 120s timeout for login
        m_api->fastLogin(sessionToken.toUtf8().constData(), this);
    }

    // Called from MEGA SDK internal thread — must not touch Qt objects directly
    void onRequestFinish(mega::MegaApi*, mega::MegaRequest*, mega::MegaError* e) override {
        int errorCode = e->getErrorCode();
        QString errorStr = QString::fromUtf8(e->getErrorString());
        Phase phase = m_phase;

        // Post result back to main thread
        QMetaObject::invokeMethod(this, [this, phase, errorCode, errorStr]() {
            handleResult(phase, errorCode, errorStr);
        }, Qt::QueuedConnection);
    }

signals:
    void loginComplete(const QString& accountId);
    void loginFailed(const QString& accountId, const QString& error, bool expired);

private slots:
    void onTimeout() {
        m_api->cancelTransfers(mega::MegaTransfer::TYPE_DOWNLOAD);
        QString phase = (m_phase == PhaseLogin) ? "Login" : "Fetch nodes";
        qWarning() << "AsyncLoginHandler:" << phase << "timeout for" << m_accountId;
        emit loginFailed(m_accountId, phase + " timeout", m_phase == PhaseLogin);
    }

private:
    void handleResult(Phase phase, int errorCode, const QString& errorStr) {
        m_timer.stop();

        if (phase == PhaseLogin) {
            if (errorCode == mega::MegaError::API_OK) {
                qDebug() << "AsyncLoginHandler: fastLogin OK, fetching nodes for" << m_accountId;
                m_phase = PhaseFetchNodes;
                m_timer.start(180000); // 180s for fetchNodes
                m_api->fetchNodes(this);
            } else {
                qWarning() << "AsyncLoginHandler: fastLogin failed for" << m_accountId << "-" << errorStr;
                emit loginFailed(m_accountId, errorStr, true);
            }
        } else { // PhaseFetchNodes
            if (errorCode == mega::MegaError::API_OK) {
                qDebug() << "AsyncLoginHandler: fetchNodes OK for" << m_accountId;
                emit loginComplete(m_accountId);
            } else {
                qWarning() << "AsyncLoginHandler: fetchNodes failed for" << m_accountId << "-" << errorStr;
                emit loginFailed(m_accountId, errorStr, false);
            }
        }
    }

    QString m_accountId;
    mega::MegaApi* m_api;
    Phase m_phase;
    QTimer m_timer;
};

SessionPool::SessionPool(CredentialStore* credentialStore, QObject* parent)
    : QObject(parent)
    , m_credentialStore(credentialStore)
    , m_maxSessions(5)
{
    // Connect to credential store signals
    connect(m_credentialStore, &CredentialStore::sessionLoaded,
            this, &SessionPool::onSessionLoaded);
    connect(m_credentialStore, &CredentialStore::error,
            this, &SessionPool::onCredentialError);
}

SessionPool::~SessionPool()
{
    // Clean up any in-flight login handlers
    for (auto* handler : m_loginHandlers) {
        delete handler;
    }
    m_loginHandlers.clear();

    releaseAllSessions(true);
}

mega::MegaApi* SessionPool::getSession(const QString& accountId, const QString& sessionToken)
{
    mega::MegaApi* resultApi = nullptr;
    bool needsRefresh = false;
    bool needsLogin = false;
    bool needsCredentialLoad = false;

    // First, check pool state under lock
    {
        QMutexLocker locker(&m_poolMutex);

        if (accountId.isEmpty()) {
            emit sessionError(accountId, "Invalid account ID");
            return nullptr;
        }

        // Check if session already exists in pool
        if (m_pool.contains(accountId)) {
            CachedSession& session = m_pool[accountId];
            session.lastUsed = QDateTime::currentDateTime();

            if (session.isConnected && session.api) {
                qDebug() << "SessionPool: Returning cached session for" << accountId;
                return session.api;
            }

            // Session exists but not connected - try to reconnect
            if (session.api && !session.isLoggingIn) {
                qDebug() << "SessionPool: Reconnecting session for" << accountId;
                needsRefresh = true;
            }

            resultApi = session.api;
        } else {
            // Need to create new session
            qDebug() << "SessionPool: Creating new session for" << accountId;

            // Check if we need to evict old sessions (will be done outside lock)
            if (m_pool.size() >= m_maxSessions) {
                // Find LRU to evict later
                // For now, just proceed - eviction will happen on next call if needed
            }

            // Create new session entry
            CachedSession newSession;
            newSession.api = createApiInstance(accountId);
            newSession.lastUsed = QDateTime::currentDateTime();
            newSession.isConnected = false;
            newSession.isLoggingIn = true;

            if (!newSession.api) {
                emit sessionError(accountId, "Failed to create MegaApi instance");
                return nullptr;
            }

            m_pool[accountId] = newSession;
            resultApi = newSession.api;

            // Determine what login action is needed
            if (!sessionToken.isEmpty()) {
                needsLogin = true;
            } else {
                needsCredentialLoad = true;
                m_pendingAccountId = accountId;
            }
        }
    }
    // Lock released here

    // Perform actions that need the lock to be released
    if (needsRefresh) {
        refreshSession(accountId);
    } else if (needsLogin) {
        performLogin(accountId, sessionToken);
    } else if (needsCredentialLoad) {
        m_credentialStore->loadSession(accountId);
    }

    return resultApi;
}

bool SessionPool::isSessionActive(const QString& accountId) const
{
    QMutexLocker locker(&m_poolMutex);

    if (!m_pool.contains(accountId)) {
        return false;
    }
    const CachedSession& session = m_pool[accountId];
    return session.isConnected && session.api != nullptr;
}

bool SessionPool::hasSession(const QString& accountId) const
{
    QMutexLocker locker(&m_poolMutex);
    return m_pool.contains(accountId);
}

void SessionPool::refreshSession(const QString& accountId)
{
    QMutexLocker locker(&m_poolMutex);

    if (!m_pool.contains(accountId)) {
        emit sessionError(accountId, "Session not found in pool");
        return;
    }

    CachedSession& session = m_pool[accountId];
    if (session.isLoggingIn) {
        qDebug() << "SessionPool: Already logging in for" << accountId;
        return;
    }

    session.isLoggingIn = true;
    session.isConnected = false;

    // Load credentials and re-login
    m_pendingAccountId = accountId;
    m_credentialStore->loadSession(accountId);
}

void SessionPool::releaseSession(const QString& accountId, bool keepCredentials)
{
    QMutexLocker locker(&m_poolMutex);

    if (!m_pool.contains(accountId)) {
        return;
    }

    qDebug() << "SessionPool: Releasing session for" << accountId;

    CachedSession& session = m_pool[accountId];
    cleanupSession(session);
    m_pool.remove(accountId);

    if (!keepCredentials) {
        m_credentialStore->deleteSession(accountId);
    }

    emit sessionReleased(accountId);
}

void SessionPool::releaseAllSessions(bool keepCredentials)
{
    QMutexLocker locker(&m_poolMutex);

    qDebug() << "SessionPool: Releasing all sessions";

    for (auto it = m_pool.begin(); it != m_pool.end(); ++it) {
        cleanupSession(it.value());
        if (!keepCredentials) {
            m_credentialStore->deleteSession(it.key());
        }
    }
    m_pool.clear();
}

void SessionPool::setMaxSessions(int max)
{
    QMutexLocker locker(&m_poolMutex);

    m_maxSessions = qMax(1, max);

    // Evict excess sessions if needed
    while (m_pool.size() > m_maxSessions) {
        locker.unlock();  // Unlock before calling releaseSession which also locks
        evictLeastRecentlyUsed();
        locker.relock();
    }
}

int SessionPool::sessionCount() const
{
    QMutexLocker locker(&m_poolMutex);
    return m_pool.size();
}

QStringList SessionPool::activeAccountIds() const
{
    QMutexLocker locker(&m_poolMutex);

    QStringList ids;
    for (auto it = m_pool.constBegin(); it != m_pool.constEnd(); ++it) {
        if (it.value().isConnected) {
            ids.append(it.key());
        }
    }
    return ids;
}

void SessionPool::touchSession(const QString& accountId)
{
    QMutexLocker locker(&m_poolMutex);

    if (m_pool.contains(accountId)) {
        m_pool[accountId].lastUsed = QDateTime::currentDateTime();
    }
}

bool SessionPool::waitForSession(const QString& accountId, int timeoutMs)
{
    // First, ensure we have a session (this may start the login process)
    mega::MegaApi* api = getSession(accountId);
    if (!api) {
        qWarning() << "SessionPool::waitForSession: No session for" << accountId;
        return false;
    }

    // If already connected, return immediately
    if (isSessionActive(accountId)) {
        qDebug() << "SessionPool::waitForSession: Session already active for" << accountId;
        return true;
    }

    // Wait for session to become active
    qDebug() << "SessionPool::waitForSession: Waiting for session" << accountId << "(timeout:" << timeoutMs << "ms)";

    int waited = 0;
    int interval = 100;
    while (waited < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, interval);
        QThread::msleep(interval);
        waited += interval;

        // Check if session is now active
        if (isSessionActive(accountId)) {
            qDebug() << "SessionPool::waitForSession: Session became active after" << waited << "ms";
            return true;
        }

        // Check if session no longer exists (error occurred)
        {
            QMutexLocker locker(&m_poolMutex);
            if (!m_pool.contains(accountId)) {
                qWarning() << "SessionPool::waitForSession: Session was removed for" << accountId;
                return false;
            }

            // Check if login failed (not logging in anymore, but still not connected)
            const CachedSession& session = m_pool[accountId];
            if (!session.isLoggingIn && !session.isConnected) {
                qWarning() << "SessionPool::waitForSession: Login failed for" << accountId;
                return false;
            }
        }
    }

    qWarning() << "SessionPool::waitForSession: Timeout waiting for session" << accountId;
    return false;
}

void SessionPool::onSessionLoaded(const QString& accountId, const QString& sessionToken)
{
    if (accountId != m_pendingAccountId) {
        return;
    }

    m_pendingAccountId.clear();
    performLogin(accountId, sessionToken);
}

void SessionPool::onCredentialError(const QString& accountId, const QString& errorMessage)
{
    if (accountId != m_pendingAccountId) {
        return;
    }

    m_pendingAccountId.clear();

    {
        QMutexLocker locker(&m_poolMutex);
        if (m_pool.contains(accountId)) {
            CachedSession& session = m_pool[accountId];
            session.isLoggingIn = false;
        }
    }

    qDebug() << "SessionPool: Credential error for" << accountId << "-" << errorMessage;
    emit loginRequired(accountId);
}

void SessionPool::evictLeastRecentlyUsed()
{
    QString lruAccountId;

    // Find the least recently used session (locked scope)
    {
        QMutexLocker locker(&m_poolMutex);
        if (m_pool.isEmpty()) {
            return;
        }

        QDateTime oldestTime = QDateTime::currentDateTime();
        for (auto it = m_pool.constBegin(); it != m_pool.constEnd(); ++it) {
            if (it.value().lastUsed < oldestTime) {
                oldestTime = it.value().lastUsed;
                lruAccountId = it.key();
            }
        }
    }

    // Release session outside of lock (releaseSession has its own lock)
    if (!lruAccountId.isEmpty()) {
        qDebug() << "SessionPool: Evicting LRU session:" << lruAccountId;
        releaseSession(lruAccountId, true);  // Keep credentials for later
    }
}

mega::MegaApi* SessionPool::createApiInstance(const QString& accountId)
{
    // Create per-account cache directory for node caching
    // This is CRITICAL - without a valid basePath, the SDK disables local node caching
    // and re-downloads the entire filesystem tree on every restart
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + "/mega_cache/" + accountId;
    QDir().mkpath(cachePath);

    qDebug() << "SessionPool: Creating MegaApi with cache path:" << cachePath;

    // Create MegaApi instance with API key and cache path
    mega::MegaApi* api = new mega::MegaApi(
        Constants::MEGA_API_KEY,
        cachePath.toUtf8().constData(),  // Enable node caching
        MEGA_USER_AGENT
    );

    if (!api) {
        qWarning() << "SessionPool: Failed to create MegaApi instance";
        return nullptr;
    }

    return api;
}

void SessionPool::performLogin(const QString& accountId, const QString& sessionToken)
{
    mega::MegaApi* api = nullptr;

    // Lock to validate session and get api pointer
    {
        QMutexLocker locker(&m_poolMutex);
        if (!m_pool.contains(accountId)) {
            emit sessionError(accountId, "Session not found in pool");
            return;
        }

        CachedSession& session = m_pool[accountId];
        if (!session.api) {
            session.isLoggingIn = false;
            emit sessionError(accountId, "MegaApi instance is null");
            return;
        }
        api = session.api;
    }

    qDebug() << "SessionPool: Performing async fastLogin for" << accountId;

    // Clean up any previous handler for this account
    if (m_loginHandlers.contains(accountId)) {
        m_loginHandlers[accountId]->deleteLater();
        m_loginHandlers.remove(accountId);
    }

    // Create async handler — non-blocking, chains login → fetchNodes via signals
    auto* handler = new AsyncLoginHandler(accountId, api, this);
    m_loginHandlers[accountId] = handler;

    connect(handler, &AsyncLoginHandler::loginComplete,
            this, &SessionPool::onAsyncLoginComplete);
    connect(handler, &AsyncLoginHandler::loginFailed,
            this, &SessionPool::onAsyncLoginFailed);

    handler->startLogin(sessionToken);
    // Returns immediately — UI stays responsive
}

void SessionPool::onAsyncLoginComplete(const QString& accountId)
{
    // Clean up handler
    if (m_loginHandlers.contains(accountId)) {
        m_loginHandlers[accountId]->deleteLater();
        m_loginHandlers.remove(accountId);
    }

    // Verify we have nodes
    mega::MegaApi* api = nullptr;
    {
        QMutexLocker locker(&m_poolMutex);
        if (!m_pool.contains(accountId)) {
            emit sessionError(accountId, "Session removed during login");
            return;
        }
        api = m_pool[accountId].api;
    }

    mega::MegaNode* rootNode = api ? api->getRootNode() : nullptr;
    if (!rootNode) {
        {
            QMutexLocker locker(&m_poolMutex);
            if (m_pool.contains(accountId)) {
                CachedSession& session = m_pool[accountId];
                session.isLoggingIn = false;
                session.isConnected = false;
            }
        }
        emit sessionError(accountId, "Failed to get root node after login");
        return;
    }
    delete rootNode;

    // Success — mark session as connected
    {
        QMutexLocker locker(&m_poolMutex);
        if (m_pool.contains(accountId)) {
            CachedSession& session = m_pool[accountId];
            session.isLoggingIn = false;
            session.isConnected = true;
            session.lastUsed = QDateTime::currentDateTime();
        }
    }

    qDebug() << "SessionPool: Session ready for" << accountId;
    emit sessionCreated(accountId);
    emit sessionReady(accountId);
}

void SessionPool::onAsyncLoginFailed(const QString& accountId, const QString& error, bool expired)
{
    // Clean up handler
    if (m_loginHandlers.contains(accountId)) {
        m_loginHandlers[accountId]->deleteLater();
        m_loginHandlers.remove(accountId);
    }

    {
        QMutexLocker locker(&m_poolMutex);
        if (m_pool.contains(accountId)) {
            CachedSession& session = m_pool[accountId];
            session.isLoggingIn = false;
            session.isConnected = false;
        }
    }

    qWarning() << "SessionPool: Login failed for" << accountId << "-" << error;
    emit sessionError(accountId, error);
    if (expired) {
        emit sessionExpired(accountId);
    }
}

void SessionPool::cleanupSession(CachedSession& session)
{
    if (session.api) {
        // Logout and delete
        if (session.isConnected) {
            session.api->localLogout();
        }
        delete session.api;
        session.api = nullptr;
    }
    session.isConnected = false;
    session.isLoggingIn = false;
}

} // namespace MegaCustom

// Required for Q_OBJECT class defined in .cpp file
#include "SessionPool.moc"
