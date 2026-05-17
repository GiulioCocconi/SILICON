# SILICON Formatting Showcase

![Sample Circuit](/public/sample_image.jpg)

This page is placeholder documentation for testing the visual treatment of common VitePress content patterns in the SILICON docs theme.

Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer wire bus, nunc at porta tempor, justo lectus fermentum neque, sed porta sem sapien at ipsum. **Bold signal paths** should remain easy to scan, and _emphasized editor hints_ should sit comfortably inside normal paragraphs.

Use this page when changing theme styles. It deliberately mixes headings, lists, tables, custom blocks, code fences, badges, and detail sections.

## Status Badges

LogiFlow <Badge type="tip" text="ready" /> FSM Suite <Badge type="warning" text="planned" /> Microcontrollers <Badge type="danger" text="experimental" />

## Quick Links

- [Getting Started](./getting-started.md) covers the first user workflow.
- [LogiFlow Basics](./logiflow.md) explains editor concepts.
- [Troubleshooting](./troubleshooting.md) collects common failure checks.
- [GitHub](https://github.com/GiulioCocconi/SILICON) is the source repository.
- <ParentLink to="">Parent Link Test</ParentLink>

## Ordered Flow

1. Create a project.
2. Place input and output components.
3. Connect gates with wires.
4. Run the simulation.
5. Save the circuit before trying larger edits.

## Task Checklist

- [x] Render Markdown content.
- [x] Show custom VitePress blocks.
- [x] Show code groups and line highlights.
- [ ] Replace placeholder text with final product documentation.

## Signal Matrix

| Input A | Input B | Gate | Output |
| --- | --- | --- | --- |
| 0 | 0 | AND | 0 |
| 0 | 1 | AND | 0 |
| 1 | 0 | AND | 0 |
| 1 | 1 | AND | 1 |

## Custom Blocks

::: info
Info blocks can hold neutral notes about SILICON behavior, build versions, or platform support.
:::

::: tip
Tip blocks should highlight practical user guidance, such as saving a project before testing a work-in-progress component.
:::

::: warning
Warning blocks should call out unstable workflows, version compatibility, or data-loss risks.
:::

::: danger
Danger blocks should be rare and reserved for destructive actions or known severe issues.
:::

::: details Click to inspect a placeholder circuit note
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aenean latch output remains stable while the enable signal is low, then updates when the simulated clock path changes.
:::

## Code

Inline code like `Circuit`, `Wire`, `Component`, and `LogiFlow` should remain legible inside paragraphs.

```cpp{4-7}
class ExampleGate {
public:
  bool evaluate(bool a, bool b) const {
    const bool left = a;
    const bool right = b;
    return left && right;
  }
};
```

::: code-group
```cpp [gate.cpp]
bool and_gate(bool a, bool b) {
  return a && b;
}
```
```json [project.silicon]
{
  "name": "lorem-ipsum-circuit",
  "components": ["input", "and", "output"],
  "simulation": "interactive"
}
```
```sh [terminal]
silicon --open lorem-ipsum-circuit.sil
```
:::

## Quote

> Digital logic documentation should make state, signal direction, and expected output visible before users need to debug.

## Definition-Style Notes

<dl>
  <dt>Component</dt>
  <dd>A placed object in the circuit scene, such as an input, gate, flip-flop, or output.</dd>
  <dt>Wire</dt>
  <dd>A connection between component ports that carries a simulated signal.</dd>
  <dt>Project</dt>
  <dd>A saved SILICON workspace containing circuit data and editor metadata.</dd>
</dl>

## Long Form Placeholder

Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur non gravida neque. Nulla facilisi. Donec sit amet arcu vitae arcu posuere interdum. Sed euismod, justo at faucibus imperdiet, sapien turpis maximus magna, non luctus massa nisl non purus.

Praesent porta, arcu sed dictum vehicula, sapien magna elementum urna, vitae pulvinar justo neque non libero. Integer tempor neque vel dui tempor, vitae placerat neque pellentesque. Etiam convallis nisi in dolor finibus, sit amet bibendum lacus cursus.
