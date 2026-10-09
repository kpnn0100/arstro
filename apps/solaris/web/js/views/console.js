/*
 * Solaris — console.js: the command line (R-SVC-7: "sends the same command lines"). Whatever an agent
 * types into solaris-cc, a person types here: the line goes to the service as it is, its output comes
 * back as solaris-cc prints it, a refusal in the service's own words, and the session's event lines
 * (every page's edits and the window's, if one shares the song) scroll past as --watch shows them.
 * The usage of the verb being typed is the grammar table's (`commands`), never a copy.
 */
import { h } from "./util.js";

const MAX_ROWS = 400;

export function cmdConsole(vm) {
  const log = h("div.console-log.scroll.mono");
  const input = h("input.console-input.mono", {
    placeholder: "a command line — e.g.  clip add --instrument drums --at 0 --length 8", spellcheck: "false", autocomplete: "off",
  });
  const hint = h("div.console-hint.mono");
  let specs = [];
  let histAt = -1;

  const push = (kind, text) => {
    const stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
    for (const line of String(text).replace(/\n$/, "").split("\n")) log.append(h("div.row." + kind, line));
    while (log.childElementCount > MAX_ROWS) log.firstElementChild.remove();
    if (stick) log.scrollTop = log.scrollHeight;
  };

  vm.session.on("*", (name, data) => push(name === "command.rejected" || name === "error" ? "evt.bad" : "evt", (data && data.line) || name));

  const usageFor = (text) => {
    const t = text.trim().replace(/\s+/g, " ");
    if (!t) return "";
    let best = null;
    for (const s of specs) if ((t + " ").startsWith(s.name + " ") && (!best || s.name.length > best.name.length)) best = s;
    if (best) return best.usage + "  —  " + best.summary;
    const near = specs.filter((s) => s.name.startsWith(t)).slice(0, 4).map((s) => s.usage);
    return near.join("   ·   ");
  };
  input.addEventListener("input", () => { hint.textContent = usageFor(input.value); });
  input.addEventListener("keydown", (e) => {
    const hist = vm.view.history.peek();
    if (e.key === "ArrowUp" && hist.length) {
      histAt = histAt < 0 ? hist.length - 1 : Math.max(0, histAt - 1);
      input.value = hist[histAt];
      e.preventDefault();
    } else if (e.key === "ArrowDown" && histAt >= 0) {
      histAt = histAt + 1 < hist.length ? histAt + 1 : -1;
      input.value = histAt < 0 ? "" : hist[histAt];
      e.preventDefault();
    } else if (e.key === "Enter") {
      const line = input.value.trim();
      if (!line) return;
      input.value = "";
      hint.textContent = "";
      histAt = -1;
      vm.view.history.value = hist.concat([line]).slice(-100);
      push("in", "> " + line);
      vm.run(line, { quiet: true }).then(
        (r) => { if (r && r.output) push("out", r.output); },
        (err) => push("bad", "refused: " + (err && err.message ? err.message : err)));
    }
  });
  // the grammar, once the session runs (and again after a restart of the app)
  const load = () => vm.session.commands().then((c) => { specs = c || []; }).catch(() => {});
  vm.session.bridge.onReady(load);

  return h("div.console", log, h("div.console-bar", h("span.prompt.mono", "›"), input), hint);
}
