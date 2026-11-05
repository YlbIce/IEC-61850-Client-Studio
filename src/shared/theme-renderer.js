(() => {
  const api = window.appearance;
  if (!api) return;
  let current = api.initial;
  const apply = (value) => {
    if (!value) return;
    current = value;
    document.documentElement.dataset.theme = value.resolved;
    document.documentElement.dataset.themeSource = value.source;
    document.documentElement.style.colorScheme = value.resolved;
    document
      .querySelectorAll(".theme-picker")
      .forEach((select) => (select.value = value.source));
  };
  apply(current);
  api.onChange(apply);
  document.addEventListener("DOMContentLoaded", () => {
    const host =
      document.querySelector(".toolbar-right") ||
      document.querySelector("header");
    if (!host) return;
    const select = document.createElement("select");
    select.className = "theme-picker";
    select.setAttribute("aria-label", "界面主题");
    select.title = "界面主题";
    for (const [value, text] of [
      ["light", "亮色"],
      ["dark", "暗色"],
      ["system", "跟随系统"],
    ]) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = text;
      select.append(option);
    }
    select.value = current.source;
    select.onchange = async () => {
      select.disabled = true;
      try {
        apply(await api.set(select.value));
      } catch (e) {
        select.value = current.source;
        window.alert(e.message);
      } finally {
        select.disabled = false;
      }
    };
    host.append(select);
  });
})();
