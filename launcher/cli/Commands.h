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

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string_view>
#include <variant>
#include <vector>

#include <QString>

#include "startup/Startup.h"

namespace Cli {

static constexpr std::string_view g_liveCheckFile = "live.check";

namespace Cmd {

struct Launch {
    // targets
    struct NoTarget {
        auto operator<=>(const NoTarget&) const = default;
    };
    struct ServerTarget {
        std::string target;
        auto operator<=>(const ServerTarget&) const = default;
    };
    struct WorldTarget {
        std::string target;
        auto operator<=>(const WorldTarget&) const = default;
    };
    using TargetBase = std::variant<NoTarget, ServerTarget, WorldTarget>;
    struct Target : TargetBase {
        using TargetBase::TargetBase;
    };

    // accounts
    struct AccountDefault {
        auto operator<=>(const AccountDefault&) const = default;
    };
    struct AccountProfile {
        std::string name;
        auto operator<=>(const AccountProfile&) const = default;
    };
    struct AccountOffline {
        std::string name;
        auto operator<=>(const AccountOffline&) const = default;
    };
    using AccountBase = std::variant<AccountDefault, AccountProfile, AccountOffline>;
    struct Account : AccountBase {
        using AccountBase::AccountBase;
    };

    std::string id;
    Target target = NoTarget{};
    Account account = AccountDefault{};
    auto operator<=>(const Launch&) const = default;
};
struct ProcessURI {
    std::string uri;
    auto operator<=>(const ProcessURI&) const = default;
};
struct ShowMainWindow {
    auto operator<=>(const ShowMainWindow&) const = default;
};
struct ShowInstanceWindow {
    std::string id;
    auto operator<=>(const ShowInstanceWindow&) const = default;
};
struct Alive {
    std::filesystem::path path;
    auto operator<=>(const Alive&) const = default;
};

// ---- mod management (headless) ----

struct ModList {
    QString instance;
    bool includeDisabled = false;
    auto operator<=>(const ModList&) const = default;
};

struct ModSearch {
    QString query;
    std::vector<QString> mcVersions;
    std::vector<QString> loaders;
    int limit = 25;
    int offset = 0;
    auto operator<=>(const ModSearch&) const = default;
};

struct ModInfo {
    QString project;
    std::optional<QString> instance;
    auto operator<=>(const ModInfo&) const = default;
};

struct ModInstall {
    QString project;
    QString instance;
    std::optional<QString> version;
    bool noDeps = false;
    bool noMetadata = false;
    // Accepted for forward compatibility. Headless runs never prompt, so this
    // is currently a no-op (see SPIKE-ROADMAP.md).
    bool assumeYes = false;
    auto operator<=>(const ModInstall&) const = default;
};

struct ModRemove {
    std::vector<QString> targets;
    QString instance;
    bool force = false;
    bool assumeYes = false;
    auto operator<=>(const ModRemove&) const = default;
};

struct ModEnable {
    std::vector<QString> targets;
    QString instance;
    bool recursive = false;
    bool assumeYes = false;
    auto operator<=>(const ModEnable&) const = default;
};

struct ModDisable {
    std::vector<QString> targets;
    QString instance;
    bool recursive = false;
    bool assumeYes = false;
    auto operator<=>(const ModDisable&) const = default;
};

struct ModUpdate {
    std::vector<QString> targets;
    bool all = false;
    QString instance;
    bool dryRun = false;
    bool noDeps = false;
    bool force = false;
    bool assumeYes = false;
    auto operator<=>(const ModUpdate&) const = default;
};

// ---- resource / shader / texture pack management (headless) ----
//
// The packs are managed the same way the mods are, but they live in their own
// folder of the instance and they have no dependency graph - so there is no
// `update` command and no dependency resolution.

/// @brief the kind of pack a pack command works on
///
/// The kind selects the model of the instance that the command operates on
/// (`resourcepacks/`, `shaderpacks/` or `texturepacks/`).
enum class PackGroup : std::uint8_t { Resource, Shader, Texture };

struct PackList {
    PackGroup group = PackGroup::Resource;
    QString instance;
    bool includeDisabled = false;
    auto operator<=>(const PackList&) const = default;
};

struct PackSearch {
    PackGroup group = PackGroup::Resource;
    QString query;
    std::vector<QString> mcVersions;
    int limit = 25;
    int offset = 0;
    auto operator<=>(const PackSearch&) const = default;
};

struct PackInfo {
    PackGroup group = PackGroup::Resource;
    QString project;
    std::optional<QString> instance;
    auto operator<=>(const PackInfo&) const = default;
};

struct PackInstall {
    PackGroup group = PackGroup::Resource;
    QString project;
    QString instance;
    std::optional<QString> version;
    bool noMetadata = false;
    // Accepted for forward compatibility. Headless runs never prompt, so this
    // is currently a no-op (see SPIKE-ROADMAP.md).
    bool assumeYes = false;
    auto operator<=>(const PackInstall&) const = default;
};

struct PackRemove {
    PackGroup group = PackGroup::Resource;
    std::vector<QString> targets;
    QString instance;
    bool force = false;
    bool assumeYes = false;
    auto operator<=>(const PackRemove&) const = default;
};

struct PackEnable {
    PackGroup group = PackGroup::Resource;
    std::vector<QString> targets;
    QString instance;
    bool assumeYes = false;
    auto operator<=>(const PackEnable&) const = default;
};

struct PackDisable {
    PackGroup group = PackGroup::Resource;
    std::vector<QString> targets;
    QString instance;
    bool assumeYes = false;
    auto operator<=>(const PackDisable&) const = default;
};

// ---- world management (headless) ----
//
// The worlds live in the `saves` folder of the game root of an instance. A
// world is addressed by its folder (the stable id) or by the name it has in
// its `level.dat` - never by its position in a list.

struct WorldList {
    QString instance;
    bool withSize = false;
    auto operator<=>(const WorldList&) const = default;
};

struct WorldInfo {
    QString instance;
    QString target;
    bool withSize = false;
    auto operator<=>(const WorldInfo&) const = default;
};

struct WorldCopy {
    QString instance;
    QString target;
    std::optional<QString> name;
    auto operator<=>(const WorldCopy&) const = default;
};

struct WorldRename {
    QString instance;
    QString target;
    QString name;
    auto operator<=>(const WorldRename&) const = default;
};

struct WorldDelete {
    QString instance;
    QString target;
    bool permanent = false;
    auto operator<=>(const WorldDelete&) const = default;
};

struct WorldImport {
    QString instance;
    QString path;
    std::optional<QString> name;
    auto operator<=>(const WorldImport&) const = default;
};

// ---- server management (headless) ----
//
// The servers of an instance live in its `servers.dat` file. They are
// addressed by their index in the list or by their name.

/// @brief how a server handles the resource packs of the client
enum class AcceptsTextures : std::uint8_t { Ask, Always, Never };

struct ServerList {
    QString instance;
    bool ping = false;
    auto operator<=>(const ServerList&) const = default;
};

struct ServerAdd {
    QString instance;
    QString name;
    QString address;
    /// The position of the new server in the list - it is appended when it is
    /// past the end of the list.
    std::optional<int> index;
    AcceptsTextures acceptsTextures = AcceptsTextures::Ask;
    auto operator<=>(const ServerAdd&) const = default;
};

struct ServerRemove {
    QString instance;
    /// The index of the server or its name.
    QString target;
    bool force = false;
    auto operator<=>(const ServerRemove&) const = default;
};

// ---- log management (headless) ----
//
// The logs of an instance live in the folders that the launcher searches for
// logs (`crash-reports`, `logs` and the game root itself). They are addressed
// by their path, relative to the game root.

struct LogList {
    QString instance;
    auto operator<=>(const LogList&) const = default;
};

struct LogRead {
    QString instance;
    /// The path of the log, relative to the game root of the instance.
    std::optional<QString> file;
    /// The number of the lines to read from the beginning of the log.
    std::optional<int> head;
    /// The number of the lines to read from the end of the log.
    std::optional<int> tail;
    /// The lines that do not match the expression are skipped.
    std::optional<QString> grep;
    /// The lines that are less severe than the level are skipped.
    std::optional<QString> level;
    /// Report the lines as JSON instead of writing them to the output.
    bool json = false;
    auto operator<=>(const LogRead&) const = default;
};

struct LogUpload {
    QString instance;
    /// The path of the log, relative to the game root of the instance.
    std::optional<QString> file;
    /// The paste service the log is uploaded to.
    QString service = QStringLiteral("mclogs");
    bool anonymize = false;
    bool assumeYes = false;
    auto operator<=>(const LogUpload&) const = default;
};

};  // namespace Cmd

using CommandBase = std::variant<Cmd::Launch,
                                 Cmd::ProcessURI,
                                 Cmd::ShowMainWindow,
                                 Cmd::ShowInstanceWindow,
                                 Cmd::Alive,
                                 Cmd::ModList,
                                 Cmd::ModSearch,
                                 Cmd::ModInfo,
                                 Cmd::ModInstall,
                                 Cmd::ModRemove,
                                 Cmd::ModEnable,
                                 Cmd::ModDisable,
                                 Cmd::ModUpdate,
                                 Cmd::PackList,
                                 Cmd::PackSearch,
                                 Cmd::PackInfo,
                                 Cmd::PackInstall,
                                 Cmd::PackRemove,
                                 Cmd::PackEnable,
                                 Cmd::PackDisable,
                                 Cmd::WorldList,
                                 Cmd::WorldInfo,
                                 Cmd::WorldCopy,
                                 Cmd::WorldRename,
                                 Cmd::WorldDelete,
                                 Cmd::WorldImport,
                                 Cmd::ServerList,
                                 Cmd::ServerAdd,
                                 Cmd::ServerRemove,
                                 Cmd::LogList,
                                 Cmd::LogRead,
                                 Cmd::LogUpload>;
struct Command : CommandBase {
    using CommandBase::CommandBase;
};

/// @brief a command that can be executed without a GUI/window (headless)
inline bool isHeadlessCommand(const Command& cmd)
{
    return std::holds_alternative<Cmd::ModList>(cmd) || std::holds_alternative<Cmd::ModSearch>(cmd) ||
           std::holds_alternative<Cmd::ModInfo>(cmd) || std::holds_alternative<Cmd::ModInstall>(cmd) ||
           std::holds_alternative<Cmd::ModRemove>(cmd) || std::holds_alternative<Cmd::ModEnable>(cmd) ||
           std::holds_alternative<Cmd::ModDisable>(cmd) || std::holds_alternative<Cmd::ModUpdate>(cmd) ||
           std::holds_alternative<Cmd::PackList>(cmd) || std::holds_alternative<Cmd::PackSearch>(cmd) ||
           std::holds_alternative<Cmd::PackInfo>(cmd) || std::holds_alternative<Cmd::PackInstall>(cmd) ||
           std::holds_alternative<Cmd::PackRemove>(cmd) || std::holds_alternative<Cmd::PackEnable>(cmd) ||
           std::holds_alternative<Cmd::PackDisable>(cmd) || std::holds_alternative<Cmd::WorldList>(cmd) ||
           std::holds_alternative<Cmd::WorldInfo>(cmd) || std::holds_alternative<Cmd::WorldCopy>(cmd) ||
           std::holds_alternative<Cmd::WorldRename>(cmd) || std::holds_alternative<Cmd::WorldDelete>(cmd) ||
           std::holds_alternative<Cmd::WorldImport>(cmd) || std::holds_alternative<Cmd::ServerList>(cmd) ||
           std::holds_alternative<Cmd::ServerAdd>(cmd) || std::holds_alternative<Cmd::ServerRemove>(cmd) ||
           std::holds_alternative<Cmd::LogList>(cmd) || std::holds_alternative<Cmd::LogRead>(cmd) ||
           std::holds_alternative<Cmd::LogUpload>(cmd);
}

/// @brief true when there is at least one command and all of them are headless
inline bool allCommandsHeadless(const std::vector<Command>& commands)
{
    return !commands.empty() && std::ranges::all_of(commands, isHeadlessCommand);
}

struct Args {
    Startup::DataPathResult dataPath;
    std::vector<Command> commands;
    auto operator<=>(const Args&) const = default;
};

}  // namespace Cli
