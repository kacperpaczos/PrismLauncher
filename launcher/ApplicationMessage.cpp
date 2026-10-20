// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "ApplicationMessage.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <variant>
#include "Json.h"
#include "Result.h"

namespace {

QString boolToArg(bool value)
{
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

bool argToBool(const QString& value)
{
    return value == QLatin1String("1") || value == QLatin1String("true");
}

QString joinStringList(const std::vector<QString>& values)
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values) {
        parts << value;
    }
    return parts.join(QLatin1Char('\n'));
}

std::vector<QString> splitStringList(const QString& value)
{
    std::vector<QString> out;
    if (value.isEmpty()) {
        return out;
    }
    const auto parts = value.split(QLatin1Char('\n'));
    out.reserve(static_cast<std::size_t>(parts.size()));
    for (const auto& part : parts) {
        out.emplace_back(part);
    }
    return out;
}

/// An optional value is stored as an empty string when it is not set.
std::optional<QString> optionalArg(const QHash<QString, QString>& args, const QString& key)
{
    if (!args.contains(key)) {
        return std::nullopt;
    }
    const auto value = args.value(key);
    if (value.isEmpty()) {
        return std::nullopt;
    }
    return value;
}

void insertOptional(QHash<QString, QString>& args, const QString& key, const std::optional<QString>& value)
{
    args.insert(key, value.has_value() ? *value : QString());
}

/// The `key=value` pairs of a settings change, joined with newlines.
QString joinKeyValues(const std::vector<std::pair<QString, QString>>& values)
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& [key, value] : values) {
        parts << (key + QLatin1Char('=') + value);
    }
    return parts.join(QLatin1Char('\n'));
}

/// The `key=value` pairs of a settings change, read from a joined list.
std::vector<std::pair<QString, QString>> splitKeyValues(const QString& value)
{
    std::vector<std::pair<QString, QString>> out;
    if (value.isEmpty()) {
        return out;
    }
    const auto parts = value.split(QLatin1Char('\n'));
    out.reserve(static_cast<std::size_t>(parts.size()));
    for (const auto& part : parts) {
        const auto separator = part.indexOf(QLatin1Char('='));
        if (separator < 0) {
            continue;
        }
        out.emplace_back(part.left(separator), part.mid(separator + 1));
    }
    return out;
}

/// The name of the way a server handles the resource packs in the IPC
/// encoding.
QString acceptsTexturesToArg(Cli::Cmd::AcceptsTextures value)
{
    switch (value) {
        case Cli::Cmd::AcceptsTextures::Always:
            return QStringLiteral("always");
        case Cli::Cmd::AcceptsTextures::Never:
            return QStringLiteral("never");
        case Cli::Cmd::AcceptsTextures::Ask:
            break;
    }
    return QStringLiteral("ask");
}

/// The way a server handles the resource packs. An unknown value asks.
Cli::Cmd::AcceptsTextures acceptsTexturesFromArg(const QString& value)
{
    if (value == QLatin1String("always")) {
        return Cli::Cmd::AcceptsTextures::Always;
    }
    if (value == QLatin1String("never")) {
        return Cli::Cmd::AcceptsTextures::Never;
    }
    return Cli::Cmd::AcceptsTextures::Ask;
}

/// The name of a pack group in the IPC encoding.
QString packGroupToArg(Cli::Cmd::PackGroup group)
{
    switch (group) {
        case Cli::Cmd::PackGroup::Resource:
            return QStringLiteral("resource");
        case Cli::Cmd::PackGroup::Shader:
            return QStringLiteral("shader");
        case Cli::Cmd::PackGroup::Texture:
            return QStringLiteral("texture");
    }
    return QStringLiteral("resource");
}

/// The pack group of an IPC argument. An unknown value is the resource group.
Cli::Cmd::PackGroup packGroupFromArg(const QString& value)
{
    if (value == QLatin1String("shader")) {
        return Cli::Cmd::PackGroup::Shader;
    }
    if (value == QLatin1String("texture")) {
        return Cli::Cmd::PackGroup::Texture;
    }
    return Cli::Cmd::PackGroup::Resource;
}

}  // namespace

Result<> ApplicationMessage::parse(const QByteArray& input)
{
    TRY_INTO(const auto& root, Json::requireObject(input, "ApplicationMessage"))

    // The messages of the first version of the protocol have no type - they
    // are requests.
    const QString messageType = root.value("type").toString();
    if (messageType == QLatin1String("accepted")) {
        type = ApplicationMessageType::Accepted;
    } else if (messageType == QLatin1String("response")) {
        type = ApplicationMessageType::Response;
    } else {
        type = ApplicationMessageType::Request;
    }

    requestId = root.value("requestId").toString();
    command = root.value("command").toString();
    args.clear();

    auto parsedArgs = root.value("args").toObject();
    for (auto iter = parsedArgs.constBegin(); iter != parsedArgs.constEnd(); iter++) {
        args.insert(iter.key(), iter.value().toString());
    }

    ok = root.value("ok").toBool();
    exitCode = root.value("exitCode").toInt();
    data = root.value("data").toObject();
    rawOutput = root.value("rawOutput").toBool();

    const auto error = root.value("error").toObject();
    errorCode = error.value("code").toString();
    errorMessage = error.value("message").toString();

    return {};
}

