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
#include <core/fstTraceWriter.hpp>
#include <core/fstWrapper.hpp>
#include <core/gates.hpp>
#include <core/siliconFst.hpp>
#include <core/simulator.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// --- Helper Utilities ------------------------------------------------------------------

// RAII helper to clean up generated test files
struct FileCleanup {
  std::string filename;
  ~FileCleanup() { std::remove(filename.c_str()); }
};

// Represents a captured waveform event for verification
struct WaveEvent {
  uint64_t time;
  fstHandle handle;
  std::string value;

  bool operator==(const WaveEvent& other) const {
    return time == other.time && handle == other.handle && value == other.value;
  }
};

// GTest printer for elegant diagnostic output instead of raw bytes
inline void PrintTo(const WaveEvent& ev, std::ostream* os) {
  *os << "{time: " << ev.time << ", handle: " << ev.handle << ", val: '" << ev.value << "'}";
}

// --- Tests -----------------------------------------------------------------------------

TEST(FstWrapperTest, ThrowsOnMissingFile) {
  EXPECT_THROW(FstReader("nonexistent_path_12345.fst"), std::runtime_error);
}

TEST(FstWrapperTest, MetadataAndBasicIO) {
  const std::string filename = "test_metadata.fst";
  FileCleanup cleanup{filename};

  fstHandle h_clk, h_rst;

  {
    FstHierarchyBuilder builder(filename);
    builder.setTimeScale(-9); // nanoseconds
    builder.setDate("2026-01-01");
    builder.setVersion("FST Wrapper Test");

    builder.setScope(FST_ST_VCD_MODULE, "TestBench");
    h_clk = builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "clk");
    h_rst = builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "rst");
    builder.upScope();

    auto writer = std::move(builder).finish();

    writer.emitTimeChange(0);
    writer.emitValueChange(h_rst, "1");
    writer.emitValueChange(h_clk, "0");

    writer.emitTimeChange(10);
    writer.emitValueChange(h_rst, "0");
    writer.emitValueChange(h_clk, "1");
  }

  FstReader reader(filename);

  EXPECT_EQ(reader.getTimescale(), -9);
  EXPECT_EQ(reader.getDate(), "2026-01-01");
  EXPECT_EQ(reader.getVersion(), "FST Wrapper Test");
  EXPECT_EQ(reader.getStartTime(), 0);
  EXPECT_EQ(reader.getEndTime(), 10);
  EXPECT_EQ(reader.getVarCount(), 2);
  EXPECT_EQ(reader.getScopeCount(), 1);

  std::vector<WaveEvent> events;
  reader.setFacProcessMaskAll();

  reader.readIterateBlocks([&](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    events.push_back({t, h, std::string(v)});
  });

  // libfst iteration yields events grouped by signal/block, not strictly by time.
  // We sort them chronologically (then by handle) to ensure safe predictable comparisons.
  std::sort(events.begin(), events.end(), [](const WaveEvent& a, const WaveEvent& b) {
    if (a.time != b.time) return a.time < b.time;
    return a.handle < b.handle;
  });

  ASSERT_EQ(events.size(), 4);
  EXPECT_EQ(events[0], (WaveEvent{0, h_clk, "0"}));
  EXPECT_EQ(events[1], (WaveEvent{0, h_rst, "1"}));
  EXPECT_EQ(events[2], (WaveEvent{10, h_clk, "1"}));
  EXPECT_EQ(events[3], (WaveEvent{10, h_rst, "0"}));
}

TEST(FstWrapperTest, HierarchyTreeConstruction) {
  const std::string filename = "test_hierarchy.fst";
  FileCleanup cleanup{filename};

  {
    FstHierarchyBuilder builder(filename);

    builder.setScope(FST_ST_VCD_MODULE, "Top");
    builder.createVar(FST_VT_VCD_WIRE, FST_VD_INPUT, 1, "clk");

    builder.setScope(FST_ST_VCD_TASK, "TaskA", "CompA");
    builder.createVar(FST_VT_VCD_REG, FST_VD_OUTPUT, 8, "data");
    builder.upScope();
    builder.upScope();

    auto writer = std::move(builder).finish();
  }

  FstReader reader(filename);
  auto top = reader.buildHierarchyTree();

  EXPECT_EQ(top.name, "Top");
  EXPECT_EQ(top.type, FST_ST_VCD_MODULE);
  ASSERT_EQ(top.vars.size(), 1);
  EXPECT_EQ(top.vars[0].name, "clk");
  EXPECT_EQ(top.vars[0].length, 1);

  ASSERT_EQ(top.children.size(), 1);
  const auto& task = top.children[0];
  EXPECT_EQ(task.name, "TaskA");
  EXPECT_EQ(task.component, "CompA");
  EXPECT_EQ(task.type, FST_ST_VCD_TASK);
  ASSERT_EQ(task.vars.size(), 1);
  EXPECT_EQ(task.vars[0].name, "data");
}

