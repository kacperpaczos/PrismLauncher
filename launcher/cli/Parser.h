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

    // instance create
    CLI::Option* createName{};
    std::string createNameValue;
    CLI::Option* createMc{};
    std::string createMcValue;
    CLI::Option* createLoader{};
    std::string createLoaderValue;
    CLI::Option* createLoaderVersion{};
    std::string createLoaderVersionValue;
    CLI::Option* createGroup{};
    std::string createGroupValue;
    CLI::Option* createIcon{};
    std::string createIconValue;
    bool createDownload = false;
    bool createNoDownload = false;
    CLI::Option* createFrom{};
    std::string createFromValue;

    // instance duplicate
    std::string duplicateId;
    CLI::Option* duplicateName{};
    std::string duplicateNameValue;
    bool duplicateCopy = false;
    bool duplicateLinks = false;
    bool duplicateHardlinks = false;
    bool duplicateClone = false;
    bool duplicateNoSaves = false;
    bool duplicateNoMods = false;
    bool duplicateNoResourcePacks = false;
    bool duplicateNoShaderPacks = false;
    bool duplicateNoOptions = false;
    bool duplicateNoConfigs = false;

    // instance rename
    std::string renameId;
    std::string renameNewName;
    bool renameKeepFolder = false;
    bool renameRenameFolder = false;

    // instance delete
    std::string deleteId;
    bool deletePurge = false;

    // instance icon set
    std::string iconSetId;
    std::string iconSetIcon;

    void addInstanceArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `settings` subcommand group.
struct SettingsCli {
    CLI::App* settings{};

    // settings list
    std::string listId;
    bool listAll = false;
    bool listModified = false;

    // settings get
    std::string getId;
    std::vector<std::string> getKeys;

    // settings set
    std::string setId;
    std::vector<std::string> setValues;

    // settings unset
    std::string unsetId;
    std::vector<std::string> unsetKeys;

    void addSettingsArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `version` subcommand group.
struct VersionCli {
    CLI::App* version{};

    // version show
    std::string showId;

    // version list-available
    bool listMc = false;
    std::string listLoader;
    std::string listMcVersion;
    std::string listType = "all";

    // version change-mc
    std::string changeMcId;
    std::string changeMcVersion;

    // version install-loader
    std::string installLoaderId;
    std::string installLoaderLoader;
    std::string installLoaderVersion;
    std::string installLoaderForceConflict = "disable";

    // version remove-loader
    std::string removeLoaderId;
    std::string removeLoaderLoader;

    // version reinstall
    std::string reinstallId;

    void addVersionArgs(CLI::App& app);
    void flush(Args& args) const;
};

struct Cli {
    LegacyCli legacyCli{};
    ModCli modCli{};
    InstanceCli instanceCli{};
    SettingsCli settingsCli{};
    VersionCli versionCli{};

    void attach(CLI::App& app, Args& args);
};

void parseArgs(int argc, char** argv, Args& args);
}  // namespace Cli
