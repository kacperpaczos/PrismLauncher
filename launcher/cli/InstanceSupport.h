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

// NOTE: keep this header free of GUI includes - it is used by the headless
// command runners.

#include <optional>
#include <vector>

#include <QJsonObject>
#include <QObjectPtr.h>
#include <QString>

#include "Result.h"
#include "cli/Commands.h"
#include "cli/Output.h"

class InstanceList;
class MinecraftInstance;
class PackProfile;
struct Component;

namespace Meta {
class VersionList;
}  // namespace Meta

namespace Cli {

/// The mod loader of an instance.
struct LoaderInfo {
    QString name;
    QString uid;
    QString version;
};

/**
 * Load the component profile of the instance and wait for the resolution of
 * the components, so that their versions are known.
 */
bool loadProfile(MinecraftInstance* instance);

/**
 * The mod loader of the instance, if it has one.
 */
std::optional<LoaderInfo> findLoader(PackProfile* profile);

/**
 * The version of a component. The cached version of a component is only known
 * after the components were resolved, so the version that is stored in the
 * profile is used as a fallback.
 */
QString componentVersion(const shared_qobject_ptr<Component>& component);

/**
 * The component uid of the mod loaders the CLI knows about.
 */
const QMap<QString, QString>& loaderUids();

/**
 * The versions of a component that support the given Minecraft version.
 *
 * The launcher marks the versions of a loader with the Minecraft version they
 * need in their requirements. Fabric and Quilt do not have such a requirement
 * (the launcher does not know about their dependencies), so every version of
 * them is usable with Minecraft 1.14 and newer.
 */
std::vector<QString> versionsForMinecraft(Meta::VersionList* list, const QString& mcVersion, const QString& loaderUid);

/**
 * Load the version list of a component. Returns nullptr when the list could
 * not be loaded.
 */
Meta::VersionList* loadVersionList(CommandContext& ctx, const QString& uid, bool* networkFailed = nullptr);

/**
 * The version of a mod loader that is used by a new instance. `latest` picks
 * the newest version that supports the given Minecraft version.
 */
Result<QString, QString> resolveLoaderVersion(CommandContext& ctx,
                                              const QString& loaderUid,
                                              const QString& wanted,
                                              const QString& mcVersion,
                                              bool& networkFailed);

/**
 * The JSON of an instance - the fields that `instance list` and
 * `instance info` have in common.
 */
QJsonObject instanceToJson(MinecraftInstance* instance, InstanceList* instances);

}  // namespace Cli
