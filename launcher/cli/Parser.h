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

/**
 * @brief the options of one pack group (`resource`, `shader` or `texture`)
 *
 * The values are bound to the CLI11 options of that group and turned into
 * `Cmd::Pack*` commands by `flush()` after a successful parse - this keeps the
 * CLI11 declarations free of ordering assumptions.
 */
struct PackGroupOptions {
    Cmd::PackGroup group = Cmd::PackGroup::Resource;
    CLI::App* app{};

    // list
    std::string listInstance;
    bool listIncludeDisabled = false;

    // search
    std::string searchQuery;
    std::vector<std::string> searchMcVersions;
    int searchLimit = 25;
    int searchOffset = 0;

    // info
    std::string infoProject;
    CLI::Option* infoInstance{};
    std::string infoInstanceValue;

    // install
    std::string installProject;
    std::string installInstance;
    CLI::Option* installVersion{};
    std::string installVersionValue;
    bool installNoMetadata = false;
    bool installAssumeYes = false;

    // remove
    std::vector<std::string> removeTargets;
    std::string removeInstance;
    bool removeForce = false;
    bool removeAssumeYes = false;

    // enable
    std::vector<std::string> enableTargets;
    std::string enableInstance;
    bool enableAssumeYes = false;

    // disable
    std::vector<std::string> disableTargets;
    std::string disableInstance;
    bool disableAssumeYes = false;
};

/// The `resource`, `shader` and `texture` subcommand groups.
struct PackCli {
    PackGroupOptions resource;
    PackGroupOptions shader;
    PackGroupOptions texture;

    void addPackArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `world` subcommand group. The options are bound to plain members here and
/// turned into `Cmd::World*` commands by `flush()` after a successful parse -
/// this keeps the CLI11 declarations free of ordering assumptions.
struct WorldCli {
    CLI::App* world{};

    // world list
    std::string listInstance;
    bool listWithSize = false;

    // world info
    std::string infoTarget;
    std::string infoInstance;
    bool infoWithSize = false;

    // world copy
    std::string copyTarget;
    std::string copyInstance;
    CLI::Option* copyName{};
    std::string copyNameValue;

    // world rename
    std::string renameTarget;
    std::string renameInstance;
    std::string renameName;

    // world delete
    std::string deleteTarget;
    std::string deleteInstance;
    bool deletePermanent = false;

    // world import
    std::string importPath;
    std::string importInstance;
    CLI::Option* importName{};
    std::string importNameValue;

    void addWorldArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `server` subcommand group. The options are bound to plain members here and
/// turned into `Cmd::Server*` commands by `flush()` after a successful parse -
/// this keeps the CLI11 declarations free of ordering assumptions.
struct ServerCli {
    CLI::App* server{};

    // server list
    std::string listInstance;
    bool listPing = false;

    // server add
    std::string addName;
    std::string addAddress;
    std::string addInstance;
    CLI::Option* addIndex{};
    std::string addIndexValue;
    CLI::Option* addAcceptsTextures{};
    std::string addAcceptsTexturesValue;

    // server remove
    std::string removeTarget;
    std::string removeInstance;
    bool removeForce = false;

    void addServerArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `log` subcommand group. The options are bound to plain members here and
/// turned into `Cmd::Log*` commands by `flush()` after a successful parse -
/// this keeps the CLI11 declarations free of ordering assumptions.
struct LogCli {
    CLI::App* log{};

    // log list
    std::string listInstance;

    // log read
    std::string readInstance;
    CLI::Option* readFile{};
    std::string readFileValue;
    CLI::Option* readHead{};
    std::string readHeadValue;
    CLI::Option* readTail{};
    std::string readTailValue;
    std::string readGrep;
    std::string readLevel;
    bool readJson = false;

    // log upload
    std::string uploadInstance;
    CLI::Option* uploadFile{};
    std::string uploadFileValue;
    CLI::Option* uploadService{};
    std::string uploadServiceValue;
    bool uploadAnonymize = false;
    bool uploadAssumeYes = false;

    void addLogArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `screenshot` subcommand group. The options are bound to plain members here
/// and turned into `Cmd::Screenshot*` commands by `flush()` after a successful
/// parse - this keeps the CLI11 declarations free of ordering assumptions.
struct ScreenshotCli {
    CLI::App* screenshot{};

    // screenshot list
    std::string listInstance;

    // screenshot upload
    std::string uploadTarget;
    std::string uploadInstance;
    CLI::Option* uploadLimit{};
    std::string uploadLimitValue;
    bool uploadAssumeYes = false;

    // screenshot copy
    std::string copyId;
    std::string copyDestination;
    std::string copyInstance;
    bool copyForce = false;

    void addScreenshotArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `notes` subcommand group. The options are bound to plain members here and
/// turned into `Cmd::Notes*` commands by `flush()` after a successful parse -
/// this keeps the CLI11 declarations free of ordering assumptions.
struct NotesCli {
    CLI::App* notes{};

    // notes show
    std::string showInstance;

    // notes set
    std::string setInstance;
    CLI::Option* setText{};
    std::string setTextValue;
    CLI::Option* setFile{};
    std::string setFileValue;
    bool setAppend = false;

    void addNotesArgs(CLI::App& app);
    void flush(Args& args) const;
};

/// `status` subcommand.
struct StatusCli {
    CLI::App* status{};

    void addStatusArgs(CLI::App& app);
    void flush(Args& args) const;
};

struct Cli {
    LegacyCli legacyCli{};
    ModCli modCli{};
    InstanceCli instanceCli{};
    SettingsCli settingsCli{};
    VersionCli versionCli{};
    PackCli packCli{};
    WorldCli worldCli{};
    ServerCli serverCli{};
    LogCli logCli{};
    ScreenshotCli screenshotCli{};
    NotesCli notesCli{};
    StatusCli statusCli{};

    void attach(CLI::App& app, Args& args);
};

void parseArgs(int argc, char** argv, Args& args);
}  // namespace Cli
