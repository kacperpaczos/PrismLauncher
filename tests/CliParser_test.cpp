// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include <CLI/CLI.hpp>

#include "cli/Parser.h"

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
        const auto args = parseOk({ "prismlauncher", "mod", "search", "jei", "--mc", "1.20.1", "1.21", "--loader", "fabric",
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

    void test_legacyLaunch_untouched()
    {
        const auto args = parseOk({ "prismlauncher", "--launch", "foo" });
        QCOMPARE(args.commands.size(), std::size_t{ 1 });
        const auto& cmd = onlyCommand<Cli::Cmd::Launch>(args);
        QCOMPARE(cmd.id, std::string("foo"));
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
