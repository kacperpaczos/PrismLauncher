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

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

namespace nbt {
class tag_compound;
}

/**
 * @brief A single server entry of the `servers.dat` file of an instance
 *
 * The struct mirrors the NBT layout of the file: a compound with the keys
 * `name`, `ip`, an optional `icon` (base64 encoded PNG) and an optional
 * `acceptTextures` byte.
 */
struct Server {
    /// Does the server accept the resource packs of the client?
    enum class AcceptsTextures : int { ASK = 0, ALWAYS = 1, NEVER = 2 };

    Server();
    Server(const QString& name, const QString& address);
    /// Read the server from its NBT compound.
    explicit Server(nbt::tag_compound& server);
    /// Write the server into its NBT compound.
    void serialize(nbt::tag_compound& server) const;

    // Data - persistent and user changeable
    QString m_name;
    QString m_address;
    AcceptsTextures m_acceptsTextures = AcceptsTextures::ASK;

    // Data - persistent and automatically updated
    QByteArray m_icon;

    // Data - temporary
    std::optional<int> m_currentPlayers;  // nullopt if not calculated/calculating
};

/**
 * @brief The `servers.dat` file of an instance
 *
 * The file is a plain (not gzipped) NBT compound with a single `servers`
 * list of server compounds. It is shared by the servers page of the GUI and
 * by the headless `server` commands of the CLI.
 *
 * A file that exists but cannot be parsed is never overwritten - the state
 * of the object is then `Corrupted` and `save()` refuses to write.
 */
class ServersDat {
   public:
    /// The result of reading a `servers.dat` file.
    enum class State {
        /// The file was read successfully.
        Loaded,
        /// The file does not exist - an empty list is used.
        Missing,
        /// The file exists but cannot be parsed - it is never overwritten.
        Corrupted
    };

    explicit ServersDat(const QString& path);

    /// The path of the file.
    const QString& path() const { return m_path; }

    /// The path of the `servers.dat` file of the instance with the given game root.
    static QString pathForGameRoot(const QString& gameRoot);

    /**
     * @brief Read the file
     *
     * The list of servers is replaced with the contents of the file. A file
     * that does not exist results in an empty list.
     */
    State load();

    /// The state of the last `load()` call.
    State state() const { return m_state; }

    /// true when the file was read successfully or does not exist.
    bool isLoaded() const { return m_state != State::Corrupted; }

    /// Write the servers back to the file. Refuses to write a corrupted file.
    bool save();

    const QList<Server>& servers() const { return m_servers; }
    QList<Server>& servers() { return m_servers; }

    /// Insert a server. An index outside of the list appends the server.
    void add(const Server& server, int index = -1);
    /// Remove the server at the given index.
    bool removeAt(int index);

   private:
    QString m_path;
    QList<Server> m_servers;
    State m_state = State::Missing;
};
