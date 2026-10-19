// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
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
