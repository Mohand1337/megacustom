#ifndef MEGA_MOCK_H
#define MEGA_MOCK_H

#ifndef MEGA_SDK_AVAILABLE

#include <string>
#include <vector>
#include <iostream>
#include <cstring>
#include <cstdint>

namespace mega {

typedef uint64_t MegaHandle;
const MegaHandle INVALID_HANDLE = -1;

class MegaError {
public:
    static const int API_OK = 0;
    static const int API_EINTERNAL = -1;
    static const int API_EARGS = -2;
    static const int API_EAGAIN = -3;
    static const int API_ERATELIMIT = -4;
    static const int API_EFAILED = -5;
    static const int API_ETOOMANY = -6;
    static const int API_ERANGE = -7;
    static const int API_EEXPIRED = -8;
    static const int API_ENOENT = -9;
    static const int API_ECIRCULAR = -10;
    static const int API_EACCESS = -11;
    static const int API_EEXIST = -12;
    static const int API_EINCOMPLETE = -13;
    static const int API_EKEY = -14;
    static const int API_ESID = -15;
    static const int API_EBLOCKED = -16;
    static const int API_EOVERQUOTA = -17;
    static const int API_ETEMPUNAVAIL = -18;
    static const int API_ETOOMANYCONNECTIONS = -19;
    static const int API_EWRITE = -20;
    static const int API_EREAD = -21;
    static const int API_EAPPKEY = -22;
    static const int API_ESSL = -23;
    static const int API_EGOINGOVERQUOTA = -24;
    static const int API_EMFAREQUIRED = -26;
    static const int API_EMASTERONLY = -27;
    static const int API_EBUSINESSPASTDUE = -28;
    static const int API_EPAYWALL = -29;

    int getErrorCode() const { return API_OK; }
    const char* getErrorString() const { return "Mock Error"; }
};

class MegaRequest {
public:
    int getType() const { return 0; }
    MegaHandle getNodeHandle() const { return 0; }
    const char* getLink() const { return "https://mega.nz/mock-link"; }
};

class MegaTransfer {
public:
    static const int STATE_PAUSED = 5;
    static const int TYPE_UPLOAD = 0;
    static const int TYPE_DOWNLOAD = 1;
    static const int COLLISION_CHECK_FINGERPRINT = 1;
    static const int COLLISION_RESOLUTION_OVERWRITE = 1;

    int getType() const { return 0; }
    long long getTotalBytes() const { return 0; }
    long long getTransferredBytes() const { return 0; }
    int getTag() const { return 0; }
    const char* getFileName() const { return "mock_file"; }
    long long getSpeed() const { return 0; }
    int getState() const { return 0; }
    const char* getPath() const { return "/mock/path"; }
};

class MegaTransferList {
public:
    virtual ~MegaTransferList() {}
    virtual int size() const { return 0; }
    virtual MegaTransfer* get(int i) const { return nullptr; }
};

class MegaNode {
public:
    static const int TYPE_UNKNOWN = -1;
    static const int TYPE_FILE = 0;
    static const int TYPE_FOLDER = 1;
    static const int TYPE_ROOT = 2;
    static const int TYPE_INCOMING = 3;
    static const int TYPE_RUBBISH = 4;

    virtual ~MegaNode() {}
    virtual bool isFolder() const { return true; }
    virtual bool isFile() const { return !isFolder(); }
    virtual const char* getName() const { return "MockNode"; }
    virtual MegaHandle getHandle() const { return 0; }
    virtual int64_t getSize() const { return 0; }
    virtual MegaNode* copy() const { return new MegaNode(*this); }
    virtual MegaHandle getOwner() const { return 0; }
    virtual int getType() const { return TYPE_FOLDER; }
    virtual int64_t getCreationTime() const { return 0; }
    virtual int64_t getModificationTime() const { return 0; }
    virtual bool isShared() const { return false; }
    virtual bool isInShare() const { return false; }
    virtual bool isOutShare() const { return false; }
};

class MegaUser {
public:
    virtual ~MegaUser() {}
    virtual const char* getEmail() const { return "mock@example.com"; }
};

class MegaNodeList {
public:
    virtual ~MegaNodeList() {}
    virtual int size() const { return 0; }
    virtual MegaNode* get(int i) const { return nullptr; }
};

class MegaShare {
public:
    static const int ACCESS_UNKNOWN = -1;
    static const int ACCESS_READ = 0;
    static const int ACCESS_READWRITE = 1;
    static const int ACCESS_FULL = 2;
    static const int ACCESS_OWNER = 3;

    int getAccess() const { return ACCESS_READ; }
    const char* getUser() const { return "mock@user.com"; }
};

class MegaShareList {
public:
    virtual ~MegaShareList() {}
    virtual int size() const { return 0; }
    virtual MegaShare* get(int i) const { return nullptr; }
};

class MegaSync {
public:
    virtual ~MegaSync() {}
};

class MegaSyncStats {
public:
    virtual ~MegaSyncStats() {}
};

class MegaApi;

class MegaRequestListener {
public:
    virtual ~MegaRequestListener() {}
    virtual void onRequestFinish(MegaApi* api, MegaRequest* request, MegaError* error) {}
};

class MegaTransferListener {
public:
    virtual ~MegaTransferListener() {}
    virtual void onTransferStart(MegaApi* api, MegaTransfer* transfer) {}
    virtual void onTransferUpdate(MegaApi* api, MegaTransfer* transfer) {}
    virtual void onTransferFinish(MegaApi* api, MegaTransfer* transfer, MegaError* error) {}
    virtual void onTransferTemporaryError(MegaApi* api, MegaTransfer* transfer, MegaError* error) {}
};

class MegaGlobalListener {
public:
    virtual ~MegaGlobalListener() {}
};

class MegaListener : public MegaRequestListener, public MegaTransferListener, public MegaGlobalListener {
public:
    virtual ~MegaListener() {}
    virtual void onSyncFileStateChanged(MegaApi*, MegaSync*, std::string*, int) {}
    virtual void onSyncAdded(MegaApi*, MegaSync*) {}
    virtual void onSyncDeleted(MegaApi*, MegaSync*) {}
    virtual void onSyncStateChanged(MegaApi*, MegaSync*) {}
    virtual void onSyncStatsUpdated(MegaApi*, MegaSyncStats*) {}
    virtual void onGlobalSyncStateChanged(MegaApi*) {}
    virtual void onSyncRemoteRootChanged(MegaApi*, MegaSync*) {}
};

class MegaApi {
public:
    static const int ORDER_NONE = 0;

