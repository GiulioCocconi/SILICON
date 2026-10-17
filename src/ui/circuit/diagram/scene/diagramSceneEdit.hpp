/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <QPointF>
#include <ui/circuit/diagram/wireManager.hpp>

namespace SILICON::ui {

struct ItemPositionChange {
  uint64_t uiId;
  QPointF  before;
  QPointF  after;
};

struct WireRouteChange {
  uint64_t             uiId;
  std::vector<QPointF> before;
  std::vector<QPointF> after;
};

enum class WireDragKind { Bend, Edge, Endpoint };

struct WireEndpointEdit {
  struct NetState {
    uint64_t routeId;
    core::Bus bus;
  };
  struct NetChange {
    uint64_t segmentId;
    NetState before;
    NetState after;
  };
  struct TopologyChange {
    uint64_t segmentId;
    WireManager::SegmentTopology before;
    WireManager::SegmentTopology after;
  };
  using ContactList = std::vector<WireManager::ExplicitContact>;

  uint64_t segmentId = 0;
  ContactList beforeContacts;
  ContactList afterContacts;
  std::vector<TopologyChange> topologyChanges;
  std::vector<NetChange> netChanges;

  /** @brief Returns whether a segment edit records contact, topology, or net changes. */
  [[nodiscard]] bool changesTopology() const
  {
    return segmentId && (!beforeContacts.empty() || !afterContacts.empty()
                         || !topologyChanges.empty() || !netChanges.empty());
  }

  /**
   * @brief Borrows the contact snapshot selected for undo or redo.
   * @param useAfter True selects the after state; false selects the before state.
   */
  [[nodiscard]] const ContactList& contacts(const bool useAfter) const
  {
    return useAfter ? afterContacts : beforeContacts;
  }
};

}  // namespace SILICON::ui
