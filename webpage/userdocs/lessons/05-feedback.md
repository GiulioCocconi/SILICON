---
title: Feedback and Memory
order: 5
---

# Feedback and Memory

## The Alarm That Must Remember

In Lesson 4, every circuit computed its output from its current inputs. The security door's alarm,

$$
y=s\land(\neg a\lor b),
$$

sounds only while the triggering condition is present.

Now suppose the alarm must **keep sounding** after the sensor returns to 0, until someone explicitly resets it. Can a combinational circuit with the sensor and the reset signal as its inputs do this?

No. Imagine two situations with identical current inputs:

- No intruder has ever been detected.
- An intruder was detected earlier, but the sensor has since returned to 0.

The circuit must give different outputs in these two situations. So the output must depend on something besides the current inputs: **the past**. The information a circuit retains from previous events is its **state**.

In this lesson we will see how ordinary gates can store state. By the end, you will be able to:

- Distinguish combinational from sequential circuits.
- Explain how **feedback** loops can produce oscillation or memory.
- Build an **astable** circuit (a ring oscillator) and a **bistable** circuit (an SR latch).
- Explain why feedback is hard to simulate, and how the unknown value X helps.
- Add memory to the security door.

## Combinational vs Sequential Circuits

| Property | Combinational | Sequential |
|---|---|---|
| Output depends on | Current inputs | Current inputs and stored state |
| Remembers previous events | No | Yes |
| Internal state | Not required | Required |
| Examples | Adder, decoder, multiplexer | Latch, flip-flop, counter |

A combinational circuit is modeled as

$$
\mathbf y=f(\mathbf x).
$$

A sequential circuit needs an additional state variable:

$$
\mathbf y=g(\mathbf x,\mathbf q),\qquad
\mathbf q_{\text{next}}=h(\mathbf x,\mathbf q),
$$

where $\mathbf x$ are the inputs, $\mathbf y$ the outputs, $\mathbf q$ the stored state and $\mathbf q_{\text{next}}$ the next state.

In a clocked sequential circuit, the state typically updates on a clock edge. Other circuits, such as latches, change state in response to input levels without any clock.

These equations describe the behavior, but not how to achieve it. **How can a circuit built from ordinary Boolean gates remember anything?** The answer is feedback.

## Feedback: Connecting the Output Back to the Input

In all our earlier circuits, signals flowed from inputs toward outputs. Such a circuit is a **directed acyclic graph** (DAG): it has no directed cycles, so its gates can be evaluated in an order that respects their dependencies.

Now connect a gate's output back to one of its own inputs. This creates a **feedback loop**. The circuit is no longer acyclic, and its output can influence its own future behavior.

Feedback is the mechanism behind oscillators and memory elements, but it does not automatically give useful memory. Depending on the Boolean functions and the delays involved, a feedback circuit may:

- Settle into a single stable configuration.
- Have more than one stable configuration.
- Change its outputs continuously.
- Fail to reach a stable result in a particular simulation model.

We will study the two most important cases: no stable state, and two stable states.

## Astable Circuits: Building an Oscillator

Take the simplest feedback circuit: a NOT gate whose output is wired to its own input. Calling the signal $q$, the circuit demands

$$
q=\neg q.
$$

No Boolean value satisfies this. If $q=0$ the gate wants to produce 1, and if $q=1$ it wants to produce 0. The circuit has **no stable Boolean solution**.

### The Role of Propagation Delay

A real gate does not respond instantly. Suppose the propagation delay is $\tau$ and the output starts at 0. After about $\tau$, the inversion produces 1. That value returns to the input, and after another $\tau$ the output goes back to 0. In a simplified delay model,

$$
q(t+\tau)=\neg q(t),
$$

and the output follows the sequence

$$
0\rightarrow1\rightarrow0\rightarrow1\rightarrow\cdots
$$

This is an **astable circuit**: it has no stable state and keeps changing. A device that generates a repeating signal is an **oscillator**.

### The Ring Oscillator

A more conventional oscillator connects an odd number of inverters in a ring. With three:

