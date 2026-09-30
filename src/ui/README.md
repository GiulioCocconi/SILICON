# UI layout

- `circuit/` owns the graphical circuit editor: components, diagram scene, routing, and editing controls.
- `documents/` owns editors and views for project documents, including code, binaries, and ISA architectures.
- `project/` owns the `.sil` project workflow, document tree, and project file actions. A project contains circuit JSON documents and may contain other document types.
- `shell/` owns the main window, menus, dialogs, appearance, and application settings.
- `waveform/` owns the waveform viewer and its window controller.
- `serialization/` connects UI objects to document serialization and component registration.

The data models and `.sil` archive format live under `src/core/`.
