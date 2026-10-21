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

#include "cli/CommandUtils.h"
#include "cli/SettingsCommands.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QVariant>
#include <QVariantMap>

#include "Application.h"
#include "BaseInstance.h"
#include "Json.h"
#include "Result.h"
#include "cli/InstanceSupport.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"

namespace Cli {
namespace {

/// The type of a setting of the schema.
enum class SettingKind {
    Bool,
    Int,
    String,
    Env,
};

/// A setting of the curated schema of the CLI.
struct SettingSchema {
    // The key of the setting in the CLI.
    const char* key;
    // The id of the setting in the settings object of the launcher.
    const char* id;
    // The type of the value of the setting.
    SettingKind kind;
    // The flag that tells the launcher that the instance overrides the global
    // value of the setting. `nullptr` when the setting only exists per
    // instance.
    const char* gate;
};

/**
 * The settings of an instance the CLI knows about.
 *
 * The launcher stores the settings of an instance in `instance.cfg`. The
 * settings that can be overridden by an instance have a flag (`OverrideXxx`)
 * that tells the launcher whether the value of the instance or the global one
 * is used - the flag has to be set together with the value, the same way the
 * settings widget of the GUI does it.
 */
const std::vector<SettingSchema>& curatedSettings()
{
    static const std::vector<SettingSchema> settings = {
        // java
        { "java.path", "JavaPath", SettingKind::String, "OverrideJavaLocation" },
        { "java.args", "JvmArgs", SettingKind::String, "OverrideJavaArgs" },
        { "java.auto", "AutomaticJava", SettingKind::Bool, nullptr },
        { "java.compat", "IgnoreJavaCompatibility", SettingKind::Bool, "OverrideJavaLocation" },
        // memory
        { "memory.min", "MinMemAlloc", SettingKind::Int, "OverrideMemory" },
        { "memory.max", "MaxMemAlloc", SettingKind::Int, "OverrideMemory" },
        { "memory.perm", "PermGen", SettingKind::Int, "OverrideMemory" },
        { "memory.warning", "LowMemWarning", SettingKind::Bool, "OverrideMemory" },
        // window
        { "window.width", "MinecraftWinWidth", SettingKind::Int, "OverrideWindow" },
        { "window.height", "MinecraftWinHeight", SettingKind::Int, "OverrideWindow" },
        { "window.maximized", "LaunchMaximized", SettingKind::Bool, "OverrideWindow" },
        // console
        { "console.show", "ShowConsole", SettingKind::Bool, "OverrideConsole" },
        { "console.autoclose", "AutoCloseConsole", SettingKind::Bool, "OverrideConsole" },
        { "console.onerror", "ShowConsoleOnError", SettingKind::Bool, "OverrideConsole" },
        { "console.logprepost", "LogPrePostOutput", SettingKind::Bool, "OverrideConsole" },
        // commands
        { "commands.preload", "PreLoadCommand", SettingKind::String, "OverrideCommands" },
        { "commands.preLaunch", "PreLaunchCommand", SettingKind::String, "OverrideCommands" },
        { "commands.wrapper", "WrapperCommand", SettingKind::String, "OverrideCommands" },
        { "commands.postExit", "PostExitCommand", SettingKind::String, "OverrideCommands" },
        // environment
        { "env", "Env", SettingKind::Env, "OverrideEnv" },
        // performance
        { "perf.gamemode", "EnableFeralGamemode", SettingKind::Bool, "OverridePerformance" },
        { "perf.mangohud", "EnableMangoHud", SettingKind::Bool, "OverridePerformance" },
        { "perf.discreteGpu", "UseDiscreteGpu", SettingKind::Bool, "OverridePerformance" },
        { "perf.zink", "UseZink", SettingKind::Bool, "OverridePerformance" },
        // native workarounds
        { "native.glfw", "CustomGLFWPath", SettingKind::String, "OverrideNativeWorkarounds" },
        { "native.openal", "CustomOpenALPath", SettingKind::String, "OverrideNativeWorkarounds" },
        { "native.useGlfw", "UseNativeGLFW", SettingKind::Bool, "OverrideNativeWorkarounds" },
        { "native.useOpenal", "UseNativeOpenAL", SettingKind::Bool, "OverrideNativeWorkarounds" },
        // game time
        { "gameTime.show", "ShowGameTime", SettingKind::Bool, "OverrideGameTime" },
        { "gameTime.record", "RecordGameTime", SettingKind::Bool, "OverrideGameTime" },
        { "gameTime.global", "ShowGlobalGameTime", SettingKind::Bool, "OverrideGameTime" },
        // miscellaneous
        { "misc.closeAfterLaunch", "CloseAfterLaunch", SettingKind::Bool, "OverrideMiscellaneous" },
        { "misc.quitAfterGameStop", "QuitAfterGameStop", SettingKind::Bool, "OverrideMiscellaneous" },
        { "misc.onlineFixes", "OnlineFixes", SettingKind::Bool, "OverrideLegacySettings" },
    };
    return settings;
}

/**
 * The settings of an instance that are not part of the curated schema. They
 * are only listed when `--all` is used.
 */
const std::vector<SettingSchema>& extraSettings()
{
    static const std::vector<SettingSchema> settings = {
        { "name", "name", SettingKind::String, nullptr },
        { "iconKey", "iconKey", SettingKind::String, nullptr },
        { "notes", "notes", SettingKind::String, nullptr },
        { "uuid", "uuid", SettingKind::String, nullptr },
        { "instanceType", "InstanceType", SettingKind::String, nullptr },
        { "export.name", "ExportName", SettingKind::String, nullptr },
        { "export.version", "ExportVersion", SettingKind::String, nullptr },
        { "export.summary", "ExportSummary", SettingKind::String, nullptr },
        { "export.author", "ExportAuthor", SettingKind::String, nullptr },
        { "export.optionalFiles", "ExportOptionalFiles", SettingKind::Bool, nullptr },
        { "gameTime.count", "CountGameTime", SettingKind::Bool, nullptr },
        { "minecraft.latest", "UseLatestMinecraftVersion", SettingKind::Bool, nullptr },
        { "minecraft.latestType", "UseLatestMinecraftVersionType", SettingKind::String, nullptr },
        { "launch.joinServer", "JoinServerOnLaunch", SettingKind::Bool, nullptr },
        { "launch.joinServerAddress", "JoinServerOnLaunchAddress", SettingKind::String, nullptr },
        { "launch.joinWorld", "JoinWorldOnLaunch", SettingKind::String, nullptr },
        { "launch.useAccount", "UseAccountForInstance", SettingKind::Bool, nullptr },
        { "launch.accountId", "InstanceAccountId", SettingKind::String, nullptr },
        { "dataPacks.enabled", "GlobalDataPacksEnabled", SettingKind::Bool, nullptr },
        { "dataPacks.path", "GlobalDataPacksPath", SettingKind::String, nullptr },
        { "modDownloadLoaders", "ModDownloadLoaders", SettingKind::String, nullptr },
    };
    return settings;
}

/// The name of the type of a setting, as it is reported by the CLI.
QString typeName(SettingKind kind)
{
    switch (kind) {
    case SettingKind::Bool:
        return QStringLiteral("bool");
    case SettingKind::Int:
        return QStringLiteral("int");
    case SettingKind::Env:
        return QStringLiteral("env");
    case SettingKind::String:
    default:
        return QStringLiteral("string");
    }
}

/// The setting of the schema with the given key. Returns nullptr when the key
/// is not known.
const SettingSchema* findSetting(const QString& key)
{
    for (const auto& entry : curatedSettings()) {
        if (key == QLatin1String(entry.key)) {
            return &entry;
        }
    }
    for (const auto& entry : extraSettings()) {
        if (key == QLatin1String(entry.key)) {
            return &entry;
        }
    }
    return nullptr;
}

/// The keys of the settings of the schema, for the error messages.
QStringList knownKeys()
{
    QStringList keys;
    for (const auto& entry : curatedSettings()) {
        keys << QString::fromLatin1(entry.key);
    }
    return keys;
}

/// The ids of the settings that must not be written through the raw settings
/// commands. They are managed by the launcher itself or by another command.
const QStringList& blockedSettingIds()
{
    static const QStringList ids = {
        // The identity of the instance - the launcher writes it itself.
        QStringLiteral("uuid"),
        QStringLiteral("InstanceType"),
        // The name and the notes have a command of their own.
        QStringLiteral("name"),
        QStringLiteral("notes"),
        // The play time of the instance - the launcher writes it itself.
        QStringLiteral("lastLaunchTime"),
        QStringLiteral("totalTimePlayed"),
        QStringLiteral("lastTimePlayed"),
    };
    return ids;
}

/// The prefix of the settings of a managed pack - the launcher writes them
/// itself, when the instance is created from a pack.
const QString managedPackPrefix = QStringLiteral("ManagedPack");

/// The message of the error of a write of a setting that may not be written.
QString notWritableMessage(const QString& id, SettingsWriteAction action)
{
    const auto command = action == SettingsWriteAction::Set ? QStringLiteral("settings set") : QStringLiteral("settings unset");
    const auto verb = action == SettingsWriteAction::Set ? QStringLiteral("changed") : QStringLiteral("reset");
    auto hint = QStringLiteral(" - it is managed by the launcher");
    if (action == SettingsWriteAction::Set) {
        if (id == QLatin1String("name")) {
            hint = QStringLiteral(" - use 'instance rename' instead");
        } else if (id == QLatin1String("notes")) {
            hint = QStringLiteral(" - use 'notes set' instead");
        }
    }
    return QStringLiteral("The setting '%1' cannot be %2 with '%3'%4.").arg(id, verb, command, hint);
}

/// The error of a value that is not valid for the setting.
SettingsWriteError invalidValue(const QString& message)
{
    return SettingsWriteError{ QStringLiteral("invalid_value"), message };
}

/// The value of a setting as JSON.
QJsonValue valueToJson(const QVariant& value, SettingKind kind)
{
    switch (kind) {
    case SettingKind::Bool:
        return value.toBool();
    case SettingKind::Int:
        return value.toInt();
    case SettingKind::Env:
        // The environment variables of an instance are stored as a JSON
        // object in a string.
        return QJsonObject::fromVariantMap(Json::toMap(value.toString()));
    case SettingKind::String:
    default:
        return value.toString();
    }
}

/// The value of a setting, converted to the type of the setting.
Result<QVariant, QString> convertValue(const SettingSchema& entry, const QString& value)
{
    switch (entry.kind) {
    case SettingKind::Bool: {
        const auto lowered = value.toLower();
        if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
            return QVariant(true);
        }
        if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
            return QVariant(false);
        }
        return std::unexpected(QStringLiteral("'%1' is not a boolean").arg(value));
    }
    case SettingKind::Int: {
        bool ok = false;
        const auto number = value.toInt(&ok);
        if (!ok) {
            return std::unexpected(QStringLiteral("'%1' is not a number").arg(value));
        }
        return QVariant(number);
    }
    case SettingKind::Env:
    case SettingKind::String:
    default:
        return QVariant(value);
    }
}