    MegaApi(const char* appKey, const char* basePath = nullptr, const char* userAgent = nullptr) {}
    virtual ~MegaApi() {}

    void addListener(MegaListener* listener) {}
    void removeListener(MegaListener* listener) {}

    void login(const char* email, const char* password, MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }

    void fastLogin(const char* session, MegaRequestListener* listener = nullptr) {
         if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }

    void multiFactorAuthLogin(const char* email, const char* password, const char* pin, MegaRequestListener* listener = nullptr) {
         if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }

    void logout(MegaRequestListener* listener = nullptr) {}
    void localLogout(MegaRequestListener* listener = nullptr) {}

    bool isLoggedIn() { return true; }
    char* dumpSession() {
        char* session = new char[10];
        strcpy(session, "mock_key");
        return session;
    }

    MegaNode* getRootNode() { return new MegaNode(); }
    MegaNode* getNodeByPath(const char* path) { return new MegaNode(); }
    MegaNode* getChildNode(MegaNode* parent, const char* name) { return new MegaNode(); }
    MegaNode* getParentNode(MegaNode* node) { return new MegaNode(); }
    MegaNode* getNodeByHandle(MegaHandle handle) { return new MegaNode(); }
    MegaUser* getMyUser() { return new MegaUser(); }
    char* getNodePath(MegaNode* node) {
        char* p = new char[20];
        strcpy(p, "/mock/path");
        return p;
    }

    void fetchNodes(MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }

    int getNumChildren(MegaNode* parent) { return 0; }
    MegaNodeList* getChildren(MegaNode* parent, int order = 0) { return new MegaNodeList(); }

    void createAccount(const char* email, const char* password, const char* name, const char* authCode, MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }

    void confirmAccount(const char* link, const char* password, MegaRequestListener* listener = nullptr) {}
    void resetPassword(const char* email, bool hasMasterKey, MegaRequestListener* listener = nullptr) {}
    void confirmResetPassword(const char* link, const char* newPassword, const char* masterKey, MegaRequestListener* listener = nullptr) {}

    void changePassword(const char* oldPassword, const char* newPassword, MegaRequestListener* listener = nullptr) {}

    void multiFactorAuthGetCode(MegaRequestListener* listener = nullptr) {}
    void multiFactorAuthDisable(const char* pin, MegaRequestListener* listener = nullptr) {}
    bool multiFactorAuthAvailable() { return false; }

    void setLogLevel(int level) {}
    const char* getMyEmail() { return "mock@example.com"; }

    void renameNode(MegaNode* node, const char* newName, MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }

    void startUpload(const char* localPath, MegaNode* parent, const char* fileName, int64_t mtime, void* appData, bool isSourceTemporary, bool isDestTemporary, void* cancelToken, MegaTransferListener* listener) {
        if (listener) {
            listener->onTransferStart(this, nullptr);
            // Simulate finish
            MegaError err;
            listener->onTransferFinish(this, nullptr, &err);
        }
    }

    void startDownload(MegaNode* node, const char* localPath, void* appData, void* cancelToken, bool createTmpIfSameName, void* byteLimit, int64_t offset, int64_t size, bool collisionPolicy, MegaTransferListener* listener) {
        if (listener) {
            listener->onTransferStart(this, nullptr);
            // Simulate finish
            MegaError err;
            listener->onTransferFinish(this, nullptr, &err);
        }
    }

    void createFolder(const char* name, MegaNode* parent, MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }

    void cancelTransfers(int type, MegaRequestListener* listener = nullptr) {}
    MegaTransferList* getTransfers(int type) { return new MegaTransferList(); }
    MegaTransferList* getTransfers() { return new MegaTransferList(); }

    void share(MegaNode* node, const char* email, int level, MegaRequestListener* listener = nullptr) {}
    MegaShareList* getOutShares(MegaNode* node) { return new MegaShareList(); }
    void exportNode(MegaNode* node, int64_t expireTime, bool deleteExisting, bool createNew, MegaRequestListener* listener = nullptr) {}
    void disableExport(MegaNode* node, MegaRequestListener* listener = nullptr) {}
    void cleanRubbishBin(MegaRequestListener* listener = nullptr) {}
    MegaNode* getRubbishNode() { return new MegaNode(); }
    void moveNode(MegaNode* node, MegaNode* target, MegaRequestListener* listener = nullptr) {}
    void moveNode(MegaNode* node, MegaNode* target, const char* newName, MegaRequestListener* listener = nullptr) {}
    void remove(MegaNode* node, MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }
    void copyNode(MegaNode* node, MegaNode* target, MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }
    void copyNode(MegaNode* node, MegaNode* target, const char* newName, MegaRequestListener* listener = nullptr) {
        if (listener) {
             MegaRequest req;
             MegaError err;
             listener->onRequestFinish(this, &req, &err);
        }
    }
};

} // namespace mega

#endif // !MEGA_SDK_AVAILABLE

#endif // MEGA_MOCK_H
