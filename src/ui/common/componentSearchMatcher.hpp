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

#include <vector>

#include <QString>
#include <QStringList>


namespace SILICON {
namespace ui {

namespace componentSearchMatcher {

struct Match {
  int  index;
  int  rank;
  int  distance;
  bool matches;
};

std::vector<Match> rank(QStringList candidates, const QString& query,
                        bool includeNonMatches);

}  // namespace componentSearchMatcher

}  // namespace ui
}  // namespace SILICON