/**
 * The description of a setting: its effective value and where the value comes
 * from.
 */
QJsonObject describeSetting(SettingsObject* settings, SettingsObject* globalSettings, const SettingSchema& entry)
{
    const auto id = QString::fromLatin1(entry.id);
    const auto value = settings->get(id);

    auto overridden = true;
    auto source = QStringLiteral("instance");
    auto gate = QString();
    if (entry.gate != nullptr) {
        gate = QString::fromLatin1(entry.gate);
        overridden = settings->get(gate).toBool();
        if (overridden) {
            source = QStringLiteral("instance");
        } else {
            // The value is the global one - it is the default of the launcher
            // unless the global settings were changed too.
            const auto globalValue = globalSettings->get(id);
            const auto globalSetting = globalSettings->getSetting(id);
            const auto globalDefault = globalSetting ? globalSetting->defValue() : QVariant();
            source = globalValue == globalDefault ? QStringLiteral("default") : QStringLiteral("global");
        }
    }

    // The key of the setting in the file of the instance.
    auto iniKey = id;
    if (const auto setting = settings->getSetting(id); setting && !setting->configKeys().isEmpty()) {
        iniKey = setting->configKeys().first();
    }

    QJsonObject object;
    object.insert(QStringLiteral("key"), QString::fromLatin1(entry.key));
    object.insert(QStringLiteral("value"), valueToJson(value, entry.kind));
    object.insert(QStringLiteral("type"), typeName(entry.kind));
    object.insert(QStringLiteral("source"), source);
    object.insert(QStringLiteral("overridden"), overridden);
    object.insert(QStringLiteral("ini_key"), iniKey);
    object.insert(QStringLiteral("gate"), gate.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(gate));
    return object;
}

