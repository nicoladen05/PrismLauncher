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

class InstanceList;

/**
 * @brief Keeps instances that share a sync profile from running at the same time.
 *
 * Linked worlds and configs are the same files on disk for every instance in a profile, so two of them
 * running at once could corrupt a world or overwrite each other's settings.
 */
namespace SyncGuard {

/// Instance setting that holds the id of the sync profile an instance belongs to (empty = none).
inline constexpr auto SETTING_KEY = "SyncProfile";

struct InstanceState {
    QString id;
    QString name;
    QString syncProfile;
    bool running = false;
};

/// Returns the first other running instance in the same sync profile, if any.
std::optional<InstanceState> findRunningConflict(const QString& instanceId,
                                                 const QString& syncProfile,
                                                 const QList<InstanceState>& instances);

/// Reads the sync state of every instance in `list`.
QList<InstanceState> collectStates(const InstanceList& list);

}  // namespace SyncGuard
