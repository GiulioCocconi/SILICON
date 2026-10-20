---
title: Why?
order: 1
---

# Why Digital Electronics?

Every day, we interact with extremely complex digital systems. A modern computer can execute billions of operations per second, a smartphone can process images and recognize speech, and a tiny microcontroller can control an entire industrial process.

All of this is possible thanks to a seemingly simple idea: *we can represent all the information we need using just two values*. These values, conventionally called **0** and **1**, form the foundation of digital electronics, which, in turn, underpins much of modern society.

## From electricity to information: the power of abstraction

Physical quantities such as voltages, currents, temperatures, and forces can vary continuously. However, by designing circuits that reliably distinguish between just two logical states, we can abstract away much of this physical complexity.

Instead of reasoning about the exact voltage at every point in a circuit, we can reason about *logical values* and the operations performed on them.

But this is just one example of a much broader concept: **abstraction**.

Consider something as familiar as a car. Most of us know how to drive one, but very few understand everything that happens beneath the hood.

1. A driver interacts with the car through a steering wheel, pedals, and a dashboard. They need to know how to accelerate, brake, and change direction, but they do not need to understand how the engine works.

2. A mechanic approaches the same car from a different perspective. They understand its components, how they interact, and how to diagnose and repair failures. Yet, they do not necessarily need to know how each component was designed.

3. An engineer designing the engine must understand its internal structure, the forces acting on its components, and the principles governing its operation.

4. A physicist might go even deeper, studying combustion, thermodynamics, or the microscopic properties of the materials from which the engine is built.

All these people interact with **the same system**, but at different **levels of abstraction**. Each level exposes certain details while hiding others, allowing us to focus on what matters for the problem at hand.

This is a fundamental principle of engineering: **we can understand and work with complex systems without understanding every detail of their implementation.**

## The Abstraction Hierarchy

The same principle applies to digital systems.

When we use a computer, we interact with applications without worrying about how the underlying hardware executes them. A programmer writes instructions without necessarily knowing how the processor implements them. A computer architect designs processors in terms of registers, arithmetic units, and memory, while a digital circuit designer implements these structures using logic gates.

Going deeper, an electronic engineer studies the transistors that make these gates possible, and a physicist investigates the properties of the semiconductor materials from which transistors are made.

We can represent these different perspectives as a hierarchy of abstraction:

| Level | What we care about |
|---|---|
| **Applications** | What the computer can do for us |
| **Software** | How algorithms and programs solve problems |
| **Computer Architecture** | How instructions, registers, and memory organize computation |
| **Digital Logic** | How logic gates and circuits process and store information |
| **Electronics** | How transistors and electrical signals implement logic |
| **Physics** | How the underlying materials and physical phenomena behave |

Each level builds upon the one below it, hiding implementation details behind a simpler model.

For example, when designing an adder, we do not need to calculate the currents flowing through every transistor. We can work with logic gates, knowing how each gate behaves without considering its internal implementation. Similarly, a computer architect can use an adder as a building block without having to design its individual gates.

This ability to compose simpler systems into more complex ones is what makes modern digital technology possible.

## Where Do We Begin?

Digital electronics covers a vast range of topics, from semiconductor devices to complete computing systems. Trying to understand all these levels simultaneously would be overwhelming.

Fortunately, abstraction allows us to choose where to begin.

In these lessons, we will focus primarily on **digital logic**, the level at which electrical circuits become logical operations.

We will start with the simplest building blocks, discover how they can be combined to perform arithmetic and store information, and gradually work our way toward more complex digital systems.

With SILICON, we will be able to construct these circuits, simulate their behavior, and experiment with them directly.

Along the way, we will sometimes look at the levels above or below digital logic to understand why certain abstractions exist and where their limitations lie.

The goal is not to memorize a collection of circuits, but to understand **how complex digital systems can emerge from a handful of simple principles**.
