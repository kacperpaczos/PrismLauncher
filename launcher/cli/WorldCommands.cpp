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

#include "cli/WorldCommands.h"

#include <optional>

#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include "Application.h"
#include "FileSystem.h"
#include "cli/CommandUtils.h"
#include "cli/Output.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/World.h"

namespace Cli {
namespace {

/**
 * The size of a world in bytes.
 *
 * The launcher computes the size of a world in the background, so the value is
 * not available right after the world was read - it is computed here instead.
 */
int64_t worldSize(const QFileInfo& file)
{
    if (file.isFile()) {
        return file.size();
    }
    if (!file.isDir()) {
        return 0;
    }
    int64_t total = 0;
    for (const auto& entry :
         QDirListing(file.absoluteFilePath(),
                     QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::ResolveSymlinks | QDirListing::IteratorFlag::Recursive)) {
        total += entry.fileInfo().size();
    }
    return total;
}

/**
 * The warnings of a world.
 *
 * A world that is a symlink (or that is under one) points outside of the
 * instance, and a world with more than one hard link is also stored somewhere
 * else - the launcher warns about both of them.
 */
QJsonArray worldWarnings(const World& world, const QString& instanceRoot)
{
    QJsonArray warnings;
    if (world.isSymLinkUnder(instanceRoot)) {
        warnings.append(QStringLiteral("symlink"));
    }
    if (world.isMoreThanOneHardLink()) {
        warnings.append(QStringLiteral("multiple_hard_links"));
    }
    return warnings;
}

/// The JSON of a world, the way the CLI reports it.
QJsonObject worldToJson(const World& world, const QString& instanceRoot, bool withSize)
{
    QJsonObject object;
    object.insert(QStringLiteral("folder"), world.folderName());
    object.insert(QStringLiteral("name"), world.name());
    object.insert(QStringLiteral("gameMode"), world.gameType().toLogString());
    const auto rawGameMode = world.gameType().original;
    object.insert(QStringLiteral("gameModeRaw"), rawGameMode ? QJsonValue(*rawGameMode) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("seed"), static_cast<qint64>(world.seed()));
    object.insert(QStringLiteral("lastPlayed"), jsonNullIfEmpty(isoDate(world.lastPlayed())));
    if (withSize) {
        object.insert(QStringLiteral("sizeBytes"), static_cast<qint64>(worldSize(world.container())));
    }
    object.insert(QStringLiteral("icon"), jsonNullIfEmpty(world.iconFile()));
    object.insert(QStringLiteral("warnings"), worldWarnings(world, instanceRoot));
    return object;
}

/**
 * The worlds of an instance.
 *
 * The metadata of a world is read synchronously - the launcher loads it in the
 * background, which does not work for a command that has to report the result
 * right away.
 */
QList<World> loadWorlds(MinecraftInstance* instance)
{
    QList<World> worlds;
    QDir dir(instance->worldDir());
    if (!dir.exists()) {
        return worlds;
    }
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const auto& entry : entries) {
        World world(entry);
        if (!world.isValid()) {
            // A folder without a level.dat is not a world.
            continue;
        }
        world.loadMetadata();
        worlds.append(world);
    }
    return worlds;
}

/**
 * The worlds that match a target.
 *
 * The folder of a world is its stable id, so it wins over the name the world
 * has in its level.dat.
 */
QList<World> matchWorlds(const QList<World>& worlds, const QString& target)
{
    QList<World> matches;
    for (const auto& world : worlds) {
        if (world.folderName() == target) {
            matches.append(world);
        }
    }
    if (!matches.isEmpty()) {
        return matches;
    }
    for (const auto& world : worlds) {
        if (world.name() == target) {
            matches.append(world);
        }
    }
    return matches;
}

/**
 * Resolve the world of a command.
 *
 * The error is reported and the exit code is returned when the world does not
 * exist or when the target matches more than one world.
 */
ExitCode resolveWorld(MinecraftInstance* instance, const QString& target, std::optional<World>& world)
{
    const auto matches = matchWorlds(loadWorlds(instance), target);
    if (matches.isEmpty()) {
        return outputError(QStringLiteral("world_not_found"),
                           QStringLiteral("No world matching '%1' was found in the instance").arg(target),
                           ExitCode::NotFound);
    }
    if (matches.size() > 1) {
        QJsonArray candidates;
        for (const auto& match : matches) {
            candidates.append(match.folderName());
        }
        QJsonObject details;
        details.insert(QStringLiteral("candidates"), candidates);
        return outputError(QStringLiteral("ambiguous_target"),
                           QStringLiteral("'%1' matches more than one world - use the folder of the world instead").arg(target),
                           ExitCode::Ambiguous,
                           details);
    }
    world = matches.first();
    return ExitCode::OK;
}

/// The names of the folders of a directory.
QStringList folderNames(const QString& dir)
{
    QDir d(dir);
    if (!d.exists()) {
        return {};
    }
    return d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

/// The folders of a directory that were not there before.
QStringList newFolders(const QString& dir, const QStringList& before)
{
    QStringList created;
    for (const auto& name : folderNames(dir)) {
        if (!before.contains(name)) {
            created.append(name);
        }
    }
    return created;
}

/// The name that a world has in its level.dat.
QString levelNameOf(const QString& folder)
{
    World world{ QFileInfo(folder) };
    if (!world.isValid()) {
        return {};
    }
    world.loadMetadata();
    return world.name();
}

}  // namespace

ExitCode runWorldCommand(WorldContext& ctx, const Cmd::WorldList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    const auto instanceRoot = QFileInfo(instance->instanceRoot()).absoluteFilePath();

    QJsonArray worlds;
    for (const auto& world : loadWorlds(instance)) {
        worlds.append(worldToJson(world, instanceRoot, cmd.withSize));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(worlds.size()));
    out.insert(QStringLiteral("worlds"), worlds);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runWorldCommand(WorldContext& ctx, const Cmd::WorldInfo& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    const auto instanceRoot = QFileInfo(instance->instanceRoot()).absoluteFilePath();

    std::optional<World> world;
    if (auto code = resolveWorld(instance, cmd.target, world); code != ExitCode::OK) {
        return code;
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("world"), worldToJson(*world, instanceRoot, cmd.withSize));
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runWorldCommand(WorldContext& ctx, const Cmd::WorldCopy& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("copy a world")); code != ExitCode::OK) {
        return code;
    }

    std::optional<World> world;
    if (auto code = resolveWorld(instance, cmd.target, world); code != ExitCode::OK) {
        return code;
    }

    const auto saves = instance->worldDir();
    const auto before = folderNames(saves);

    // The copy is installed next to the world it was copied from - the name of
    // the copy is the name of the world, unless another one was given.
    World copy = *world;
    if (!copy.install(saves, cmd.name.value_or(QString()))) {
        return outputError(QStringLiteral("copy_failed"),
                           QStringLiteral("The world '%1' could not be copied").arg(cmd.target),
                           ExitCode::Generic);
    }

    const auto created = newFolders(saves, before);
    if (created.isEmpty()) {
        return outputError(QStringLiteral("copy_failed"),
                           QStringLiteral("The world '%1' was copied, but the copy could not be found").arg(cmd.target),
                           ExitCode::Generic);
    }

    World result(QFileInfo(saves, created.first()));
    result.loadMetadata();

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("copied"), worldToJson(result, QFileInfo(instance->instanceRoot()).absoluteFilePath(), false));
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runWorldCommand(WorldContext& ctx, const Cmd::WorldRename& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("rename a world")); code != ExitCode::OK) {
        return code;
    }

    std::optional<World> world;
    if (auto code = resolveWorld(instance, cmd.target, world); code != ExitCode::OK) {
        return code;
    }

    const auto saves = instance->worldDir();
    const auto before = folderNames(saves);
    const auto oldFolder = world->folderName();

    if (!world->rename(cmd.name)) {
        return outputError(QStringLiteral("rename_failed"),
                           QStringLiteral("The world '%1' could not be renamed").arg(cmd.target),
                           ExitCode::Generic);
    }

    // A rename changes the level.dat of the world and the folder it lives in.
    // The two can fail on their own, so the result is checked and a partial
    // failure is reported.
    const auto created = newFolders(saves, before);
    const bool folderRenamed = created.size() == 1;
    const auto renamedFolder = folderRenamed ? created.first() : oldFolder;
    const bool levelDatUpdated = levelNameOf(FS::PathCombine(saves, renamedFolder)) == cmd.name;

    if (!folderRenamed || !levelDatUpdated) {
        QJsonObject details;
        details.insert(QStringLiteral("folder"), oldFolder);
        details.insert(QStringLiteral("name"), cmd.name);
        details.insert(QStringLiteral("folder_renamed"), folderRenamed);
        details.insert(QStringLiteral("level_dat_updated"), levelDatUpdated);
        return outputError(QStringLiteral("rename_partial"),
                           QStringLiteral("The world '%1' was only renamed partially - the folder and the name in the level.dat do not match")
                               .arg(cmd.target),
                           ExitCode::Generic,
                           details);
    }

    World result(QFileInfo(saves, renamedFolder));
    result.loadMetadata();

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("renamed"), worldToJson(result, QFileInfo(instance->instanceRoot()).absoluteFilePath(), false));
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runWorldCommand(WorldContext& ctx, const Cmd::WorldDelete& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("delete a world")); code != ExitCode::OK) {
        return code;
    }

    std::optional<World> world;
    if (auto code = resolveWorld(instance, cmd.target, world); code != ExitCode::OK) {
        return code;
    }

    const auto path = world->container().filePath();
    bool removed = false;
    if (cmd.permanent) {
        // A symlink is removed itself - the folder it points to is not a part
        // of the instance.
        const QFileInfo info(path);
        removed = info.isSymLink() ? QFile::remove(path) : QDir(path).removeRecursively();
    } else {
        removed = FS::trash(path);
        if (!removed) {
            return outputError(QStringLiteral("trash_failed"),
                               QStringLiteral("The world '%1' could not be moved to the trash - use --permanent to delete it instead")
                                   .arg(cmd.target),
                               ExitCode::Generic);
        }
    }
    if (!removed) {
        return outputError(QStringLiteral("delete_failed"),
                           QStringLiteral("The world '%1' could not be deleted").arg(cmd.target),
                           ExitCode::Generic);
    }

    QJsonObject entry;
    entry.insert(QStringLiteral("folder"), world->folderName());
    entry.insert(QStringLiteral("name"), world->name());
    entry.insert(QStringLiteral("permanent"), cmd.permanent);

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("deleted"), entry);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runWorldCommand(WorldContext& ctx, const Cmd::WorldImport& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("import a world")); code != ExitCode::OK) {
        return code;
    }

    const QFileInfo source(cmd.path);
    if (!source.exists()) {
        return outputError(QStringLiteral("import_source_not_found"),
                           QStringLiteral("The world '%1' does not exist").arg(cmd.path),
                           ExitCode::NotFound);
    }

    // A zip may have the world in a subfolder - the offset of the level.dat is
    // found while the metadata is read.
    World world(source);
    world.loadMetadata();
    if (!world.isValid()) {
        return outputError(QStringLiteral("invalid_world"),
                           QStringLiteral("'%1' does not contain a world (no level.dat was found)").arg(cmd.path),
                           ExitCode::NotFound);
    }

    const auto saves = instance->worldDir();
    const auto before = folderNames(saves);

    if (!world.install(saves, cmd.name.value_or(QString()))) {
        return outputError(QStringLiteral("import_failed"),
                           QStringLiteral("The world '%1' could not be imported").arg(cmd.path),
                           ExitCode::Generic);
    }

    const auto created = newFolders(saves, before);
    if (created.isEmpty()) {
        return outputError(QStringLiteral("import_failed"),
                           QStringLiteral("The world '%1' was imported, but it could not be found").arg(cmd.path),
                           ExitCode::Generic);
    }

    World result(QFileInfo(saves, created.first()));
    result.loadMetadata();

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("imported"), worldToJson(result, QFileInfo(instance->instanceRoot()).absoluteFilePath(), false));
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