TEST(FstWrapperTest, NumericAndVariableLengthEmissions) {
  const std::string filename = "test_emissions.fst";
  FileCleanup cleanup{filename};

  fstHandle h_32, h_64, h_varlen;

  {
    FstHierarchyBuilder builder(filename);
    builder.setScope(FST_ST_VCD_MODULE, "Core");
    h_32 = builder.createVar(FST_VT_VCD_INTEGER, FST_VD_IMPLICIT, 4, "val32");
    h_64 = builder.createVar(FST_VT_VCD_INTEGER, FST_VD_IMPLICIT, 4, "val64");
    h_varlen = builder.createVar(FST_VT_GEN_STRING, FST_VD_IMPLICIT, 0, "valstr");
    auto writer = std::move(builder).finish();

    writer.emitTimeChange(5);
    writer.emitValue32(h_32, 4, 0xA);
    writer.emitValue64(h_64, 4, 0x5);
    writer.emitVariableLengthValueChange(h_varlen, "Hello_FST");
  }

  FstReader reader(filename);
  reader.setFacProcessMaskAll();

  std::map<fstHandle, std::string> latest_vals;
  reader.readIterateBlocks([&](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    latest_vals[h] = std::string(v);
  });

  EXPECT_EQ(latest_vals[h_32], "1010");
  EXPECT_EQ(latest_vals[h_64], "0101");
  EXPECT_EQ(latest_vals[h_varlen], "Hello_FST");
}

TEST(FstWrapperTest, TimeRangeFiltering) {
  const std::string filename = "test_timerange.fst";
  FileCleanup cleanup{filename};

  fstHandle h_clk;
  {
    FstHierarchyBuilder builder(filename);
    builder.setScope(FST_ST_VCD_MODULE, "TB");
    h_clk = builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "clk");
    auto writer = std::move(builder).finish();

    // Generate a trace from time 10 to 50
    for (uint64_t t = 10; t <= 50; t += 10) {
      writer.emitTimeChange(t);
      writer.emitValueChange(h_clk, (t % 20 == 0) ? "1" : "0");
    }
  }

  FstReader reader(filename);
  reader.setFacProcessMaskAll();

  // Set reading limit completely outside the bounds of the file's min/max.
  // This verifies `libfst` respects the bound and securely drops out-of-range blocks.
  reader.setLimitTimeRange(5000, 6000);

  std::vector<uint64_t> times;
  reader.readIterateBlocks([&](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    times.push_back(t);
  });

  EXPECT_TRUE(times.empty()) << "Time out of absolute bounds should result in zero callbacks.";

  // Clear limits, should read all 5 time steps
  reader.setUnlimitedTimeRange();
  times.clear();
  reader.readIterateBlocks([&](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    times.push_back(t);
  });
  EXPECT_EQ(times.size(), 5) << "Clearing the time range should restore all blocks.";
}

TEST(FstWrapperTest, FacilityMaskFiltering) {
  const std::string filename = "test_facmask.fst";
  FileCleanup cleanup{filename};

  fstHandle h_v1, h_v2;
  {
    FstHierarchyBuilder builder(filename);
    builder.setScope(FST_ST_VCD_MODULE, "TB");
    h_v1 = builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "v1");
    h_v2 = builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "v2");
    auto writer = std::move(builder).finish();

    writer.emitTimeChange(10);
    writer.emitValueChange(h_v1, "1");
    writer.emitValueChange(h_v2, "1");
  }

  FstReader reader(filename);

  reader.clearFacProcessMaskAll();
  reader.setFacProcessMask(h_v2);

  std::vector<fstHandle> handles;
  reader.readIterateBlocks([&](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    handles.push_back(h);
  });

  ASSERT_EQ(handles.size(), 1);
  EXPECT_EQ(handles[0], h_v2);
}

