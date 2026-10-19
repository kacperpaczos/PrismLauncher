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

/// The name of a pack group in the IPC encoding.
QString packGroupToArg(Cli::Cmd::PackGroup group)
{
    switch (group) {
        case Cli::Cmd::PackGroup::Resource:
            return QStringLiteral("resource");
        case Cli::Cmd::PackGroup::Shader:
            return QStringLiteral("shader");
    }
    return QStringLiteral("resource");
}

/// The pack group of an IPC argument. An unknown value is the resource group.
Cli::Cmd::PackGroup packGroupFromArg(const QString& value)
{
    if (value == QLatin1String("shader")) {
        return Cli::Cmd::PackGroup::Shader;
    }
    return Cli::Cmd::PackGroup::Resource;
}

}  // namespace

Result<> ApplicationMessage::parse(const QByteArray& input)
{
    TRY_INTO(const auto& root, Json::requireObject(input, "ApplicationMessage"))

    command = root.value("command").toString();
    args.clear();

    auto parsedArgs = root.value("args").toObject();
    for (auto iter = parsedArgs.constBegin(); iter != parsedArgs.constEnd(); iter++) {
        args.insert(iter.key(), iter.value().toString());
    }
    return {};
}

QByteArray ApplicationMessage::serialize() const
{
    QJsonObject root;
    root.insert("command", command);
    QJsonObject outArgs;
    for (auto iter = args.constBegin(); iter != args.constEnd(); iter++) {
        outArgs.insert(iter.key(), iter.value());
    }
    root.insert("args", outArgs);

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
    } serializer{};

    std::visit(serializer, cmd);
    return serializer.message;
}
