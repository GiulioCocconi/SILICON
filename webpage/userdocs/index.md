<!--
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
-->

# Getting Started

SILICON is an open source suite for simulating digital circuits, finite state machines, and microcontrollers. The current user-facing workflow starts with LogiFlow, the graphical logic editor.

## Install SILICON

1. Open the main website.
2. Go to **Download**.
3. Pick the release or nightly build for your platform.
4. Start SILICON from the downloaded package.

::: tip
SILICON is currently pre-alpha software. Prefer saving small projects often while testing new workflows.
:::

## Create a Project

1. Start SILICON.
2. Create a new project or open an existing one.
3. Choose the LogiFlow workspace when you want to design digital logic circuits.
4. Save the project before building larger circuits.

## First Circuit

Start with a small combinational circuit:

1. Place two input components.
2. Place one logic gate.
3. Place one output component.
4. Connect each input to the gate.
5. Connect the gate result to the output.
6. Toggle the inputs and verify that the output updates as expected.

## Next Steps

- Read [LogiFlow Basics](./guide/logiflow.md) for editor and simulation concepts.
- Read [Silicon Internals](./guide/internals.md) for simulator, project-format,
  waveform, Yosys, and native-component contributor guidance.
- Read [Troubleshooting](./guide/troubleshooting.md) when a project does not behave as expected.
- Use the internal API docs only when you are extending SILICON itself.