TEST(FstWrapperTest, EnumTables) {
  const std::string filename = "test_enums.fst";
  FileCleanup cleanup{filename};

  {
    FstHierarchyBuilder builder(filename);

    // 1. Declare the scope first!
    builder.setScope(FST_ST_VCD_MODULE, "Core");

    auto enumHandle = builder.createEnumTable("StateEnum", 2, {
      {"IDLE", "00"},
      {"BUSY", "01"}
    });

    // 2. The reference must precede the variable it modifies WITHOUT any other scope changes.
    builder.emitEnumTableRef(enumHandle);
    builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 2, "state");

    auto writer = std::move(builder).finish();
  }

  FstReader reader(filename);
  std::string captured_enum_str;

  // Search the raw hierarchy attributes to extract the encoded payload.
  reader.iterateHierarchy([&](const fstHier* h) {
    if (h->htyp == FST_HT_ATTRBEGIN && h->u.attr.name) {
      std::string attr_name(h->u.attr.name, h->u.attr.name_length);
      // FST places the enum name in the payload header.
      if (attr_name.find("StateEnum") != std::string::npos) {
        captured_enum_str = attr_name;
      }
    }
  });

  ASSERT_FALSE(captured_enum_str.empty()) << "Failed to locate dynamic enum payload in generated FST attributes.";

  // Decode the raw payload using the wrapper tool
  auto [name, mapping] = FstReader::parseEnumTable(captured_enum_str);

  EXPECT_EQ(name, "StateEnum");
  ASSERT_EQ(mapping.size(), 2);

  EXPECT_EQ(mapping[0].first, "IDLE");
  EXPECT_EQ(mapping[0].second, "00");

  EXPECT_EQ(mapping[1].first, "BUSY");
  EXPECT_EQ(mapping[1].second, "01");
}

TEST(FstWrapperTest, ThrowsOnMultipleRoots) {
  const std::string filename = "test_multiroots.fst";
  FileCleanup cleanup{filename};

  {
    FstHierarchyBuilder builder(filename);
    builder.setScope(FST_ST_VCD_MODULE, "RootA");
    builder.upScope();
    builder.setScope(FST_ST_VCD_MODULE, "RootB");
    builder.upScope();
    std::move(builder).finish();
  }

  FstReader reader(filename);
  EXPECT_THROW(auto a = reader.buildHierarchyTree(), std::runtime_error);
}

TEST(FstTraceWriterTest, RegistersSignalsAndEmitsSnapshots) {
  const std::string filename = "test_trace_writer.fst";
  FileCleanup cleanup{filename};

  fstHandle h_scalar, h_bus;

  {
    FstTraceWriter writer(
        filename,
        {{"scalar", 1}, {"bus", 4}, {"ignored_zero_width", 0}},
        {.topScopeName = "Trace"});

    EXPECT_EQ(writer.signalCount(), 2);
    ASSERT_TRUE(writer.handleForSignal("scalar").has_value());
    ASSERT_TRUE(writer.handleForSignal("bus").has_value());
    EXPECT_FALSE(writer.handleForSignal("ignored_zero_width").has_value());

    h_scalar = *writer.handleForSignal("scalar");
    h_bus    = *writer.handleForSignal("bus");

    const std::vector<std::string> firstValues{"1", "1010", "1"};
    const std::vector<std::string> secondValues{"0", "x1"};
    writer.emitSnapshot(0, firstValues);
    writer.emitSnapshot(5, secondValues);
  }

  FstReader reader(filename);

  EXPECT_EQ(reader.getTimescale(), -9);
  EXPECT_EQ(reader.getVarCount(), 2);

  const auto hierarchy = reader.buildHierarchyTree();
  EXPECT_EQ(hierarchy.name, "Trace");
  ASSERT_EQ(hierarchy.vars.size(), 2);
  EXPECT_EQ(hierarchy.vars[0].name, "scalar");
  EXPECT_EQ(hierarchy.vars[0].length, 1);
  EXPECT_EQ(hierarchy.vars[1].name, "bus");
  EXPECT_EQ(hierarchy.vars[1].length, 4);

  std::map<std::pair<uint64_t, fstHandle>, std::string> values;
  reader.setFacProcessMaskAll();
  reader.readIterateBlocks([&values](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    values[{t, h}] = std::string(v);
  });

  EXPECT_EQ((values[{0, h_scalar}]), "1");
  EXPECT_EQ((values[{0, h_bus}]), "1010");
  EXPECT_EQ((values[{5, h_scalar}]), "0");
  EXPECT_EQ((values[{5, h_bus}]), "xxx1");
}

TEST(FstTraceWriterTest, NormalizesSampledValues) {
  const std::string filename = "test_trace_writer_normalize.fst";
  FileCleanup cleanup{filename};

  fstHandle h_wide, h_empty, h_invalid, h_missing;

  {
    FstTraceWriter writer(
        filename,
        {{"wide", 3}, {"empty", 2}, {"invalid", 2}, {"missing", 2}},
        {.topScopeName = "Trace"});

    h_wide    = *writer.handleForSignal("wide");
    h_empty   = *writer.handleForSignal("empty");
    h_invalid = *writer.handleForSignal("invalid");
    h_missing = *writer.handleForSignal("missing");

    const std::vector<std::string> values{"10101", "", "2z"};
    writer.emitSnapshot(0, values);
  }

  FstReader reader(filename);

  std::map<std::pair<uint64_t, fstHandle>, std::string> values;
  reader.setFacProcessMaskAll();
  reader.readIterateBlocks([&values](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    values[{t, h}] = std::string(v);
  });

  EXPECT_EQ((values[{0, h_wide}]), "101");
  EXPECT_EQ((values[{0, h_empty}]), "xx");
  EXPECT_EQ((values[{0, h_invalid}]), "xz");
  EXPECT_EQ((values[{0, h_missing}]), "xx");
}