$$
q_1=\neg q_3,\qquad q_2=\neg q_1,\qquad q_3=\neg q_2.
$$

Combining these gives $q_1=\neg q_1$, so again no stable solution exists. If each inverter has delay $\tau$, a transition takes about $3\tau$ to travel around the ring and another $3\tau$ to restore the original polarity, so the period is about $6\tau$. For an odd number $N$ of identical inverters,

$$
\boxed{T\approx2N\tau},\qquad f\approx\frac{1}{2N\tau}.
$$

Real ring oscillators also depend on transistor characteristics, load capacitance, supply voltage and temperature.

::: warning Simulation and Physical Reality
A Boolean simulator does not automatically reproduce every analog behavior of a physical oscillator.

With zero propagation delay, the feedback has no stable binary solution, so a simulation may fail to converge.

With nonzero modeled delays and suitable initialization, transitions can circulate through the ring.

If every inverter starts at X, however, ordinary four-valued logic gives $\neg X=X$, so the simulator may stay at X instead of beginning to oscillate.

This is not a contradiction: Boolean simulation omits the analog noise, device mismatches and transition dynamics that start oscillation in physical hardware.
:::

### Why Odd?

Connect an even number of inverters and the picture changes. For two:

$$
q_1=\neg q_2,\qquad q_2=\neg q_1.
$$

Substituting gives $q_1=q_1$, which is no contradiction at all. In fact there are **two** stable binary configurations,

$$
(q_1,q_2)=(0,1)\quad\text{and}\quad(q_1,q_2)=(1,0),
$$

and the circuit can stay in either one. That is exactly what a memory element needs.

## Bistable Circuits: Storing One Bit

A **bistable circuit** has two stable states. Instead of oscillating, it stays in one state until something forces it into the other, so the two states can represent one bit.

The two-inverter loop is bistable, but it has no inputs: we cannot choose its state. Replacing the inverters with NOR gates adds inputs and gives the classic **SR latch**.

### The SR Latch

An SR latch consists of two cross-coupled NOR gates. Its inputs are

- $S$ (**Set**), which requests that the stored value become 1,
- $R$ (**Reset**), which requests that it become 0,

and its outputs are $Q$, the stored bit, and $\overline Q$, normally the complement of $Q$. The equations are

$$
Q=\neg(R\lor\overline Q),\qquad
\overline Q=\neg(S\lor Q).
$$

These describe the stable relationships between signals; how the circuit evolves also depends on its previous state and on gate delays.

**Setting.** Assert $S=1$ with $R=0$. The second NOR receives a 1, forcing $\overline Q=0$. The first gate then produces $Q=\neg(0\lor0)=1$. Release $S$ back to 0: the outputs stay at $Q=1$, $\overline Q=0$. The input that established the state is gone, but the feedback maintains it.

**Resetting.** Assert $R=1$ with $S=0$. The first NOR forces $Q=0$, and the second produces $\overline Q=1$. When $R$ returns to 0, the circuit holds $Q=0$, $\overline Q=1$.

### The State Table

Unlike a combinational truth table, a state table must say how the output depends on the previous state.

| $S$ | $R$ | $Q_{\text{next}}$ | Operation |
|---|---|---|---|
| 0 | 0 | $Q$ | Hold |
| 0 | 1 | 0 | Reset |
| 1 | 0 | 1 | Set |
| 1 | 1 | Invalid | Forbidden input combination |

The key row is $S=R=0$. For identical inputs, the output may be 0 or 1, depending on history. That is precisely what separates a sequential circuit from a combinational one.

### Why Is $S=R=1$ Forbidden?

With both inputs at 1, both NOR gates are forced to 0, so $Q=\overline Q=0$ and the outputs are no longer complements. Worse, if both inputs return to 0 at the same time, the final state depends on tiny differences in propagation delay and device behavior. Real hardware may even become metastable. The basic NOR latch should never have Set and Reset asserted together.

### Memory from Feedback

Neither NOR gate can remember anything alone: each computes an ordinary Boolean function of its inputs. But connected through feedback, the pair preserves information. The memory is not in any one gate.

