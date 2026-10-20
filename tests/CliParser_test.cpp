// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include <CLI/CLI.hpp>

#include "cli/Parser.h"
#include "cli/Output.h"

namespace {

Cli::Args parseOk(std::vector<std::string> argv)
{
    // CLI11's `parse(std::vector<std::string>&)` takes the argument list without
    // the program name and reversed (it pops from the back).
    argv.erase(argv.begin());
    std::reverse(argv.begin(), argv.end());

    Cli::Args args{};
    CLI::App app{};
    Cli::Cli cli{};
    cli.attach(app, args);

    app.parse(argv);
    cli.modCli.flush(args);
    cli.instanceCli.flush(args);
    cli.settingsCli.flush(args);
    cli.versionCli.flush(args);
    return args;
}

bool parseThrows(const std::vector<std::string>& argv)
{
    try {
        parseOk(argv);
    } catch (const CLI::ParseError&) {
        return true;
    }
    return false;
}

template <typename T>
const T& onlyCommand(const Cli::Args& args)
{
    return std::get<T>(args.commands.front());
}

/// Path to the launcher binary under test, so the exit-code/JSON contract can be
/// exercised end-to-end (parseArgs() exits the process).
QString launcherBinary()
{
#ifdef PRISM_TEST_LAUNCHER_BINARY
    return QString::fromUtf8(PRISM_TEST_LAUNCHER_BINARY);
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/prismlauncher");
#endif
}

struct ProcessResult {
    int exitCode = -1;
    QByteArray out;
};

ProcessResult runLauncher(const QStringList& args)
{
    QProcess proc;
    proc.start(launcherBinary(), args);
    if (!proc.waitForStarted(30000)) {
        return {};
    }
    if (!proc.waitForFinished(60000)) {
        proc.kill();
        proc.waitForFinished();
        return {};
    }
    return { proc.exitCode(), proc.readAllStandardOutput() };
}

}  // namespace

class CliParserTest : public QObject {
    Q_OBJECT

