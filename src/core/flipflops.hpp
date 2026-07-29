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

#include <core/component.hpp>
#include <core/simulator.hpp>
#include <core/wire.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace SILICON::core {

/**
 * @class FlipFlop
 * @brief Base class for edge-triggered storage elements with async clear/preset.
 *
 * FlipFlop centralizes common sequential behavior: selected clock-edge detection,
 * asynchronous CLR/PRE handling, output driving with optional delay, and context-aware
 * reactive evaluation. Derived classes provide only the synchronous capture rule.
 *
 * @note CLR and PRE are active-high. Unconnected CLR/PRE pins are treated as LOW.
 * When both are active, the stored state becomes UNKNOWN. Unknown async control
 * states also drive UNKNOWN.
 */
class FlipFlop : public Component {
public:
  /**
   * @brief Constructs a flip-flop base with the input indices used for control pins.
   * @param clockIndex Index of the clock input bus
   * @param clearIndex Index of the asynchronous clear input bus
   * @param presetIndex Index of the asynchronous preset input bus
   */
  FlipFlop(unsigned int clockIndex, unsigned int clearIndex, unsigned int presetIndex);

  /**
   * @brief Constructs a flip-flop base with predefined buses and control pin indices.
   * @param inputs Input buses
   * @param outputs Output buses, ordered as Q and not-Q
   * @param clockIndex Index of the clock input bus
   * @param clearIndex Index of the asynchronous clear input bus
   * @param presetIndex Index of the asynchronous preset input bus
   */
  FlipFlop(std::vector<Bus> inputs, std::vector<Bus> outputs, unsigned int clockIndex,
           unsigned int clearIndex, unsigned int presetIndex);

  /**
   * @brief Clear FF internal state (state, last transitions time, ...).
   */
  void clearState();

  /**
   * @brief Evaluates async controls and selected clock edges.
   *
   * Initial/full evaluations drive the initial stored state when needed. Reactive
   * evaluations use SILICON::simulation::Context::previousState for edge detection and timing;
   * async CLR/PRE are evaluated first on every call.
   *
   * @param sim SILICON::simulation::Simulator used to drive output wires
   * @param context Evaluation context supplied by the simulator
   */
  void simulate(SILICON::simulation::Simulator& sim, const SILICON::simulation::Context& context) override;

  [[nodiscard]] bool usesStagedSequentialOutputs() const override { return true; }

  /**
   * @brief Clears wire connections while keeping optional async controls unconnected.
   *
   * @details Graphical simulation rebuilds component wiring by clearing every
   * component and then reconnecting only ports that touch a graphical wire. Generic
   * clearing replaces each bus slot with a placeholder UNKNOWN wire, which is correct
   * for required signal pins because an unwired input is unknown.
   *
   * CLR and PRE are optional active-high controls. When they are not connected, they
   * must remain null rather than become UNKNOWN placeholders so
   * optionalControlStateOrInactive() reads them as inactive LOW.
   *
   * @note Connected CLR/PRE ports are still replaced later by the graphical wiring
   * pass, so this only defines the default state for unconnected async controls.
   */
  void clearWires() override;

protected:
  /**
   * @brief Returns the currently stored state.
   * @return Latched storage state
   */
  [[nodiscard]] State latchedState() const { return state; }

  /**
   * @brief Computes the state captured on the selected clock edge.
   * @return New storage state for the concrete flip-flop type
   */
  virtual State captureState() const = 0;

  /**
   * @brief Returns true for synchronous pins governed by setup/hold timing.
   */
  [[nodiscard]] virtual bool isTimingSensitiveInput(unsigned int inputIndex) const = 0;

private:
  void initializeProperties();
  void driveOutput(SILICON::simulation::Simulator& sim, State newState);
  [[nodiscard]] bool
  hasTimingSensitiveInputChange(const SILICON::simulation::Context& context) const;
  [[nodiscard]] bool violatesSetupTime(uint64_t currentTime,
                                       bool     timingInputChanged) const;
  [[nodiscard]] bool violatesHoldTime(uint64_t currentTime,
                                      bool     timingInputChanged) const;

