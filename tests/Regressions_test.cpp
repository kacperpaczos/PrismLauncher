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

#include <QDir>
#include <QStandardPaths>
#include <QTest>
#include <QUrl>

#include <filesystem>
#include <variant>
#include <vector>

#include <cli/Commands.h>
#include <cli/Parser.h>
#include <qhashfunctions.h>
#include <qtestcase.h>
#include <startup/Startup.h>
#include "BuildConfig.h"
#include "CLI/CLI.hpp"

namespace Cli {

static QString joinForTest(const std::vector<QString>& values)
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values) {
        parts << value;
    }
    return parts.join(QStringLiteral(", "));
}

static QString packGroupForTest(Cmd::PackGroup group)
{
    switch (group) {
        case Cmd::PackGroup::Resource:
            return QStringLiteral("resource");
        case Cmd::PackGroup::Shader:
            return QStringLiteral("shader");
        case Cmd::PackGroup::Texture:
            return QStringLiteral("texture");
    }
    return QStringLiteral("resource");
}

// overload of QTest::toString
static char* toString(const Args& args)
{
    using namespace Cmd;
    struct {
        QString operator()(const Launch& launch)
        {
            struct {
                QString operator()(const Launch::NoTarget& /**/) { return "NoTarget{}"; }
                QString operator()(const Launch::ServerTarget& server)
                {
                    return QStringLiteral("ServerTarget{ target: \"%1\"}").arg(QString::fromStdString(server.target));
                }
                QString operator()(const Launch::WorldTarget& world)
                {
                    return QStringLiteral("WorldTarget{ target: \"%1\"}").arg(QString::fromStdString(world.target));
                }
            } targetVisitor;

            auto target = std::visit(targetVisitor, launch.target);

            struct {
                QString operator()(const Launch::AccountDefault& /**/) { return "AccountDefault{}"; }
                QString operator()(const Launch::AccountProfile& profile)
                {
                    return QStringLiteral("AccountProfile{ .name: \"%1\"}").arg(QString::fromStdString(profile.name));
                }
                QString operator()(const Launch::AccountOffline& profile)
                {
                    return QStringLiteral("AccountOffline{ name: \"%1\"}").arg(QString::fromStdString(profile.name));
                }
            } accountVisitor;

            auto account = std::visit(accountVisitor, launch.account);

            return QStringLiteral("Launch{ id: \"%1\", target: %2, account: %3 }").arg(QString::fromStdString(launch.id), target, account);
        }
        QString operator()(const ProcessURI& processURI)
        {
            return QStringLiteral("ProcessURI{ uri: \"%1\"}").arg(QString::fromStdString(processURI.uri));
        }
        QString operator()(const ShowMainWindow& /**/) { return "ShowMainWindow{}"; }
        QString operator()(const ShowInstanceWindow& showInstWin)
        {
            return QStringLiteral("ShowInstanceWindow{ id: \"%1\"}").arg(QString::fromStdString(showInstWin.id));
        }
        QString operator()(const Alive& alive)
        {
            return QStringLiteral("Alive{ path: \"%1\"}").arg(QString::fromStdU16String(alive.path.u16string()));
        }
        QString operator()(const ModList& modList)
        {
            return QStringLiteral("ModList{ instance: \"%1\", includeDisabled: %2 }")
                .arg(modList.instance, QString::number(modList.includeDisabled));
        }
        QString operator()(const ModSearch& modSearch)
        {
            return QStringLiteral("ModSearch{ query: \"%1\", mc: [%2], loaders: [%3], limit: %4, offset: %5 }")
                .arg(modSearch.query, joinForTest(modSearch.mcVersions), joinForTest(modSearch.loaders))
                .arg(modSearch.limit)
                .arg(modSearch.offset);
        }
        QString operator()(const ModInfo& modInfo)
        {
            return QStringLiteral("ModInfo{ project: \"%1\", instance: \"%2\" }")
                .arg(modInfo.project, modInfo.instance.value_or(QString()));
        }
        QString operator()(const ModInstall& modInstall)
        {
            return QStringLiteral("ModInstall{ project: \"%1\", instance: \"%2\", version: \"%3\", noDeps: %4, noMetadata: %5 }")
                .arg(modInstall.project, modInstall.instance, modInstall.version.value_or(QString()),
                     QString::number(modInstall.noDeps), QString::number(modInstall.noMetadata));
        }
        QString operator()(const ModRemove& modRemove)
        {
            return QStringLiteral("ModRemove{ targets: [%1], instance: \"%2\", force: %3 }")
                .arg(joinForTest(modRemove.targets), modRemove.instance, QString::number(modRemove.force));
        }
        QString operator()(const ModEnable& modEnable)
        {
            return QStringLiteral("ModEnable{ targets: [%1], instance: \"%2\", recursive: %3 }")
                .arg(joinForTest(modEnable.targets), modEnable.instance, QString::number(modEnable.recursive));
        }
        QString operator()(const ModDisable& modDisable)
        {
            return QStringLiteral("ModDisable{ targets: [%1], instance: \"%2\", recursive: %3 }")
                .arg(joinForTest(modDisable.targets), modDisable.instance, QString::number(modDisable.recursive));
        }
        QString operator()(const ModUpdate& modUpdate)
        {
            return QStringLiteral("ModUpdate{ targets: [%1], all: %2, instance: \"%3\", dryRun: %4, noDeps: %5, force: %6 }")
                .arg(joinForTest(modUpdate.targets))
                .arg(QString::number(modUpdate.all), modUpdate.instance, QString::number(modUpdate.dryRun),
                     QString::number(modUpdate.noDeps), QString::number(modUpdate.force));
        }
        QString operator()(const PackList& packList)
        {
            return QStringLiteral("PackList{ group: \"%1\", instance: \"%2\", includeDisabled: %3 }")
                .arg(packGroupForTest(packList.group), packList.instance, QString::number(packList.includeDisabled));
        }
        QString operator()(const PackSearch& packSearch)
        {
            return QStringLiteral("PackSearch{ group: \"%1\", query: \"%2\", mc: [%3], limit: %4, offset: %5 }")
                .arg(packGroupForTest(packSearch.group), packSearch.query, joinForTest(packSearch.mcVersions))
                .arg(packSearch.limit)
                .arg(packSearch.offset);
        }
        QString operator()(const PackInfo& packInfo)
        {
            return QStringLiteral("PackInfo{ group: \"%1\", project: \"%2\", instance: \"%3\" }")
                .arg(packGroupForTest(packInfo.group), packInfo.project, packInfo.instance.value_or(QString()));
        }
        QString operator()(const PackInstall& packInstall)
        {
            return QStringLiteral("PackInstall{ group: \"%1\", project: \"%2\", instance: \"%3\", version: \"%4\", noMetadata: %5 }")
                .arg(packGroupForTest(packInstall.group), packInstall.project, packInstall.instance,
                     packInstall.version.value_or(QString()), QString::number(packInstall.noMetadata));
        }
        QString operator()(const PackRemove& packRemove)
        {
            return QStringLiteral("PackRemove{ group: \"%1\", targets: [%2], instance: \"%3\", force: %4 }")
                .arg(packGroupForTest(packRemove.group), joinForTest(packRemove.targets), packRemove.instance,
                     QString::number(packRemove.force));
        }
        QString operator()(const PackEnable& packEnable)
        {
            return QStringLiteral("PackEnable{ group: \"%1\", targets: [%2], instance: \"%3\" }")
                .arg(packGroupForTest(packEnable.group), joinForTest(packEnable.targets), packEnable.instance);
        }
        QString operator()(const PackDisable& packDisable)
        {
            return QStringLiteral("PackDisable{ group: \"%1\", targets: [%2], instance: \"%3\" }")
                .arg(packGroupForTest(packDisable.group), joinForTest(packDisable.targets), packDisable.instance);
        }
        QString operator()(const WorldList& worldList)
        {
            return QStringLiteral("WorldList{ instance: \"%1\", withSize: %2 }")
                .arg(worldList.instance, QString::number(worldList.withSize));
        }
        QString operator()(const WorldInfo& worldInfo)
        {
            return QStringLiteral("WorldInfo{ instance: \"%1\", target: \"%2\", withSize: %3 }")
                .arg(worldInfo.instance, worldInfo.target, QString::number(worldInfo.withSize));
        }
        QString operator()(const WorldCopy& worldCopy)
        {
            return QStringLiteral("WorldCopy{ instance: \"%1\", target: \"%2\", name: \"%3\" }")
                .arg(worldCopy.instance, worldCopy.target, worldCopy.name.value_or(QString()));
        }
        QString operator()(const WorldRename& worldRename)
        {
            return QStringLiteral("WorldRename{ instance: \"%1\", target: \"%2\", name: \"%3\" }")
                .arg(worldRename.instance, worldRename.target, worldRename.name);
        }
        QString operator()(const WorldDelete& worldDelete)
        {
            return QStringLiteral("WorldDelete{ instance: \"%1\", target: \"%2\", permanent: %3 }")
                .arg(worldDelete.instance, worldDelete.target, QString::number(worldDelete.permanent));
        }
        QString operator()(const WorldImport& worldImport)
        {
            return QStringLiteral("WorldImport{ instance: \"%1\", path: \"%2\", name: \"%3\" }")
                .arg(worldImport.instance, worldImport.path, worldImport.name.value_or(QString()));
        }
        QString operator()(const ServerList& serverList)
        {
            return QStringLiteral("ServerList{ instance: \"%1\", ping: %2 }")
                .arg(serverList.instance, QString::number(serverList.ping));
        }
        QString operator()(const ServerAdd& serverAdd)
        {
            return QStringLiteral("ServerAdd{ instance: \"%1\", name: \"%2\", address: \"%3\", index: %4 }")
                .arg(serverAdd.instance, serverAdd.name, serverAdd.address)
                .arg(serverAdd.index.has_value() ? QString::number(*serverAdd.index) : QStringLiteral("none"));
        }
        QString operator()(const ServerRemove& serverRemove)
        {
            return QStringLiteral("ServerRemove{ instance: \"%1\", target: \"%2\", force: %3 }")
                .arg(serverRemove.instance, serverRemove.target, QString::number(serverRemove.force));
        }
        QString operator()(const LogList& logList)
        {
            return QStringLiteral("LogList{ instance: \"%1\" }").arg(logList.instance);
        }
        QString operator()(const LogRead& logRead)
        {
            return QStringLiteral("LogRead{ instance: \"%1\", file: \"%2\", head: %3, tail: %4, grep: \"%5\", level: \"%6\", json: %7 }")
                .arg(logRead.instance, logRead.file.value_or(QString()))
                .arg(logRead.head.has_value() ? QString::number(*logRead.head) : QStringLiteral("none"))
                .arg(logRead.tail.has_value() ? QString::number(*logRead.tail) : QStringLiteral("none"))
                .arg(logRead.grep.value_or(QString()), logRead.level.value_or(QString()))
                .arg(QString::number(logRead.json));
        }
        QString operator()(const LogUpload& logUpload)
        {
            return QStringLiteral("LogUpload{ instance: \"%1\", file: \"%2\", service: \"%3\", anonymize: %4, assumeYes: %5 }")
                .arg(logUpload.instance, logUpload.file.value_or(QString()), logUpload.service)
                .arg(QString::number(logUpload.anonymize), QString::number(logUpload.assumeYes));
        }
    } cmdVisitor;

    QStringList cmds{};

    for (const auto& cmd : args.commands) {
        cmds.append(std::visit(cmdVisitor, cmd));
    }
    auto repr = QStringLiteral("Args{ commands: [%1]}").arg(cmds.join(", ")).toStdString();
    // QTest::toString requires us to output an allocated char*
    return qstrdup(repr.data());
}

}  // namespace Cli

