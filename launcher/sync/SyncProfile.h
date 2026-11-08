// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
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

#include <QList>
#include <QString>

/**
 * @brief One path shared through a sync profile, relative to the instance's game root
 * (for example "saves/My World", "config" or "options.txt").
 */
struct SyncItem {
    enum class Kind { Directory, File };

    QString path;
    Kind kind = Kind::Directory;
    /// Never link this item, always copy it in before launch and back out after the game exits.
    bool copyOnly = false;

    bool operator==(const SyncItem& other) const = default;
};

/**
 * @brief A named folder of shared content that any number of instances can join.
 *
 * On disk a profile is a folder holding `profile.json` and a `content/` folder that
 * mirrors the layout of an instance's game root.
 */
class SyncProfile {
   public:
    SyncProfile() = default;
    SyncProfile(QString id, QString rootPath);

    /// Reads `<rootPath>/profile.json`. Returns false (and sets error) when it is missing or invalid.
    static bool load(const QString& rootPath, SyncProfile& out, QString* error = nullptr);
    /// Writes `<rootPath>/profile.json` and creates the content folder.
    bool save(QString* error = nullptr) const;

    QString id() const { return m_id; }
    QString name() const { return m_name.isEmpty() ? m_id : m_name; }
    void setName(const QString& name) { m_name = name; }

    QString rootPath() const { return m_rootPath; }
    QString contentPath() const;
    QString contentPath(const QString& relativePath) const;

    const QList<SyncItem>& items() const { return m_items; }
    void setItems(const QList<SyncItem>& items) { m_items = items; }
    /// Adds an item, or replaces the one with the same path. Returns false for invalid paths.
    bool addItem(const SyncItem& item);
    bool removeItem(const QString& path);

    /// Cleans a user supplied relative path. Returns an empty string when the path is absolute or escapes the game root.
    static QString normalizeItemPath(const QString& path);

   private:
    QString m_id;
    QString m_name;
    QString m_rootPath;
    QList<SyncItem> m_items;
};
