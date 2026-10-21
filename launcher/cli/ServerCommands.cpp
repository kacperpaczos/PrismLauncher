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

#include "cli/ServerCommands.h"

#include <optional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "Application.h"
#include "cli/CommandUtils.h"
#include "cli/Output.h"
#include "cli/TaskRunner.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/ServersDat.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "ui/pages/instance/ServerPingTask.h"

namespace Cli {
namespace {

// Upper bound for the query of the status of a server.
constexpr int g_PingTimeoutMs = 60000;

/// The name of the way a server handles the resource packs of the client.
QString acceptsTexturesName(Server::AcceptsTextures value)
{
    switch (value) {
        case Server::AcceptsTextures::ALWAYS:
            return QStringLiteral("always");
        case Server::AcceptsTextures::NEVER:
            return QStringLiteral("never");
        case Server::AcceptsTextures::ASK:
            break;
    }
    return QStringLiteral("ask");
}

/// The way a server handles the resource packs of the client.
Server::AcceptsTextures acceptsTexturesValue(Cmd::AcceptsTextures value)
{
    switch (value) {
        case Cmd::AcceptsTextures::Always:
            return Server::AcceptsTextures::ALWAYS;
        case Cmd::AcceptsTextures::Never:
            return Server::AcceptsTextures::NEVER;
        case Cmd::AcceptsTextures::Ask:
            break;
    }
    return Server::AcceptsTextures::ASK;
}

/// The JSON of a server, the way the CLI reports it.
QJsonObject serverToJson(const Server& server, int index)
{
    QJsonObject object;
    object.insert(QStringLiteral("index"), index);
    object.insert(QStringLiteral("name"), server.m_name);
    object.insert(QStringLiteral("address"), server.m_address);
    object.insert(QStringLiteral("acceptsTextures"), acceptsTexturesName(server.m_acceptsTextures));
    object.insert(QStringLiteral("hasIcon"), !server.m_icon.isEmpty());
    return object;
}

/**
 * The number of the players that are on a server.
 *
 * The result is empty when the server could not be reached.
 */
std::optional<int> pingServer(const QString& address)
{
    const auto target = MinecraftTarget::fromServerAddress(address);
    auto task = makeShared<ServerPingTask>(target.address, target.port);
    if (!runTaskSync(task, g_PingTimeoutMs)) {
        return std::nullopt;
    }
    return task->m_outputOnlinePlayers;
}

/**
 * Read the server list of an instance.
 *
 * A file that exists but cannot be parsed is never overwritten - the command
 * fails with a parse error instead.
 */
ExitCode loadServers(MinecraftInstance* instance, std::optional<ServersDat>& dat)
{
    ServersDat loaded(ServersDat::pathForGameRoot(instance->gameRoot()));
    if (loaded.load() == ServersDat::State::Corrupted) {
        return outputError(QStringLiteral("corrupted_servers"),
                           QStringLiteral("The server list of the instance could not be parsed - it was not changed"),
                           ExitCode::ParseError);
    }
    dat = std::move(loaded);
    return ExitCode::OK;
}

/// Write the server list of an instance back to its file.
ExitCode saveServers(ServersDat& dat)
{
    if (!dat.save()) {
        return outputError(QStringLiteral("save_failed"), QStringLiteral("The server list could not be written to '%1'").arg(dat.path()), ExitCode::Generic);
    }
    return ExitCode::OK;
}

}  // namespace

ExitCode runServerCommand(CommandContext& ctx, const Cmd::ServerList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    std::optional<ServersDat> dat;
    if (auto code = loadServers(instance, dat); code != ExitCode::OK) {
        return code;
    }

    QJsonArray servers;
    const auto& all = dat->servers();
    for (int i = 0; i < all.size(); ++i) {
        auto object = serverToJson(all.at(i), i);
        if (cmd.ping) {
            const auto players = pingServer(all.at(i).m_address);
            object.insert(QStringLiteral("players"), players ? QJsonValue(*players) : QJsonValue(QJsonValue::Null));
        }
        servers.append(object);
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(servers.size()));
    out.insert(QStringLiteral("servers"), servers);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runServerCommand(CommandContext& ctx, const Cmd::ServerAdd& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    // The check is in-process: a separate CLI process cannot see that an
    // instance is running, so it only guards the commands that are dispatched
    // inside a running launcher.
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("change the servers")); code != ExitCode::OK) {
        return code;
    }

    std::optional<ServersDat> dat;
    if (auto code = loadServers(instance, dat); code != ExitCode::OK) {
        return code;
    }

    Server server(cmd.name, cmd.address);
    server.m_acceptsTextures = acceptsTexturesValue(cmd.acceptsTextures);
    // An index outside of the list appends the server.
    const auto count = dat->servers().size();
    const int position = (cmd.index.has_value() && *cmd.index >= 0 && *cmd.index <= count) ? *cmd.index : count;
    dat->add(server, cmd.index.value_or(-1));

    if (auto code = saveServers(*dat); code != ExitCode::OK) {
        return code;
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("added"), serverToJson(dat->servers().at(position), position));
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runServerCommand(CommandContext& ctx, const Cmd::ServerRemove& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    // The check is in-process: a separate CLI process cannot see that an
    // instance is running, so it only guards the commands that are dispatched
    // inside a running launcher.
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("change the servers")); code != ExitCode::OK) {
        return code;
    }

    std::optional<ServersDat> dat;
    if (auto code = loadServers(instance, dat); code != ExitCode::OK) {
        return code;
    }

    // The target is either the index of the server or its name.
    QList<int> matches;
    bool isIndex = false;
    int index = -1;
    {
        bool ok = false;
        index = cmd.target.toInt(&ok);
        isIndex = ok;
    }
    if (isIndex) {
        if (index < 0 || index >= dat->servers().size()) {
            return outputError(QStringLiteral("server_not_found"),
                               QStringLiteral("There is no server at the index %1").arg(index),
                               ExitCode::NotFound);
        }
        matches.append(index);
    } else {
        for (int i = 0; i < dat->servers().size(); ++i) {
            if (dat->servers().at(i).m_name == cmd.target) {
                matches.append(i);
            }
        }
        if (matches.isEmpty()) {
            return outputError(QStringLiteral("server_not_found"),
                               QStringLiteral("No server with the name '%1' was found").arg(cmd.target),
                               ExitCode::NotFound);
        }
        if (matches.size() > 1 && !cmd.force) {
            QJsonArray candidates;
            for (const auto& match : matches) {
                candidates.append(dat->servers().at(match).m_name);
            }
            QJsonObject details;
            details.insert(QStringLiteral("candidates"), candidates);
            return outputError(QStringLiteral("ambiguous_target"),
                               QStringLiteral("'%1' matches more than one server - use the index of the server instead").arg(cmd.target),
                               ExitCode::Ambiguous,
                               details);
        }
    }

    // The list is not changed while it is iterated, so the servers are
    // removed from the back.
    QJsonArray removed;
    for (auto it = matches.crbegin(); it != matches.crend(); ++it) {
        const auto index = *it;
        auto object = serverToJson(dat->servers().at(index), index);
        dat->removeAt(index);
        removed.prepend(object);
    }

    if (auto code = saveServers(*dat); code != ExitCode::OK) {
        return code;
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("removed"), removed);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
