"use strict";

const SVG_NS = "http://www.w3.org/2000/svg";
const $ = (id) => document.getElementById(id);
const sessionToken = new URLSearchParams(location.search).get("token") || "";
const documents = [];
const selectableTags = new Set([
  "svg", "g", "path", "rect", "circle", "ellipse", "line", "polyline",
  "polygon", "text", "tspan", "textPath", "use", "image", "symbol",
  "defs", "clipPath", "mask", "pattern", "marker",
]);
let palette;
let activeIndex = -1;
let theme = "light";
let previewElements = [];
let selectionBox;
let saving = false;
let frameReady = false;

function status(message, error = false) {
  $("status").textContent = message;
  $("status").dataset.error = String(error);
}

async function api(path, body) {
  const response = await fetch(path, {
    method: body === undefined ? "GET" : "POST",
    headers: { "X-Normalize-Token": sessionToken, "Content-Type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  if (!response.ok) {
    const error = await response.json();
    throw new Error(error.error || `Request failed (${response.status})`);
  }
  return response;
}

function elements(root) {
  return [root, ...root.querySelectorAll("*")];
}

function activeDocument() {
  return documents[activeIndex];
}

function signature(doc) {
  return JSON.stringify([...doc.edits].sort(([a], [b]) => a - b));
}

function dirty(doc) {
  return signature(doc) !== doc.savedSignature;
}

function elementLabel(element) {
  return `<${element.localName}>${element.id ? ` #${element.id}` : ""}`;
}

function addDocument(name, svg, sanitized = false) {
  const parsed = new DOMParser().parseFromString(svg, "image/svg+xml");
  if (parsed.querySelector("parsererror") || parsed.documentElement.namespaceURI !== SVG_NS) {
    throw new Error(`${name}: invalid SVG`);
  }
  const root = parsed.documentElement;
  documents.push({ name, root, originals: elements(root), edits: new Map(),
    selected: null, savedSignature: "[]", sanitized });
}

function setPaint(element, property, value) {
  // Presentation attributes cannot override inline styles or CSS rules.
  element.setAttribute(property, value);
  element.style.setProperty(property, value, "important");
}

function buildSVG(doc, selectedTheme) {
  const root = doc.root.cloneNode(true);
  const nodes = elements(root);
  for (const [index, changes] of doc.edits) {
    for (const [property, value] of Object.entries(changes)) {
      const element = nodes[index];
      if (value === "none") {
        element.removeAttribute(`data-silicon-${property}`);
        setPaint(element, property, "none");
      } else {
        element.setAttribute(`data-silicon-${property}`, value);
      }
    }
  }
  for (const element of nodes) {
    for (const property of ["fill", "stroke"]) {
      const color = palette[selectedTheme][element.getAttribute(`data-silicon-${property}`)];
      if (color) setPaint(element, property, color);
    }
  }
  return root;
}

function refreshFileList() {
  $("file-count").textContent = documents.length;
  $("files-empty").hidden = documents.length > 0;
  $("files").replaceChildren();
  documents.forEach((doc, index) => {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.textContent = `${doc.name}${dirty(doc) ? " •" : ""}`;
    button.title = doc.name;
    button.setAttribute("aria-current", String(index === activeIndex));
    button.addEventListener("click", () => activate(index));
    item.append(button);
    $("files").append(item);
  });
  const doc = activeDocument();
  $("save").disabled = !doc || saving;
  $("save-all").disabled = !documents.length || saving;
  $("overwrite").disabled = saving;
  $("reset").disabled = !doc || !doc.edits.size;
  $("active-name").textContent = doc ? doc.name : "No SVG selected";
  $("save-state").textContent = doc && dirty(doc) ? "Unsaved changes" : "No unsaved changes";
}

function refreshTree() {
  $("elements").replaceChildren();
  const doc = activeDocument();
  if (!doc) return;
  doc.originals.forEach((element, index) => {
    if (element.namespaceURI !== SVG_NS || !selectableTags.has(element.localName)) return;
    const button = document.createElement("button");
    button.textContent = `<${element.localName}>`;
    if (element.id) {
      const label = document.createElement("span");
      label.className = "element-id";
      label.textContent = `#${element.id}`;
      button.append(label);
    }
    let depth = 0;
    for (let parent = element.parentElement; parent; parent = parent.parentElement) depth++;
    button.style.paddingLeft = `${8 + Math.min(depth, 8) * 12}px`;
    button.title = elementLabel(element);
    button.dataset.index = index;
    button.setAttribute("aria-pressed", String(index === doc.selected));
    button.addEventListener("click", () => selectElement(index));
    $("elements").append(button);
  });
}

function activate(index) {
  activeIndex = index;
  refreshFileList();
  refreshTree();
  renderPreview();
  const doc = activeDocument();
  status(doc.sanitized
    ? `${doc.name}: removed scripts, animation, embedded HTML, event handlers, or external links for a static preview.`
    : `${doc.name}: select an element to assign palette colors.`);
}

function renderPreview() {
  const doc = activeDocument();
  if (!doc || !frameReady) return;
  $("empty-preview").hidden = true;
  $("preview").hidden = false;
  const frame = $("preview").contentDocument;
  const root = frame.importNode(buildSVG(doc, theme), true);
  previewElements = elements(root);
  const indexes = new WeakMap(previewElements.map((element, index) => [element, index]));

  // SVGs without a viewBox still need a coordinate system for a fitted preview.
  if (!root.hasAttribute("viewBox")) {
    const length = (name, fallback) => {
      const value = root[name]?.baseVal;
      return value && value.unitType !== 2 && value.value > 0 ? value.value : fallback;
    };
    root.setAttribute("viewBox", `0 0 ${length("width", 300)} ${length("height", 150)}`);
  }
  root.setAttribute("preserveAspectRatio", "xMidYMid meet");
  root.style.setProperty("width", "100%", "important");
  root.style.setProperty("height", "100%", "important");
  root.style.setProperty("display", "block", "important");
  root.style.setProperty("position", "static", "important");
  root.style.setProperty("color", palette[theme].SILICON_INK);

  selectionBox = frame.createElement("div");
  // Inline important styles keep SVG stylesheets from changing the overlay.
  selectionBox.style.cssText = "all:initial!important;position:fixed!important;pointer-events:none!important;" +
    "border:2px solid " + palette[theme].SILICON_BLUE + "!important;box-sizing:border-box!important;" +
    "border-radius:3px!important;z-index:2147483647!important;display:none!important";
  frame.body.replaceChildren(root, selectionBox);
  frame.body.style.background = palette[theme].SILICON_BACKGROUND;
  frame.onclick = (event) => {
    event.preventDefault();
    let target = event.target;
    while (target && target !== frame.body) {
      const index = indexes.get(target);
      if (index !== undefined && selectableTags.has(target.localName)) {
        selectElement(index);
        return;
      }
      target = target.parentElement;
    }
  };
  // Clicking should work for paths with pointer-events:none as well.
  for (const element of previewElements) {
    if (selectableTags.has(element.localName)) element.style.setProperty("pointer-events", "visiblePainted", "important");
  }
  refreshInspector();
  requestAnimationFrame(refreshSelectionBox);
}

function selectElement(index) {
  const doc = activeDocument();
  if (!doc) return;
  doc.selected = index;
  for (const button of $("elements").children) {
    button.setAttribute("aria-pressed", String(Number(button.dataset.index) === index));
  }
  refreshInspector();
  refreshSelectionBox();
}

function refreshSelectionBox() {
  const doc = activeDocument();
  const element = doc && previewElements[doc.selected];
  if (!selectionBox || !element) {
    if (selectionBox) selectionBox.style.setProperty("display", "none", "important");
    return;
  }
  const rect = element.getBoundingClientRect();
  const styles = {
    display: rect.width || rect.height ? "block" : "none",
    left: `${rect.left - 3}px`, top: `${rect.top - 3}px`,
    width: `${Math.max(6, rect.width + 6)}px`, height: `${Math.max(6, rect.height + 6)}px`,
  };
  for (const [property, value] of Object.entries(styles)) selectionBox.style.setProperty(property, value, "important");
}

function originalPaint(element, property) {
  const inline = element.style.getPropertyValue(property);
  const attribute = element.getAttribute(property);
  if (inline) return `${inline} (inline style)`;
  if (attribute !== null) return attribute;
  return "Inherited / stylesheet / SVG default";
}

function refreshInspector() {
  const doc = activeDocument();
  const original = doc && doc.originals[doc.selected];
  const element = doc && previewElements[doc.selected];
  $("paint-controls").hidden = !element;
  $("selection-hint").hidden = Boolean(element);
  $("selection-label").textContent = original ? elementLabel(original) : "Select an element";
  if (!element) return;
  const computed = $("preview").contentWindow.getComputedStyle(element);
  for (const property of ["fill", "stroke"]) {
    const token = element.getAttribute(`data-silicon-${property}`);
    const edit = doc.edits.get(doc.selected)?.[property];
    const select = $(`${property}-select`);
    const currentValue = computed.getPropertyValue(property);
    $(`${property}-current`).textContent = currentValue;
    $(`${property}-swatch`).style.background = currentValue === "none" || currentValue.startsWith("url(")
      ? "repeating-conic-gradient(#ddd 0% 25%, white 0% 50%) 0 / 8px 8px" : currentValue;
    $(`${property}-original`).textContent = originalPaint(original, property);
    select.replaceChildren();
    const option = (value, text) => {
      const node = document.createElement("option");
      node.value = value;
      node.textContent = text;
      select.append(node);
    };
    option("original", "Original paint / restore original");
    option("none", "None (no paint)");
    for (const [name, color] of Object.entries(palette[theme])) option(name, `${name}  ${color}`);
    if (token && !palette[theme][token]) option(token, `${token} (unknown token)`);
    select.value = edit || token || "original";
    $(`${property}-mapping`).textContent = token
      ? `data-silicon-${property}="${token}"${palette[theme][token] ? "" : " — not in palette"}`
      : "No Silicon palette mapping";
    const swatches = $(`${property}-palette`);
    swatches.replaceChildren();
    for (const [name, color] of Object.entries(palette[theme])) {
      const button = document.createElement("button");
      button.style.background = color;
      button.title = `${name} · ${color}`;
      button.setAttribute("aria-label", `${property}: ${name} (${color})`);
      button.setAttribute("aria-pressed", String(name === token));
      button.addEventListener("click", () => assignPaint(property, name));
      swatches.append(button);
    }
  }
}

function assignPaint(property, value) {
  const doc = activeDocument();
  if (!doc || doc.selected === null) return;
  if (!["fill", "stroke"].includes(property)) return;
  if (value !== "original" && value !== "none" && !palette[theme][value]) return;
  const changes = { ...doc.edits.get(doc.selected) };
  if (value === "original") delete changes[property];
  else changes[property] = value;
  if (Object.keys(changes).length) doc.edits.set(doc.selected, changes);
  else doc.edits.delete(doc.selected);
  renderPreview();
  refreshFileList();
  status(value === "original" ? `Restored original ${property}.`
    : value === "none" ? `Removed ${property} and its palette mapping.`
      : `Assigned ${value} to ${property}; Silicon metadata will be included in the saved SVG.`);
}

function applyTheme() {
  theme = $("theme").value;
  document.documentElement.dataset.theme = theme;
  document.documentElement.style.colorScheme = theme;
  const aliases = { ink: "INK", background: "BACKGROUND", surface: "SURFACE", raised: "SURFACE_RAISED",
    grid: "GRID", muted: "GREY", accent: "BLUE", selected: "SELECTED" };
  for (const [property, token] of Object.entries(aliases)) {
    document.documentElement.style.setProperty(`--${property}`, palette[theme][`SILICON_${token}`]);
  }
  renderPreview();
}

async function openFiles(event) {
  const files = Array.from(event.target.files);
  event.target.value = "";
  event.target.disabled = true;
  const firstNew = documents.length;
  const failures = [];
  for (const [index, file] of files.entries()) {
    try {
      status(`Opening ${index + 1}/${files.length}: optimizing ${file.name} with SVGO…`);
      if (file.size > 10 * 1024 * 1024) throw new Error("SVG exceeds the 10 MiB size limit");
      const response = await api("/api/import", { svg: await file.text() });
      const imported = await response.json();
      addDocument(file.name, imported.svg, imported.sanitized);
      $("dependency").hidden = true;
    } catch (error) {
      failures.push(`${file.name}: ${error.message}`);
      if (error.message.includes("SVGO is required")) $("dependency").hidden = false;
    }
  }
  event.target.disabled = false;
  if (documents.length > firstNew) activate(firstNew);
  if (failures.length) status(failures.join("; "), true);
}

function snapshotDocument(doc) {
  return {
    doc, name: doc.name, signature: signature(doc),
    svg: new XMLSerializer().serializeToString(buildSVG(doc, "light")),
  };
}

function exportName(name, used = new Set(), overwrite = false, reserved = new Set()) {
  const stem = name.replace(/\.svg$/i, "").replace(/[<>:"/\\|?*\x00-\x1f]/g, "-").replace(/[. ]+$/, "") || "document";
  const base = overwrite ? stem : `${stem}.normalized`;
  const extension = overwrite ? (name.match(/\.svg$/i)?.[0] || ".svg") : ".svg";
  const first = `${base}${extension}`;
  let candidate = first;
  for (let suffix = 2; used.has(candidate.toLowerCase()) ||
    (candidate !== first && reserved.has(candidate.toLowerCase())); suffix++) candidate = `${base}-${suffix}${extension}`;
  used.add(candidate.toLowerCase());
  return candidate;
}

async function writeBlob(handle, blob) {
  const writable = await handle.createWritable();
  try {
    await writable.write(blob);
    await writable.close();
  } catch (error) {
    await writable.abort().catch(() => {});
    throw error;
  }
}

function downloadBlob(blob, name) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}

async function saveAs() {
  const doc = activeDocument();
  if (!doc || saving) return;
  const snapshot = snapshotDocument(doc);
  const overwrite = $("overwrite").checked;
  let handle;
  saving = true;
  refreshFileList();
  try {
    // Open the picker during the user gesture, before the export request.
    if (window.showSaveFilePicker) {
      handle = await window.showSaveFilePicker({
        suggestedName: exportName(doc.name, new Set(), overwrite),
        types: [{ description: "SVG image", accept: { "image/svg+xml": [".svg"] } }],
      });
    }
    status(`Saving ${doc.name}…`);
    const response = await api("/api/export", { svg: snapshot.svg });
    const blob = await response.blob();
    if (handle) {
      await writeBlob(handle, blob);
      status(`Saved ${doc.name} as an optimized SVG.`);
    } else {
      const name = exportName(doc.name, new Set(), overwrite);
      downloadBlob(blob, name);
      status(`Download requested for ${name}.`);
    }
    doc.savedSignature = snapshot.signature;
  } catch (error) {
    if (error.name === "AbortError") status("Save cancelled; edits retained.");
    else status(`Save failed: ${error.message}`, true);
  } finally {
    saving = false;
    refreshFileList();
  }
}

async function saveAll() {
  if (!documents.length || saving) return;
  const snapshots = documents.map(snapshotDocument);
  const overwrite = $("overwrite").checked;
  saving = true;
  refreshFileList();
  try {
    if (window.showDirectoryPicker) {
      // Request directory access during the click gesture, before any network call.
      const directory = await window.showDirectoryPicker({ mode: "readwrite" });
      const reserved = new Set(snapshots.map(snapshot => exportName(snapshot.name, new Set(), true).toLowerCase()));
      const used = overwrite ? new Set() : new Set(reserved);
      if (!overwrite) {
        for await (const [name] of directory.entries()) used.add(name.toLowerCase());
      }
      const failures = [];
      let saved = 0;
      for (const [index, snapshot] of snapshots.entries()) {
        status(`Saving ${index + 1}/${snapshots.length}: ${snapshot.name}…`);
        try {
          const response = await api("/api/export", { svg: snapshot.svg });
          const blob = await response.blob();
          const handle = await directory.getFileHandle(exportName(snapshot.name, used, overwrite, reserved), { create: true });
          await writeBlob(handle, blob);
          snapshot.doc.savedSignature = snapshot.signature;
          saved++;
        } catch (error) {
          failures.push(`${snapshot.name}: ${error.message}`);
        }
        refreshFileList();
      }
      status(`Saved ${saved}/${snapshots.length} SVGs to ${directory.name}.` +
        (failures.length ? ` Failed: ${failures.join("; ")}. Edits retained for failed files.` : ""), Boolean(failures.length));
    } else {
      status(`Preparing ${snapshots.length} SVGs for download…`);
      const response = await api("/api/export-all", {
        files: snapshots.map(({ name, svg }) => ({ name, svg })),
        overwrite,
      });
      downloadBlob(await response.blob(), "silicon-normalized-svgs.zip");
      for (const snapshot of snapshots) snapshot.doc.savedSignature = snapshot.signature;
      status(`Download requested: silicon-normalized-svgs.zip (${snapshots.length} optimized SVGs).`);
    }
  } catch (error) {
    if (error.name === "AbortError") status("Save All cancelled; edits retained.");
    else status(`Save All failed: ${error.message}. Edits retained.`, true);
  } finally {
    saving = false;
    refreshFileList();
  }
}

async function initialize() {
  try {
    const response = await api("/api/state");
    const state = await response.json();
    palette = state.palette;
    $("dependency").hidden = state.svgo;
    for (const file of state.files) addDocument(file.name, file.svg, file.sanitized);
    $("open-files").addEventListener("change", openFiles);
    $("theme").addEventListener("change", applyTheme);
    $("save").addEventListener("click", saveAs);
    $("save-all").addEventListener("click", saveAll);
    for (const property of ["fill", "stroke"]) {
      $(`${property}-select`).addEventListener("change", (event) => assignPaint(property, event.target.value));
    }
    $("reset").addEventListener("click", () => {
      const doc = activeDocument();
      if (!doc || !confirm(`Discard all edits to ${doc.name} and restore the opened SVG?`)) return;
      doc.edits.clear();
      refreshFileList();
      renderPreview();
      status(`Restored ${doc.name} to its opened state.`);
    });
    window.addEventListener("beforeunload", (event) => {
      if (documents.some(dirty)) { event.preventDefault(); event.returnValue = ""; }
    });
    new ResizeObserver(refreshSelectionBox).observe($("preview"));
    $("preview").addEventListener("load", () => {
      frameReady = true;
      renderPreview();
    });
    // SVG styles remain inside this sandboxed frame; scripts and remote loads
    // are blocked even if a document contains a stylesheet or embedded URL.
    $("preview").srcdoc = '<!doctype html><html><head><meta http-equiv="Content-Security-Policy" ' +
      'content="default-src \'none\'; script-src \'none\'; style-src \'unsafe-inline\'; img-src data:; ' +
      'font-src data:; base-uri \'none\'; form-action \'none\'"><style>' +
      'html{height:100%;overflow:hidden}body{margin:0;padding:32px;height:100%;box-sizing:border-box;overflow:hidden}' +
      '</style></head><body></body></html>';
    applyTheme();
    if (documents.length) activate(0);
    else status("Open one or more SVGs to begin.");
  } catch (error) {
    status(`Unable to start editor: ${error.message}`, true);
  }
}

initialize();
