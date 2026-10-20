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

#include <QJsonObject>
#include <QObjectPtr.h>
#include <QString>

#include "cli/Commands.h"
#include "cli/Output.h"

class InstanceList;
class MinecraftInstance;
class PackProfile;
struct Component;

namespace Cli {

/// The mod loader of an instance.
struct LoaderInfo {
    QString name;
    QString uid;
    QString version;
};

/**
 * Look up an instance by id. Returns nullptr when it does not exist.
 */
MinecraftInstance* resolveInstance(ModContext& ctx, const QString& id);

/**
 * A mutating command refuses to work on a running instance. The hint tells
 * the user how the command can be forced.
 */
ExitCode checkInstanceRunning(MinecraftInstance* instance, const QString& instanceId, const QString& action, const QString& hint = {});

/**
 * Make sure the component profile of the instance is loaded. The GUI does that
 * when the instance is opened, a headless run has to do it itself.
 */
bool loadComponentProfile(MinecraftInstance* instance);

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
 * The JSON of an instance - the fields that `instance list` and
 * `instance info` have in common.
 */
QJsonObject instanceToJson(MinecraftInstance* instance, InstanceList* instances);

}  // namespace Cli