/// The description of a single environment variable of an instance.
QJsonObject describeEnvVariable(SettingsObject* settings, const QString& name)
{
    const auto map = Json::toMap(settings->get(QStringLiteral("Env")).toString());
    QJsonObject object;
    object.insert(QStringLiteral("key"), QStringLiteral("env.%1").arg(name));
    const auto it = map.constFind(name);
    object.insert(QStringLiteral("value"),
                  it == map.constEnd() ? QJsonValue(QJsonValue::Null) : QJsonValue(it.value().toString()));
    object.insert(QStringLiteral("type"), QStringLiteral("env"));
    object.insert(QStringLiteral("source"), QStringLiteral("instance"));
    object.insert(QStringLiteral("overridden"), it != map.constEnd());
    object.insert(QStringLiteral("ini_key"), QStringLiteral("Env"));
    object.insert(QStringLiteral("gate"), QJsonValue(QStringLiteral("OverrideEnv")));
    return object;
}

}  // namespace

QStringList notWritableSettingsIds()
{
    return blockedSettingIds();
}

bool isWritableSettingsId(const QString& id)
{
    if (id.startsWith(managedPackPrefix)) {
        // The settings of a managed pack are written by the launcher.
        return false;
    }
    return !blockedSettingIds().contains(id);
}

