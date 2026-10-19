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
                                 Cmd::PackDisable>;
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
           std::holds_alternative<Cmd::PackDisable>(cmd);
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
