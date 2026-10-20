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
#include "cli/Output.h"
#include "startup/Startup.h"

#include <QUrl>

namespace Cli {
namespace {

/// The values of a repeated option as a list of strings.
std::vector<QString> toQStringList(const std::vector<std::string>& in)
{
    std::vector<QString> out;
    out.reserve(in.size());
    for (const auto& value : in) {
        out.emplace_back(QString::fromStdString(value));
    }
    return out;
}

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

struct KeyValueValidator : CLI::Validator {
    KeyValueValidator()
    {
        name_ = "key=value";
        func_ = [](const std::string& value) -> std::string {
            if (value.find('=') == std::string::npos) {
                return "the value has to be in the `key=value` form";
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
    // Let global options (e.g. `-d/--dir`) appear after the `mod` subcommand
    // (and its subcommands keep their own fallthrough below).
    mod->fallthrough();

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
        list->fallthrough();
    }

    {
        // mod search [query] [--mc <version>...] [--loader <loader>...] [--limit N] [--offset N] [--json]
        auto* search = mod->add_subcommand("search", "Search for mods on Modrinth.");
        search->add_option("query", searchQuery, "Search query.")->option_text("query");
        // Exactly one value per occurrence, so a trailing positional query is not
        // swallowed: `mod search --mc 1.20.1 jei` => query=jei, mc=[1.20.1].
        search->add_option("--mc", searchMcVersions, "Filter by Minecraft version (repeat the flag for multiple versions).")
            ->option_text("version")
            ->expected(1, -1)
            ->allow_extra_args(false);
        search->add_option("--loader", searchLoaders, "Filter by mod loader (repeat the flag for multiple loaders).")
            ->option_text("loader")
            ->expected(1, -1)
            ->allow_extra_args(false);
        search->add_option("--limit", searchLimit, "Maximum number of results (0-100).")
            ->option_text("N")
            ->check(CLI::Range(0, 100));
        search->add_option("--offset", searchOffset, "Result offset, for pagination (0-1000).")
            ->option_text("N")
            ->check(CLI::Range(0, 1000));
        addJsonFlag(search);
        search->fallthrough();
    }

    {
        // mod info <project> [-i <instance>] [--json]
        auto* info = mod->add_subcommand("info", "Show information about a mod project.");
        info->add_option("project", infoProject, "Mod slug or project ID.")->option_text("project")->required();
        infoInstance = addInstanceOption(info, infoInstanceValue, /*required=*/false);
        addJsonFlag(info);
        info->fallthrough();
    }

    {
        // mod install <project> -i <instance> [--version <v>] [--no-deps] [--no-metadata] [-y] [--json]
        auto* install = mod->add_subcommand("install", "Install a mod into an instance.");
        install->add_option("project", installProject, "Mod slug or project ID.")->option_text("project")->required();
        addInstanceOption(install, installInstance, /*required=*/true);
        installVersion = install->add_option("--version", installVersionValue, "Install a specific version.")->option_text("version");
        install->add_flag("--no-deps", installNoDeps, "Do not install dependencies.");
        install->add_flag("--no-metadata", installNoMetadata, "Do not write packwiz metadata for the mod.");
        install->add_flag("-y,--yes", installAssumeYes, "Do not ask for confirmation.");
        addJsonFlag(install);
        install->fallthrough();
    }

    {
        // mod remove <target>... -i <instance> [--force] [-y] [--json]
        auto* remove = mod->add_subcommand("remove", "Remove one or more mods from an instance.");
        remove->add_option("targets", removeTargets, "Mod filename, slug, project ID or name.")
            ->option_text("target")
            ->expected(1, -1)
            ->required();
        addInstanceOption(remove, removeInstance, /*required=*/true);
        remove->add_flag("--force", removeForce, "Remove even if the mod is locked or ambiguous.");
        remove->add_flag("-y,--yes", removeAssumeYes, "Do not ask for confirmation.");
        addJsonFlag(remove);
        remove->fallthrough();
    }

    {
        // mod enable <target>... -i <instance> [--recursive] [-y] [--json]
        auto* enable = mod->add_subcommand("enable", "Enable one or more mods in an instance.");
        enable->add_option("targets", enableTargets, "Mod filename, slug, project ID or name.")
            ->option_text("target")
            ->expected(1, -1)
            ->required();
        addInstanceOption(enable, enableInstance, /*required=*/true);
        enable->add_flag("--recursive", enableRecursive, "Also enable dependencies.");
        enable->add_flag("-y,--yes", enableAssumeYes, "Do not ask for confirmation.");
        addJsonFlag(enable);
        enable->fallthrough();
    }

    {
        // mod disable <target>... -i <instance> [--recursive] [-y] [--json]
        auto* disable = mod->add_subcommand("disable", "Disable one or more mods in an instance.");
        disable->add_option("targets", disableTargets, "Mod filename, slug, project ID or name.")
            ->option_text("target")
            ->expected(1, -1)
            ->required();
        addInstanceOption(disable, disableInstance, /*required=*/true);
        disable->add_flag("--recursive", disableRecursive, "Also disable dependants.");
        disable->add_flag("-y,--yes", disableAssumeYes, "Do not ask for confirmation.");
        addJsonFlag(disable);
        disable->fallthrough();
    }

    {
        // mod update [<target>...] --all -i <instance> [--dry-run] [--no-deps] [--force] [-y] [--json]
        auto* update = mod->add_subcommand("update", "Update one or more mods in an instance.");
        auto* updateTargetsOpt = update->add_option("targets", updateTargets, "Mod filename, slug, project ID or name.")
                                     ->option_text("target")
                                     ->expected(1, -1);
        auto* updateAllOpt = update->add_flag("--all", updateAll, "Update every mod in the instance.");
        addInstanceOption(update, updateInstance, /*required=*/true);
        update->add_flag("--dry-run", updateDryRun, "Only report what would be updated.");
        update->add_flag("--no-deps", updateNoDeps, "Do not update dependencies.");
        update->add_flag("--force", updateForce, "Update mods that are locked to a version.");
        update->add_flag("-y,--yes", updateAssumeYes, "Do not ask for confirmation.");
        addJsonFlag(update);
        update->fallthrough();

        // `--all` and explicit targets are mutually exclusive, and at least one
        // of them has to be given.
        updateAllOpt->excludes(updateTargetsOpt);
        updateTargetsOpt->excludes(updateAllOpt);
        update->parse_complete_callback([&] {
            if (!updateAll && updateTargets.empty()) {
                throw CLI::ValidationError("mod update", "either `--all` or at least one target is required");
            }
        });
    }

    // `mod` has no positional of its own, so an unmatched positional would
    // currently fall through to the top-level `uri` import option and be
    // silently accepted. Mixing a mod command with a resource import is not
    // supported, so reject it explicitly.
    mod->parse_complete_callback([&app] {
        auto* uri = app.get_option_no_throw("uri");
        if (uri != nullptr && uri->count() > 0) {
            throw CLI::ValidationError("mod", "unexpected positional argument for a mod subcommand");
        }
    });
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
        cmd.assumeYes = installAssumeYes;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("remove")) {
        Cmd::ModRemove cmd;
        cmd.targets = toQStringList(removeTargets);
        cmd.instance = QString::fromStdString(removeInstance);
        cmd.force = removeForce;
        cmd.assumeYes = removeAssumeYes;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("enable")) {
        Cmd::ModEnable cmd;
        cmd.targets = toQStringList(enableTargets);
        cmd.instance = QString::fromStdString(enableInstance);
        cmd.recursive = enableRecursive;
        cmd.assumeYes = enableAssumeYes;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("disable")) {
        Cmd::ModDisable cmd;
        cmd.targets = toQStringList(disableTargets);
        cmd.instance = QString::fromStdString(disableInstance);
        cmd.recursive = disableRecursive;
        cmd.assumeYes = disableAssumeYes;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("update")) {
        Cmd::ModUpdate cmd;
        cmd.targets = toQStringList(updateTargets);
        cmd.all = updateAll;
        cmd.instance = QString::fromStdString(updateInstance);
        cmd.dryRun = updateDryRun;
        cmd.noDeps = updateNoDeps;
        cmd.force = updateForce;
        cmd.assumeYes = updateAssumeYes;
        args.commands.emplace_back(std::move(cmd));
    }
}



void InstanceCli::addInstanceArgs(CLI::App& app)
{
    instance = app.add_subcommand("instance", "Manage the instances of the launcher.");
    instance->require_subcommand();
    instance->fallthrough();

    auto addJsonFlag = [](CLI::App* sub) { sub->add_flag("--json", "Emit machine-readable JSON output (default)."); };

    {
        // instance list [--group <g>]
        auto* list = instance->add_subcommand("list", "List the instances of the launcher.");
        listGroup = list->add_option("--group", listGroupValue, "Only list the instances of this group.")->option_text("group");
        addJsonFlag(list);
        list->fallthrough();
    }

    {
        // instance info <id>
        auto* info = instance->add_subcommand("info", "Show the details of an instance.");
        info->add_option("id", infoId, "Instance ID.")->option_text("id")->required();
        addJsonFlag(info);
        info->fallthrough();
    }

    {
        // instance create --name <n> --mc <ver> [...] | --from <url|file>
        auto* create = instance->add_subcommand("create", "Create a new instance.");
        createName = create->add_option("--name", createNameValue, "The name of the new instance.")->option_text("name");
        createMc = create->add_option("--mc", createMcValue, "The Minecraft version of the new instance.")->option_text("version");
        createLoader = create->add_option("--loader", createLoaderValue, "The mod loader of the new instance.")
                          ->option_text("loader")
                          ->check(CLI::IsMember(std::vector<std::string>{ "fabric", "forge", "neoforge", "quilt", "none" }));
        createLoaderVersion = create->add_option("--loader-version", createLoaderVersionValue, "The version of the mod loader.")
                                  ->option_text("version");
        createGroup = create->add_option("--group", createGroupValue, "The group of the new instance.")->option_text("group");
        createIcon = create->add_option("--icon", createIconValue, "The icon of the new instance (an icon key or an image file).")
                         ->option_text("icon");
        auto* download = create->add_flag("--download", createDownload, "Download the files of the new instance (the default).");
        auto* noDownload = create->add_flag("--no-download", createNoDownload, "Do not download the files of the new instance.");
        createFrom = create->add_option("--from", createFromValue, "Import an instance from a URL or a file instead of creating a vanilla one.")
                        ->option_text("url|file");
        addJsonFlag(create);
        create->fallthrough();

        // `--download` and `--no-download` contradict each other.
        download->excludes(noDownload);

        // A vanilla instance needs a name and a Minecraft version, an imported
        // one takes them from the pack.
        create->parse_complete_callback([this] {
            if (createFrom != nullptr && createFrom->count() > 0) {
                return;
            }
            if (createName == nullptr || createName->count() == 0) {
                throw CLI::ValidationError("instance create", "either `--from` or `--name` is required");
            }
            if (createMc == nullptr || createMc->count() == 0) {
                throw CLI::ValidationError("instance create", "either `--from` or `--mc` is required");
            }
        });
    }

    {
        // instance duplicate <id> [--name <n>] [--copy|--links|--hardlinks|--clone] [--no-saves] ...
        auto* duplicate = instance->add_subcommand("duplicate", "Duplicate an instance.");
        duplicate->add_option("id", duplicateId, "Instance ID.")->option_text("id")->required();
        duplicateName = duplicate->add_option("--name", duplicateNameValue, "The name of the copy.")->option_text("name");
        auto* copy = duplicate->add_flag("--copy", duplicateCopy, "Copy the files of the instance.");
        auto* links = duplicate->add_flag("--links", duplicateLinks, "Copy the files of the instance as symbolic links.");
        auto* hardlinks = duplicate->add_flag("--hardlinks", duplicateHardlinks, "Copy the files of the instance as hard links.");
        auto* clone = duplicate->add_flag("--clone", duplicateClone, "Copy the files of the instance with a copy-on-write clone.");
        duplicate->add_flag("--no-saves", duplicateNoSaves, "Do not copy the saves of the instance.");
        duplicate->add_flag("--no-mods", duplicateNoMods, "Do not copy the mods of the instance.");
        duplicate->add_flag("--no-resource-packs", duplicateNoResourcePacks, "Do not copy the resource packs of the instance.");
        duplicate->add_flag("--no-shader-packs", duplicateNoShaderPacks, "Do not copy the shader packs of the instance.");
        duplicate->add_flag("--no-options", duplicateNoOptions, "Do not copy the options of the instance.");
        duplicate->add_flag("--no-configs", duplicateNoConfigs, "Do not copy the configs of the instance.");
        addJsonFlag(duplicate);
        duplicate->fallthrough();

        // Only one copy mode can be used at a time.
        for (auto* option : { copy, links, hardlinks, clone }) {
            for (auto* other : { copy, links, hardlinks, clone }) {
                if (option != other) {
                    option->excludes(other);
                }
            }
        }
    }

    {
        // instance rename <id> <new-name> [--keep-folder|--rename-folder]
        auto* rename = instance->add_subcommand("rename", "Rename an instance.");
        rename->add_option("id", renameId, "Instance ID.")->option_text("id")->required();
        rename->add_option("new-name", renameNewName, "The new name of the instance.")->option_text("name")->required();
        auto* keepFolder = rename->add_flag("--keep-folder", renameKeepFolder, "Keep the folder of the instance as it is.");
        auto* renameFolder = rename->add_flag("--rename-folder", renameRenameFolder, "Rename the folder of the instance too (the default).");
        addJsonFlag(rename);
        rename->fallthrough();

        keepFolder->excludes(renameFolder);
        renameFolder->excludes(keepFolder);
    }

    {
        // instance delete <id> [--purge]
        auto* remove = instance->add_subcommand("delete", "Delete an instance.");
        remove->add_option("id", deleteId, "Instance ID.")->option_text("id")->required();
        remove->add_flag("--purge", deletePurge, "Delete the files of the instance instead of moving them to the trash.");
        addJsonFlag(remove);
        remove->fallthrough();
    }

    {
        // instance icon set <id> <key|file>
        auto* icon = instance->add_subcommand("icon", "Manage the icon of an instance.");
        icon->require_subcommand();
        icon->fallthrough();
        auto* set = icon->add_subcommand("set", "Set the icon of an instance.");
        set->add_option("id", iconSetId, "Instance ID.")->option_text("id")->required();
        set->add_option("icon", iconSetIcon, "The icon key or the path of an image file.")->option_text("icon")->required();
        addJsonFlag(set);
        set->fallthrough();
    }
}

void InstanceCli::flush(Args& args) const
{
    if (instance == nullptr) {
        return;
    }

    auto subParsed = [this](const char* name) -> bool {
        auto* sub = instance->get_subcommand_no_throw(name);
        return sub != nullptr && sub->parsed();
    };

    if (subParsed("list")) {
        Cmd::InstanceList cmd;
        if (listGroup != nullptr && listGroup->count() > 0) {
            cmd.group = QString::fromStdString(listGroupValue);
        }
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("info")) {
        Cmd::InstanceInfo cmd;
        cmd.id = QString::fromStdString(infoId);
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("create")) {
        Cmd::InstanceCreate cmd;
        if (createName != nullptr && createName->count() > 0) {
            cmd.name = QString::fromStdString(createNameValue);
        }
        if (createMc != nullptr && createMc->count() > 0) {
            cmd.mcVersion = QString::fromStdString(createMcValue);
        }
        if (createLoader != nullptr && createLoader->count() > 0) {
            cmd.loader = QString::fromStdString(createLoaderValue);
        }
        if (createLoaderVersion != nullptr && createLoaderVersion->count() > 0) {
            cmd.loaderVersion = QString::fromStdString(createLoaderVersionValue);
        }
        if (createGroup != nullptr && createGroup->count() > 0) {
            cmd.group = QString::fromStdString(createGroupValue);
        }
        if (createIcon != nullptr && createIcon->count() > 0) {
            cmd.icon = QString::fromStdString(createIconValue);
        }
        if (createFrom != nullptr && createFrom->count() > 0) {
            cmd.from = QString::fromStdString(createFromValue);
        }
        // The files are downloaded unless `--no-download` is used.
        cmd.download = !createNoDownload;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("duplicate")) {
        Cmd::InstanceDuplicate cmd;
        cmd.id = QString::fromStdString(duplicateId);
        if (duplicateName != nullptr && duplicateName->count() > 0) {
            cmd.name = QString::fromStdString(duplicateNameValue);
        }
        if (duplicateLinks) {
            cmd.copyMode = QStringLiteral("links");
        } else if (duplicateHardlinks) {
            cmd.copyMode = QStringLiteral("hardlinks");
        } else if (duplicateClone) {
            cmd.copyMode = QStringLiteral("clone");
        } else {
            cmd.copyMode = QStringLiteral("copy");
        }
        cmd.noSaves = duplicateNoSaves;
        cmd.noMods = duplicateNoMods;
        cmd.noResourcePacks = duplicateNoResourcePacks;
        cmd.noShaderPacks = duplicateNoShaderPacks;
        cmd.noOptions = duplicateNoOptions;
        cmd.noConfigs = duplicateNoConfigs;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("rename")) {
        Cmd::InstanceRename cmd;
        cmd.id = QString::fromStdString(renameId);
        cmd.newName = QString::fromStdString(renameNewName);
        // The folder of the instance is renamed unless `--keep-folder` is used.
        cmd.renameFolder = !renameKeepFolder;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("delete")) {
        Cmd::InstanceDelete cmd;
        cmd.id = QString::fromStdString(deleteId);
        cmd.purge = deletePurge;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("icon")) {
        auto* icon = instance->get_subcommand_no_throw("icon");
        auto* set = icon != nullptr ? icon->get_subcommand_no_throw("set") : nullptr;
        if (set != nullptr && set->parsed()) {
            Cmd::InstanceIconSet cmd;
            cmd.id = QString::fromStdString(iconSetId);
            cmd.icon = QString::fromStdString(iconSetIcon);
            args.commands.emplace_back(std::move(cmd));
        }
    }
}

void SettingsCli::addSettingsArgs(CLI::App& app)
{
    settings = app.add_subcommand("settings", "Manage the settings of an instance.");
    settings->require_subcommand();
    settings->fallthrough();

    auto addJsonFlag = [](CLI::App* sub) { sub->add_flag("--json", "Emit machine-readable JSON output (default)."); };

    {
        // settings list <id> [--all|--modified]
        auto* list = settings->add_subcommand("list", "List the settings of an instance.");
        list->add_option("id", listId, "Instance ID.")->option_text("id")->required();
        list->add_flag("--all", listAll, "List the settings that are not part of the curated schema too.");
        list->add_flag("--modified", listModified, "List only the settings the instance overrides.");
        addJsonFlag(list);
        list->fallthrough();
    }

    {
        // settings get <id> <key>...
        auto* get = settings->add_subcommand("get", "Show the effective value of the settings of an instance.");
        get->add_option("id", getId, "Instance ID.")->option_text("id")->required();
        get->add_option("keys", getKeys, "The setting keys to show.")
            ->option_text("key")
            ->expected(1, -1)
            ->required();
        addJsonFlag(get);
        get->fallthrough();
    }

    {
        // settings set <id> <key>=<value>...
        auto* set = settings->add_subcommand("set", "Change the settings of an instance.");
        set->add_option("id", setId, "Instance ID.")->option_text("id")->required();
        set->add_option("values", setValues, "The settings to change, as `key=value` pairs.")
            ->option_text("key=value")
            ->expected(1, -1)
            ->check(KeyValueValidator{})
            ->required();
        addJsonFlag(set);
        set->fallthrough();
    }

    {
        // settings unset <id> <key>...
        auto* unset = settings->add_subcommand("unset", "Reset the settings of an instance to the global ones.");
        unset->add_option("id", unsetId, "Instance ID.")->option_text("id")->required();
        unset->add_option("keys", unsetKeys, "The setting keys to reset.")
            ->option_text("key")
            ->expected(1, -1)
            ->required();
        addJsonFlag(unset);
        unset->fallthrough();
    }
}

void SettingsCli::flush(Args& args) const
{
    if (settings == nullptr) {
        return;
    }

    auto subParsed = [this](const char* name) -> bool {
        auto* sub = settings->get_subcommand_no_throw(name);
        return sub != nullptr && sub->parsed();
    };

    if (subParsed("list")) {
        Cmd::SettingsList cmd;
        cmd.id = QString::fromStdString(listId);
        cmd.all = listAll;
        cmd.modified = listModified;
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("get")) {
        Cmd::SettingsGet cmd;
        cmd.id = QString::fromStdString(getId);
        cmd.keys = toQStringList(getKeys);
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("set")) {
        Cmd::SettingsSet cmd;
        cmd.id = QString::fromStdString(setId);
        for (const auto& value : setValues) {
            const auto separator = value.find('=');
            if (separator == std::string::npos) {
                // The parser validates the format, this is only a safety net.
                continue;
            }
            cmd.values.emplace_back(QString::fromStdString(value.substr(0, separator)), QString::fromStdString(value.substr(separator + 1)));
        }
        args.commands.emplace_back(std::move(cmd));
    } else if (subParsed("unset")) {
        Cmd::SettingsUnset cmd;
        cmd.id = QString::fromStdString(unsetId);
        cmd.keys = toQStringList(unsetKeys);
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

    // subcommand based (headless) instance management
    instanceCli.addInstanceArgs(app);

    // subcommand based (headless) instance settings
    settingsCli.addSettingsArgs(app);


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
        // --help / --version (CLI11 success errors) keep CLI11's own output and
        // exit code 0.
        if (e.get_exit_code() == 0) {
            std::exit(app.exit(e));
        }

        // Genuine parse errors go through the JSON envelope contract instead of
        // CLI11's internal codes (104/106/109/...). Keep the message on one line
        // so the envelope stays easy to consume.
        QString message = QString::fromStdString(e.what());
        message.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
        message = message.simplified();
        if (message.isEmpty()) {
            message = QStringLiteral("invalid command line");
        }
        outputError(QStringLiteral("parse_error"), message, ExitCode::Usage);
        std::exit(static_cast<int>(ExitCode::Usage));
    };

    // turn the parsed subcommands (if any) into `Cmd::*` commands
    cli.modCli.flush(args);
    cli.instanceCli.flush(args);
    cli.settingsCli.flush(args);
}

}  // namespace Cli
