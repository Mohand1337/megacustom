#ifndef MEGACUSTOM_GUARDS_H
#define MEGACUSTOM_GUARDS_H

/**
 * Null check guard macros for consistent null handling.
 *
 * Use these macros to provide consistent early-return behavior
 * when encountering null pointers, especially in Qt signal handlers
 * and callback functions.
 */

// Return from function if pointer is null
#define RETURN_IF_NULL(ptr) \
    do { if (!(ptr)) return; } while(0)

// Return specified value if pointer is null
#define RETURN_VALUE_IF_NULL(ptr, val) \
    do { if (!(ptr)) return (val); } while(0)

// Return false if pointer is null
#define RETURN_FALSE_IF_NULL(ptr) \
    RETURN_VALUE_IF_NULL(ptr, false)

// Return nullptr if pointer is null
#define RETURN_NULL_IF_NULL(ptr) \
    RETURN_VALUE_IF_NULL(ptr, nullptr)

// Return empty string if pointer is null
#define RETURN_EMPTY_IF_NULL(ptr) \
    RETURN_VALUE_IF_NULL(ptr, std::string{})

// Continue loop iteration if pointer is null
#define CONTINUE_IF_NULL(ptr) \
    do { if (!(ptr)) continue; } while(0)

// Break loop if pointer is null
#define BREAK_IF_NULL(ptr) \
    do { if (!(ptr)) break; } while(0)

// Log and return if null (for debugging)
#define LOG_RETURN_IF_NULL(ptr, logger, msg) \
    do { \
        if (!(ptr)) { \
            logger.log(LogLevel::Warning, LogCategory::System, msg); \
            return; \
        } \
    } while(0)

/**
 * Scoped null check - executes block only if pointer is valid
 * Usage:
 *   WITH_VALID_PTR(myPtr) {
 *       myPtr->doSomething();
 *   }
 */
#define WITH_VALID_PTR(ptr) \
    if ((ptr))

/**
 * Assert non-null with message (debug builds only)
 */
#ifdef NDEBUG
    #define ASSERT_NOT_NULL(ptr, msg) ((void)0)
#else
    #include <cassert>
    #define ASSERT_NOT_NULL(ptr, msg) \
        assert((ptr) && (msg))
#endif

/**
 * Template-based null check with optional default
 */
namespace megacustom {

/**
 * Execute function only if pointer is non-null
 * @param ptr Pointer to check
 * @param func Function to execute with dereferenced pointer
 * @return Result of func, or default-constructed value if ptr is null
 */
template<typename T, typename Func>
auto withNullCheck(T* ptr, Func&& func)
    -> decltype(func(*ptr))
{
    using ReturnType = decltype(func(*ptr));
    if (!ptr) {
        return ReturnType{};
    }
    return func(*ptr);
}

/**
 * Execute function only if pointer is non-null, with custom default
 * @param ptr Pointer to check
 * @param func Function to execute with dereferenced pointer
 * @param defaultVal Value to return if ptr is null
 * @return Result of func, or defaultVal if ptr is null
 */
template<typename T, typename Func, typename Default>
auto withNullCheck(T* ptr, Func&& func, Default&& defaultVal)
    -> decltype(func(*ptr))
{
    if (!ptr) {
        return std::forward<Default>(defaultVal);
    }
    return func(*ptr);
}

/**
 * Safe dereference with default value
 * @param ptr Pointer to dereference
 * @param defaultVal Value to return if ptr is null
 * @return *ptr or defaultVal
 */
template<typename T>
T safeDeref(const T* ptr, T defaultVal = T{}) {
    return ptr ? *ptr : defaultVal;
}

/**
 * Check multiple pointers, return false if any are null
 */
template<typename... Ptrs>
bool allValid(Ptrs... ptrs) {
    return (... && (ptrs != nullptr));
}

/**
 * Check multiple pointers, return true if any are null
 */
template<typename... Ptrs>
bool anyNull(Ptrs... ptrs) {
    return (... || (ptrs == nullptr));
}

} // namespace megacustom

#endif // MEGACUSTOM_GUARDS_H
