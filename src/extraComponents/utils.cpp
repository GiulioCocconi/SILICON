/*
 Copyright (c) 2025. Giulio Cocconi
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

#include "utils.hpp"

WireSplitter::WireSplitter(Bus input, const std::vector<Bus>& outputs)
  : Component({input}, outputs, "WireSplitter")
{
  this->setAction([this] {
    const unsigned int N = this->outputs.size();
    for (unsigned int i = 0; i < N; i++) {
      // Get the value of input bit i
      const State s = (this->inputs[0].size() == N)
                          ? Wire::safeGetCurrentState(this->inputs[0][i])
                          : State::ERROR;
      // Set the value of ith output
      if (this->outputs[i].size() != 0)
        Wire::safeSetCurrentState(this->outputs[i][0], s, weak_from_this());
    }
  });
}

WireMerger::WireMerger(const std::vector<Bus>& inputs, Bus output)
  : Component(inputs, {output}, "WireMerger")
{
  this->setAction([this] {
    const unsigned int N = this->inputs.size();
    for (unsigned int i = 0; i < N; i++) {
      // Get the value of ith input
      const State s = (this->inputs[i].size() != 0)
                          ? Wire::safeGetCurrentState(this->inputs[i][0])
                          : State::ERROR;
      // Set the value of output bit i
      Wire::safeSetCurrentState(this->outputs[0][i], s, weak_from_this());
    }
  });
}