class RegressionTests : public QObject {
    Q_OBJECT

    enum class CLIErrorType : std::uint8_t {
        Error,
        ParseError,
        OptionAlreadyAdded,
        CallForHelp,
        ArgumentMismatch,
        RequiresError,
        ExcludesError,
        FileError,
        ValidationError,
        OptionNotFound
    };

   private slots:

    // the exceptions we might expect to be thrown

    void test_legacyCli_data()
    {
        QTest::addColumn<std::vector<std::string>>("argv");
        QTest::addColumn<Cli::Args>("expected");
        QTest::addColumn<std::optional<CLIErrorType>>("throws");

        {
            using namespace Cli::Cmd;
            QTest::newRow("launch online")
                << std::vector<std::string>{ "--launch", "an_instance" }
                << Cli::Args{ .commands = {
                    Launch{ .id = "an_instance",
                        .target = Launch::NoTarget{},
                        .account = Launch::AccountDefault{}, }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch online /w show-window")
                << std::vector<std::string>{ "--launch", "an_instance", "--show-window" }
                << Cli::Args{ .commands = {
                    Launch{ .id = "an_instance",
                        .target = Launch::NoTarget{},
                        .account = Launch::AccountDefault{}, }, ShowMainWindow{}, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch online /w show")
                << std::vector<std::string>{ "--launch", "an_instance", "--show", "an_instance" }
                << Cli::Args{ .commands = {
                    Launch{ .id = "an_instance",
                        .target = Launch::NoTarget{},
                        .account = Launch::AccountDefault{}, }, ShowInstanceWindow{ .id = "an_instance" }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch online short")
                << std::vector<std::string>{ "-l", "an_instance" }
                << Cli::Args{ .commands = {
                                  Launch{ .id = "an_instance", .target = Launch::NoTarget{}, .account = Launch::AccountDefault{} }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("uri with launch")
                << std::vector<std::string>{ "a/uri", "-l", "an_instance" }
                << Cli::Args{ .commands = { Launch{ .id = "an_instance",
                                                    .target = Launch::NoTarget{},
                                                    .account = Launch::AccountDefault{}, },
                                            ProcessURI{ .uri = "a/uri" }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch online /w account")
                << std::vector<std::string>{ "--launch", "an_instance", "-a", "profile_name" }
                << Cli::Args{ .commands = { Launch{ .id = "an_instance",
                                                    .target = Launch::NoTarget{},
                                                    .account = Launch::AccountProfile{ .name = "profile_name" }, }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch online /w server")
                << std::vector<std::string>{ "--launch", "an_instance", "-s", "mc.serveraddress.net:1234" }
                << Cli::Args{ .commands = { Launch{ .id = "an_instance",
                                                    .target = Launch::ServerTarget{ .target = "mc.serveraddress.net:1234" },
                                                    .account = Launch::AccountDefault{}, }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch online /w world")
                << std::vector<std::string>{ "--launch", "an_instance", "-w", "world_name" }
                << Cli::Args{ .commands = { Launch{ .id = "an_instance",
                                                    .target = Launch::WorldTarget{ .target = "world_name" },
                                                    .account = Launch::AccountDefault{}, }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch online /w account /w server")
                << std::vector<std::string>{ "--launch", "an_instance", "-s", "mc.serveraddress.net:1234", "-a", "profile_name" }
                << Cli::Args{ .commands = { Launch{ .id = "an_instance",
                                                    .target = Launch::ServerTarget{ .target = "mc.serveraddress.net:1234" },
                                                    .account = Launch::AccountProfile{ .name = "profile_name" }, }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("launch offline")
                << std::vector<std::string>{ "--launch", "an_instance", "-o", "offline_name" }
                << Cli::Args{ .commands = { Launch{ .id = "an_instance",
                                                    .target = Launch::NoTarget{},
                                                    .account = Launch::AccountOffline{ .name = "offline_name" }, }, }, }
                << std::optional<CLIErrorType>{};
            QTest::newRow("can't launch offline with an account")
                << std::vector<std::string>{ "--launch", "an_instance", "-o", "offline_name", "-a", "profile_name" } << Cli::Args{}
                << std::optional<CLIErrorType>{ CLIErrorType::ExcludesError };
            QTest::newRow("can't launch with a world and server target")
                << std::vector<std::string>{ "--launch", "an_instance", "-w", "world_name", "-s", "mc.serveraddress.net:1234" }
                << Cli::Args{} << std::optional<CLIErrorType>{ CLIErrorType::ExcludesError };
            QTest::newRow("can't launch without --launch (world)") << std::vector<std::string>{ "-w", "world_name" } << Cli::Args{}
                                                                   << std::optional<CLIErrorType>{ CLIErrorType::RequiresError };
            QTest::newRow("can't launch without --launch (server)")
                << std::vector<std::string>{ "-s", "mc.serveraddress.net:1234" } << Cli::Args{}
                << std::optional<CLIErrorType>{ CLIErrorType::RequiresError };
            QTest::newRow("can't launch without --launch (account)") << std::vector<std::string>{ "-a", "profile_name" } << Cli::Args{}
                                                                     << std::optional<CLIErrorType>{ CLIErrorType::RequiresError };
            QTest::newRow("can't launch without --launch (ofline)") << std::vector<std::string>{ "-o", "offline_name" } << Cli::Args{}
                                                                    << std::optional<CLIErrorType>{ CLIErrorType::RequiresError };

            QTest::newRow("show main window") << std::vector<std::string>{ "--show-window" }
                                              << Cli::Args{ .commands = { ShowMainWindow{} } } << std::optional<CLIErrorType>{};
            QTest::newRow("show instance window")
                << std::vector<std::string>{ "--show", "an_instance" }
                << Cli::Args{ .commands = { ShowInstanceWindow{ .id = "an_instance" } } } << std::optional<CLIErrorType>{};

            QTest::newRow("alive") << std::vector<std::string>{ "--alive" }
                                   << Cli::Args{ .commands = { Alive{ .path = std::filesystem::current_path() } } }
                                   << std::optional<CLIErrorType>{};
            auto realPath = QFINDTESTDATA("testdata/Regressions");
            QTest::newRow("alive /w path") << std::vector<std::string>{ "--alive", realPath.toStdString() }
                                           << Cli::Args{ .commands = { Alive{ .path = realPath.toStdString() } } }
                                           << std::optional<CLIErrorType>{};
            QTest::newRow("alive /w bad path") << std::vector<std::string>{ "--alive", "path/to/loc" } << Cli::Args{}
                                               << std::optional<CLIErrorType>{ CLIErrorType::ValidationError };

            QTest::newRow("import") << std::vector<std::string>{ "-I", "path/to/import" }
                                    << Cli::Args{ .commands = { ProcessURI{ .uri = "path/to/import" } } } << std::optional<CLIErrorType>{};
            QTest::newRow("multi import") << std::vector<std::string>{ "-I", "path/to/import", "-I", "path/to/import2",
                                                                       "-I", "path/to/import3", }
                                          << Cli::Args{ .commands = { ProcessURI{ .uri = "path/to/import" },
                                                                      ProcessURI{ .uri = "path/to/import2" },
                                                                      ProcessURI{ .uri = "path/to/import3" }, }, }
                                          << std::optional<CLIErrorType>{};
            QTest::newRow("import after positional")
                << std::vector<std::string>{ "a_uri/path", "-I", "path/to/import" }
                << Cli::Args{ .commands = { ProcessURI{ .uri = "path/to/import" }, ProcessURI{ .uri = "a_uri/path" } } }
                << std::optional<CLIErrorType>{};
            QTest::newRow("import before positional")
                << std::vector<std::string>{ "-I", "path/to/import", "a_uri/path" }
                << Cli::Args{ .commands = { ProcessURI{ .uri = "path/to/import" }, ProcessURI{ .uri = "a_uri/path" } } }
                << std::optional<CLIErrorType>{};

            QTest::newRow("multi uri") << std::vector<std::string>{ "path/to/import",  "path/to/import2", "path/to/import3" }
                                       << Cli::Args{ .commands = { ProcessURI{ .uri = "path/to/import" },
                                                                   ProcessURI{ .uri = "path/to/import2" },
                                                                   ProcessURI{ .uri = "path/to/import3" }, }, }
                                       << std::optional<CLIErrorType>{};

            QTest::newRow("accepts a datapath")
                << std::vector<std::string>{ "-d", "~/prism" }
                << Cli::Args{ .dataPath = { .dataPath = "~/prism", .source = Startup::DataPathSource::Commandline }, .commands = {} }
                << std::optional<CLIErrorType>{};

            QTest::newRow("accepts a datapath with other commands")
                << std::vector<std::string>{ "-d",
                                             "~/prism",
                                             "-l",
                                             "an_instance",
                                             "-o",
                                             "offline_name",
                                             "-I",
                                             "path/to/import",
                                             "positional/import",
                                             "another/positional/import",
                                             "--show-window", }
                << Cli::Args{ .dataPath = { .dataPath = "~/prism", .source = Startup::DataPathSource::Commandline },
                              .commands = { Launch{
                                                .id = "an_instance",
                                                .target = Launch::NoTarget{},
                                                .account = Launch::AccountOffline{ .name = "offline_name" },

                                            },
                                            ShowMainWindow{},
                                            ProcessURI{ .uri = "path/to/import" },
                                            ProcessURI{ .uri = "positional/import" },
                                            ProcessURI{ .uri = "another/positional/import" },

                              }, }
                << std::optional<CLIErrorType>{};
        }
    }

    void test_legacyCli()
    {
        QFETCH(std::vector<std::string>, argv);
        QFETCH(Cli::Args, expected);
        QFETCH(std::optional<CLIErrorType>, throws);

        Cli::Args parsed{};
        CLI::App parser{};
        Cli::Cli cli{};
        cli.attach(parser, parsed);

        // not sure *WHY* these needs to be reversed but this is how CLI11's internal tests do it...
        // https://github.com/CLIUtils/CLI11/blob/eced4d837d0ab57242b1c25d03780d35bf2b0d52/tests/app_helper.hpp#L34
        std::ranges::reverse(argv);

        if (throws) {
            switch (throws.value()) {
                case CLIErrorType::ArgumentMismatch: {
                    QVERIFY_THROWS_EXCEPTION(CLI::ArgumentMismatch, parser.parse(argv));
                    break;
                }
                case CLIErrorType::OptionAlreadyAdded: {
                    QVERIFY_THROWS_EXCEPTION(CLI::OptionAlreadyAdded, parser.parse(argv));
                    break;
                }
                case CLIErrorType::CallForHelp: {
                    QVERIFY_THROWS_EXCEPTION(CLI::CallForHelp, parser.parse(argv));
                    break;
                }
                case CLIErrorType::RequiresError: {
                    QVERIFY_THROWS_EXCEPTION(CLI::RequiresError, parser.parse(argv));
                    break;
                }
                case CLIErrorType::ExcludesError: {
                    QVERIFY_THROWS_EXCEPTION(CLI::ExcludesError, parser.parse(argv));
                    break;
                }
                case CLIErrorType::ValidationError: {
                    QVERIFY_THROWS_EXCEPTION(CLI::ValidationError, parser.parse(argv));
                    break;
                }
                case CLIErrorType::FileError: {
                    QVERIFY_THROWS_EXCEPTION(CLI::FileError, parser.parse(argv));
                    break;
                }
                case CLIErrorType::OptionNotFound: {
                    QVERIFY_THROWS_EXCEPTION(CLI::OptionNotFound, parser.parse(argv));
                    break;
                }
                case CLIErrorType::ParseError: {
                    QVERIFY_THROWS_EXCEPTION(CLI::ParseError, parser.parse(argv));
                    break;
                }
                case CLIErrorType::Error:
                default: {
                    QVERIFY_THROWS_EXCEPTION(CLI::Error, parser.parse(argv));
                }
            }
        } else {
            QVERIFY_THROWS_NO_EXCEPTION(parser.parse(argv));
        }

        QCOMPARE(parsed, expected);
    }

    void test_resolveStandardDataPath()
    {
        // get standard writable loc and peel off qt's auto added app name
        auto standardLocation =
            std::filesystem::path{ QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdU16String() }.parent_path();

        const auto rootDirs = QDir(QFINDTESTDATA("testdata/Regressions")).absolutePath();
        const auto nonPortableRoot = std::filesystem::path{ rootDirs.toStdU16String() } / "NonPortableRoot";

#ifndef Q_OS_MACOS
        const auto portableTxtRoot = std::filesystem::path{ rootDirs.toStdU16String() } / "PortableTXTRoot";
        const auto userDataRoot = std::filesystem::path{ rootDirs.toStdU16String() } / "UserDataRoot";

        const auto portableTxtResult = Startup::resolveDataPath(portableTxtRoot);
        QCOMPARE(portableTxtResult.dataPath, portableTxtRoot);

        const auto userDataResult = Startup::resolveDataPath(userDataRoot);
        QCOMPARE(userDataResult.dataPath, userDataRoot / "UserData");
#endif

        const auto standardLocationPath = QString::fromStdU16String(standardLocation.u16string());
        const auto standardLocationUrl = QUrl::fromLocalFile(standardLocationPath);

        const auto nonPortableResult = Startup::resolveDataPath(nonPortableRoot);
        const auto nonPortableDataPath = QString::fromStdU16String(nonPortableResult.dataPath.u16string());
        const auto nonPortableResultUrl = QUrl::fromLocalFile(nonPortableDataPath);

        qDebug() << "non-portable data path:" << nonPortableDataPath;
        QVERIFY2(standardLocationUrl.isParentOf(nonPortableResultUrl),
                 qPrintable(QStringLiteral("non-portable dataPath `%1` is not in the standard location (`%2`)")
                                .arg(nonPortableDataPath)
                                .arg(standardLocationPath)));
        QCOMPARE(QString::fromStdU16String(nonPortableResult.dataPath.filename().u16string()), BuildConfig.LAUNCHER_NAME);
        auto portableParentName = QString::fromStdU16String(nonPortableResult.dataPath.parent_path().u16string()).toLower();
        QVERIFY2(portableParentName != BuildConfig.LAUNCHER_NAME.toLower() &&
                     portableParentName != QCoreApplication::applicationName().toLower() &&
                     portableParentName != QCoreApplication::organizationName().toLower(),
                 qPrintable(QStringLiteral("non-portable data path `%1` appears to have too many parts").arg(portableParentName)));
    }
};

QTEST_GUILESS_MAIN(RegressionTests)
#include "Regressions_test.moc"
