/* Homepage bootstrap. Documentation sites use their native theme controllers. */
(() => {
  const key = "vitepress-theme-appearance";
  const media = matchMedia("(prefers-color-scheme: dark)");
  let preference = "auto";
  const read = () => {
    try { preference = localStorage.getItem(key) || "auto"; } catch { /* Use system. */ }
    if (!["auto", "light", "dark"].includes(preference)) preference = "auto";
  };
  const apply = () => {
    document.documentElement.dataset.themePreference = preference;
    document.documentElement.classList.toggle("dark",
      preference === "dark" || (preference === "auto" && media.matches));
    window.dispatchEvent(new Event("silicon-theme-change"));
  };
  read();
  apply();
  media.addEventListener("change", apply);
  addEventListener("storage", (event) => {
    if (event.key === key || event.key === null) { read(); apply(); }
  });
  addEventListener("silicon-theme-select", (event) => {
    preference = event.detail;
    try { localStorage.setItem(key, preference); } catch { /* Session-only choice. */ }
    apply();
  });
})();
