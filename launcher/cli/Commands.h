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
#include <filesystem>
#include <optional>
#include <ranges>
#include <string_view>
#include <variant>
#include <vector>

#include <QString>

#include "startup/Startup.h"

class Application;

namespace Cli {

static constexpr std::string_view g_liveCheckFile = "live.check";

/// Context handed to every command runner. It carries the application
/// (instances, settings, network, ...) without pulling Application.h into the
/// headers of the commands.
struct ModContext {
    Application& app;
};

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

// ---- instance management (headless) ----

struct InstanceList {
    std::optional<QString> group;
    auto operator<=>(const InstanceList&) const = default;
};

struct InstanceInfo {
    QString id;
    auto operator<=>(const InstanceInfo&) const = default;
};

struct InstanceCreate {
    // The name of the new instance.
    QString name;
    // The Minecraft version of the new instance.
    QString mcVersion;
    // The mod loader of the new instance (`fabric`, `forge`, `neoforge`,
    // `quilt` or `none`).
    std::optional<QString> loader;
    // The version of the mod loader (`latest` for the newest one).
    std::optional<QString> loaderVersion;
    // The group of the new instance.
    std::optional<QString> group;
    // The icon of the new instance (an icon key or the path of an image).
    std::optional<QString> icon;
    // Whether the files of the instance are downloaded.
    bool download = true;
    // Import an instance from a URL or a file instead of creating a vanilla
    // one.
    std::optional<QString> from;
    auto operator<=>(const InstanceCreate&) const = default;
};

struct InstanceDuplicate {
    QString id;
    // The name of the copy (the name of the original with a suffix by
    // default).
    std::optional<QString> name;
    // How the files of the instance are copied (`copy`, `links`, `hardlinks`
    // or `clone`).
    QString copyMode = QStringLiteral("copy");
    // The parts of the instance that are not copied.
    bool noSaves = false;
    bool noMods = false;
    bool noResourcePacks = false;
    bool noShaderPacks = false;
    bool noOptions = false;
    bool noConfigs = false;
    auto operator<=>(const InstanceDuplicate&) const = default;
};

struct InstanceRename {
    QString id;
    QString newName;
    // Whether the folder of the instance is renamed too (the id of the
    // instance changes with it).
    bool renameFolder = true;
    auto operator<=>(const InstanceRename&) const = default;
};

struct InstanceDelete {
    QString id;
    // Whether the files of the instance are deleted instead of being moved to
    // the trash.
    bool purge = false;
    auto operator<=>(const InstanceDelete&) const = default;
};

struct InstanceIconSet {
    QString id;
    // The icon key or the path of an image file.
    QString icon;
    auto operator<=>(const InstanceIconSet&) const = default;
};

// ---- instance settings (headless) ----

struct SettingsList {
    QString id;
    // Whether the settings that are not part of the curated schema are
    // listed too.
    bool all = false;
    // Whether only the settings the instance overrides are listed.
    bool modified = false;
    auto operator<=>(const SettingsList&) const = default;
};

struct SettingsGet {
    QString id;
    std::vector<QString> keys;
    auto operator<=>(const SettingsGet&) const = default;
};

struct SettingsSet {
    QString id;
    // The `key=value` pairs that are written.
    std::vector<std::pair<QString, QString>> values;
    auto operator<=>(const SettingsSet&) const = default;
};

struct SettingsUnset {
    QString id;
    std::vector<QString> keys;
    auto operator<=>(const SettingsUnset&) const = default;
};

// ---- component profile / version management (headless) ----

struct VersionShow {
    QString id;
    auto operator<=>(const VersionShow&) const = default;
};

struct VersionListAvailable {
    // The mod loader whose versions are listed (`fabric`, `forge`,
    // `neoforge` or `quilt`).
    std::optional<QString> loader;
    // The Minecraft version the versions of the loader have to support.
    std::optional<QString> mcVersion;
    // The type of the versions (`release`, `snapshot` or `all`).
    QString type = QStringLiteral("all");
    auto operator<=>(const VersionListAvailable&) const = default;
};

struct VersionChangeMc {
    QString id;
    QString version;
    auto operator<=>(const VersionChangeMc&) const = default;
};

struct VersionInstallLoader {
    QString id;
    // The mod loader (`fabric`, `forge`, `neoforge` or `quilt`).
    QString loader;
    // The version of the mod loader (`latest` for the newest one).
    QString version;
    // What happens to a mod loader that conflicts with the new one (`keep`,
    // `disable` or `replace`).
    QString forceConflict = QStringLiteral("disable");
    auto operator<=>(const VersionInstallLoader&) const = default;
};

struct VersionRemoveLoader {
    QString id;
    QString loader;
    auto operator<=>(const VersionRemoveLoader&) const = default;
};

struct VersionReinstall {
    QString id;
    auto operator<=>(const VersionReinstall&) const = default;
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
                                 Cmd::InstanceList,
                                 Cmd::InstanceInfo,
                                 Cmd::InstanceCreate,
                                 Cmd::InstanceDuplicate,
                                 Cmd::InstanceRename,
                                 Cmd::InstanceDelete,
                                 Cmd::InstanceIconSet,
                                 Cmd::SettingsList,
                                 Cmd::SettingsGet,
                                 Cmd::SettingsSet,
                                 Cmd::SettingsUnset,
                                 Cmd::VersionShow,
                                 Cmd::VersionListAvailable,
                                 Cmd::VersionChangeMc,
                                 Cmd::VersionInstallLoader,
                                 Cmd::VersionRemoveLoader,
                                 Cmd::VersionReinstall>;
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
           std::holds_alternative<Cmd::InstanceList>(cmd) || std::holds_alternative<Cmd::InstanceInfo>(cmd) ||
           std::holds_alternative<Cmd::InstanceCreate>(cmd) || std::holds_alternative<Cmd::InstanceDuplicate>(cmd) ||
           std::holds_alternative<Cmd::InstanceRename>(cmd) || std::holds_alternative<Cmd::InstanceDelete>(cmd) ||
           std::holds_alternative<Cmd::InstanceIconSet>(cmd) || std::holds_alternative<Cmd::SettingsList>(cmd) ||
           std::holds_alternative<Cmd::SettingsGet>(cmd) || std::holds_alternative<Cmd::SettingsSet>(cmd) ||
           std::holds_alternative<Cmd::SettingsUnset>(cmd) || std::holds_alternative<Cmd::VersionShow>(cmd) ||
           std::holds_alternative<Cmd::VersionListAvailable>(cmd) || std::holds_alternative<Cmd::VersionChangeMc>(cmd) ||
           std::holds_alternative<Cmd::VersionInstallLoader>(cmd) ||
           std::holds_alternative<Cmd::VersionRemoveLoader>(cmd) || std::holds_alternative<Cmd::VersionReinstall>(cmd);
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