QByteArray ApplicationMessage::serialize() const
{
    QJsonObject root;
    switch (type) {
        case ApplicationMessageType::Request: {
            // A request without a request id is a message of the first version
            // of the protocol - it is not answered. Keeping the format the
            // same lets an old launcher handle it.
            if (!requestId.isEmpty()) {
                root.insert("type", QStringLiteral("request"));
                root.insert("requestId", requestId);
            }
            root.insert("command", command);
            QJsonObject outArgs;
            for (auto iter = args.constBegin(); iter != args.constEnd(); iter++) {
                outArgs.insert(iter.key(), iter.value());
            }
            root.insert("args", outArgs);
            break;
        }
        case ApplicationMessageType::Accepted: {
            root.insert("type", QStringLiteral("accepted"));
            root.insert("requestId", requestId);
            break;
        }
        case ApplicationMessageType::Response: {
            root.insert("type", QStringLiteral("response"));
            root.insert("requestId", requestId);
            root.insert("ok", ok);
            root.insert("exitCode", exitCode);
            root.insert("data", data);
            if (rawOutput) {
                root.insert("rawOutput", true);
            }
            if (!errorCode.isEmpty()) {
                QJsonObject error;
                error.insert("code", errorCode);
                error.insert("message", errorMessage);
                root.insert("error", error);
            }
            break;
        }
    }

    return Json::toText(root);
}

