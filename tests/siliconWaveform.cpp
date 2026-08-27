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

#include "tests.hpp"

#include <core/fstWrapper.hpp>
#include <core/siliconWaveform.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace SILICON::core;
using namespace SILICON::waveform;
using namespace SILICON::waveform::fst;

namespace {

struct FileCleanup {
  std::string filename;
  ~FileCleanup() { std::remove(filename.c_str()); }
};

}  // namespace

TEST(SiliconWaveformTest, AppendsAndReplacesLatestTimestamp)
{
  Trace trace;
  resetTrace(trace, {{"a", 1}, {"bus", 4}}, 1);

  appendSnapshot(trace, 0, {busValueFromBits("0"), busValueFromBits("0011")});
  appendSnapshot(trace, 0, {busValueFromBits("1"), busValueFromBits("0101")});
  appendSnapshot(trace, 4, {busValueFromBits("0"), busValueFromBits("1111")});

  ASSERT_EQ(trace.samples.size(), 2);
  EXPECT_EQ(trace.samples[0].time, 0);
  EXPECT_EQ(trace.samples[0].values,
            (std::vector<BusValue>{busValueFromBits("1"), busValueFromBits("0101")}));
  EXPECT_EQ(trace.samples[1].time, 4);
  EXPECT_EQ(trace.samples[1].values,
            (std::vector<BusValue>{busValueFromBits("0"), busValueFromBits("1111")}));
}

TEST(SiliconWaveformTest, RebuildsAndAppliesEditIntervals)
{
  Trace trace;
  resetTrace(trace, {{"in", 1}, {"bus", 4}, {"out", 1}}, 2);

  rebuildEditableTrace(trace, 10);
  ASSERT_EQ(trace.samples.size(), 2);
  EXPECT_EQ(trace.samples[0].values,
            (std::vector<BusValue>{busValueFromBits("0"), busValueFromBits("0000")}));
  EXPECT_EQ(trace.samples[1].time, 10);

  applyEditInterval(trace, 10, 1, 2, 5, busValueFromBits("1010"));

  ASSERT_EQ(trace.samples.size(), 4);
  EXPECT_EQ(trace.samples[0].time, 0);
  EXPECT_EQ(trace.samples[0].values,
            (std::vector<BusValue>{busValueFromBits("0"), busValueFromBits("0000")}));
  EXPECT_EQ(trace.samples[1].time, 2);
  EXPECT_EQ(trace.samples[1].values,
            (std::vector<BusValue>{busValueFromBits("0"), busValueFromBits("1010")}));
  EXPECT_EQ(trace.samples[2].time, 5);
  EXPECT_EQ(trace.samples[2].values,
            (std::vector<BusValue>{busValueFromBits("0"), busValueFromBits("0000")}));
  EXPECT_EQ(trace.samples[3].time, 10);
}

TEST(SiliconWaveformTest, PreservesFourStateValues)
{
  Trace trace;
  resetTrace(trace, {{"in", 4}}, 1);
  const auto value = busValueFromBits("10XE");
  appendSnapshot(trace, 0, {value});
  EXPECT_EQ(trace.samples.front().values.front(), value);
}

TEST(SiliconWaveformTest, WritesFstTrace)
{
  const std::string filename = "test_silicon_waveform.fst";
  FileCleanup       cleanup{filename};

  Trace trace;
  resetTrace(trace, {{"clk", 1}, {"data", 4}}, 1);
  appendSnapshot(trace, 0, {busValueFromBits("0"), busValueFromBits("0011")});
  appendSnapshot(trace, 5, {busValueFromBits("1"), busValueFromBits("1010")});

  writeTrace(filename, trace);

  Reader  reader(filename);
  const auto hierarchy = reader.buildHierarchyTree();
  ASSERT_EQ(hierarchy.name, "Waveform");
  ASSERT_EQ(hierarchy.vars.size(), 2);
  EXPECT_EQ(hierarchy.vars[0].name, "clk");
  EXPECT_EQ(hierarchy.vars[0].length, 1);
  EXPECT_EQ(hierarchy.vars[1].name, "data");
  EXPECT_EQ(hierarchy.vars[1].length, 4);

  std::map<fstHandle, std::string> namesByHandle;
  for (const auto& var : hierarchy.vars)
    namesByHandle.emplace(var.handle, var.name);

  std::vector<std::tuple<uint64_t, std::string, std::string>> events;
  reader.setFacProcessMaskAll();
  reader.readIterateBlocks(
      [&](uint64_t time, fstHandle handle, std::string_view value, uint32_t len) {
        events.emplace_back(time, namesByHandle.at(handle), std::string(value));
      });

  std::ranges::sort(events);

  EXPECT_EQ(events, (std::vector<std::tuple<uint64_t, std::string, std::string>>{
                        {0, "clk", "0"},
                        {0, "data", "0011"},
                        {5, "clk", "1"},
                        {5, "data", "1010"},
                    }));
}
