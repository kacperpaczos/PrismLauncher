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

#include "minecraft/ServersDat.h"

#include <FileSystem.h>
#include <PSaveFile.h>

#include <QFile>
#include <QFileInfo>

#include <tag_compound.h>
#include <tag_list.h>
#include <tag_primitive.h>
#include <tag_string.h>

#include <io/stream_reader.h>
#include <io/stream_writer.h>

#include <sstream>

Server::Server()
{
    m_name = QObject::tr("Minecraft Server");
}

Server::Server(const QString& name, const QString& address)
{
    m_name = name;
    m_address = address;
}

Server::Server(nbt::tag_compound& server)
{
    std::string addressStr(server["ip"]);
    m_address = QString::fromUtf8(addressStr.c_str());

    std::string nameStr(server["name"]);
    m_name = QString::fromUtf8(nameStr.c_str());

    if (server["icon"]) {
        std::string base64str(server["icon"]);
        m_icon = QByteArray::fromBase64(base64str.c_str());
    }

    if (server.has_key("acceptTextures", nbt::tag_type::Byte)) {
        bool value = server["acceptTextures"].as<nbt::tag_byte>().get();
        if (value) {
            m_acceptsTextures = AcceptsTextures::ALWAYS;
        } else {
            m_acceptsTextures = AcceptsTextures::NEVER;
        }
    }
}

void Server::serialize(nbt::tag_compound& server) const
{
    server.insert("name", m_name.trimmed().toUtf8().toStdString());
    server.insert("ip", m_address.trimmed().toUtf8().toStdString());
    if (m_icon.size()) {
        server.insert("icon", m_icon.toBase64().toStdString());
    }
    if (m_acceptsTextures != AcceptsTextures::ASK) {
        server.insert("acceptTextures", nbt::tag_byte(m_acceptsTextures == AcceptsTextures::ALWAYS));
    }
}

ServersDat::ServersDat(const QString& path) : m_path(path) {}

ServersDat::~ServersDat() = default;

ServersDat::ServersDat(ServersDat&&) noexcept = default;

ServersDat& ServersDat::operator=(ServersDat&&) noexcept = default;

QString ServersDat::pathForGameRoot(const QString& gameRoot)
{
    return FS::PathCombine(gameRoot, "servers.dat");
}

ServersDat::State ServersDat::load()
{
    m_servers.clear();

    if (!QFile::exists(m_path)) {
        // The file is created when the servers are saved for the first time.
        m_state = State::Missing;
        return m_state;
    }

    std::unique_ptr<nbt::tag_compound> root;
    try {
        auto res = FS::read(m_path);
        if (!res) {
            qWarning() << "Could not read the server list:" << m_path << res.error();
            m_state = State::Corrupted;
            return m_state;
        }
        const auto& input = res.value();
        std::istringstream stream(std::string(input.constData(), input.size()));
        auto pair = nbt::io::read_compound(stream);

        if (pair.first != "" || pair.second == nullptr) {
            qWarning() << "The server list is not valid NBT:" << m_path;
            m_state = State::Corrupted;
            return m_state;
        }

        root = std::move(pair.second);
    } catch (const std::exception& e) {
        qWarning() << "The server list could not be parsed:" << m_path << e.what();
        m_state = State::Corrupted;
        return m_state;
    }

    // The tags that the CLI does not know are kept - they are written back
    // unchanged, so that an edit of the list does not lose them.
    m_unknownTags = std::make_unique<nbt::tag_compound>();
    for (const auto& [key, value] : *root) {
        if (key == "servers") {
            continue;
        }
        m_unknownTags->insert(key, nbt::value(value));
    }

    try {
        auto& serversList = root->at("servers").as<nbt::tag_list>();
        for (auto& iter : serversList) {
            auto& serverTag = iter.as<nbt::tag_compound>();
            m_servers.append(Server(serverTag));
        }
    } catch (const std::bad_cast&) {
        qWarning() << "The server list has no server list in it:" << m_path;
        m_state = State::Corrupted;
        return m_state;
    } catch (const std::out_of_range&) {
        qWarning() << "The server list has no servers in it:" << m_path;
        m_state = State::Corrupted;
        return m_state;
    }

    m_state = State::Loaded;
    return m_state;
}

bool ServersDat::save()
{
    if (m_state == State::Corrupted) {
        // A corrupted file is never overwritten - the user has to deal with it.
        qWarning() << "Refusing to overwrite the corrupted server list:" << m_path;
        return false;
    }

    nbt::tag_compound out;
    if (m_unknownTags) {
        // The tags that the CLI does not know are written back unchanged.
        for (const auto& [key, value] : *m_unknownTags) {
            out.insert(key, nbt::value(value));
        }
    }
    nbt::tag_list list;
    for (const auto& server : m_servers) {
        nbt::tag_compound serverNbt;
        server.serialize(serverNbt);
        list.push_back(std::move(serverNbt));
    }
    out.insert("servers", nbt::value(std::move(list)));

    std::ostringstream stream;
    try {
        nbt::io::write_tag("", out, stream);
    } catch (const std::exception& e) {
        qWarning() << "The server list could not be serialized:" << m_path << e.what();
        return false;
    }
    const QByteArray data(stream.str().data(), static_cast<int>(stream.str().size()));

    if (!FS::ensureFilePathExists(m_path)) {
        qWarning() << "Could not create the directory of the server list:" << m_path;
        return false;
    }

    // Keep the previous contents of the file around, so that a broken write
    // does not lose the servers.
    if (QFile::exists(m_path)) {
        QFile::remove(m_path + ".bak");
        QFile::copy(m_path, m_path + ".bak");
    }

    // The file is written atomically - a failed write leaves the old file
    // in place.
    PSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not open the server list for writing:" << m_path << file.errorString();
        return false;
    }
    if (file.write(data) != data.size()) {
        qWarning() << "Could not write the server list:" << m_path << file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        qWarning() << "Could not save the server list:" << m_path << file.errorString();
        return false;
    }

    m_state = State::Loaded;
    return true;
}

void ServersDat::add(const Server& server, int index)
{
    if (index < 0 || index > m_servers.size()) {
        m_servers.append(server);
        return;
    }
    m_servers.insert(index, server);
}

bool ServersDat::removeAt(int index)
{
    if (index < 0 || index >= m_servers.size()) {
        return false;
    }
    m_servers.removeAt(index);
    return true;
}
