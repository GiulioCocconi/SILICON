# UI layout

- `circuit/` owns the graphical circuit editor: components, diagram scene, routing, and editing controls.
- `documents/` owns editors and views for project documents, including code, binaries, and ISA architectures.
- `project/` owns the `.sil` project workflow, document tree, and project file actions. A project contains circuit JSON documents and may contain other document types.
- `shell/` owns the main window, menus, dialogs, appearance, and application settings.
- `waveform/` owns the waveform viewer and its window controller.
- `serialization/` connects UI objects to document serialization and component registration.

The data models and `.sil` archive format live under `src/core/`.

## Ownership and callbacks

Qt parents own ordinary child widgets, layouts, and actions. Graphics scenes
and parent items own their graphics items. Raw pointers to those objects are
borrowed. Keep those ownership trees unless an object must be released before
its parent or before other C++ state disappears.

Use value members or `std::unique_ptr` for independent and ordered ownership.
Declare dependencies before their consumers: C++ members are destroyed in reverse
declaration order. An explicitly owned Qt object may still have a parent for
presentation, provided its C++ owner always releases it before that parent dies.
Do not combine explicit ownership with `WA_DeleteOnClose` or `deleteLater()`.

Use `CallbackScope` for connections that access derived-class or adapter state.
Declare the scope after the state and owners those callbacks use. Its QObject
context disconnects signals and discards queued deliveries before member or Qt
child teardown begins. `CallbackScope::guard()` also protects callbacks retained
outside Qt's signal system. A `QMetaObject::Connection` handle does not disconnect
when it goes out of scope.

Dialog helpers accept an optional callback context independently of their widget
parent. Supply the controller's context when a dialog can outlive its controller;
otherwise the parent is the default callback owner. Event filters and browser
listeners likewise need scoped registrations when their lifetime must end before
their Qt owner is destroyed.

Default ordinary UI destructors where ownership and member order suffice. Keep
custom destructors in resource owners that must unregister, synchronize, or finish
framework interactions. `WindowLifetime` contains Qt's dock and toolbar drag
cleanup so it runs while the main-window layout is still intact.
