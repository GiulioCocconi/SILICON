import { useSyncExternalStore } from "react";

const subscribe = (update: () => void) => {
  window.addEventListener("silicon-theme-change", update);
  return () => window.removeEventListener("silicon-theme-change", update);
};
const snapshot = () => document.documentElement.dataset.themePreference || "auto";

export function ThemeSelect() {
  const preference = useSyncExternalStore(subscribe, snapshot, () => "auto");
  return (
    <select aria-label="Color theme" value={preference}
      onChange={(event) => window.dispatchEvent(new CustomEvent("silicon-theme-select", { detail: event.target.value }))}
      className="rounded-md border-2 border-foreground bg-card text-foreground px-1 py-2 text-xs">
      <option value="auto">System</option>
      <option value="light">Light</option>
      <option value="dark">Dark</option>
    </select>
  );
}
