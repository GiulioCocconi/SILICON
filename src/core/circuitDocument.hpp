/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#pragma once

#include <string_view>
#include <vector>

#include <core/circuit.hpp>

namespace SILICON::core {

class ComponentRegistry;

/** Semantic reusable-circuit implementation and interface. */
struct SubcircuitDefinition {
  Circuit                  circuit;
  std::vector<CircuitPort> inputs;
  std::vector<CircuitPort> outputs;
};

/**
 * Explicit source of project-local reusable Circuit definitions.
 *
 * Resolver pointers passed into circuits and components are non-owning. The
 * resolver must outlive every circuit, component, elaborator, simulation
 * session, or serialization operation that retains or uses that pointer.
 */
class CircuitResolver {
public:
  virtual ~CircuitResolver() = default;

  [[nodiscard]] virtual SubcircuitDefinition resolve(std::string_view slug) const = 0;
};

/**
 * Parses persisted Circuit document contents while preserving every component,
 * including the boundary I/O components used by document-level serializers.
 */
[[nodiscard]] Circuit
deserializeCircuitDocument(std::string_view contents, const ComponentRegistry& registry,
                           const CircuitResolver* resolver = nullptr);

/**
 * Parses persisted Circuit document contents without constructing graphical objects.
 * Boundary I/O components define the reusable interface and are removed from the
 * returned implementation circuit.
 */
[[nodiscard]] SubcircuitDefinition
parseCircuitDocument(std::string_view contents, const ComponentRegistry& registry,
                     const CircuitResolver* resolver = nullptr);

}  // namespace SILICON::core
