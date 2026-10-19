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

#include "cli/ModCommands.h"

namespace Cli {
namespace {

ExitCode notImplemented(const QString& subcommand)
{
    return outputError(QStringLiteral("not_implemented"), QStringLiteral("`mod %1` is not implemented yet").arg(subcommand));
}

}  // namespace

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModList& /*cmd*/)
{
    return notImplemented(QStringLiteral("list"));
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModSearch& /*cmd*/)
{
    return notImplemented(QStringLiteral("search"));
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModInfo& /*cmd*/)
{
    return notImplemented(QStringLiteral("info"));
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModInstall& /*cmd*/)
{
    return notImplemented(QStringLiteral("install"));
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModRemove& /*cmd*/)
{
    return notImplemented(QStringLiteral("remove"));
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModEnable& /*cmd*/)
{
    return notImplemented(QStringLiteral("enable"));
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModDisable& /*cmd*/)
{
    return notImplemented(QStringLiteral("disable"));
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModUpdate& /*cmd*/)
{
    return notImplemented(QStringLiteral("update"));
}

}  // namespace Cli
