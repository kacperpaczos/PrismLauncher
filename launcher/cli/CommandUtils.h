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
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceType.h"

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
 * @brief resolve the targets of a remove/enable/disable command
 *
 * @param noun  the word that is used in the failure messages ("mod",
 *              "resource pack", ...)
 * @param force  resolve ambiguous targets to all of their matches instead of
 *               reporting them
 */
ResolvedTargets resolveTargets(ResourceFolderModel* model, const std::vector<QString>& targets, const QString& noun, bool force = false);

}  // namespace Cli