Result<Cli::Command> ApplicationMessage::toCliCommand() const
{
    // Launch
    if (command == "launch") {
        const QString notFoundMsg = "missing `%1` for Launch command";
        TRY_INTO(auto targetType, Try::findByMapKey(args, "targetType", notFoundMsg));
        TRY_INTO(auto target, Try::findByMapKey(args, "target", notFoundMsg));
        TRY_INTO(auto accountType, Try::findByMapKey(args, "accountType", notFoundMsg));
        TRY_INTO(auto account, Try::findByMapKey(args, "account", notFoundMsg));
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));

        Cli::Cmd::Launch launch{
            .id = id.toStdString(),
        };

        if (targetType == "no_target") {
            launch.target = Cli::Cmd::Launch::NoTarget{};
        } else if (targetType == "server") {
            launch.target = Cli::Cmd::Launch::ServerTarget{
                .target = target.toStdString(),
            };
        } else if (targetType == "world") {
            launch.target = Cli::Cmd::Launch::WorldTarget{
                .target = target.toStdString(),
            };
        } else {
            return std::unexpected{ QString("unknown launch target type `%1`").arg(targetType) };
        }

        if (accountType == "default") {
            launch.account = Cli::Cmd::Launch::AccountDefault{};
        } else if (accountType == "profile") {
            launch.account = Cli::Cmd::Launch::AccountProfile{
                .name = account.toStdString(),
            };

        } else if (accountType == "offline") {
            launch.account = Cli::Cmd::Launch::AccountOffline{
                .name = account.toStdString(),
            };
        } else {
            return std::unexpected{ QString("unknown account type `%1`").arg(accountType) };
        }

        return launch;
    }

    // ProcessUri
    if (command == "processUri") {
        const QString notFoundMsg = "missing `%1` for processUri command";
        TRY_INTO(auto uri, Try::findByMapKey(args, "uri", notFoundMsg));

        return Cli::Cmd::ProcessURI{ .uri = uri.toStdString() };
    }

    // ShowMainWindow
    if (command == "showMainWindow") {
        return Cli::Cmd::ShowMainWindow{};
    }

    // ShowInstanceWindow
    if (command == "showInstanceWindow") {
        const QString notFoundMsg = "missing `%1` for showInstanceWindow command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));

        return Cli::Cmd::ShowInstanceWindow{ .id = id.toStdString() };
    }

    // Alive
    if (command == "alive") {
        const QString notFoundMsg = "missing `%1` for alive command";
        TRY_INTO(auto path, Try::findByMapKey(args, "path", notFoundMsg));
        return Cli::Cmd::Alive{ .path = path.toStdU16String() };
    }

    // ModList
    if (command == "mod_list") {
        const QString notFoundMsg = "missing `%1` for mod_list command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::ModList{
            .instance = instance,
            .includeDisabled = argToBool(args.value("includeDisabled")),
        };
    }

    // ModSearch
    if (command == "mod_search") {
        return Cli::Cmd::ModSearch{
            .query = args.value("query"),
            .mcVersions = splitStringList(args.value("mcVersions")),
            .loaders = splitStringList(args.value("loaders")),
            .limit = args.value("limit").toInt(),
            .offset = args.value("offset").toInt(),
        };
    }

    // ModInfo
    if (command == "mod_info") {
        const QString notFoundMsg = "missing `%1` for mod_info command";
        TRY_INTO(auto project, Try::findByMapKey(args, "project", notFoundMsg));
        Cli::Cmd::ModInfo cmd{ .project = project };
        if (args.contains("instance")) {
            cmd.instance = args.value("instance");
        }
        return cmd;
    }

    // ModInstall
    if (command == "mod_install") {
        const QString notFoundMsg = "missing `%1` for mod_install command";
        TRY_INTO(auto project, Try::findByMapKey(args, "project", notFoundMsg));
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        Cli::Cmd::ModInstall cmd{
            .project = project,
            .instance = instance,
            .noDeps = argToBool(args.value("noDeps")),
            .noMetadata = argToBool(args.value("noMetadata")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
        if (args.contains("version")) {
            cmd.version = args.value("version");
        }
        return cmd;
    }

    // ModRemove
    if (command == "mod_remove") {
        const QString notFoundMsg = "missing `%1` for mod_remove command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::ModRemove{
            .targets = splitStringList(args.value("targets")),
            .instance = instance,
            .force = argToBool(args.value("force")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
    }

    // ModEnable
    if (command == "mod_enable") {
        const QString notFoundMsg = "missing `%1` for mod_enable command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::ModEnable{
            .targets = splitStringList(args.value("targets")),
            .instance = instance,
            .recursive = argToBool(args.value("recursive")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
    }

    // ModDisable
    if (command == "mod_disable") {
        const QString notFoundMsg = "missing `%1` for mod_disable command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::ModDisable{
            .targets = splitStringList(args.value("targets")),
            .instance = instance,
            .recursive = argToBool(args.value("recursive")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
    }

    // ModUpdate
    if (command == "mod_update") {
        const QString notFoundMsg = "missing `%1` for mod_update command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::ModUpdate{
            .targets = splitStringList(args.value("targets")),
            .all = argToBool(args.value("all")),
            .instance = instance,
            .dryRun = argToBool(args.value("dryRun")),
            .noDeps = argToBool(args.value("noDeps")),
            .force = argToBool(args.value("force")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
    }

    // InstanceList
    if (command == "instance_list") {
        Cli::Cmd::InstanceList cmd;
        cmd.group = optionalArg(args, "group");
        return cmd;
    }

    // InstanceInfo
    if (command == "instance_info") {
        const QString notFoundMsg = "missing `%1` for instance_info command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        return Cli::Cmd::InstanceInfo{ .id = id };
    }

    // InstanceCreate
    if (command == "instance_create") {
        const QString notFoundMsg = "missing `%1` for instance_create command";
        TRY_INTO(auto name, Try::findByMapKey(args, "name", notFoundMsg));
        return Cli::Cmd::InstanceCreate{
            .name = name,
            .mcVersion = args.value("mcVersion"),
            .loader = optionalArg(args, "loader"),
            .loaderVersion = optionalArg(args, "loaderVersion"),
            .group = optionalArg(args, "group"),
            .icon = optionalArg(args, "icon"),
            .download = argToBool(args.value("download")),
            .from = optionalArg(args, "from"),
        };
    }

    // InstanceDuplicate
    if (command == "instance_duplicate") {
        const QString notFoundMsg = "missing `%1` for instance_duplicate command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        return Cli::Cmd::InstanceDuplicate{
            .id = id,
            .name = optionalArg(args, "name"),
            .copyMode = args.value("copyMode"),
            .noSaves = argToBool(args.value("noSaves")),
            .noMods = argToBool(args.value("noMods")),
            .noResourcePacks = argToBool(args.value("noResourcePacks")),
            .noShaderPacks = argToBool(args.value("noShaderPacks")),
            .noOptions = argToBool(args.value("noOptions")),
            .noConfigs = argToBool(args.value("noConfigs")),
        };
    }

    // InstanceRename
    if (command == "instance_rename") {
        const QString notFoundMsg = "missing `%1` for instance_rename command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto newName, Try::findByMapKey(args, "newName", notFoundMsg));
        return Cli::Cmd::InstanceRename{
            .id = id,
            .newName = newName,
            .renameFolder = argToBool(args.value("renameFolder")),
        };
    }

    // InstanceDelete
    if (command == "instance_delete") {
        const QString notFoundMsg = "missing `%1` for instance_delete command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        return Cli::Cmd::InstanceDelete{
            .id = id,
            .purge = argToBool(args.value("purge")),
        };
    }

    // InstanceIconSet
    if (command == "instance_icon_set") {
        const QString notFoundMsg = "missing `%1` for instance_icon_set command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto icon, Try::findByMapKey(args, "icon", notFoundMsg));
        return Cli::Cmd::InstanceIconSet{ .id = id, .icon = icon };
    }

    // SettingsList
    if (command == "settings_list") {
        const QString notFoundMsg = "missing `%1` for settings_list command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        return Cli::Cmd::SettingsList{
            .id = id,
            .all = argToBool(args.value("all")),
            .modified = argToBool(args.value("modified")),
        };
    }

    // SettingsGet
    if (command == "settings_get") {
        const QString notFoundMsg = "missing `%1` for settings_get command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto keys, Try::findByMapKey(args, "keys", notFoundMsg));
        return Cli::Cmd::SettingsGet{
            .id = id,
            .keys = splitStringList(keys),
        };
    }

    // SettingsSet
    if (command == "settings_set") {
        const QString notFoundMsg = "missing `%1` for settings_set command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto values, Try::findByMapKey(args, "values", notFoundMsg));
        return Cli::Cmd::SettingsSet{
            .id = id,
            .values = splitKeyValues(values),
        };
    }

    // SettingsUnset
    if (command == "settings_unset") {
        const QString notFoundMsg = "missing `%1` for settings_unset command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto keys, Try::findByMapKey(args, "keys", notFoundMsg));
        return Cli::Cmd::SettingsUnset{
            .id = id,
            .keys = splitStringList(keys),
        };
    }

    // VersionShow
    if (command == "version_show") {
        const QString notFoundMsg = "missing `%1` for version_show command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        return Cli::Cmd::VersionShow{ .id = id };
    }

    // VersionListAvailable
    if (command == "version_list_available") {
        return Cli::Cmd::VersionListAvailable{
            .loader = optionalArg(args, "loader"),
            .mcVersion = optionalArg(args, "mcVersion"),
            .type = args.value("type", "all"),
        };
    }

    // VersionChangeMc
    if (command == "version_change_mc") {
        const QString notFoundMsg = "missing `%1` for version_change_mc command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto version, Try::findByMapKey(args, "version", notFoundMsg));
        return Cli::Cmd::VersionChangeMc{ .id = id, .version = version };
    }

    // VersionInstallLoader
    if (command == "version_install_loader") {
        const QString notFoundMsg = "missing `%1` for version_install_loader command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto loader, Try::findByMapKey(args, "loader", notFoundMsg));
        TRY_INTO(auto version, Try::findByMapKey(args, "version", notFoundMsg));
        return Cli::Cmd::VersionInstallLoader{
            .id = id,
            .loader = loader,
            .version = version,
            .forceConflict = args.value("forceConflict", "disable"),
        };
    }

    // VersionRemoveLoader
    if (command == "version_remove_loader") {
        const QString notFoundMsg = "missing `%1` for version_remove_loader command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto loader, Try::findByMapKey(args, "loader", notFoundMsg));
        return Cli::Cmd::VersionRemoveLoader{ .id = id, .loader = loader };
    }

    // VersionReinstall
    if (command == "version_reinstall") {
        const QString notFoundMsg = "missing `%1` for version_reinstall command";
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        return Cli::Cmd::VersionReinstall{ .id = id };
    }

    // PackList
    if (command == "pack_list") {
        const QString notFoundMsg = "missing `%1` for pack_list command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::PackList{
            .group = packGroupFromArg(args.value("group")),
            .instance = instance,
            .includeDisabled = argToBool(args.value("includeDisabled")),
        };
    }

    // PackSearch
    if (command == "pack_search") {
        return Cli::Cmd::PackSearch{
            .group = packGroupFromArg(args.value("group")),
            .query = args.value("query"),
            .mcVersions = splitStringList(args.value("mcVersions")),
            .limit = args.value("limit").toInt(),
            .offset = args.value("offset").toInt(),
        };
    }

    // PackInfo
    if (command == "pack_info") {
        const QString notFoundMsg = "missing `%1` for pack_info command";
        TRY_INTO(auto project, Try::findByMapKey(args, "project", notFoundMsg));
        Cli::Cmd::PackInfo cmd{
            .group = packGroupFromArg(args.value("group")),
            .project = project,
        };
        if (args.contains("instance")) {
            cmd.instance = args.value("instance");
        }
        return cmd;
    }

    // PackInstall
    if (command == "pack_install") {
        const QString notFoundMsg = "missing `%1` for pack_install command";
        TRY_INTO(auto project, Try::findByMapKey(args, "project", notFoundMsg));
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        Cli::Cmd::PackInstall cmd{
            .group = packGroupFromArg(args.value("group")),
            .project = project,
            .instance = instance,
            .noMetadata = argToBool(args.value("noMetadata")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
        if (args.contains("version")) {
            cmd.version = args.value("version");
        }
        return cmd;
    }

    // PackRemove
    if (command == "pack_remove") {
        const QString notFoundMsg = "missing `%1` for pack_remove command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::PackRemove{
            .group = packGroupFromArg(args.value("group")),
            .targets = splitStringList(args.value("targets")),
            .instance = instance,
            .force = argToBool(args.value("force")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
    }

    // PackEnable
    if (command == "pack_enable") {
        const QString notFoundMsg = "missing `%1` for pack_enable command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::PackEnable{
            .group = packGroupFromArg(args.value("group")),
            .targets = splitStringList(args.value("targets")),
            .instance = instance,
            .assumeYes = argToBool(args.value("assumeYes")),
        };
    }

    // PackDisable
    if (command == "pack_disable") {
        const QString notFoundMsg = "missing `%1` for pack_disable command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::PackDisable{
            .group = packGroupFromArg(args.value("group")),
            .targets = splitStringList(args.value("targets")),
            .instance = instance,
            .assumeYes = argToBool(args.value("assumeYes")),
        };
    }

    // WorldList
    if (command == "world_list") {
        const QString notFoundMsg = "missing `%1` for world_list command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::WorldList{
            .instance = instance,
            .withSize = argToBool(args.value("withSize")),
        };
    }

    // WorldInfo
    if (command == "world_info") {
        const QString notFoundMsg = "missing `%1` for world_info command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto target, Try::findByMapKey(args, "target", notFoundMsg));
        return Cli::Cmd::WorldInfo{
            .instance = instance,
            .target = target,
            .withSize = argToBool(args.value("withSize")),
        };
    }

    // WorldCopy
    if (command == "world_copy") {
        const QString notFoundMsg = "missing `%1` for world_copy command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto target, Try::findByMapKey(args, "target", notFoundMsg));
        Cli::Cmd::WorldCopy cmd{
            .instance = instance,
            .target = target,
        };
        if (args.contains("name")) {
            cmd.name = args.value("name");
        }
        return cmd;
    }

    // WorldRename
    if (command == "world_rename") {
        const QString notFoundMsg = "missing `%1` for world_rename command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto target, Try::findByMapKey(args, "target", notFoundMsg));
        TRY_INTO(auto name, Try::findByMapKey(args, "name", notFoundMsg));
        return Cli::Cmd::WorldRename{
            .instance = instance,
            .target = target,
            .name = name,
        };
    }

    // WorldDelete
    if (command == "world_delete") {
        const QString notFoundMsg = "missing `%1` for world_delete command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto target, Try::findByMapKey(args, "target", notFoundMsg));
        return Cli::Cmd::WorldDelete{
            .instance = instance,
            .target = target,
            .permanent = argToBool(args.value("permanent")),
        };
    }

    // WorldImport
    if (command == "world_import") {
        const QString notFoundMsg = "missing `%1` for world_import command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto path, Try::findByMapKey(args, "path", notFoundMsg));
        Cli::Cmd::WorldImport cmd{
            .instance = instance,
            .path = path,
        };
        if (args.contains("name")) {
            cmd.name = args.value("name");
        }
        return cmd;
    }

    // LogList
    if (command == "log_list") {
        const QString notFoundMsg = "missing `%1` for log_list command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::LogList{
            .instance = instance,
        };
    }

    // LogRead
    if (command == "log_read") {
        const QString notFoundMsg = "missing `%1` for log_read command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        Cli::Cmd::LogRead cmd{
            .instance = instance,
            .json = argToBool(args.value("json")),
        };
        if (args.contains("file")) {
            cmd.file = args.value("file");
        }
        if (args.contains("head")) {
            cmd.head = args.value("head").toInt();
        }
        if (args.contains("tail")) {
            cmd.tail = args.value("tail").toInt();
        }
        if (args.contains("grep")) {
            cmd.grep = args.value("grep");
        }
        if (args.contains("level")) {
            cmd.level = args.value("level");
        }
        return cmd;
    }

    // LogUpload
    if (command == "log_upload") {
        const QString notFoundMsg = "missing `%1` for log_upload command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        Cli::Cmd::LogUpload cmd{
            .instance = instance,
            .service = args.value("service"),
            .anonymize = argToBool(args.value("anonymize")),
            .assumeYes = argToBool(args.value("assumeYes")),
        };
        if (args.contains("file")) {
            cmd.file = args.value("file");
        }
        return cmd;
    }

    // NotesShow
    if (command == "notes_show") {
        const QString notFoundMsg = "missing `%1` for notes_show command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::NotesShow{
            .instance = instance,
        };
    }

    // NotesSet
    if (command == "notes_set") {
        const QString notFoundMsg = "missing `%1` for notes_set command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        Cli::Cmd::NotesSet cmd{
            .instance = instance,
            .append = argToBool(args.value("append")),
        };
        if (args.contains("text")) {
            cmd.text = args.value("text");
        }
        if (args.contains("file")) {
            cmd.file = args.value("file");
        }
        return cmd;
    }

    // ScreenshotList
    if (command == "screenshot_list") {
        const QString notFoundMsg = "missing `%1` for screenshot_list command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::ScreenshotList{
            .instance = instance,
        };
    }

    // ScreenshotUpload
    if (command == "screenshot_upload") {
        const QString notFoundMsg = "missing `%1` for screenshot_upload command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto target, Try::findByMapKey(args, "target", notFoundMsg));
        Cli::Cmd::ScreenshotUpload cmd{
            .instance = instance,
            .target = target,
            .assumeYes = argToBool(args.value("assumeYes")),
        };
        if (args.contains("limit")) {
            cmd.limit = args.value("limit").toInt();
        }
        return cmd;
    }

    // ScreenshotCopy
    if (command == "screenshot_copy") {
        const QString notFoundMsg = "missing `%1` for screenshot_copy command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto id, Try::findByMapKey(args, "id", notFoundMsg));
        TRY_INTO(auto destination, Try::findByMapKey(args, "destination", notFoundMsg));
        return Cli::Cmd::ScreenshotCopy{
            .instance = instance,
            .id = id,
            .destination = destination,
            .force = argToBool(args.value("force")),
        };
    }

    // ServerList
    if (command == "server_list") {
        const QString notFoundMsg = "missing `%1` for server_list command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        return Cli::Cmd::ServerList{
            .instance = instance,
            .ping = argToBool(args.value("ping")),
        };
    }

    // ServerAdd
    if (command == "server_add") {
        const QString notFoundMsg = "missing `%1` for server_add command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto name, Try::findByMapKey(args, "name", notFoundMsg));
        TRY_INTO(auto address, Try::findByMapKey(args, "address", notFoundMsg));
        Cli::Cmd::ServerAdd cmd{
            .instance = instance,
            .name = name,
            .address = address,
            .acceptsTextures = acceptsTexturesFromArg(args.value("acceptsTextures")),
        };
        if (args.contains("index")) {
            cmd.index = args.value("index").toInt();
        }
        return cmd;
    }

    // ServerRemove
    if (command == "server_remove") {
        const QString notFoundMsg = "missing `%1` for server_remove command";
        TRY_INTO(auto instance, Try::findByMapKey(args, "instance", notFoundMsg));
        TRY_INTO(auto target, Try::findByMapKey(args, "target", notFoundMsg));
        return Cli::Cmd::ServerRemove{
            .instance = instance,
            .target = target,
            .force = argToBool(args.value("force")),
        };
    }

    return std::unexpected{ QString("unknown cli command `%1`").arg(command) };
}