   private slots:
    void test_modList()
    {
        const auto args = parseOk({ "prismlauncher", "mod", "list", "-i", "X" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::ModList>(args);
        QCOMPARE(cmd.instance, QString("X"));
        QCOMPARE(cmd.includeDisabled, false);
    }

    void test_modList_includeDisabled()
    {
        const auto args = parseOk({ "prismlauncher", "mod", "list", "-i", "X", "--include-disabled", "--json" });
        const auto& cmd = onlyCommand<Cli::Cmd::ModList>(args);
        QCOMPARE(cmd.instance, QString("X"));
        QCOMPARE(cmd.includeDisabled, true);
    }

    void test_modSearch()
    {
        const auto args = parseOk({ "prismlauncher", "mod", "search", "jei", "--mc", "1.20.1", "--mc", "1.21", "--loader", "fabric",
                                    "--limit", "5", "--offset", "10" });
        const auto& cmd = onlyCommand<Cli::Cmd::ModSearch>(args);
        QCOMPARE(cmd.query, QString("jei"));
        QCOMPARE(cmd.mcVersions.size(), std::size_t{ 2 });
        QCOMPARE(cmd.mcVersions.at(0), QString("1.20.1"));
        QCOMPARE(cmd.mcVersions.at(1), QString("1.21"));
        QCOMPARE(cmd.loaders.size(), std::size_t{ 1 });
        QCOMPARE(cmd.loaders.at(0), QString("fabric"));
        QCOMPARE(cmd.limit, 5);
        QCOMPARE(cmd.offset, 10);
    }

    void test_modSearch_queryAfterFilterOptions()
    {
        // Regression (review W1): `--mc <version>` used to greedily consume the
        // positional query, silently turning `mod search --mc 1.20.1 jei` into a
        // search for "1.20.1".
        const auto afterMc = parseOk({ "prismlauncher", "mod", "search", "--mc", "1.20.1", "jei" });
        const auto& cmdA = onlyCommand<Cli::Cmd::ModSearch>(afterMc);
        QCOMPARE(cmdA.query, QString("jei"));
        QCOMPARE(cmdA.mcVersions.size(), std::size_t{ 1 });
        QCOMPARE(cmdA.mcVersions.at(0), QString("1.20.1"));

        const auto afterLoader = parseOk({ "prismlauncher", "mod", "search", "--loader", "fabric", "jei" });
        const auto& cmdB = onlyCommand<Cli::Cmd::ModSearch>(afterLoader);
        QCOMPARE(cmdB.query, QString("jei"));
        QCOMPARE(cmdB.loaders.size(), std::size_t{ 1 });
        QCOMPARE(cmdB.loaders.at(0), QString("fabric"));

        // no query at all is still a valid (browse) search
        const auto noQuery = parseOk({ "prismlauncher", "mod", "search", "--mc", "1.20.1" });
        const auto& cmdC = onlyCommand<Cli::Cmd::ModSearch>(noQuery);
        QCOMPARE(cmdC.query, QString());
        QCOMPARE(cmdC.mcVersions.size(), std::size_t{ 1 });
    }

    void test_modSearch_valueRanges()
    {
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "search", "--limit", "101", "jei" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "search", "--offset", "1001", "jei" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "search", "--limit", "-1", "jei" }), true);
        // boundary values are fine
        const auto args = parseOk({ "prismlauncher", "mod", "search", "--limit", "100", "--offset", "1000", "jei" });
        const auto& cmd = onlyCommand<Cli::Cmd::ModSearch>(args);
        QCOMPARE(cmd.limit, 100);
        QCOMPARE(cmd.offset, 1000);
    }

    void test_modInfo_optionalInstance()
    {
        const auto withoutInstance = parseOk({ "prismlauncher", "mod", "info", "jei" });
        const auto& cmdA = onlyCommand<Cli::Cmd::ModInfo>(withoutInstance);
        QCOMPARE(cmdA.project, QString("jei"));
        QCOMPARE(cmdA.instance.has_value(), false);

        const auto withInstance = parseOk({ "prismlauncher", "mod", "info", "jei", "-i", "X" });
        const auto& cmdB = onlyCommand<Cli::Cmd::ModInfo>(withInstance);
        QCOMPARE(cmdB.instance.has_value(), true);
        QCOMPARE(*cmdB.instance, QString("X"));
    }

    void test_modInstall_optionalVersion()
    {
        const auto withVersion = parseOk({ "prismlauncher", "mod", "install", "jei", "-i", "X", "--version", "1.2", "--no-deps",
                                           "--no-metadata" });
        const auto& cmdA = onlyCommand<Cli::Cmd::ModInstall>(withVersion);
        QCOMPARE(cmdA.project, QString("jei"));
        QCOMPARE(cmdA.instance, QString("X"));
        QVERIFY(cmdA.version.has_value());
        QCOMPARE(*cmdA.version, QString("1.2"));
        QCOMPARE(cmdA.noDeps, true);
        QCOMPARE(cmdA.noMetadata, true);

        const auto withoutVersion = parseOk({ "prismlauncher", "mod", "install", "jei", "-i", "X" });
        const auto& cmdB = onlyCommand<Cli::Cmd::ModInstall>(withoutVersion);
        QCOMPARE(cmdB.version.has_value(), false);
        QCOMPARE(cmdB.noDeps, false);
    }

    void test_modRemove_targets()
    {
        const auto args = parseOk({ "prismlauncher", "mod", "remove", "jei", "sodium", "-i", "X", "--force" });
        const auto& cmd = onlyCommand<Cli::Cmd::ModRemove>(args);
        QCOMPARE(cmd.targets.size(), std::size_t{ 2 });
        QCOMPARE(cmd.targets.at(0), QString("jei"));
        QCOMPARE(cmd.targets.at(1), QString("sodium"));
        QCOMPARE(cmd.instance, QString("X"));
        QCOMPARE(cmd.force, true);
    }

    void test_modRemoveEnableDisable_requireTargets()
    {
        // Review W3: mutating commands without a target used to parse fine and
        // then fail (or, worse, act on everything) at runtime.
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "remove", "-i", "X" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "enable", "-i", "X" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "disable", "-i", "X" }), true);
    }

    void test_modEnableDisable()
    {
        const auto enable = parseOk({ "prismlauncher", "mod", "enable", "jei", "-i", "X", "--recursive" });
        const auto& enableCmd = onlyCommand<Cli::Cmd::ModEnable>(enable);
        QCOMPARE(enableCmd.targets.size(), std::size_t{ 1 });
        QCOMPARE(enableCmd.recursive, true);

        const auto disable = parseOk({ "prismlauncher", "mod", "disable", "jei", "-i", "X" });
        const auto& disableCmd = onlyCommand<Cli::Cmd::ModDisable>(disable);
        QCOMPARE(disableCmd.instance, QString("X"));
        QCOMPARE(disableCmd.recursive, false);
    }

    void test_modUpdate()
    {
        const auto all = parseOk({ "prismlauncher", "mod", "update", "--all", "-i", "X", "--dry-run" });
        const auto& allCmd = onlyCommand<Cli::Cmd::ModUpdate>(all);
        QCOMPARE(allCmd.all, true);
        QCOMPARE(allCmd.dryRun, true);
        QCOMPARE(allCmd.targets.size(), std::size_t{ 0 });

        const auto targets = parseOk({ "prismlauncher", "mod", "update", "jei", "-i", "X", "--no-deps", "--force" });
        const auto& targetsCmd = onlyCommand<Cli::Cmd::ModUpdate>(targets);
        QCOMPARE(targetsCmd.all, false);
        QCOMPARE(targetsCmd.targets.size(), std::size_t{ 1 });
        QCOMPARE(targetsCmd.noDeps, true);
        QCOMPARE(targetsCmd.force, true);
    }

    void test_modUpdate_requiresTargetsOrDefault()
    {
        // Review W3: neither `--all` nor an explicit target is a usage error...
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "update", "-i", "X" }), true);
        // ...and the two are mutually exclusive.
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "update", "--all", "jei", "-i", "X" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "update", "jei", "--all", "-i", "X" }), true);
    }

    void test_modAssumeYes_perCommand()
    {
        // Review W4: `-y/--yes` used to be one shared variable, so a flag on one
        // command could leak into the state observed by another. It is now stored
        // on each command.
        const auto install = parseOk({ "prismlauncher", "mod", "install", "jei", "-i", "X", "-y" });
        QCOMPARE(onlyCommand<Cli::Cmd::ModInstall>(install).assumeYes, true);

        const auto remove = parseOk({ "prismlauncher", "mod", "remove", "jei", "-i", "X", "--yes" });
        QCOMPARE(onlyCommand<Cli::Cmd::ModRemove>(remove).assumeYes, true);

        const auto enable = parseOk({ "prismlauncher", "mod", "enable", "jei", "-i", "X", "-y" });
        QCOMPARE(onlyCommand<Cli::Cmd::ModEnable>(enable).assumeYes, true);

        const auto disable = parseOk({ "prismlauncher", "mod", "disable", "jei", "-i", "X", "-y" });
        QCOMPARE(onlyCommand<Cli::Cmd::ModDisable>(disable).assumeYes, true);

        const auto update = parseOk({ "prismlauncher", "mod", "update", "--all", "-i", "X", "-y" });
        QCOMPARE(onlyCommand<Cli::Cmd::ModUpdate>(update).assumeYes, true);

        // default is false
        const auto plain = parseOk({ "prismlauncher", "mod", "remove", "jei", "-i", "X" });
        QCOMPARE(onlyCommand<Cli::Cmd::ModRemove>(plain).assumeYes, false);
    }

    void test_globalDir_acceptedAfterModSubcommand()
    {
        // Review W8: `-d/--dir` is declared on the top-level app, so the mod
        // subcommands need fallthrough for it to work in any position.
        const auto afterLeaf = parseOk({ "prismlauncher", "mod", "list", "-i", "X", "-d", "/tmp" });
        QCOMPARE(afterLeaf.dataPath.source, Startup::DataPathSource::Commandline);
        QCOMPARE(QString::fromStdString(afterLeaf.dataPath.dataPath.string()), QString("/tmp"));

        const auto afterGroup = parseOk({ "prismlauncher", "mod", "-d", "/tmp", "list", "-i", "X" });
        QCOMPARE(QString::fromStdString(afterGroup.dataPath.dataPath.string()), QString("/tmp"));

        // fallthrough must not turn unknown options or stray positionals into
        // silently ignored input.
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "list", "-i", "X", "--bogus" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "list", "-i", "X", "stray" }), true);
    }

    void test_legacyLaunch_untouched()
    {
        const auto args = parseOk({ "prismlauncher", "--launch", "foo" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::Launch>(args);
        QCOMPARE(cmd.id, std::string("foo"));
    }

    void test_instanceList()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "list" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::InstanceList>(args);
        QVERIFY(!cmd.group.has_value());

        const auto grouped = parseOk({ "prismlauncher", "instance", "list", "--group", "modded" });
        const auto& groupedCmd = onlyCommand<Cli::Cmd::InstanceList>(grouped);
        QVERIFY(groupedCmd.group.has_value());
        QCOMPARE(*groupedCmd.group, QString("modded"));
    }

    void test_instanceInfo()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "info", "My Instance" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        QCOMPARE(onlyCommand<Cli::Cmd::InstanceInfo>(args).id, QString("My Instance"));

        // the id is required
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "info" }), true);
    }

    void test_instanceCreate()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "create", "--name", "Test", "--mc", "1.21.1", "--loader", "fabric",
                                    "--loader-version", "latest", "--group", "modded", "--icon", "default" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::InstanceCreate>(args);
        QCOMPARE(cmd.name, QString("Test"));
        QCOMPARE(cmd.mcVersion, QString("1.21.1"));
        QVERIFY(cmd.loader.has_value());
        QCOMPARE(*cmd.loader, QString("fabric"));
        QVERIFY(cmd.loaderVersion.has_value());
        QCOMPARE(*cmd.loaderVersion, QString("latest"));
        QVERIFY(cmd.group.has_value());
        QCOMPARE(*cmd.group, QString("modded"));
        QVERIFY(cmd.icon.has_value());
        QCOMPARE(*cmd.icon, QString("default"));
        QVERIFY(!cmd.from.has_value());
        // the files are downloaded unless `--no-download` is used
        QCOMPARE(cmd.download, true);
    }

    void test_instanceCreate_noDownload()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "create", "--name", "Test", "--mc", "1.21.1", "--no-download" });
        QCOMPARE(onlyCommand<Cli::Cmd::InstanceCreate>(args).download, false);
    }

    void test_instanceCreate_from()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "create", "--from", "https://example.com/pack.mrpack" });
        const auto& cmd = onlyCommand<Cli::Cmd::InstanceCreate>(args);
        QVERIFY(cmd.from.has_value());
        QCOMPARE(*cmd.from, QString("https://example.com/pack.mrpack"));
        // a vanilla instance needs a name and a Minecraft version
        QCOMPARE(cmd.name, QString());
        QCOMPARE(cmd.mcVersion, QString());
    }

    void test_instanceCreate_requiresNameOrFrom()
    {
        // a vanilla instance needs a name and a Minecraft version
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "create" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "create", "--name", "Test" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "create", "--mc", "1.21.1" }), true);
        // ...an imported one does not
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "create", "--from", "pack.mrpack" }), false);
        // the loader is validated
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "create", "--name", "T", "--mc", "1.21.1", "--loader", "bukkit" }), true);
    }

    void test_instanceDuplicate()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "duplicate", "X", "--name", "Y", "--hardlinks", "--no-saves",
                                    "--no-mods" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::InstanceDuplicate>(args);
        QCOMPARE(cmd.id, QString("X"));
        QVERIFY(cmd.name.has_value());
        QCOMPARE(*cmd.name, QString("Y"));
        QCOMPARE(cmd.copyMode, QString("hardlinks"));
        QCOMPARE(cmd.noSaves, true);
        QCOMPARE(cmd.noMods, true);
        QCOMPARE(cmd.noConfigs, false);
    }

    void test_instanceDuplicate_copyModeIsExclusive()
    {
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "duplicate", "X", "--copy", "--links" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "duplicate", "X", "--clone", "--hardlinks" }), true);
        // the default is a plain copy
        const auto args = parseOk({ "prismlauncher", "instance", "duplicate", "X" });
        QCOMPARE(onlyCommand<Cli::Cmd::InstanceDuplicate>(args).copyMode, QString("copy"));
    }

    void test_instanceRename()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "rename", "X", "New Name" });
        const auto& cmd = onlyCommand<Cli::Cmd::InstanceRename>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.newName, QString("New Name"));
        // the folder is renamed unless `--keep-folder` is used
        QCOMPARE(cmd.renameFolder, true);

        const auto kept = parseOk({ "prismlauncher", "instance", "rename", "X", "New Name", "--keep-folder" });
        QCOMPARE(onlyCommand<Cli::Cmd::InstanceRename>(kept).renameFolder, false);

        // the two flags contradict each other
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "rename", "X", "Y", "--keep-folder", "--rename-folder" }), true);
        // the new name is required
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "rename", "X" }), true);
    }

    void test_instanceDelete()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "delete", "X" });
        const auto& cmd = onlyCommand<Cli::Cmd::InstanceDelete>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.purge, false);

        const auto purged = parseOk({ "prismlauncher", "instance", "delete", "X", "--purge" });
        QCOMPARE(onlyCommand<Cli::Cmd::InstanceDelete>(purged).purge, true);
    }

    void test_instanceIconSet()
    {
        const auto args = parseOk({ "prismlauncher", "instance", "icon", "set", "X", "default" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::InstanceIconSet>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.icon, QString("default"));

        // the icon is required
        QCOMPARE(parseThrows({ "prismlauncher", "instance", "icon", "set", "X" }), true);
    }

    void test_settingsList()
    {
        const auto args = parseOk({ "prismlauncher", "settings", "list", "X" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::SettingsList>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.all, false);
        QCOMPARE(cmd.modified, false);

        const auto all = parseOk({ "prismlauncher", "settings", "list", "X", "--all" });
        QCOMPARE(onlyCommand<Cli::Cmd::SettingsList>(all).all, true);

        const auto modified = parseOk({ "prismlauncher", "settings", "list", "X", "--modified" });
        QCOMPARE(onlyCommand<Cli::Cmd::SettingsList>(modified).modified, true);

        // the instance is required
        QCOMPARE(parseThrows({ "prismlauncher", "settings", "list" }), true);
    }

    void test_settingsGet()
    {
        const auto args = parseOk({ "prismlauncher", "settings", "get", "X", "memory.max", "memory.min" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::SettingsGet>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.keys.size(), std::size_t{ 2 });
        QCOMPARE(cmd.keys[0], QString("memory.max"));
        QCOMPARE(cmd.keys[1], QString("memory.min"));

        // at least one key is required
        QCOMPARE(parseThrows({ "prismlauncher", "settings", "get", "X" }), true);
    }

    void test_settingsSet()
    {
        const auto args = parseOk({ "prismlauncher", "settings", "set", "X", "memory.max=4096", "env.FOO=bar=baz" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::SettingsSet>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.values.size(), std::size_t{ 2 });
        QCOMPARE(cmd.values[0].first, QString("memory.max"));
        QCOMPARE(cmd.values[0].second, QString("4096"));
        QCOMPARE(cmd.values[1].first, QString("env.FOO"));
        // only the first `=` separates the key from the value
        QCOMPARE(cmd.values[1].second, QString("bar=baz"));

        // the values have to be in the `key=value` form
        QCOMPARE(parseThrows({ "prismlauncher", "settings", "set", "X", "memory.max" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "settings", "set", "X" }), true);
    }

    void test_settingsUnset()
    {
        const auto args = parseOk({ "prismlauncher", "settings", "unset", "X", "memory.max", "memory.min" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::SettingsUnset>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.keys.size(), std::size_t{ 2 });
        QCOMPARE(cmd.keys[0], QString("memory.max"));

        QCOMPARE(parseThrows({ "prismlauncher", "settings", "unset", "X" }), true);
    }

    void test_versionShow()
    {
        const auto args = parseOk({ "prismlauncher", "version", "show", "X" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        QCOMPARE(onlyCommand<Cli::Cmd::VersionShow>(args).id, QString("X"));

        QCOMPARE(parseThrows({ "prismlauncher", "version", "show" }), true);
    }

    void test_versionListAvailable()
    {
        const auto args = parseOk({ "prismlauncher", "version", "list-available" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::VersionListAvailable>(args);
        QVERIFY(!cmd.loader.has_value());
        QVERIFY(!cmd.mcVersion.has_value());
        QCOMPARE(cmd.type, QString("all"));

        const auto loader = parseOk({ "prismlauncher", "version", "list-available", "--loader", "fabric", "--mc-version", "1.21.1",
                                      "--type", "release" });
        const auto& loaderCmd = onlyCommand<Cli::Cmd::VersionListAvailable>(loader);
        QVERIFY(loaderCmd.loader.has_value());
        QCOMPARE(*loaderCmd.loader, QString("fabric"));
        QVERIFY(loaderCmd.mcVersion.has_value());
        QCOMPARE(*loaderCmd.mcVersion, QString("1.21.1"));
        QCOMPARE(loaderCmd.type, QString("release"));

        // the loader and the type are validated
        QCOMPARE(parseThrows({ "prismlauncher", "version", "list-available", "--loader", "bukkit" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "version", "list-available", "--type", "beta" }), true);
    }

    void test_versionChangeMc()
    {
        const auto args = parseOk({ "prismlauncher", "version", "change-mc", "X", "1.21.1" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::VersionChangeMc>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.version, QString("1.21.1"));

        // the version is required
        QCOMPARE(parseThrows({ "prismlauncher", "version", "change-mc", "X" }), true);
    }

    void test_versionInstallLoader()
    {
        const auto args = parseOk({ "prismlauncher", "version", "install-loader", "X", "fabric", "latest" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::VersionInstallLoader>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.loader, QString("fabric"));
        QCOMPARE(cmd.version, QString("latest"));
        // the conflicts are disabled by default
        QCOMPARE(cmd.forceConflict, QString("disable"));

        const auto keep = parseOk({ "prismlauncher", "version", "install-loader", "X", "quilt", "1.0.0", "--force-conflict",
                                    "keep" });
        QCOMPARE(onlyCommand<Cli::Cmd::VersionInstallLoader>(keep).forceConflict, QString("keep"));

        // the loader and the version are required and validated
        QCOMPARE(parseThrows({ "prismlauncher", "version", "install-loader", "X", "fabric" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "version", "install-loader", "X", "bukkit", "latest" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "version", "install-loader", "X", "fabric", "latest", "--force-conflict", "nope" }),
                 true);
    }

    void test_versionRemoveLoader()
    {
        const auto args = parseOk({ "prismlauncher", "version", "remove-loader", "X", "fabric" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::VersionRemoveLoader>(args);
        QCOMPARE(cmd.id, QString("X"));
        QCOMPARE(cmd.loader, QString("fabric"));

        QCOMPARE(parseThrows({ "prismlauncher", "version", "remove-loader", "X" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "version", "remove-loader", "X", "bukkit" }), true);
    }

    void test_versionReinstall()
    {
        const auto args = parseOk({ "prismlauncher", "version", "reinstall", "X" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        QCOMPARE(onlyCommand<Cli::Cmd::VersionReinstall>(args).id, QString("X"));

        QCOMPARE(parseThrows({ "prismlauncher", "version", "reinstall" }), true);
    }

    void test_positionalUri_untouched()
    {
        const auto args = parseOk({ "prismlauncher", "https://example.com/foo.mrpack" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::ProcessURI>(args);
        QCOMPARE(cmd.uri, std::string("https://example.com/foo.mrpack"));
    }

    void test_modMissingRequiredInstance()
    {
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "list" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod", "update", "--all" }), true);
        QCOMPARE(parseThrows({ "prismlauncher", "mod" }), true);
    }

    void test_parseError_jsonEnvelopeAndExitCode()
    {
        const auto result = runLauncher({ "mod", "list" });
        QCOMPARE(result.exitCode, static_cast<int>(Cli::ExitCode::Usage));

        const QJsonDocument doc = QJsonDocument::fromJson(result.out);
        QVERIFY2(doc.isObject(), qPrintable(QString::fromUtf8(result.out)));

        const QJsonObject root = doc.object();
        QCOMPARE(root.value("ok").toBool(), false);
        const QJsonObject error = root.value("error").toObject();
        QCOMPARE(error.value("code").toString(), QStringLiteral("parse_error"));
        QVERIFY(!error.value("message").toString().isEmpty());
        // newlines are stripped so the envelope is a single line
        QVERIFY(!error.value("message").toString().contains(QLatin1Char('\n')));
    }

    void test_helpAndVersion_keepExitZero()
    {
        const auto version = runLauncher({ "--version" });
        QCOMPARE(version.exitCode, 0);
        QVERIFY(!version.out.trimmed().isEmpty());

        const auto help = runLauncher({ "--help" });
        QCOMPARE(help.exitCode, 0);
        QVERIFY(!help.out.trimmed().isEmpty());
    }

    void test_settingsSetGet_fixtureInstance()
    {
        // The settings of an instance are stored in `instance.cfg`, so a
        // fixture instance is enough to check the schema of the commands.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto instanceDir = dir.filePath(QStringLiteral("instances/spike-test"));
        QVERIFY(QDir().mkpath(instanceDir + QStringLiteral("/minecraft")));

        QFile config(instanceDir + QStringLiteral("/instance.cfg"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("[General]\nConfigVersion=1.3\nInstanceType=OneSix\nname=spike-test\niconKey=default\n"
                     "uuid=11111111-2222-3333-4444-555555555555\n");
        config.close();

        qputenv("QT_QPA_PLATFORM", "offscreen");

        const auto set = runLauncher({ "-d", dir.path(), "settings", "set", "spike-test", "memory.min=1024", "memory.max=8192" });
        QCOMPARE(set.exitCode, static_cast<int>(Cli::ExitCode::OK));

        const auto get = runLauncher({ "-d", dir.path(), "settings", "get", "spike-test", "memory.max", "memory.min" });
        QCOMPARE(get.exitCode, static_cast<int>(Cli::ExitCode::OK));

        const QJsonDocument doc = QJsonDocument::fromJson(get.out);
        QVERIFY2(doc.isObject(), qPrintable(QString::fromUtf8(get.out)));
        const QJsonObject root = doc.object();
        QCOMPARE(root.value("ok").toBool(), true);
        const QJsonArray settings = root.value("settings").toArray();
        QCOMPARE(settings.size(), 2);

        // the value of the instance is used, and the group is overridden
        QCOMPARE(settings.at(0).toObject().value("key").toString(), QStringLiteral("memory.max"));
        QCOMPARE(settings.at(0).toObject().value("value").toInt(), 8192);
        QCOMPARE(settings.at(0).toObject().value("type").toString(), QStringLiteral("int"));
        QCOMPARE(settings.at(0).toObject().value("source").toString(), QStringLiteral("instance"));
        QCOMPARE(settings.at(0).toObject().value("overridden").toBool(), true);
        QCOMPARE(settings.at(0).toObject().value("ini_key").toString(), QStringLiteral("MaxMemAlloc"));
        QCOMPARE(settings.at(0).toObject().value("gate").toString(), QStringLiteral("OverrideMemory"));
        QCOMPARE(settings.at(1).toObject().value("key").toString(), QStringLiteral("memory.min"));
        QCOMPARE(settings.at(1).toObject().value("value").toInt(), 1024);

        // the value is written to the file of the instance, together with the
        // flag of the group
        QFile written(instanceDir + QStringLiteral("/instance.cfg"));
        QVERIFY(written.open(QIODevice::ReadOnly));
        const auto content = QString::fromUtf8(written.readAll());
        QVERIFY2(content.contains(QStringLiteral("MaxMemAlloc=8192")), qPrintable(content));
        QVERIFY2(content.contains(QStringLiteral("MinMemAlloc=1024")), qPrintable(content));
        QVERIFY2(content.contains(QStringLiteral("OverrideMemory=true")), qPrintable(content));

        // an unknown key is a usage error
        const auto unknown = runLauncher({ "-d", dir.path(), "settings", "set", "spike-test", "nosuchkey=1" });
        QCOMPARE(unknown.exitCode, static_cast<int>(Cli::ExitCode::Usage));

        // the minimum memory cannot be bigger than the maximum memory
        const auto invalid = runLauncher({ "-d", dir.path(), "settings", "set", "spike-test", "memory.min=99999" });
        QCOMPARE(invalid.exitCode, static_cast<int>(Cli::ExitCode::Usage));

        // unsetting the setting brings back the global value
        const auto unset = runLauncher({ "-d", dir.path(), "settings", "unset", "spike-test", "memory.max" });
        QCOMPARE(unset.exitCode, static_cast<int>(Cli::ExitCode::OK));
        const auto afterUnset = runLauncher({ "-d", dir.path(), "settings", "get", "spike-test", "memory.max" });
        QCOMPARE(afterUnset.exitCode, static_cast<int>(Cli::ExitCode::OK));
        const QJsonDocument unsetDoc = QJsonDocument::fromJson(afterUnset.out);
        const QJsonObject unsetSettings = unsetDoc.object().value("settings").toArray().at(0).toObject();
        QCOMPARE(unsetSettings.value("overridden").toBool(), false);
        QCOMPARE(unsetSettings.value("source").toString(), QStringLiteral("default"));
    }

    void test_allCommandsHeadless()
    {
        // pure headless startup: the only case that may exit the launcher
        Cli::Args pure{};
        pure.commands.emplace_back(Cli::Cmd::ModList{ .instance = QStringLiteral("X") });
        QCOMPARE(Cli::allCommandsHeadless(pure.commands), true);

        // mixed startup: headless commands must run without exiting the app
        Cli::Args mixed{};
        mixed.commands.emplace_back(Cli::Cmd::ModList{ .instance = QStringLiteral("X") });
        mixed.commands.emplace_back(Cli::Cmd::Launch{ .id = "foo" });
        QCOMPARE(Cli::allCommandsHeadless(mixed.commands), false);

        // empty command set is a normal GUI startup
        Cli::Args empty{};
        QCOMPARE(Cli::allCommandsHeadless(empty.commands), false);
    }
};

QTEST_GUILESS_MAIN(CliParserTest)
#include "CliParser_test.moc"
