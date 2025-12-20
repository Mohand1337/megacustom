#include "cli/CommandRegistry.h"
#include <iostream>
#include <algorithm>
#include <iomanip>

namespace MegaCustom {
namespace CLI {

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry instance;
    return instance;
}

bool CommandRegistry::registerCommand(CommandPtr command) {
    if (!command) {
        return false;
    }

    const std::string name = command->name();

    // Check for name collision
    if (m_commands.find(name) != m_commands.end()) {
        std::cerr << "Warning: Command '" << name << "' already registered\n";
        return false;
    }

    // Check alias collisions
    for (const auto& alias : command->aliases()) {
        if (m_commands.find(alias) != m_commands.end() ||
            m_aliases.find(alias) != m_aliases.end()) {
            std::cerr << "Warning: Alias '" << alias << "' collides with existing command\n";
            return false;
        }
    }

    // Register aliases
    for (const auto& alias : command->aliases()) {
        m_aliases[alias] = name;
    }

    // Register command
    m_commands[name] = std::move(command);
    return true;
}

bool CommandRegistry::unregisterCommand(const std::string& name) {
    auto it = m_commands.find(name);
    if (it == m_commands.end()) {
        return false;
    }

    // Remove aliases
    for (const auto& alias : it->second->aliases()) {
        m_aliases.erase(alias);
    }

    m_commands.erase(it);
    return true;
}

Command* CommandRegistry::getCommand(const std::string& name) const {
    // Check direct name
    auto it = m_commands.find(name);
    if (it != m_commands.end()) {
        return it->second.get();
    }

    // Check aliases
    auto aliasIt = m_aliases.find(name);
    if (aliasIt != m_aliases.end()) {
        it = m_commands.find(aliasIt->second);
        if (it != m_commands.end()) {
            return it->second.get();
        }
    }

    return nullptr;
}

std::vector<Command*> CommandRegistry::getAllCommands() const {
    std::vector<Command*> commands;
    commands.reserve(m_commands.size());

    for (const auto& pair : m_commands) {
        commands.push_back(pair.second.get());
    }

    // Sort by name
    std::sort(commands.begin(), commands.end(),
              [](Command* a, Command* b) { return a->name() < b->name(); });

    return commands;
}

int CommandRegistry::dispatch(int argc, char* argv[]) {
    // No arguments - print help
    if (argc < 2) {
        printHelp(argv[0]);
        return 1;
    }

    std::string commandName = argv[1];

    // Handle built-in commands
    if (commandName == "help" || commandName == "--help" || commandName == "-h") {
        printHelp(argv[0]);
        return 0;
    }

    if (commandName == "version" || commandName == "--version" || commandName == "-v") {
        printVersion();
        return 0;
    }

    // Find and execute command
    Command* command = getCommand(commandName);
    if (!command) {
        std::cerr << "Error: Unknown command '" << commandName << "'\n";
        std::cerr << "Use '" << argv[0] << " help' for usage information.\n";
        return 1;
    }

    // Build argument vector (skip program name and command name)
    std::vector<std::string> args;
    for (int i = 2; i < argc; i++) {
        args.push_back(argv[i]);
    }

    // Check for help flag
    if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) {
        command->printHelp();
        return 0;
    }

    // Execute command
    return command->execute(args);
}

void CommandRegistry::printHelp(const std::string& programName) const {
    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << " " << m_appName << " v" << m_appVersion << "\n";
    std::cout << " " << m_appDescription << "\n";
    std::cout << "=================================================\n\n";

    std::cout << "Usage: " << programName << " <command> [options]\n\n";
    std::cout << "Commands:\n";

    // Find max command name length for alignment
    size_t maxLen = 0;
    for (const auto& pair : m_commands) {
        maxLen = std::max(maxLen, pair.first.length());
    }

    // Print commands
    auto commands = getAllCommands();
    for (const auto* cmd : commands) {
        std::cout << "  " << std::left << std::setw(maxLen + 2)
                  << cmd->name() << cmd->description() << "\n";
    }

    std::cout << "\nUse '" << programName << " <command> --help' for command-specific help.\n";
}

void CommandRegistry::printVersion() const {
    std::cout << m_appName << " version " << m_appVersion << "\n";
    std::cout << "Built with Mega C++ SDK\n";
}

void CommandRegistry::setAppInfo(const std::string& name, const std::string& version,
                                  const std::string& description) {
    m_appName = name;
    m_appVersion = version;
    m_appDescription = description;
}

void CommandRegistry::clear() {
    m_commands.clear();
    m_aliases.clear();
}

} // namespace CLI
} // namespace MegaCustom