ApplicationMessage ApplicationMessage::fromCliCommand(const Cli::Command& cmd)
{
    struct CmdVisitorSerializer {
        ApplicationMessage message;

        void operator()(const Cli::Cmd::Launch& launch)
        {
            message.command = "launch";
            struct LaunchTargetSerializer {
                std::pair<QString, QString> operator()(const Cli::Cmd::Launch::NoTarget& /*unused*/) { return { "no_target", "" }; }
                std::pair<QString, QString> operator()(const Cli::Cmd::Launch::ServerTarget& server)
                {
                    return { "server", QString::fromStdString(server.target) };
                }
                std::pair<QString, QString> operator()(const Cli::Cmd::Launch::WorldTarget& world)
                {
                    return { "world", QString::fromStdString(world.target) };
                }
            } targetSerializer{};

            struct LaunchAccountSerializer {
                std::pair<QString, QString> operator()(const Cli::Cmd::Launch::AccountDefault& /*unused*/) { return { "default", "" }; }
                std::pair<QString, QString> operator()(const Cli::Cmd::Launch::AccountProfile& profile)
                {
                    return { "profile", QString::fromStdString(profile.name) };
                }
                std::pair<QString, QString> operator()(const Cli::Cmd::Launch::AccountOffline& offline)
                {
                    return { "offline", QString::fromStdString(offline.name) };
                }
            } accountSerializer{};

            auto [targetType, target] = std::visit(targetSerializer, launch.target);
            auto [accountType, account] = std::visit(accountSerializer, launch.account);

            message.args.insert("targetType", targetType);
            message.args.insert("target", target);
            message.args.insert("accountType", accountType);
            message.args.insert("account", account);
            message.args.insert("id", QString::fromStdString(launch.id));
        }
        void operator()(const Cli::Cmd::ProcessURI& processUri)
        {
            message.command = "processUri";
            message.args.insert("uri", QString::fromStdString(processUri.uri));
        }
        void operator()(const Cli::Cmd::ShowMainWindow& /*unused*/) { message.command = "showMainWindow"; }
        void operator()(const Cli::Cmd::ShowInstanceWindow& showInstanceWindow)
        {
            message.command = "showInstanceWindow";
            message.args.insert("id", QString::fromStdString(showInstanceWindow.id));
        }
        void operator()(const Cli::Cmd::Alive& alive)
        {
            message.command = "alive";
            message.args.insert("path", QString::fromStdU16String(alive.path.u16string()));
        }
        void operator()(const Cli::Cmd::ModList& modList)
        {
            message.command = "mod_list";
            message.args.insert("instance", modList.instance);
            message.args.insert("includeDisabled", boolToArg(modList.includeDisabled));
        }
        void operator()(const Cli::Cmd::ModSearch& modSearch)
        {
            message.command = "mod_search";
            message.args.insert("query", modSearch.query);
            message.args.insert("mcVersions", joinStringList(modSearch.mcVersions));
            message.args.insert("loaders", joinStringList(modSearch.loaders));
            message.args.insert("limit", QString::number(modSearch.limit));
            message.args.insert("offset", QString::number(modSearch.offset));
        }
        void operator()(const Cli::Cmd::ModInfo& modInfo)
        {
            message.command = "mod_info";
            message.args.insert("project", modInfo.project);
            if (modInfo.instance.has_value()) {
                message.args.insert("instance", *modInfo.instance);
            }
        }
        void operator()(const Cli::Cmd::ModInstall& modInstall)
        {
            message.command = "mod_install";
            message.args.insert("project", modInstall.project);
            message.args.insert("instance", modInstall.instance);
            if (modInstall.version.has_value()) {
                message.args.insert("version", *modInstall.version);
            }
            message.args.insert("noDeps", boolToArg(modInstall.noDeps));
            message.args.insert("noMetadata", boolToArg(modInstall.noMetadata));
            message.args.insert("assumeYes", boolToArg(modInstall.assumeYes));
        }
        void operator()(const Cli::Cmd::ModRemove& modRemove)
        {
            message.command = "mod_remove";
            message.args.insert("targets", joinStringList(modRemove.targets));
            message.args.insert("instance", modRemove.instance);
            message.args.insert("force", boolToArg(modRemove.force));
            message.args.insert("assumeYes", boolToArg(modRemove.assumeYes));
        }
        void operator()(const Cli::Cmd::ModEnable& modEnable)
        {
            message.command = "mod_enable";
            message.args.insert("targets", joinStringList(modEnable.targets));
            message.args.insert("instance", modEnable.instance);
            message.args.insert("recursive", boolToArg(modEnable.recursive));
            message.args.insert("assumeYes", boolToArg(modEnable.assumeYes));
        }
        void operator()(const Cli::Cmd::ModDisable& modDisable)
        {
            message.command = "mod_disable";
            message.args.insert("targets", joinStringList(modDisable.targets));
            message.args.insert("instance", modDisable.instance);
            message.args.insert("recursive", boolToArg(modDisable.recursive));
            message.args.insert("assumeYes", boolToArg(modDisable.assumeYes));
        }
        void operator()(const Cli::Cmd::ModUpdate& modUpdate)
        {
            message.command = "mod_update";
            message.args.insert("targets", joinStringList(modUpdate.targets));
            message.args.insert("all", boolToArg(modUpdate.all));
            message.args.insert("instance", modUpdate.instance);
            message.args.insert("dryRun", boolToArg(modUpdate.dryRun));
            message.args.insert("noDeps", boolToArg(modUpdate.noDeps));
            message.args.insert("force", boolToArg(modUpdate.force));
            message.args.insert("assumeYes", boolToArg(modUpdate.assumeYes));
        }
        void operator()(const Cli::Cmd::InstanceList& instanceList)
        {
            message.command = "instance_list";
            insertOptional(message.args, "group", instanceList.group);
        }
        void operator()(const Cli::Cmd::InstanceInfo& instanceInfo)
        {
            message.command = "instance_info";
            message.args.insert("id", instanceInfo.id);
        }
        void operator()(const Cli::Cmd::InstanceCreate& instanceCreate)
        {
            message.command = "instance_create";
            message.args.insert("name", instanceCreate.name);
            message.args.insert("mcVersion", instanceCreate.mcVersion);
            insertOptional(message.args, "loader", instanceCreate.loader);
            insertOptional(message.args, "loaderVersion", instanceCreate.loaderVersion);
            insertOptional(message.args, "group", instanceCreate.group);
            insertOptional(message.args, "icon", instanceCreate.icon);
            message.args.insert("download", boolToArg(instanceCreate.download));
            insertOptional(message.args, "from", instanceCreate.from);
        }
        void operator()(const Cli::Cmd::InstanceDuplicate& instanceDuplicate)
        {
            message.command = "instance_duplicate";
            message.args.insert("id", instanceDuplicate.id);
            insertOptional(message.args, "name", instanceDuplicate.name);
            message.args.insert("copyMode", instanceDuplicate.copyMode);
            message.args.insert("noSaves", boolToArg(instanceDuplicate.noSaves));
            message.args.insert("noMods", boolToArg(instanceDuplicate.noMods));
            message.args.insert("noResourcePacks", boolToArg(instanceDuplicate.noResourcePacks));
            message.args.insert("noShaderPacks", boolToArg(instanceDuplicate.noShaderPacks));
            message.args.insert("noOptions", boolToArg(instanceDuplicate.noOptions));
            message.args.insert("noConfigs", boolToArg(instanceDuplicate.noConfigs));
        }
        void operator()(const Cli::Cmd::InstanceRename& instanceRename)
        {
            message.command = "instance_rename";
            message.args.insert("id", instanceRename.id);
            message.args.insert("newName", instanceRename.newName);
            message.args.insert("renameFolder", boolToArg(instanceRename.renameFolder));
        }
        void operator()(const Cli::Cmd::InstanceDelete& instanceDelete)
        {
            message.command = "instance_delete";
            message.args.insert("id", instanceDelete.id);
            message.args.insert("purge", boolToArg(instanceDelete.purge));
        }
        void operator()(const Cli::Cmd::InstanceIconSet& instanceIconSet)
        {
            message.command = "instance_icon_set";
            message.args.insert("id", instanceIconSet.id);
            message.args.insert("icon", instanceIconSet.icon);
        }
        void operator()(const Cli::Cmd::SettingsList& settingsList)
        {
            message.command = "settings_list";
            message.args.insert("id", settingsList.id);
            message.args.insert("all", boolToArg(settingsList.all));
            message.args.insert("modified", boolToArg(settingsList.modified));
        }
        void operator()(const Cli::Cmd::SettingsGet& settingsGet)
        {
            message.command = "settings_get";
            message.args.insert("id", settingsGet.id);
            message.args.insert("keys", joinStringList(settingsGet.keys));
        }
        void operator()(const Cli::Cmd::SettingsSet& settingsSet)
        {
            message.command = "settings_set";
            message.args.insert("id", settingsSet.id);
            message.args.insert("values", joinKeyValues(settingsSet.values));
        }
        void operator()(const Cli::Cmd::SettingsUnset& settingsUnset)
        {
            message.command = "settings_unset";
            message.args.insert("id", settingsUnset.id);
            message.args.insert("keys", joinStringList(settingsUnset.keys));
        }
        void operator()(const Cli::Cmd::VersionShow& versionShow)
        {
            message.command = "version_show";
            message.args.insert("id", versionShow.id);
        }
        void operator()(const Cli::Cmd::VersionListAvailable& versionListAvailable)
        {
            message.command = "version_list_available";
            insertOptional(message.args, "loader", versionListAvailable.loader);
            insertOptional(message.args, "mcVersion", versionListAvailable.mcVersion);
            message.args.insert("type", versionListAvailable.type);
        }
        void operator()(const Cli::Cmd::VersionChangeMc& versionChangeMc)
        {
            message.command = "version_change_mc";
            message.args.insert("id", versionChangeMc.id);
            message.args.insert("version", versionChangeMc.version);
        }
        void operator()(const Cli::Cmd::VersionInstallLoader& versionInstallLoader)
        {
            message.command = "version_install_loader";
            message.args.insert("id", versionInstallLoader.id);
            message.args.insert("loader", versionInstallLoader.loader);
            message.args.insert("version", versionInstallLoader.version);
            message.args.insert("forceConflict", versionInstallLoader.forceConflict);
        }
        void operator()(const Cli::Cmd::VersionRemoveLoader& versionRemoveLoader)
        {
            message.command = "version_remove_loader";
            message.args.insert("id", versionRemoveLoader.id);
            message.args.insert("loader", versionRemoveLoader.loader);
        }
        void operator()(const Cli::Cmd::VersionReinstall& versionReinstall)
        {
            message.command = "version_reinstall";
            message.args.insert("id", versionReinstall.id);
        }
        void operator()(const Cli::Cmd::PackList& packList)
        {
            message.command = "pack_list";
            message.args.insert("group", packGroupToArg(packList.group));
            message.args.insert("instance", packList.instance);
            message.args.insert("includeDisabled", boolToArg(packList.includeDisabled));
        }
        void operator()(const Cli::Cmd::PackSearch& packSearch)
        {
            message.command = "pack_search";
            message.args.insert("group", packGroupToArg(packSearch.group));
            message.args.insert("query", packSearch.query);
            message.args.insert("mcVersions", joinStringList(packSearch.mcVersions));
            message.args.insert("limit", QString::number(packSearch.limit));
            message.args.insert("offset", QString::number(packSearch.offset));
        }
        void operator()(const Cli::Cmd::PackInfo& packInfo)
        {
            message.command = "pack_info";
            message.args.insert("group", packGroupToArg(packInfo.group));
            message.args.insert("project", packInfo.project);
            if (packInfo.instance.has_value()) {
                message.args.insert("instance", *packInfo.instance);
            }
        }
        void operator()(const Cli::Cmd::PackInstall& packInstall)
        {
            message.command = "pack_install";
            message.args.insert("group", packGroupToArg(packInstall.group));
            message.args.insert("project", packInstall.project);
            message.args.insert("instance", packInstall.instance);
            if (packInstall.version.has_value()) {
                message.args.insert("version", *packInstall.version);
            }
            message.args.insert("noMetadata", boolToArg(packInstall.noMetadata));
            message.args.insert("assumeYes", boolToArg(packInstall.assumeYes));
        }
        void operator()(const Cli::Cmd::PackRemove& packRemove)
        {
            message.command = "pack_remove";
            message.args.insert("group", packGroupToArg(packRemove.group));
            message.args.insert("targets", joinStringList(packRemove.targets));
            message.args.insert("instance", packRemove.instance);
            message.args.insert("force", boolToArg(packRemove.force));
            message.args.insert("assumeYes", boolToArg(packRemove.assumeYes));
        }
        void operator()(const Cli::Cmd::PackEnable& packEnable)
        {
            message.command = "pack_enable";
            message.args.insert("group", packGroupToArg(packEnable.group));
            message.args.insert("targets", joinStringList(packEnable.targets));
            message.args.insert("instance", packEnable.instance);
            message.args.insert("assumeYes", boolToArg(packEnable.assumeYes));
        }
        void operator()(const Cli::Cmd::PackDisable& packDisable)
        {
            message.command = "pack_disable";
            message.args.insert("group", packGroupToArg(packDisable.group));
            message.args.insert("targets", joinStringList(packDisable.targets));
            message.args.insert("instance", packDisable.instance);
            message.args.insert("assumeYes", boolToArg(packDisable.assumeYes));
        }
        void operator()(const Cli::Cmd::WorldList& worldList)
        {
            message.command = "world_list";
            message.args.insert("instance", worldList.instance);
            message.args.insert("withSize", boolToArg(worldList.withSize));
        }
        void operator()(const Cli::Cmd::WorldInfo& worldInfo)
        {
            message.command = "world_info";
            message.args.insert("instance", worldInfo.instance);
            message.args.insert("target", worldInfo.target);
            message.args.insert("withSize", boolToArg(worldInfo.withSize));
        }
        void operator()(const Cli::Cmd::WorldCopy& worldCopy)
        {
            message.command = "world_copy";
            message.args.insert("instance", worldCopy.instance);
            message.args.insert("target", worldCopy.target);
            if (worldCopy.name.has_value()) {
                message.args.insert("name", *worldCopy.name);
            }
        }
        void operator()(const Cli::Cmd::WorldRename& worldRename)
        {
            message.command = "world_rename";
            message.args.insert("instance", worldRename.instance);
            message.args.insert("target", worldRename.target);
            message.args.insert("name", worldRename.name);
        }
        void operator()(const Cli::Cmd::WorldDelete& worldDelete)
        {
            message.command = "world_delete";
            message.args.insert("instance", worldDelete.instance);
            message.args.insert("target", worldDelete.target);
            message.args.insert("permanent", boolToArg(worldDelete.permanent));
        }
        void operator()(const Cli::Cmd::WorldImport& worldImport)
        {
            message.command = "world_import";
            message.args.insert("instance", worldImport.instance);
            message.args.insert("path", worldImport.path);
            if (worldImport.name.has_value()) {
                message.args.insert("name", *worldImport.name);
            }
        }
        void operator()(const Cli::Cmd::ServerList& serverList)
        {
            message.command = "server_list";
            message.args.insert("instance", serverList.instance);
            message.args.insert("ping", boolToArg(serverList.ping));
        }
        void operator()(const Cli::Cmd::ServerAdd& serverAdd)
        {
            message.command = "server_add";
            message.args.insert("instance", serverAdd.instance);
            message.args.insert("name", serverAdd.name);
            message.args.insert("address", serverAdd.address);
            if (serverAdd.index.has_value()) {
                message.args.insert("index", QString::number(*serverAdd.index));
            }
            message.args.insert("acceptsTextures", acceptsTexturesToArg(serverAdd.acceptsTextures));
        }
        void operator()(const Cli::Cmd::ServerRemove& serverRemove)
        {
            message.command = "server_remove";
            message.args.insert("instance", serverRemove.instance);
            message.args.insert("target", serverRemove.target);
            message.args.insert("force", boolToArg(serverRemove.force));
        }
        void operator()(const Cli::Cmd::LogList& logList)
        {
            message.command = "log_list";
            message.args.insert("instance", logList.instance);
        }
        void operator()(const Cli::Cmd::LogRead& logRead)
        {
            message.command = "log_read";
            message.args.insert("instance", logRead.instance);
            if (logRead.file.has_value()) {
                message.args.insert("file", *logRead.file);
            }
            if (logRead.head.has_value()) {
                message.args.insert("head", QString::number(*logRead.head));
            }
            if (logRead.tail.has_value()) {
                message.args.insert("tail", QString::number(*logRead.tail));
            }
            if (logRead.grep.has_value()) {
                message.args.insert("grep", *logRead.grep);
            }
            if (logRead.level.has_value()) {
                message.args.insert("level", *logRead.level);
            }
            message.args.insert("json", boolToArg(logRead.json));
        }
        void operator()(const Cli::Cmd::LogUpload& logUpload)
        {
            message.command = "log_upload";
            message.args.insert("instance", logUpload.instance);
            if (logUpload.file.has_value()) {
                message.args.insert("file", *logUpload.file);
            }
            message.args.insert("service", logUpload.service);
            message.args.insert("anonymize", boolToArg(logUpload.anonymize));
            message.args.insert("assumeYes", boolToArg(logUpload.assumeYes));
        }
        void operator()(const Cli::Cmd::ScreenshotList& screenshotList)
        {
            message.command = "screenshot_list";
            message.args.insert("instance", screenshotList.instance);
        }
        void operator()(const Cli::Cmd::ScreenshotUpload& screenshotUpload)
        {
            message.command = "screenshot_upload";
            message.args.insert("instance", screenshotUpload.instance);
            message.args.insert("target", screenshotUpload.target);
            if (screenshotUpload.limit.has_value()) {
                message.args.insert("limit", QString::number(*screenshotUpload.limit));
            }
            message.args.insert("assumeYes", boolToArg(screenshotUpload.assumeYes));
        }
        void operator()(const Cli::Cmd::ScreenshotCopy& screenshotCopy)
        {
            message.command = "screenshot_copy";
            message.args.insert("instance", screenshotCopy.instance);
            message.args.insert("id", screenshotCopy.id);
            message.args.insert("destination", screenshotCopy.destination);
            message.args.insert("force", boolToArg(screenshotCopy.force));
        }
        void operator()(const Cli::Cmd::NotesShow& notesShow)
        {
            message.command = "notes_show";
            message.args.insert("instance", notesShow.instance);
        }
        void operator()(const Cli::Cmd::NotesSet& notesSet)
        {
            message.command = "notes_set";
            message.args.insert("instance", notesSet.instance);
            if (notesSet.text.has_value()) {
                message.args.insert("text", *notesSet.text);
            }
            if (notesSet.file.has_value()) {
                message.args.insert("file", *notesSet.file);
            }
            message.args.insert("append", boolToArg(notesSet.append));
        }
    } serializer{};

    std::visit(serializer, cmd);
    return serializer.message;
}

ApplicationMessage ApplicationMessage::request(const Cli::Command& cmd, const QString& requestId)
{
    auto message = fromCliCommand(cmd);
    message.type = ApplicationMessageType::Request;
    message.requestId = requestId;
    return message;
}

ApplicationMessage ApplicationMessage::accepted(const QString& requestId)
{
    ApplicationMessage message;
    message.type = ApplicationMessageType::Accepted;
    message.requestId = requestId;
    return message;
}

ApplicationMessage ApplicationMessage::response(const QString& requestId, bool ok, int exitCode, const QJsonObject& data)
{
    ApplicationMessage message;
    message.type = ApplicationMessageType::Response;
    message.requestId = requestId;
    message.ok = ok;
    message.exitCode = exitCode;
    message.data = data;

    // The error of the envelope of the command is copied to the response, so
    // that the client does not have to dig it out of the data.
    const auto error = data.value("error").toObject();
    if (error.value("code").isString()) {
        message.errorCode = error.value("code").toString();
        message.errorMessage = error.value("message").toString();
    }

    return message;
}
