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

#include <optional>

#include "SyncLink.h"
#include "SyncProfile.h"

/**
 * @brief Connects one instance's game root to a sync profile.
 *
 * `attach()` runs before launch: it links every item into the instance (or, for items that cannot be
 * linked, copies newer files in from the profile). `finish()` runs after the game exits and copies
 * changed copy-mode files back. Linked items need nothing after exit.
 *
 * The first instance to share an item seeds the profile with its own copy. When an instance joins a
 * profile that already has the item, the instance's own copy is moved aside to
 * `<item>.sync-backup-<timestamp>` before linking, so nothing is ever deleted.
 */
class SyncEngine {
   public:
    struct ItemResult {
        enum class Status {
            Linked,         ///< link created or already in place
            Copied,         ///< copy-mode item brought up to date
            NothingToSync,  ///< item exists on neither side yet
            Failed,
        };

        QString path;
        SyncLink::Method method = SyncLink::Method::Copy;
        Status status = Status::Failed;
        QString backupPath;  ///< set when the instance's own copy was moved aside
        QString error;
    };

    SyncEngine(SyncProfile profile, QString gameRoot);

    /// Overrides the file symlink probe, mainly for tests.
    void setFileSymlinksAllowed(std::optional<bool> allowed) { m_fileSymlinksAllowed = allowed; }

    /// Before launch: link or copy every item into the instance.
    QList<ItemResult> attach();
    /// After exit: copy changed copy-mode items back to the profile.
    QList<ItemResult> finish();
    /// Leaves the profile for one item: replaces the link with a standalone copy of the shared content.
    bool detach(const SyncItem& item, QString* error = nullptr);

    SyncLink::Method methodFor(const SyncItem& item) const;

    static bool allSucceeded(const QList<ItemResult>& results);

   private:
    ItemResult attachItem(const SyncItem& item);
    bool isDirectory(const SyncItem& item) const;
    bool fileSymlinksAllowed() const;
    void allowSymlinkedWorlds();

    SyncProfile m_profile;
    QString m_gameRoot;
    std::optional<bool> m_fileSymlinksAllowed;
};

namespace SyncFiles {
/// Copies `src` to `dst` (file or folder, recursively), only where `src` is newer or `dst` is missing.
/// Returns false on the first failure.
bool copyNewer(const QString& src, const QString& dst, QString* error = nullptr);
/// Moves `src` to `dst`, falling back to copy and delete across file systems.
bool movePath(const QString& src, const QString& dst, QString* error = nullptr);
/// Returns `<path>.sync-backup-<timestamp>`, made unique if needed.
QString backupPathFor(const QString& path);
}  // namespace SyncFiles