**It emerges from the structure of the circuit.**

Registers, counters and processors all rely on state-holding elements built on this idea.

## Astable, Monostable and Bistable

These terms belong to a broader classification by number of stable states.

| Circuit | Stable states | Characteristic behavior |
|---|---|---|
| Astable | 0 | Continuously changes state |
| Monostable | 1 | Leaves its stable state when triggered, then returns |
| Bistable | 2 | Stays in either state until switched |

A monostable circuit, or *one-shot*, produces a temporary response to a trigger. Our examples cover the other two: the **ring oscillator** is astable and the **SR latch** is bistable.

Both use feedback, yet behave completely differently. The topology and timing of a circuit matter, not just the Boolean functions of its gates.

## Why Feedback Makes Simulation Harder

In an acyclic circuit, a simulator can evaluate gates in dependency order. With feedback there may be no gate whose inputs are independent of the others.

Take the inverter loop $q=\neg q$. If the simulator keeps re-evaluating the gate without advancing time, the value alternates $0,1,0,1,\ldots$ and never settles.

For the SR latch with $S=R=0$, the equations instead admit two stable solutions, $(Q,\overline Q)=(0,1)$ and $(1,0)$. A simulator that knows the previous state can preserve it. If both signals are unknown, the equations alone do not say which solution applies.

These are two distinct problems:

- **Oscillation:** repeated updates may never converge.
- **Ambiguity:** several stable states exist, and the available information does not select one.

A simulator must tell these situations apart rather than pick an answer arbitrarily. With nonzero delays, it must also schedule signal transitions over simulated time.

## When Signals Aren't 0 or 1

Boolean algebra uses 0 and 1. SILICON also represents **X**, an unknown value, and **E**, an error. Both are important when studying feedback.

### Unknown Values

An X means the simulator cannot tell whether a signal is 0 or 1. It does not necessarily propagate: if the known inputs already determine the output, the result is definite.

| Expression | Result |
|---|---|
| $0\land X$ | 0 |
| $1\land X$ | X |
| $1\lor X$ | 1 |
| $0\lor X$ | X |
| $\neg X$ | X |

### Unknown Values and Feedback

Suppose an SR latch has never been set or reset, so both outputs are X. With $S=R=0$,

$$
Q=\neg(0\lor X)=X,\qquad\overline Q=\neg(0\lor X)=X.
$$

The simulator cannot choose one of the two valid stable states without more information. But a Set or Reset establishes a definite state: asserting $S=1$ forces $\overline Q=0$, and then the other gate determines $Q=1$.

So X makes the difference between an *unspecified* state and an *established* state visible.

### Unknown Does Not Mean Don't Care

The `x` in the priority circuit's table (Lesson 4) was a **don't-care**: either 0 or 1 gives the same specified output. A simulated X is different. It means the actual binary value is not known. A circuit may still produce a definite output from an X input if the output does not depend on it.

### Errors

SILICON uses E for an error condition, including a conflict where several components drive the same wire. Unlike X, E signals an invalid simulation. For the gates in these lessons, errors propagate even where a known input would otherwise decide the result:

$$
0\land E=E.
$$

Resolve the source of an error before interpreting downstream values.

### High Impedance

Hardware description languages also use **Z**, high impedance: an output electrically disconnected from its wire. This lets several drivers share a line by taking turns. SILICON's four-valued model does not represent Z, but the concept is worth knowing for real hardware.

## Lab: Feedback in SILICON

See [SILICON Basics](../guide/silicon.md) for the editor's general workflow. Use one-bit inputs and outputs, and inspect intermediate signals.

### Experiment 1: An Astable Circuit

Build a ring of three NOT gates: connect each output to the next gate's input, and the last output to the first input. Observe the simulation. The important question is not whether a wire changes, but whether the circuit can reach a stable binary configuration.

1. Examine the outputs with the available propagation-delay settings.
2. If supported, initialize a signal to a known binary value and watch how transitions propagate.
3. Compare the behavior when the ring starts with unknown signals.
4. Replace the three-inverter loop with a two-inverter loop.

**Questions**