std::optional<SettingsWriteError> checkSettingsKeyWritable(const QString& key, SettingsWriteAction action)
{
    // The environment variables are not settings of the schema - they are
    // written as a part of the `Env` setting.
    if (key.startsWith(QLatin1String("env."))) {
        return std::nullopt;
    }

    const auto* entry = findSetting(key);
    if (entry != nullptr) {
        const auto id = QString::fromLatin1(entry->id);
        if (!isWritableSettingsId(id)) {
            return SettingsWriteError{ QStringLiteral("key_not_writable"), notWritableMessage(id, action) };
        }
        return std::nullopt;
    }

    // A key that is not in the schema of the CLI can still be a setting of the
    // launcher that must not be written (the play time of the instance, the
    // information of a managed pack, ...).
    if (!isWritableSettingsId(key)) {
        return SettingsWriteError{ QStringLiteral("key_not_writable"), notWritableMessage(key, action) };
    }
    return std::nullopt;
}

std::optional<SettingsWriteError> checkSettingsValue(const QString& key, const QString& value)
{
    const auto* entry = findSetting(key);
    if (entry == nullptr) {
        // The key itself is checked by the caller.
        return std::nullopt;
    }

    switch (entry->kind) {
    case SettingKind::Bool: {
        const auto lowered = value.toLower();
        if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on" || lowered == "false" || lowered == "0" ||
            lowered == "no" || lowered == "off") {
            return std::nullopt;
        }
        return invalidValue(QStringLiteral("'%1' is not a boolean").arg(value));
    }
    case SettingKind::Int: {
        bool ok = false;
        const auto number = value.toInt(&ok);
        if (!ok) {
            return invalidValue(QStringLiteral("'%1' is not a number").arg(value));
        }
        if (number < 0) {
            return invalidValue(QStringLiteral("The value of '%1' cannot be negative").arg(key));
        }
        if ((key == QLatin1String("memory.min") || key == QLatin1String("memory.max")) && number <= 16) {
            // The launcher cannot start the game with less than 16 MB.
            return invalidValue(QStringLiteral("The value of '%1' has to be bigger than 16 (MB)").arg(key));
        }
        if ((key == QLatin1String("window.width") || key == QLatin1String("window.height")) && number < 1) {
            return invalidValue(QStringLiteral("The value of '%1' has to be at least 1").arg(key));
        }
        return std::nullopt;
    }
    case SettingKind::Env: {
        // The environment variables of an instance are stored as a JSON object
        // in a string.
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(value.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return invalidValue(QStringLiteral("The value of '%1' has to be a JSON object").arg(key));
        }
        return std::nullopt;
    }
    case SettingKind::String:
    default:
        return std::nullopt;
    }
}

ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.id, instance); code != ExitCode::OK) {
        return code;
    }
    // The settings of the instance have to be loaded before they can be read.
    auto* settings = instance->settings();
    auto* globalSettings = ctx.app.settings();

    QJsonArray list;
    auto addSettings = [&list, settings, globalSettings](const std::vector<SettingSchema>& schema) {
        for (const auto& entry : schema) {
            list.append(describeSetting(settings, globalSettings, entry));
        }
    };
    addSettings(curatedSettings());
    if (cmd.all) {
        addSettings(extraSettings());
    }

    if (cmd.modified) {
        QJsonArray modified;
        for (const auto& value : list) {
            if (value.toObject().value(QStringLiteral("overridden")).toBool()) {
                modified.append(value);
            }
        }
        list = modified;
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("total"), static_cast<int>(list.size()));
    out.insert(QStringLiteral("settings"), list);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsGet& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.id, instance); code != ExitCode::OK) {
        return code;
    }
    auto* settings = instance->settings();
    auto* globalSettings = ctx.app.settings();

    QJsonArray list;
    for (const auto& key : cmd.keys) {
        if (key.startsWith(QStringLiteral("env."))) {
            list.append(describeEnvVariable(settings, key.mid(4)));
            continue;
        }
        const auto* entry = findSetting(key);
        if (!entry) {
            QJsonObject details;
            details.insert(QStringLiteral("known_keys"), QJsonArray::fromStringList(knownKeys()));
            return outputError(QStringLiteral("unknown_setting"),
                               QStringLiteral("There is no setting with the key '%1'").arg(key),
                               ExitCode::Usage,
                               details);
        }
        list.append(describeSetting(settings, globalSettings, *entry));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("total"), static_cast<int>(list.size()));
    out.insert(QStringLiteral("settings"), list);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsSet& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.id, instance); code != ExitCode::OK) {
        return code;
    }
    auto* settings = instance->settings();

    // The keys and the values are checked before anything is written, so a
    // wrong value does not change half of the settings.
    std::vector<std::pair<const SettingSchema*, QVariant>> changes;
    auto env = Json::toMap(settings->get(QStringLiteral("Env")).toString());
    bool envChanged = false;
    for (const auto& [key, value] : cmd.values) {
        if (key.startsWith(QStringLiteral("env."))) {
            if (key.size() == 4) {
                return outputError(QStringLiteral("invalid_setting"),
                                   QStringLiteral("The name of the environment variable is missing"),
                                   ExitCode::Usage);
            }
            env.insert(key.mid(4), value);
            envChanged = true;
            continue;
        }
        // The settings the launcher manages itself are not writable - they
        // have a command of their own. The check comes before the lookup of
        // the key, because some of those settings are not part of the schema
        // of the CLI.
        if (const auto error = checkSettingsKeyWritable(key, SettingsWriteAction::Set)) {
            return outputError(error->code, error->message, ExitCode::Usage);
        }
        const auto* entry = findSetting(key);
        if (!entry) {
            QJsonObject details;
            details.insert(QStringLiteral("known_keys"), QJsonArray::fromStringList(knownKeys()));
            return outputError(QStringLiteral("unknown_setting"),
                               QStringLiteral("There is no setting with the key '%1'").arg(key),
                               ExitCode::Usage,
                               details);
        }
        // The value has to be valid for the setting.
        if (const auto error = checkSettingsValue(key, value)) {
            return outputError(error->code, error->message, ExitCode::Usage);
        }
        auto converted = convertValue(*entry, value);
        if (!converted) {
            return outputError(QStringLiteral("invalid_value"),
                               QStringLiteral("The value of '%1' is not valid: %2").arg(key, converted.error()),
                               ExitCode::Usage);
        }
        changes.emplace_back(entry, *converted);
    }

    // The memory of the instance has to fit into itself.
    auto minMemory = settings->get(QStringLiteral("MinMemAlloc")).toInt();
    auto maxMemory = settings->get(QStringLiteral("MaxMemAlloc")).toInt();
    for (const auto& [key, value] : cmd.values) {
        if (key == QLatin1String("memory.min")) {
            minMemory = value.toInt();
        } else if (key == QLatin1String("memory.max")) {
            maxMemory = value.toInt();
        }
    }
    if (minMemory > maxMemory) {
        return outputError(QStringLiteral("invalid_memory"),
                           QStringLiteral("The minimum memory (%1 MB) is bigger than the maximum memory (%2 MB)")
                               .arg(minMemory)
                               .arg(maxMemory),
                           ExitCode::Usage);
    }

    // The changes are written in one go, so the file of the instance is
    // written only once.
    {
        SettingsObject::Lock lock(settings);
        for (const auto& [entry, value] : changes) {
            const auto id = QString::fromLatin1(entry->id);
            if (!settings->set(id, value)) {
                return outputError(QStringLiteral("setting_failed"),
                                   QStringLiteral("The setting '%1' could not be changed").arg(id),
                                   ExitCode::Generic);
            }
            if (entry->gate != nullptr) {
                // The value of an instance is only used when the flag of the
                // group is set - the same way the settings widget of the GUI
                // does it.
                settings->set(QString::fromLatin1(entry->gate), true);
            }
        }
        if (envChanged) {
            // The environment variables are all in one setting, so the whole
            // map is written at once.
            settings->set(QStringLiteral("Env"), Json::fromMap(env));
            settings->set(QStringLiteral("OverrideEnv"), true);
        }
    }

    QJsonArray list;
    for (const auto& [key, value] : cmd.values) {
        if (key.startsWith(QStringLiteral("env."))) {
            list.append(describeEnvVariable(settings, key.mid(4)));
            continue;
        }
        const auto* entry = findSetting(key);
        if (!entry) {
            continue;
        }
        list.append(describeSetting(settings, ctx.app.settings(), *entry));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("total"), static_cast<int>(list.size()));
    out.insert(QStringLiteral("settings"), list);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsUnset& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.id, instance); code != ExitCode::OK) {
        return code;
    }
    auto* settings = instance->settings();

    // The keys are checked before anything is written.
    for (const auto& key : cmd.keys) {
        if (key.startsWith(QStringLiteral("env."))) {
            continue;
        }
        // The settings the launcher manages itself cannot be reset either -
        // the launcher writes them itself.
        if (const auto error = checkSettingsKeyWritable(key, SettingsWriteAction::Unset)) {
            return outputError(error->code, error->message, ExitCode::Usage);
        }
        if (!findSetting(key)) {
            QJsonObject details;
            details.insert(QStringLiteral("known_keys"), QJsonArray::fromStringList(knownKeys()));
            return outputError(QStringLiteral("unknown_setting"),
                               QStringLiteral("There is no setting with the key '%1'").arg(key),
                               ExitCode::Usage,
                               details);
        }
    }

    {
        SettingsObject::Lock lock(settings);
        for (const auto& key : cmd.keys) {
            if (key.startsWith(QStringLiteral("env."))) {
                continue;
            }
            const auto* entry = findSetting(key);
            if (!entry) {
                continue;
            }
            // The value of the instance is removed, so the global one is
            // used again.
            settings->reset(QString::fromLatin1(entry->id));
            if (entry->gate != nullptr) {
                // The flag of the group is reset too, so the whole group
                // falls back to the global values.
                settings->reset(QString::fromLatin1(entry->gate));
            }
        }
    }

    // The environment variables are removed from the map of the instance.
    auto env = Json::toMap(settings->get(QStringLiteral("Env")).toString());
    bool envChanged = false;
    for (const auto& key : cmd.keys) {
        if (!key.startsWith(QStringLiteral("env."))) {
            continue;
        }
        if (env.remove(key.mid(4)) > 0) {
            envChanged = true;
        }
    }
    if (envChanged) {
        SettingsObject::Lock lock(settings);
        if (env.isEmpty()) {
            settings->reset(QStringLiteral("Env"));
        } else {
            settings->set(QStringLiteral("Env"), Json::fromMap(env));
        }
    }

    QJsonArray list;
    for (const auto& key : cmd.keys) {
        if (key.startsWith(QStringLiteral("env."))) {
            list.append(describeEnvVariable(settings, key.mid(4)));
            continue;
        }
        const auto* entry = findSetting(key);
        if (!entry) {
            continue;
        }
        list.append(describeSetting(settings, ctx.app.settings(), *entry));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("total"), static_cast<int>(list.size()));
    out.insert(QStringLiteral("settings"), list);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