  /**
   * @brief Checks if an ambiguous clock transition occurred that *might* be the
   * triggering edge.
   *
   * This is used to propagate UNKNOWN (X) states when a clock edge cannot be guaranteed.
   * It specifically prevents false positives and negatives that a simple `clock ==
   * UNKNOWN` check would cause. For a Positive-Edge Triggered (PET) flip-flop:
   *  - LOW -> UNKNOWN: Could be a rising edge. (Returns true)
   *  - UNKNOWN -> HIGH: Could be a rising edge. (Returns true)
   *  - HIGH -> UNKNOWN: Cannot be a rising edge; it is either holding HIGH or falling.
   * (Returns false)
   *
   * @param currentClock The current state of the clock pin
   * @return true if the transition from previousClock to currentClock could be the active
   * edge
   */
  [[nodiscard]] bool mayHaveSelectedEdge(State previousClock, State currentClock) const;

  State        state = State::UNKNOWN;
  unsigned int clockIndex;
  unsigned int clearIndex;
  unsigned int presetIndex;
  uint64_t     propagationDelay = 0;
  uint64_t     setupTime        = 0;
  uint64_t     holdTime         = 0;

  std::optional<uint64_t> lastTimingInputChangeTime;
  std::optional<uint64_t> lastSelectedClockEdgeTime;

  // Cached properties to avoid costly map lookups per delta cycle.
  SILICON::simulation::Simulator::EdgeType edgeType = SILICON::simulation::Simulator::EdgeType::RISE;
};

/**
 * @class DFlipFlop
 * @brief D-type flip-flop.
 *
 * Captures D on the configured clock edge when async controls are inactive.
 * Input order: D, Clock, Clear, Preset. Output order: Q, not-Q.
 */
class DFlipFlop : public FlipFlop {
public:
  /** @brief Serialization/type-registry identifier. */
  static constexpr std::string_view Type = "DFlipFlop";

  /**
   * @brief Returns the component type name.
   * @return Type identifier
   */
  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"D Flip Flop", "Captures D on the configured clock edge.",
            ComponentCategory::FlipFlops};
  }

  /** @brief Input bus indices for DFlipFlop. */
  enum class Inputs : unsigned int {
    /** @brief Data input captured on the selected clock edge. */
    D = 0,
    /** @brief Clock input. */
    Clock = 1,
    /** @brief Active-high asynchronous clear. */
    Clear = 2,
    /** @brief Active-high asynchronous preset. */
    Preset = 3,
  };

  /** @brief Constructs an unconnected D flip-flop. */
  DFlipFlop()
    : FlipFlop(busIndex(Inputs::Clock), busIndex(Inputs::Clear), busIndex(Inputs::Preset))
  {
  }

  /**
   * @brief Constructs a connected D flip-flop.
   * @param d Data input
   * @param clock Clock input
   * @param clear Active-high asynchronous clear
   * @param preset Active-high asynchronous preset
   * @param q Q output
   * @param notQ Complement output
   */
  DFlipFlop(Wire_ptr d, Wire_ptr clock, Wire_ptr clear, Wire_ptr preset, Wire_ptr q,
            Wire_ptr notQ);

  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

protected:
  /** @copydoc FlipFlop::captureState */
  State captureState() const override;
  bool  isTimingSensitiveInput(unsigned int inputIndex) const override;
};

/**
 * @class EFlipFlop
 * @brief Enabled D-type flip-flop.
 *
 * Captures D on the configured clock edge only when Enable is HIGH. If Enable is LOW,
 * the current stored state is retained. Unknown Enable captures UNKNOWN.
 * Input order: D, Enable, Clock, Clear, Preset. Output order: Q, not-Q.
 */
class EFlipFlop : public FlipFlop {
public:
  /** @brief Serialization/type-registry identifier. */
  static constexpr std::string_view Type = "EFlipFlop";

  /**
   * @brief Returns the component type name.
   * @return Type identifier
   */
  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Enabled D Flip Flop",
            "Captures D on the configured clock edge while enable is HIGH.",
            ComponentCategory::FlipFlops};
  }

  /** @brief Input bus indices for EFlipFlop. */
  enum class Inputs : unsigned int {
    /** @brief Data input captured when Enable is HIGH. */
    D = 0,
    /** @brief Capture enable input. */
    Enable = 1,
    /** @brief Clock input. */
    Clock = 2,
    /** @brief Active-high asynchronous clear. */
    Clear = 3,
    /** @brief Active-high asynchronous preset. */
    Preset = 4,
  };

  /** @brief Constructs an unconnected enabled D flip-flop. */
  EFlipFlop()
    : FlipFlop(busIndex(Inputs::Clock), busIndex(Inputs::Clear), busIndex(Inputs::Preset))
  {
  }

  /**
   * @brief Constructs a connected enabled D flip-flop.
   * @param d Data input
   * @param enable Capture enable input
   * @param clock Clock input
   * @param clear Active-high asynchronous clear
   * @param preset Active-high asynchronous preset
   * @param q Q output
   * @param notQ Complement output
   */
  EFlipFlop(Wire_ptr d, Wire_ptr enable, Wire_ptr clock, Wire_ptr clear, Wire_ptr preset,
            Wire_ptr q, Wire_ptr notQ);

  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

