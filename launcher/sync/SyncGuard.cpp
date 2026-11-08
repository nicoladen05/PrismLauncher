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

#include "SyncGuard.h"

#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

namespace SyncGuard {

std::optional<InstanceState> findRunningConflict(const QString& instanceId,
                                                 const QString& syncProfile,
                                                 const QList<InstanceState>& instances)
{
    if (syncProfile.isEmpty()) {
        return std::nullopt;
    }
    for (const auto& other : instances) {
        if (other.id != instanceId && other.running && other.syncProfile == syncProfile) {
            return other;
        }
    }
    return std::nullopt;
}

QList<InstanceState> collectStates(const InstanceList& list)
{
    QList<InstanceState> states;
    for (int i = 0; i < list.count(); i++) {
        auto* inst = list.at(i);
        states.append({ inst->id(), inst->name(), inst->settings()->get(SETTING_KEY).toString(), inst->isRunning() });
    }
    return states;
}

}  // namespace SyncGuard
