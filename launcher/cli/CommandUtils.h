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

// NOTE: the helpers of the headless resource commands (mods, resource packs,
// shader packs and texture packs). They are shared, so that every kind of
// resource is reported in exactly the same way.

#include <string>
#include <vector>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "Result.h"
#include "cli/Output.h"
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/ResourceType.h"
#include "net/NetJob.h"

class Application;
class MinecraftInstance;
class Resource;
class ResourceFolderModel;

namespace Cli {

/// @brief a string that is empty when there is nothing to report
QJsonValue jsonNullIfEmpty(const QString& value);
QJsonValue jsonNullIfEmpty(const QVariant& value);

/// @brief the name of the provider a resource came from
QString sourceName(ModPlatform::ResourceProvider provider);

/**
 * @brief the slug of a resource without the `.pw.toml` suffix
 *
 * The metadata of a resource is loaded with the name of the index file, so the
 * slug can carry the `.pw.toml` suffix. The suffix is not part of the slug of
 * the resource, so it is stripped for the output.
 */
QString normalizedSlug(const QString& slug);

/// @brief the ISO-8601 (UTC) representation of a date and time
QString isoDate(const QDateTime& date);

/// @brief the JSON of an installed resource, the way the CLI reports it
///
/// @param withLockUpdate  whether the `lock_update` field is part of the
///                        report - the mod report has it, the pack reports do
///                        not.
QJsonObject resourceToJson(const Resource* resource, bool withLockUpdate);

/// @brief look up an instance by id
///
/// Returns nullptr when it does not exist.
MinecraftInstance* resolveInstance(Application& app, const QString& id);

/**
 * @brief whether two paths point at the same file
 *
 * The paths are resolved canonically, so that a symlink or a relative path
 * is compared with the file it points at. The paths of files that do not
 * exist are compared as absolute paths.
 */
bool isSameFile(const QString& a, const QString& b);

/**
 * @brief look up the instance of a command
 *
 * The error is reported and the exit code is returned when the instance does
 * not exist.
 */
ExitCode resolveInstance(Application& app, const QString& id, MinecraftInstance*& instance);

/**
 * @brief refuse a mutating command on a running instance
 *
 * The check is in-process: a separate CLI process cannot see that an instance
 * is running, so it only guards the commands that are dispatched inside a
 * running launcher.
 *
 * @param action  what the command was about to do ("copy a world", ...)
 * @param hint    an additional hint on how to force the command
 */
ExitCode checkInstanceRunning(MinecraftInstance* instance, const QString& instanceId, const QString& action, const QString& hint = {});

/**
 * @brief make sure the component profile of the instance is loaded
 *
 * The GUI does that when the instance is opened, a headless run has to do it
 * itself.
 */
bool loadComponentProfile(MinecraftInstance* instance);

/**
 * @brief wait for the resource model to finish its (re)scan and all pending
 *        parse tasks
 *
 * The model update runs on a thread pool, so a plain event loop with a polling
 * timer is used here - the same approach the GUI takes with its progress
 * dialog.
 */
bool waitForModel(ResourceFolderModel* model);

/// @brief resolve a project (slug or id) on Modrinth
Result<ModPlatform::IndexedPack> fetchProject(const QString& project);

/// @brief fetch all versions of a project
Result<QVector<ModPlatform::IndexedVersion>> fetchProjectVersions(const ModPlatform::IndexedPack::Ptr& pack,
                                                                  ModPlatform::ResourceType resourceType);

/**
 * @brief find the resources that match a user provided target
 *
 * The lookup order is:
 *   1. exact file name (with or without the `.disabled` suffix)
 *   2. metadata slug
 *   3. metadata project id
 *   4. display name
 */
QList<Resource*> matchTarget(ResourceFolderModel* model, const QString& target);

/// @brief the result of resolving the targets of a remove/enable/disable command
struct ResolvedTargets {
    QList<Resource*> resources;
    QJsonArray failed;
    QJsonArray candidates;
    bool ambiguous = false;
};

/**
 * @brief the machine readable codes of the failed entries of the commands
 *
 * Every entry of the `failed` array of a command carries one of them, so that
 * a script can tell what went wrong without parsing the message.
 */
namespace FailureCode {
/// The target did not match anything.
inline constexpr auto NotFound = "not_found";
/// The target matched more than one thing.
inline constexpr auto Ambiguous = "ambiguous";
/// The thing is locked to a specific version.
inline constexpr auto Locked = "locked";
/// The thing was already in the wanted state - nothing had to be done.
inline constexpr auto AlreadyAbsent = "already_absent";
/// The change could not be written.
inline constexpr auto Io = "io";
/// The download of the thing failed.
inline constexpr auto Download = "download";
}  // namespace FailureCode

/// @brief the outcome of a command that changed more than one target
struct FailureOutcome {
    /// The exit code of the command.
    ExitCode code = ExitCode::OK;
    /// The code of the error envelope - empty when the command succeeded.
    QString error;
    /// Whether only some of the targets were changed.
    bool partial = false;
};

/**
 * @brief evaluate the failed entries of a command that changed more than one target
 *
 * The policy of the commands that change more than one target:
 *
 * - all the targets failed: the command reports an error - the exit code is 3
 *   when all of the failures are `not_found`, 5 when all of them are
 *   `ambiguous` and 1 otherwise;
 * - only some of the targets failed: the command reports an error with the
 *   `partial` field, unless every failure is only informational (the target
 *   was already in the wanted state) - then the command succeeded.
 *
 * @param failed  the entries that failed - each of them carries a `code`
 * @param changed the number of the targets that were changed
 */
FailureOutcome evaluateFailures(const QJsonArray& failed, int changed);

/**
 * @brief write the envelope of a command that had failed targets
 *
 * @param outcome         the outcome of the evaluation of the failures
 * @param out             the envelope of the command - the `partial` field is
 *                        added when only some of the targets were changed
 * @param noun            the word that is used in the messages ("mod",
 *                        "resource pack", ...)
 * @param allFailedMessage the message of the envelope when every target failed
 */
ExitCode reportFailures(const FailureOutcome& outcome, QJsonObject& out, const QString& noun, const QString& allFailedMessage);

/**
 * @brief the code and the exit code of the failure of a task
 *
 * A task that did not finish in the grace period after the timeout is
 * reported with the `operation_did_not_finish` code and the exit code 1 - the
 * command cannot tell what the task did, so it must not claim that it failed
 * for another reason.
 */
struct TaskFailure {
    /// The code of the error envelope.
    QString code;
    /// The exit code of the command.
    ExitCode exitCode = ExitCode::Generic;
};

/// @brief the code and the exit code of the failure of a task
TaskFailure taskFailure(const QString& code, const QString& reason, ExitCode exitCode);

/// @brief the error of a failed task, with the code and the exit code of the failure
CliError taskFailureError(const CliError& error, const QString& reason);

/**
 * @brief resolve the targets of a remove/enable/disable command
 *
 * @param noun  the word that is used in the failure messages ("mod",
 *              "resource pack", ...)
 * @param force  resolve ambiguous targets to all of their matches instead of
 *               reporting them
 */
ResolvedTargets resolveTargets(ResourceFolderModel* model, const std::vector<QString>& targets, const QString& noun, bool force = false);

/// A NetJob for the CLI paths - it never asks for a retry, there is nobody
/// around to answer the dialog in a headless run.
NetJob::Ptr makeCliNetJob(const QString& name);

/// Runs a paged Modrinth search - the endpoint hardcodes the page size, so
/// the results have to be requested page by page - and builds the JSON
/// envelope shared by the search commands: {offset, limit, total, results}.
/// `args.type` decides which project type is searched.
Result<QJsonObject, QString> modrinthSearchJson(ResourceAPI::SearchArgs args, int limit, int offset);

}  // namespace Cli