protected:
  /** @copydoc FlipFlop::captureState */
  State captureState() const override;
  bool  isTimingSensitiveInput(unsigned int inputIndex) const override;
};

/**
 * @class DLatch
 * @brief Active-high, level-sensitive D latch.
 *
 * The latch is transparent while Enable is HIGH and retains its stored state while
 * Enable is LOW. Unknown Enable drives an unknown stored state.
 * Input order: D, Enable. Output order: Q, not-Q.
 */
class DLatch : public Component {
public:
  /** @brief Serialization/type-registry identifier. */
  static constexpr std::string_view Type = "DLatch";

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"D Latch", "Follows D while enable is HIGH and holds while disabled.",
            ComponentCategory::FlipFlops};
  }

  /** @brief Input bus indices for DLatch. */
  enum class Inputs : unsigned int {
    /** @brief Data input followed while the latch is transparent. */
    D = 0,
    /** @brief Active-high transparency enable. */
    Enable = 1,
  };

  /** @brief Constructs an unconnected D latch. */
  DLatch();

  /**
   * @brief Constructs a connected D latch.
   * @param d Data input
   * @param enable Active-high transparency enable
   * @param q Q output
   * @param notQ Complement output
   */
  DLatch(Wire_ptr d, Wire_ptr enable, Wire_ptr q, Wire_ptr notQ);

  void               simulate(SILICON::simulation::Simulator& sim, const SILICON::simulation::Context& context) override;
  [[nodiscard]] bool usesStagedSequentialOutputs() const override { return true; }
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

private:
  void initializeProperties();
  void driveOutput(SILICON::simulation::Simulator& sim, State newState);

  State    state            = State::UNKNOWN;
  uint64_t propagationDelay = 0;
};

/**
 * @class JKFlipFlop
 * @brief JK flip-flop.
 *
 * Applies the standard JK truth table on the configured clock edge: hold for 00,
 * reset for 01, set for 10, and toggle for 11. Unknown J or K captures UNKNOWN.
 * Input order: J, K, Clock, Clear, Preset. Output order: Q, not-Q.
 */
class JKFlipFlop : public FlipFlop {
public:
  /** @brief Serialization/type-registry identifier. */
  static constexpr std::string_view Type = "JKFlipFlop";

  /**
   * @brief Returns the component type name.
   * @return Type identifier
   */
  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"JK Flip Flop", "Stores, sets, resets, or toggles state from J and K.",
            ComponentCategory::FlipFlops};
  }

  /** @brief Input bus indices for JKFlipFlop. */
  enum class Inputs : unsigned int {
    /** @brief J input. */
    J = 0,
    /** @brief K input. */
    K = 1,
    /** @brief Clock input. */
    Clock = 2,
    /** @brief Active-high asynchronous clear. */
    Clear = 3,
    /** @brief Active-high asynchronous preset. */
    Preset = 4,
  };

  /** @brief Constructs an unconnected JK flip-flop. */
  JKFlipFlop()
    : FlipFlop(busIndex(Inputs::Clock), busIndex(Inputs::Clear), busIndex(Inputs::Preset))
  {
  }

  /**
   * @brief Constructs a connected JK flip-flop.
   * @param j J input
   * @param k K input
   * @param clock Clock input
   * @param clear Active-high asynchronous clear
   * @param preset Active-high asynchronous preset
   * @param q Q output
   * @param notQ Complement output
   */
  JKFlipFlop(Wire_ptr j, Wire_ptr k, Wire_ptr clock, Wire_ptr clear, Wire_ptr preset,
             Wire_ptr q, Wire_ptr notQ);

  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

protected:
  /** @copydoc FlipFlop::captureState */
  State captureState() const override;
  bool  isTimingSensitiveInput(unsigned int inputIndex) const override;
};

}  // namespace SILICON::core
