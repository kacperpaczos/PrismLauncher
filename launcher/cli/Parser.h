// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "cli/Commands.h"

#include <CLI/CLI.hpp>

namespace Cli {

// struct to hold options and mid parse storage
struct LegacyCli {
    CLI::App* launchGroup{};
    CLI::Option* launch{};
    CLI::Option* server{};
    CLI::Option* world{};
    CLI::Option* account{};
    CLI::Option* offline{};
    CLI::Option* showMain{};
    CLI::Option* showInstance{};
    CLI::Option* alive{};
    CLI::Option* import{};
    std::optional<Cmd::Launch> processingLaunch;

    void addLegacyArgs(CLI::App& app, Args& args);
};

/// `mod` subcommand group. Options are bound to plain members here and turned
/// into `Cmd::Mod*` commands by `flush()` after a successful parse - this keeps
/// the CLI11 declarations free of ordering assumptions.
struct ModCli {
    CLI::App* mod{};

    // mod list
    std::string listInstance;
    bool listIncludeDisabled = false;

    // mod search
    std::string searchQuery;
    std::vector<std::string> searchMcVersions;
    std::vector<std::string> searchLoaders;
    int searchLimit = 25;
    int searchOffset = 0;

    // mod info
    std::string infoProject;
    CLI::Option* infoInstance{};
    std::string infoInstanceValue;

    // mod install
    std::string installProject;
    std::string installInstance;
    CLI::Option* installVersion{};
    std::string installVersionValue;
    bool installNoDeps = false;
    bool installNoMetadata = false;
    bool installAssumeYes = false;

    // mod remove
    std::vector<std::string> removeTargets;
    std::string removeInstance;
    bool removeForce = false;
    bool removeAssumeYes = false;

    // mod enable
    std::vector<std::string> enableTargets;
    std::string enableInstance;
    bool enableRecursive = false;
    bool enableAssumeYes = false;

    // mod disable
    std::vector<std::string> disableTargets;
    std::string disableInstance;
    bool disableRecursive = false;
    bool disableAssumeYes = false;

    // mod update
    std::vector<std::string> updateTargets;
    bool updateAll = false;
    std::string updateInstance;
    bool updateDryRun = false;
    bool updateNoDeps = false;
    bool updateForce = false;
    bool updateAssumeYes = false;

    void addModArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `instance` subcommand group.
struct InstanceCli {
    CLI::App* instance{};

    // instance list
    CLI::Option* listGroup{};
    std::string listGroupValue;

    // instance info
    std::string infoId;

    void addInstanceArgs(CLI::App& app);
    void flush(Args& args) const;
};

struct Cli {
    LegacyCli legacyCli{};
    ModCli modCli{};
    InstanceCli instanceCli{};

    void attach(CLI::App& app, Args& args);
};

void parseArgs(int argc, char** argv, Args& args);
}  // namespace Cli
