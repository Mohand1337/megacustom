#ifndef CLI_COMMAND_H
#define CLI_COMMAND_H

#include <string>
#include <vector>
#include <memory>

namespace MegaCustom {
namespace CLI {

/**
 * Base class for all CLI command handlers
 *
 * Implement this interface to create new CLI commands.
 * Commands are registered with CommandRegistry and dispatched
 * based on the first argument to the program.
 *
 * Example usage:
 *   class AuthCommand : public Command {
 *   public:
 *       std::string name() const override { return "auth"; }
 *       std::string description() const override { return "Authentication operations"; }
 *       int execute(const std::vector<std::string>& args) override;
 *       void printHelp() const override;
 *   };
 */
class Command {
public:
    virtual ~Command() = default;

    /**
     * Get the command name (used for dispatching)
     * This is the first argument after the program name.
     * Example: "auth" for "megacustom auth login"
     */
    virtual std::string name() const = 0;

    /**
     * Get a brief description for help text
     * Shown in the main help listing.
     */
    virtual std::string description() const = 0;

    /**
     * Get command aliases (optional)
     * Alternative names that also trigger this command.
     * Example: {"wordpress"} for "wp" command
     */
    virtual std::vector<std::string> aliases() const { return {}; }

    /**
     * Execute the command with given arguments
     * @param args Arguments after the command name
     * @return Exit code (0 = success)
     */
    virtual int execute(const std::vector<std::string>& args) = 0;

    /**
     * Print detailed help for this command
     * Called when user runs "megacustom <command> --help"
     */
    virtual void printHelp() const = 0;

    /**
     * Check if command requires authentication
     * If true, command will fail early if user is not logged in.
     * Default: true (most commands need auth)
     */
    virtual bool requiresAuth() const { return true; }

    /**
     * Check if command requires initialization
     * If true, MegaManager will be initialized before execute().
     * Default: true
     */
    virtual bool requiresInit() const { return true; }
};

using CommandPtr = std::unique_ptr<Command>;

} // namespace CLI
} // namespace MegaCustom

#endif // CLI_COMMAND_H
