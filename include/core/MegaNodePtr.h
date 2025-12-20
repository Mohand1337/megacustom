#ifndef MEGACUSTOM_MEGA_NODE_PTR_H
#define MEGACUSTOM_MEGA_NODE_PTR_H

#include <megaapi.h>
#include <utility>

namespace megacustom {

/**
 * RAII wrapper for mega::MegaNode pointers.
 *
 * The MEGA SDK returns raw pointers that the caller must delete.
 * This wrapper ensures automatic cleanup and prevents memory leaks.
 *
 * Usage:
 *   MegaNodePtr root(megaApi->getRootNode());
 *   if (root) {
 *       std::cout << root->getName() << std::endl;
 *   }
 *   // Automatic cleanup when out of scope
 */
class MegaNodePtr {
public:
    /**
     * Construct from raw pointer (takes ownership)
     * @param node MegaNode pointer to manage (can be nullptr)
     */
    explicit MegaNodePtr(mega::MegaNode* node = nullptr) noexcept
        : m_node(node) {}

    /**
     * Destructor - deletes managed node
     */
    ~MegaNodePtr() {
        delete m_node;
    }

    /**
     * Move constructor - transfers ownership
     */
    MegaNodePtr(MegaNodePtr&& other) noexcept
        : m_node(other.release()) {}

    /**
     * Move assignment - transfers ownership
     */
    MegaNodePtr& operator=(MegaNodePtr&& other) noexcept {
        if (this != &other) {
            delete m_node;
            m_node = other.release();
        }
        return *this;
    }

    // Prevent copying (unique ownership)
    MegaNodePtr(const MegaNodePtr&) = delete;
    MegaNodePtr& operator=(const MegaNodePtr&) = delete;

    /**
     * Get raw pointer (does not transfer ownership)
     */
    mega::MegaNode* get() const noexcept {
        return m_node;
    }

    /**
     * Arrow operator for member access
     */
    mega::MegaNode* operator->() const noexcept {
        return m_node;
    }

    /**
     * Dereference operator
     */
    mega::MegaNode& operator*() const noexcept {
        return *m_node;
    }

    /**
     * Check if pointer is valid
     */
    explicit operator bool() const noexcept {
        return m_node != nullptr;
    }

    /**
     * Release ownership and return raw pointer
     * Caller becomes responsible for deletion
     */
    mega::MegaNode* release() noexcept {
        mega::MegaNode* tmp = m_node;
        m_node = nullptr;
        return tmp;
    }

    /**
     * Reset to new pointer (deletes old)
     */
    void reset(mega::MegaNode* node = nullptr) noexcept {
        if (m_node != node) {
            delete m_node;
            m_node = node;
        }
    }

    /**
     * Swap with another MegaNodePtr
     */
    void swap(MegaNodePtr& other) noexcept {
        std::swap(m_node, other.m_node);
    }

    /**
     * Compare node handles for equality
     * Note: Compares by handle, not pointer address
     */
    bool handleEquals(const MegaNodePtr& other) const noexcept {
        if (!m_node || !other.m_node) return false;
        return m_node->getHandle() == other.m_node->getHandle();
    }

    /**
     * Compare with raw MegaNode by handle
     */
    bool handleEquals(const mega::MegaNode* other) const noexcept {
        if (!m_node || !other) return false;
        return m_node->getHandle() == other->getHandle();
    }

private:
    mega::MegaNode* m_node;
};

/**
 * RAII wrapper for mega::MegaNodeList pointers.
 */
class MegaNodeListPtr {
public:
    explicit MegaNodeListPtr(mega::MegaNodeList* list = nullptr) noexcept
        : m_list(list) {}

    ~MegaNodeListPtr() {
        delete m_list;
    }

    MegaNodeListPtr(MegaNodeListPtr&& other) noexcept
        : m_list(other.release()) {}

    MegaNodeListPtr& operator=(MegaNodeListPtr&& other) noexcept {
        if (this != &other) {
            delete m_list;
            m_list = other.release();
        }
        return *this;
    }

    MegaNodeListPtr(const MegaNodeListPtr&) = delete;
    MegaNodeListPtr& operator=(const MegaNodeListPtr&) = delete;

    mega::MegaNodeList* get() const noexcept { return m_list; }
    mega::MegaNodeList* operator->() const noexcept { return m_list; }
    mega::MegaNodeList& operator*() const noexcept { return *m_list; }
    explicit operator bool() const noexcept { return m_list != nullptr; }

    mega::MegaNodeList* release() noexcept {
        mega::MegaNodeList* tmp = m_list;
        m_list = nullptr;
        return tmp;
    }

    void reset(mega::MegaNodeList* list = nullptr) noexcept {
        if (m_list != list) {
            delete m_list;
            m_list = list;
        }
    }

    // Convenience accessors
    int size() const noexcept {
        return m_list ? m_list->size() : 0;
    }

    /**
     * Get node at index (creates new MegaNodePtr with copy)
     * Note: MegaNodeList::get() returns internal pointer, must copy
     */
    MegaNodePtr at(int index) const {
        if (!m_list || index < 0 || index >= m_list->size()) {
            return MegaNodePtr(nullptr);
        }
        return MegaNodePtr(m_list->get(index)->copy());
    }

private:
    mega::MegaNodeList* m_list;
};

/**
 * RAII wrapper for mega::MegaUser pointers.
 */
class MegaUserPtr {
public:
    explicit MegaUserPtr(mega::MegaUser* user = nullptr) noexcept
        : m_user(user) {}

    ~MegaUserPtr() {
        delete m_user;
    }

    MegaUserPtr(MegaUserPtr&& other) noexcept
        : m_user(other.release()) {}

    MegaUserPtr& operator=(MegaUserPtr&& other) noexcept {
        if (this != &other) {
            delete m_user;
            m_user = other.release();
        }
        return *this;
    }

    MegaUserPtr(const MegaUserPtr&) = delete;
    MegaUserPtr& operator=(const MegaUserPtr&) = delete;

    mega::MegaUser* get() const noexcept { return m_user; }
    mega::MegaUser* operator->() const noexcept { return m_user; }
    explicit operator bool() const noexcept { return m_user != nullptr; }

    mega::MegaUser* release() noexcept {
        mega::MegaUser* tmp = m_user;
        m_user = nullptr;
        return tmp;
    }

    void reset(mega::MegaUser* user = nullptr) noexcept {
        if (m_user != user) {
            delete m_user;
            m_user = user;
        }
    }

private:
    mega::MegaUser* m_user;
};

} // namespace megacustom

#endif // MEGACUSTOM_MEGA_NODE_PTR_H