TEST(SiliconFstWriterTest, RegistersCircuitBusesAndEmitsSnapshots) {
  const std::string filename = "test_silicon_writer.fst";
  FileCleanup cleanup{filename};

  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto c = std::make_shared<Wire>(State::LOW);
  auto d = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>(State::UNKNOWN);

  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  gate->setInput(0, Bus({a, b}));
  gate->setInput(1, Bus({c, d}));
  Circuit circuit(gate, false);

  fstHandle h_input0, h_input1, h_output0;

  {
    SiliconFstWriter writer(filename, circuit, {.topScopeName = "Trace"});

    EXPECT_EQ(writer.busCount(), 3);
    ASSERT_TRUE(writer.handleForBus("input_0").has_value());
    ASSERT_TRUE(writer.handleForBus("input_1").has_value());
    ASSERT_TRUE(writer.handleForBus("output_0").has_value());

    h_input0 = *writer.handleForBus("input_0");
    h_input1 = *writer.handleForBus("input_1");
    h_output0 = *writer.handleForBus("output_0");

    writer.emitSnapshot(0);

    a->forceSetCurrentState(State::HIGH);
    b->forceSetCurrentState(State::LOW);
    c->forceSetCurrentState(State::UNKNOWN);
    d->forceSetCurrentState(State::ERROR);
    o->forceSetCurrentState(State::ERROR);

    writer.emitSnapshot(5);
  }

  FstReader reader(filename);

  EXPECT_EQ(reader.getTimescale(), -9);
  EXPECT_EQ(reader.getVarCount(), 3);

  const auto hierarchy = reader.buildHierarchyTree();
  EXPECT_EQ(hierarchy.name, "Trace");
  ASSERT_EQ(hierarchy.vars.size(), 3);
  EXPECT_EQ(hierarchy.vars[0].name, "input_0");
  EXPECT_EQ(hierarchy.vars[0].length, 2);
  EXPECT_EQ(hierarchy.vars[1].name, "input_1");
  EXPECT_EQ(hierarchy.vars[1].length, 2);
  EXPECT_EQ(hierarchy.vars[2].name, "output_0");
  EXPECT_EQ(hierarchy.vars[2].length, 1);

  std::map<std::pair<uint64_t, fstHandle>, std::string> values;
  reader.setFacProcessMaskAll();
  reader.readIterateBlocks([&values](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    values[{t, h}] = std::string(v);
  });

  EXPECT_EQ((values[{0, h_input0}]), "10");
  EXPECT_EQ((values[{0, h_input1}]), "10");
  EXPECT_EQ((values[{0, h_output0}]), "x");

  EXPECT_EQ((values[{5, h_input0}]), "01");
  EXPECT_EQ((values[{5, h_input1}]), "zx");
  EXPECT_EQ((values[{5, h_output0}]), "z");
}

TEST(SiliconFstWriterTest, SimulatorFstTraceUsesConfiguredBusNames) {
  const std::string filename = "test_simulator_trace.fst";
  FileCleanup cleanup{filename};

  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>(State::UNKNOWN);

  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto circuit = std::make_shared<Circuit>(gate, false);

  {
    Simulator simulator(circuit);
    simulator.setTraceBuses({
      {"clock", Bus({a})},
      {"enable", Bus({b})},
      {"result", Bus({o})},
    });
    simulator.enableFstTracing(filename, {.topScopeName = "SimTrace"});
    simulator.setBus(Bus({a}), 1);
    simulator.run(5);
  }

  FstReader reader(filename);
  const auto hierarchy = reader.buildHierarchyTree();

  EXPECT_EQ(hierarchy.name, "SimTrace");
  ASSERT_EQ(hierarchy.vars.size(), 3);

  std::map<std::string, fstHandle> handles;
  for (const auto& var : hierarchy.vars)
    handles[var.name] = var.handle;

  ASSERT_TRUE(handles.contains("clock"));
  ASSERT_TRUE(handles.contains("enable"));
  ASSERT_TRUE(handles.contains("result"));

  std::map<std::pair<uint64_t, fstHandle>, std::string> values;
  reader.setFacProcessMaskAll();
  reader.readIterateBlocks([&values](uint64_t t, fstHandle h, std::string_view v, uint32_t len) {
    values[{t, h}] = std::string(v);
  });

  EXPECT_EQ((values[{0, handles["clock"]}]), "1");
  EXPECT_EQ((values[{0, handles["enable"]}]), "1");
  EXPECT_EQ((values[{5, handles["result"]}]), "1");
}
