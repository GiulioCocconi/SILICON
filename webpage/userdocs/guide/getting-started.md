# Getting Started

SILICON is an open source application for designing and simulating digital circuits. A project contains circuit JSON documents and can also contain code, architecture, and binary documents.

## Install SILICON

1. Open the main website.
2. Go to **Download**.
3. Pick the release or nightly build for your platform.
4. Start SILICON from the downloaded package.

To run SILICON in a browser, use **Run SILICON online** on the website or
choose **Stable** or **Unstable** on the Download page. Stable is recommended
when available; Unstable runs the latest successful main snapshot.

::: tip
SILICON is currently pre-alpha software. Prefer saving small projects often while testing new workflows.
:::

## Create a Project

1. Start SILICON.
2. Create a new project or open an existing `.sil` project archive.
3. Add circuit documents to the project to design digital logic.
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

- Read [SILICON Basics](./silicon.md) for editor and simulation concepts.
- Read [Troubleshooting](./troubleshooting.md) when a project does not behave as expected.
- Use the internal API docs only when you are extending SILICON itself.