- Why is there no stable Boolean assignment for three inverters?
- Why are there two for two inverters?
- What is the effect of propagation delay?
- Why might an unknown initial state remain unknown instead of oscillating?

::: tip Simulation Note
The observable result depends on how the simulation kernel handles feedback, delays, initialization and nonconvergence. Do not assume a Boolean simulator will spontaneously start a physical oscillation.
:::

### Experiment 2: Building a Bistable Latch

Construct an SR latch from two NOR gates, connecting each output to an input of the other. Use separate inputs $S$ and $R$ and observe both $Q$ and $\overline Q$.

Apply the sequence below and record what you see.

| $S$ | $R$ | Expected behavior |
|---|---|---|
| 1 | 0 | Set: $Q=1$, $\overline Q=0$ |
| 0 | 0 | Hold |
| 0 | 1 | Reset: $Q=0$, $\overline Q=1$ |
| 0 | 0 | Hold |

Finally, examine the invalid combination $S=R=1$ and consider what may happen when both inputs are released.

**Questions**

- What lets the output stay unchanged after Set or Reset is released?
- Can a combinational truth table describe the latch completely?
- Why might the initial state be X?
- What makes simultaneous Set and Reset invalid?

### Experiment 3: Remembering an Alarm

Return to the security door. We now want the alarm to stay active until explicitly reset. Define

$$
S=s(\neg a\lor b),
$$

connect it to the Set input of an SR latch, add a separate Reset input $R$, and use $Q$ as the alarm.

The system has two parts:

1. A **combinational circuit** that decides whether an alarm-triggering condition exists.
2. A **sequential circuit** that remembers whether the alarm has been triggered.

Test this sequence:

| Step | Trigger condition $S$ | Reset $R$ | Alarm $Q$ |
|---|---|---|---|
| 1 | 0 | 1 | 0 |
| 2 | 0 | 0 | 0 |
| 3 | 1 | 0 | 1 |
| 4 | 0 | 0 | 1 |
| 5 | 0 | 1 | 0 |

Look at step 4: the trigger has disappeared, but the alarm remains active. This was impossible for our original combinational design.

Make sure Set and Reset are never asserted together. What should happen if someone presses Reset while the intruder is still present? Think about how you might guarantee that Reset wins.

## What We Can Do Now

We saw that some requirements cannot be met by any combinational circuit, because the output must depend on history. Feedback gives gates a way to depend on their own past.

Depending on its structure, a feedback loop can oscillate (the **ring oscillator**, astable) or hold one of two states (the **SR latch**, bistable). The latch shows that memory can emerge from gates that individually compute ordinary Boolean functions. And because feedback breaks the dependency order of simple circuits, simulation needs care and extra values such as X.

Three ideas now organize digital design:

- **Function:** what result a circuit computes.
- **Structure:** how its components are connected.
- **State:** what information a circuit retains from previous events.

Next, we return to combinational logic with a more systematic simplification technique, **Karnaugh maps**. Later we will build on the latch to study flip-flops, registers and clocked systems.

## Try It Yourself

1. **Combinational or sequential?** Classify an adder, an SR latch, a multiplexer and a counter. What distinguishes the two categories?

2. **Feedback and stability.** Determine the stable Boolean assignments, if any, of a ring containing one, two, three or four NOT gates.

3. **The SR latch.** Starting from $Q=0$, describe the output after the input sequence Set → Hold → Reset → Hold → Set.

4. **A persistent alarm.** Why can't the original security door function remember an intrusion? What additional state does the latch provide?

5. **Unknown states.** Why may a Boolean simulator leave an uninitialized SR latch at X even though the circuit has two valid stable states?

6. **Timing.** A three-inverter ring oscillator has an idealized delay of $2\,\mathrm{ns}$ per inverter. Estimate its period and frequency with the simple delay model.

7. **Design challenge.** Create a circuit that remembers whether either of two sensors has been activated, cleared by a Reset input. Which part is combinational and which is sequential?

8. **Reset priority.** Modify the persistent alarm so that Reset always wins if Set and Reset are requested together. Which gate would you add, and on which signal?
