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

#include "componentSearchMatcher.hpp"

#include <algorithm>
#include <ranges>

#include <QStringList>


namespace SILICON {
namespace ui {

namespace {

int editDistance(QString lhs, QString rhs)
{
  lhs = lhs.toCaseFolded();
  rhs = rhs.toCaseFolded();

  std::vector<int> previous(rhs.size() + 1);
  std::vector<int> current(rhs.size() + 1);

  for (qsizetype i = 0; i <= rhs.size(); ++i) {
    previous[i] = static_cast<int>(i);
  }

  for (qsizetype i = 1; i <= lhs.size(); ++i) {
    current[0] = static_cast<int>(i);

    for (qsizetype j = 1; j <= rhs.size(); ++j) {
      const int substitutionCost = lhs[i - 1] == rhs[j - 1] ? 0 : 1;
      current[j]                 = std::min(
          {previous[j] + 1, current[j - 1] + 1, previous[j - 1] + substitutionCost});
    }

    std::swap(previous, current);
  }

  return previous[rhs.size()];
}

bool isFuzzyMatch(const QString& query, const QString& text, const int distance)
{
  if (query.isEmpty())
    return true;

  const int fieldSize   = static_cast<int>(std::min(query.size(), text.size()));
  const int maxDistance = std::max(1, fieldSize / 3);
  return distance <= maxDistance;
}

}  // namespace

namespace componentSearchMatcher {

std::vector<Match> rank(QStringList candidates, const QString& query,
                        const bool includeNonMatches)
{
  const QString normalizedQuery = query.trimmed();

  std::vector<Match> ranked;
  ranked.reserve(static_cast<size_t>(candidates.size()));

  for (int index = 0; index < candidates.size(); ++index) {
    const QString& text     = candidates[index];
    int            rank     = 3;
    int            distance = 0;

    if (!normalizedQuery.isEmpty()) {
      if (text.compare(normalizedQuery, Qt::CaseInsensitive) == 0)
        rank = 0;
      else if (text.startsWith(normalizedQuery, Qt::CaseInsensitive))
        rank = 1;
      else if (text.contains(normalizedQuery, Qt::CaseInsensitive))
        rank = 2;

      distance = editDistance(normalizedQuery, text);
    }

    const bool matches = rank < 3 || isFuzzyMatch(normalizedQuery, text, distance);
    if (includeNonMatches || matches)
      ranked.push_back({index, rank, distance, matches});
  }

  std::ranges::sort(ranked, [&](const Match& lhs, const Match& rhs) {
    if (lhs.rank != rhs.rank)
      return lhs.rank < rhs.rank;

    if (lhs.distance != rhs.distance)
      return lhs.distance < rhs.distance;

    if (candidates[lhs.index].size() != candidates[rhs.index].size())
      return candidates[lhs.index].size() < candidates[rhs.index].size();

    return QString::compare(candidates[lhs.index], candidates[rhs.index],
                            Qt::CaseInsensitive)
           < 0;
  });

  return ranked;
}

}  // namespace componentSearchMatcher

}  // namespace ui
}  // namespace SILICON
