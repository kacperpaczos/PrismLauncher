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

#include "cli/Parser.h"

#include <algorithm>
#include <cstdlib>

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

#include <CLI/CLI.hpp>

#include "BuildConfig.h"
#include "cli/Commands.h"
#include "startup/Startup.h"

#include <QUrl>

namespace Cli {
namespace {

struct URLValidator : CLI::Validator {
    URLValidator()
    {
        name_ = "URL";
        func_ = [](const std::string& uri) -> std::string {
            auto uriStr = QString::fromStdString(uri);

            const QUrl url(uriStr);
            if (!url.isValid()) {
                return url.errorString().toStdString();
            }

            return {};
        };
    }
};

}  // namespace

void LegacyCli::addLegacyArgs(CLI::App& app, Args& args)
{
    // legacy api
    //
    // { { { "d", "dir" }, "Use a custom path as application root (use '.' for current directory)", "directory" },
    //   { { "l", "launch" }, "Launch the specified instance (by instance ID)", "instance" },
    //   { { "s", "server" }, "Join the specified server on launch (only valid in combination with --launch)", "address" },
    //   { { "w", "world" }, "Join the specified world on launch (only valid in combination with --launch)", "world" },
    //   { { "a", "profile" }, "Use the account specified by its profile name (only valid in combination with --launch)", "profile" },
    //   { { "o", "offline" }, "Launch offline, with given player name (only valid in combination with --launch)", "offline" },
    //   { "alive", "Write a small '" + g_liveCheckFile + "' file after the launcher starts" },
    //   { "show-window", "Show the main launcher window (useful in combination with --launch)" },
    //   { { "I", "import" }, "Import instance or resource from specified local path or URL", "url" },
    //   { "show", "Opens the window for the specified instance (by instance ID)", "show" } });

    // legacy launch api
    {
        // nameless subcommand, captures
        launchGroup = app.add_option_group("launch", "launch an instance")
                          ->preparse_callback([this](std::size_t /*_*/) {
                              // default construct when group encountered
                              processingLaunch.emplace();
                          })
                          ->parse_complete_callback([&args, this] {
                              if (processingLaunch) {
                                  // push the launch command
                                  args.commands.emplace_back(std::move(processingLaunch.value()));
                                  processingLaunch = {};
                              }
                          })
                          ->silent();
        launch = launchGroup
                     ->add_option_function<std::string>(
                         "-l,--launch", [this](const std::string& id) { processingLaunch->id = id; },
                         "Launch the specified instance (by instance ID).")
                     ->option_text("id");

        server = launchGroup
                     ->add_option_function<std::string>(
                         "-s,--server",
                         [this](const std::string& address) { processingLaunch->target = Cmd::Launch::ServerTarget{ .target = address }; },
                         "Join the specified server on launch.")
                     ->option_text("address")
                     ->needs(launch);

        world = launchGroup
                    ->add_option_function<std::string>(
                        "-w,--world",
                        [this](const std::string& world) { processingLaunch->target = Cmd::Launch::WorldTarget{ .target = world }; },
                        "Join the specified world on launch.")
                    ->option_text("world")
                    ->needs(launch)
                    ->excludes(server);

        server->excludes(world);

        account =
            launchGroup
                ->add_option_function<std::string>(
                    "-a,--profile,--account",
                    [this](const std::string& account) { processingLaunch->account = Cmd::Launch::AccountProfile{ .name = account }; },
                    "Use the account specified by its profile name.")
                ->option_text("profile")
                ->needs(launch);

        offline =
            launchGroup
                ->add_option_function<std::string>(
                    "-o,--offline", [this](const auto& name) { processingLaunch->account = Cmd::Launch::AccountOffline{ .name = name }; },
                    "Launch offline, with given player name.")
                ->option_text("name")
                ->needs(launch)
                ->excludes(account);

        account->excludes(offline);
    }

    {
        // legacy show-window
        showMain = app.add_flag_function(
            "--show-window",
            [&args](std::int64_t /*count*/) {
                if (std::ranges::none_of(args.commands,
                                         [](const Command& cmd) -> bool { return std::holds_alternative<Cmd::ShowMainWindow>(cmd); })) {
                    args.commands.emplace_back(Cmd::ShowMainWindow{});
                }
            },
            "Show the main launcher window (useful in combination with --launch).");
    }

    {
        // legacy show
        showInstance = app.add_option_function<std::string>(
                              "--show",
                              [&args](const std::string& id) {
                                  if (std::ranges::none_of(args.commands, [id](const Command& cmd) -> bool {
                                          return std::holds_alternative<Cmd::ShowInstanceWindow>(cmd) &&
                                                 std::get<Cmd::ShowInstanceWindow>(cmd).id == id;
                                      })) {
                                      args.commands.emplace_back(Cmd::ShowInstanceWindow{ .id = id });
                                  }
                              },
                              "Opens the window for the specified instance (by instance ID).")
                           ->option_text("id");
    }

    {
        // legacy alive
        alive = app.add_option_function<std::filesystem::path>(
                       "--alive",
                       [&args](std::filesystem::path path) {
                           if (path.empty()) {
                               path = std::filesystem::current_path();
                           }
                           path = std::filesystem::absolute(path);
                           if (std::ranges::none_of(args.commands, [&](const Command& cmd) -> bool {
                                   return std::holds_alternative<Cmd::Alive>(cmd) && std::get<Cmd::Alive>(cmd).path == path;
                               })) {
                               args.commands.emplace_back(Cmd::Alive{ .path = path });
                           }
                       },
                       std::string{ "Write a small '" } + std::string{ g_liveCheckFile } +
                           "' file after the launcher starts.\n"
                           "The file is written in the directory specified.\n"
                           "(Defaults to the current directory)")
                    ->check(CLI::ExistingDirectory)
                    ->default_str(std::filesystem::current_path().string())
                    ->expected(0, 1);
    }

    {
        // legacy import
        app.add_option_function<std::vector<std::string>>(
               "-I,--import",
               [&args](const std::vector<std::string>& uris) {
                   for (const auto& uri : uris) {
                       args.commands.emplace_back(Cmd::ProcessURI{ .uri = uri });
                   }
               },
               "Import instance or resource from specified local path or URL")
            ->option_text("uri ...")
            ->check(CLI::ExistingPath | URLValidator{});
    }
}

void ModCli::addModArgs(CLI::App& app)
{
    mod = app.add_subcommand("mod", "Manage the mods of an instance.");
    mod->require_subcommand();

    auto addInstanceOption = [](CLI::App* sub, std::string& dest, bool required) {
        auto* opt = sub->add_option("-i,--instance", dest, "Instance ID.")->option_text("instance");
        if (required) {
            opt->required();
        }
        return opt;
    };

    auto addJsonFlag = [](CLI::App* sub) { sub->add_flag("--json", "Emit machine-readable JSON output (default)."); };

    {
        // mod list -i <instance> [--include-disabled] [--json]
        auto* list = mod->add_subcommand("list", "List the mods installed in an instance.");
        addInstanceOption(list, listInstance, /*required=*/true);
        list->add_flag("--include-disabled", listIncludeDisabled, "Include disabled mods.");
        addJsonFlag(list);
    }

    {
        // mod search [query] [--mc <version>...] [--loader <loader>...] [--limit N] [--offset N] [--json]
        auto* search = mod->add_subcommand("search", "Search for mods on Modrinth.");
        search->add_option("query", searchQuery, "Search query.")->option_text("query");
        search->add_option("--mc", searchMcVersions, "Filter by Minecraft version (can be repeated).")
            ->option_text("version")
            ->take_all();
        search->add_option("--loader", searchLoaders, "Filter by mod loader (can be repeated).")->option_text("loader")->take_all();
        search->add_option("--limit", searchLimit, "Maximum number of results.")->option_text("N");
        search->add_option("--offset", searchOffset, "Result offset, for pagination.")->option_text("N");
        addJsonFlag(search);
    }

    {
        // mod info <project> [-i <instance>] [--json]
        auto* info = mod->add_subcommand("info", "Show information about a mod project.");
        info->add_option("project", infoProject, "Mod slug or project ID.")->option_text("project")->required();
        infoInstance = addInstanceOption(info, infoInstanceValue, /*required=*/false);
        addJsonFlag(info);
    }

    {
        // mod install <project> -i <instance> [--version <v>] [--no-deps] [--no-metadata] [-y] [--json]
        auto* install = mod->add_subcommand("install", "Install a mod into an instance.");
        install->add_option("project", installProject, "Mod slug or project ID.")->option_text("project")->required();
        addInstanceOption(install, installInstance, /*required=*/true);
        installVersion = install->add_option("--version", installVersionValue, "Install a specific version.")->option_text("version");
        install->add_flag("--no-deps", installNoDeps, "Do not install dependencies.");
        install->add_flag("--no-metadata", installNoMetadata, "Do not write packwiz metadata for the mod.");
        install->add_flag("-y,--yes", assumeYes, "Do not ask for confirmation.");
        addJsonFlag(install);
    }

    {
        // mod remove <target>... -i <instance> [--force] [-y] [--json]
        auto* remove = mod->add_subcommand("remove", "Remove one or more mods from an instance.");
        remove->add_option("targets", removeTargets, "Mod filename, slug, project ID or name.")
            ->option_text("target")
            ->expected(0, -1);
        addInstanceOption(remove, removeInstance, /*required=*/true);
        remove->add_flag("--force", removeForce, "Remove even if the mod is locked or ambiguous.");
        remove->add_flag("-y,--yes", assumeYes, "Do not ask for confirmation.");
        addJsonFlag(remove);
    }

    {
        // mod enable <target>... -i <instance> [--recursive] [-y] [--json]
        auto* enable = mod->add_subcommand("enable", "Enable one or more mods in an instance.");
        enable->add_option("targets", enableTargets, "Mod filename, slug, project ID or name.")
            ->option_text("target")
            ->expected(0, -1);
        addInstanceOption(enable, enableInstance, /*required=*/true);
        enable->add_flag("--recursive", enableRecursive, "Also enable dependencies.");
        enable->add_flag("-y,--yes", assumeYes, "Do not ask for confirmation.");
        addJsonFlag(enable);
    }

    {
        // mod disable <target>... -i <instance> [--recursive] [-y] [--json]
        auto* disable = mod->add_subcommand("disable", "Disable one or more mods in an instance.");
        disable->add_option("targets", disableTargets, "Mod filename, slug, project ID or name.")
            ->option_text("target")
            ->expected(0, -1);
        addInstanceOption(disable, disableInstance, /*required=*/true);
        disable->add_flag("--recursive", disableRecursive, "Also disable dependants.");
        disable->add_flag("-y,--yes", assumeYes, "Do not ask for confirmation.");
        addJsonFlag(disable);
    }

    {
        // mod update [<target>...] --all -i <instance> [--dry-run] [--no-deps] [--force] [-y] [--json]
        auto* update = mod->add_subcommand("update", "Update one or more mods in an instance.");
        update->add_option("targets", updateTargets, "Mod filename, slug, project ID or name.")
            ->option_text("target")
            ->expected(0, -1);
        update->add_flag("--all", updateAll, "Update every mod in the instance.");
        addInstanceOption(update, updateInstance, /*required=*/true);
        update->add_flag("--dry-run", updateDryRun, "Only report what would be updated.");
        update->add_flag("--no-deps", updateNoDeps, "Do not update dependencies.");
        update->add_flag("--force", updateForce, "Update mods that are locked to a version.");
        update->add_flag("-y,--yes", assumeYes, "Do not ask for confirmation.");
        addJsonFlag(update);
    }
}

void ModCli::flush(Args& args) const
{
    if (mod == nullptr) {
        return;
    }

    auto subParsed = [this](const char* name) -> bool {
        auto* sub = mod->get_subcommand_no_throw(name);
        return sub != nullptr && sub->parsed();
    };

    auto toQStringList = [](const std::vector<std::string>& in) {
        std::vector<QString> out;
        out.reserve(in.size());
        for (const auto& value : in) {
            out.emplace_back(QString::fromStdString(value));
        }
        return out;
    };

    if (subParsed("list")) {
        Cmd::ModList cmd;
        cmd.instance = QString::fromStdString(listInstance);
        cmd.includeDisabled = listIncludeDisabled;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("search")) {
        Cmd::ModSearch cmd;
        cmd.query = QString::fromStdString(searchQuery);
        cmd.mcVersions = toQStringList(searchMcVersions);
        cmd.loaders = toQStringList(searchLoaders);
        cmd.limit = searchLimit;
        cmd.offset = searchOffset;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("info")) {
        Cmd::ModInfo cmd;
        cmd.project = QString::fromStdString(infoProject);
        if (infoInstance != nullptr && infoInstance->count() > 0) {
            cmd.instance = QString::fromStdString(infoInstanceValue);
        }
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("install")) {
        Cmd::ModInstall cmd;
        cmd.project = QString::fromStdString(installProject);
        cmd.instance = QString::fromStdString(installInstance);
        if (installVersion != nullptr && installVersion->count() > 0) {
            cmd.version = QString::fromStdString(installVersionValue);
        }
        cmd.noDeps = installNoDeps;
        cmd.noMetadata = installNoMetadata;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("remove")) {
        Cmd::ModRemove cmd;
        cmd.targets = toQStringList(removeTargets);
        cmd.instance = QString::fromStdString(removeInstance);
        cmd.force = removeForce;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("enable")) {
        Cmd::ModEnable cmd;
        cmd.targets = toQStringList(enableTargets);
        cmd.instance = QString::fromStdString(enableInstance);
        cmd.recursive = enableRecursive;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("disable")) {
        Cmd::ModDisable cmd;
        cmd.targets = toQStringList(disableTargets);
        cmd.instance = QString::fromStdString(disableInstance);
        cmd.recursive = disableRecursive;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("update")) {
        Cmd::ModUpdate cmd;
        cmd.targets = toQStringList(updateTargets);
        cmd.all = updateAll;
        cmd.instance = QString::fromStdString(updateInstance);
        cmd.dryRun = updateDryRun;
        cmd.noDeps = updateNoDeps;
        cmd.force = updateForce;
        args.commands.emplace_back(std::move(cmd));
    }
}

void Cli::attach(CLI::App& app, Args& args)
{
    // set app name and description
    app.name(BuildConfig.LAUNCHER_APP_BINARY_NAME.toStdString());
    app.description(BuildConfig.LAUNCHER_SUMMARY.toStdString());

    app.set_version_flag("--version", BuildConfig.printableVersionString().toStdString());

    std::string dPathSource;
    switch (args.dataPath.source) {
        case Startup::DataPathSource::SystemEnvironment: {
            dPathSource = QString(" (From `%1_DATA_DIR`)").arg(BuildConfig.LAUNCHER_NAME.toUpper()).toStdString();
            break;
        }
        case Startup::DataPathSource::PortableData:
        case Startup::DataPathSource::PortableUserData: {
            dPathSource = " (portable install)";
            break;
        }
        case Startup::DataPathSource::PersistentDataPath: {
            dPathSource = " (default data dir)";
            break;
        }
        default: {
            break;
        }
    }
    app.add_option_function<std::filesystem::path>(
           "-d,--dir",
           [&args](const std::filesystem::path& dataDir) {
               args.dataPath.dataPath = dataDir;
               args.dataPath.source = Startup::DataPathSource::Commandline;
           },
           "Use a custom path as application root (use '.' for current directory)")
        ->default_str(args.dataPath.dataPath.string() + dPathSource);

    legacyCli.addLegacyArgs(app, args);

    // subcommand based (headless) mod management
    modCli.addModArgs(app);

    // these legacy options can be deprecated once we have a better subcommand based cli in place
    // CLI::deprecate_option(legacyCli.launch, "replacement");
    // CLI::deprecate_option(legacyCli.server, "replacement");
    // CLI::deprecate_option(legacyCli.world, "replacement");
    // CLI::deprecate_option(legacyCli.offline, "replacement");
    // CLI::deprecate_option(legacyCli.account, "replacement");
    // CLI::deprecate_option(legacyCli.import, "replacement");

    app.add_option_function<std::vector<std::string>>(
           "uri",
           [&args](const std::vector<std::string>& uris) {
               for (const auto& uri : uris) {
                   args.commands.emplace_back(Cmd::ProcessURI{ .uri = uri });
               }
           },
           "a resource to import")
        ->option_text("uri")
        ->expected(0, -1)
        ->take_all()
        ->check(CLI::ExistingPath | URLValidator{});

    // app.validate_optional_arguments();
}

void parseArgs(int argc, char** argv, Args& args)
{
    CLI::App app{};

    /// ensured that argv is utf8
    /// (only does something on windows, returns argv unchanged everywhere else)
    char** const utf8Argv = app.ensure_utf8(argv);

    Cli cli{};
    cli.attach(app, args);

    // try parse or exit
    try {
        app.parse(argc, utf8Argv);
    } catch (const CLI::ParseError& e) {
        // CLI::App::exit prints to stderr with help or parsing errors before
        // returning an exit code
        std::exit(app.exit(e));
    };

    // turn the parsed `mod` subcommand (if any) into a `Cmd::Mod*` command
    cli.modCli.flush(args);
}

}  // namespace Cli
