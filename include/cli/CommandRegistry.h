#ifndef CLI_COMMAND_REGISTRY_H
#define CLI_COMMAND_REGISTRY_H

#include "Command.h"
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace MegaCustom {
namespace CLI {

/**
 * Singleton registry for CLI commands
 *
 * Commands register themselves with the registry, which then
 * handles command lookup and dispatching.
 *
 * Example:
 *   // Register commands at startup
 *   CommandRegistry::instance().registerCommand(std::make_unique<AuthCommand>());
 *   CommandRegistry::instance().registerCommand(std::make_unique<UploadCommand>());
 *
 *   // Dispatch command
 *   return CommandRegistry::instance().dispatch(argc, argv);
 */
class CommandRegistry {
public:
    /**
     * Get singleton instance
     */
    static CommandRegistry& instance();

    // Non-copyable
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    /**
     * Register a command
     * @param command Unique pointer to command (registry takes ownership)
     * @return true if registered successfully, false if name collision
     */
    bool registerCommand(CommandPtr command);

    /**
     * Unregister a command by name
     * @param name Command name to remove
     * @return true if command was removed
     */
    bool unregisterCommand(const std::string& name);

    /**
     * Get a command by name or alias
     * @param name Command name or alias
     * @return Pointer to command or nullptr if not found
     */
    Command* getCommand(const std::string& name) const;

    /**
     * Get all registered commands
     * @return Vector of command pointers (sorted by name)
     */
    std::vector<Command*> getAllCommands() const;

    /**
     * Dispatch a command from argc/argv
     * Parses arguments, finds command, and executes it.
     * @param argc Argument count from main()
     * @param argv Argument values from main()
     * @return Exit code from command (or 1 if command not found)
     */
    int dispatch(int argc, char* argv[]);

    /**
     * Print help listing all commands
     * Called for "help", "--help", "-h"
     */
    void printHelp(const std::string& programName) const;

    /**
     * Print version information
     */
    void printVersion() const;

    /**
     * Set application info for help/version display
     */
    void setAppInfo(const std::string& name, const std::string& version,
                    const std::string& description);

    /**
     * Clear all registered commands
     * Primarily for testing
     */
    void clear();

private:
    CommandRegistry() = default;

    // Commands by name
    std::map<std::string, CommandPtr> m_commands;

    // Alias -> canonical name mapping
    std::map<std::string, std::string> m_aliases;

    // App info
    std::string m_appName = "MegaCustom";
    std::string m_appVersion = "1.0.0";
    std::string m_appDescription = "MEGA Cloud File Operations";
};

/**
 * Helper macro for command registration
 * Use in a source file to auto-register a command:
 *
 *   REGISTER_COMMAND(AuthCommand)
 *
 * This creates a static initializer that registers the command
 * when the program starts.
 */
#define REGISTER_COMMAND(CommandClass) \
    namespace { \
        static bool _registered_##CommandClass = []() { \
            MegaCustom::CLI::CommandRegistry::instance().registerCommand( \
                std::make_unique<CommandClass>()); \
            return true; \
        }(); \
    }

} // namespace CLI
} // namespace MegaCustom

#endif // CLI_COMMAND_REGISTRY_H
