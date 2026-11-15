import test from "node:test";
import assert from "node:assert/strict";
import { buildEntries, pageEntries, actionEntries, indexEntries, rankEntries, escapeHtml } from "../theme/assets/theme/js/palette.js";

const pages = [
  { title: "install", href: "install/", workspace: "docs" },
  { title: "persistent sessions", href: "persistent/", workspace: "docs" },
];
const actions = [{ label: "toggle theme", action: "theme" }];
const index = {
  docs: [
    { location: "persistent/", title: "persistent sessions", text: "Run the daemon with synchronized output" },
    { location: "persistent/#daemon", title: "Contour daemon on a server", text: "x" },
  ],
};

test("buildEntries: without index gives pages and actions", () => {
  const e = buildEntries(pages, null, actions);
  assert.deepEqual(e.map((x) => x.kind), ["page", "page", "action"]);
  assert.equal(e[2].action, "theme");
  assert.equal(e[0].href, "install/");
});

test("buildEntries: headings and text from the index", () => {
  const e = buildEntries(pages, index, actions);
  const h = e.find((x) => x.kind === "heading");
  assert.equal(h.label, "persistent sessions › Contour daemon on a server");
  assert.equal(h.href, "persistent/#daemon");
  const t = e.find((x) => x.kind === "text");
  assert.equal(t.label, "persistent sessions");
  assert.match(t.hay, /synchronized output/);
  assert.equal(t.text, undefined);
  assert.equal(e.filter((x) => x.kind === "heading").length, 1);
});

test("entry builders: where is derived, never stored; hay is lowercase", () => {
  const [p] = pageEntries(pages);
  assert.deepEqual([p.kind, p.workspace, p.hay, p.where], ["page", "docs", "install docs", undefined]);
  assert.equal(actionEntries(actions)[0].hay, "toggle theme");
  assert.deepEqual(indexEntries(index).map((x) => x.kind), ["text", "heading"]);
  assert.deepEqual(indexEntries(null), []);
  assert.deepEqual(buildEntries(pages, index, actions), [...pageEntries(pages), ...actionEntries(actions), ...indexEntries(index)]);
});

test("rankEntries: kind order and word matching", () => {
  const e = buildEntries(pages, index, actions);
  const r = rankEntries(e, "inst");
  assert.equal(r[0].label, "install");
  assert.equal(rankEntries(e, "sync out")[0].kind, "text");
  assert.deepEqual(rankEntries(e, "theme").map((x) => x.action), ["theme"]);
  assert.deepEqual(rankEntries(e, "DAEMON server").map((x) => x.kind), ["heading"]);
});

test("rankEntries: hostile query is literal", () => {
  const e = buildEntries(pages, index, actions);
  assert.deepEqual(rankEntries(e, "[(*\\"), []);
});

test("rankEntries: empty query is actions then pages, limited", () => {
  const e = buildEntries(pages, index, actions);
  assert.deepEqual(rankEntries(e, "  ").map((x) => x.kind), ["action", "page", "page"]);
  assert.equal(rankEntries(e, "", 2).length, 2);
});

test("rankEntries: prefix beats later match, shorter beats longer", () => {
  const es = pageEntries([{ title: "a install" }, { title: "install guide" }, { title: "install" }]);
  assert.deepEqual(rankEntries(es, "install").map((x) => x.label), ["install", "install guide", "a install"]);
});

test("escapeHtml", () => {
  const s = escapeHtml('<img src=x onerror=alert(1)>&"');
  assert.ok(!s.includes("<") && !s.includes('"'));
  assert.ok(s.includes("&amp;"));
});

// createPalette with a tiny fake DOM.
import { createPalette } from "../theme/assets/theme/js/palette.js";

function fake(entries) {
  const L = { overlay: {}, input: {}, list: {} };
  const on = (o) => (o.h = {}, (t, f) => { o.h[t] = f; });
  const cls = new Set();
  const overlay = { classList: { add: (c) => cls.add(c), remove: (c) => cls.delete(c), contains: (c) => cls.has(c) }, addEventListener: on(L.overlay), ownerDocument: {} };
  const attrs = {};
  const input = { value: "", addEventListener: on(L.input), focus() {}, attrs,
    setAttribute(k, v) { attrs[k] = v; }, removeAttribute(k) { delete attrs[k]; } };
  const list = { innerHTML: "", addEventListener: on(L.list), querySelector: () => null };
  const notice = { textContent: "" };
  const picked = [];
  const closed = [];
  const p = createPalette({ overlay, input, list, notice, onPick: (e) => picked.push(e), onClose: () => closed.push(1) });
  p.setEntries(entries);
  const key = (k, m = {}) => {
    const ev = { key: k, ctrlKey: false, stopped: false, prevented: false, preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; }, ...m };
    L.input.h.keydown(ev);
    return ev;
  };
  return { p, L, input, list, overlay, notice, picked, closed, key, cls };
}
const E = pageEntries([
  { title: "one", workspace: "docs", href: "1/" },
  { title: "two", workspace: "docs", href: "2/" },
  { title: "three", workspace: "docs", href: "3/" },
]);

test("palette: arrows and Ctrl+N/P move, Enter picks, keys stop propagation", () => {
  const f = fake(E);
  f.p.open();
  assert.ok(f.p.isOpen());
  assert.ok(f.key("ArrowDown").stopped);
  f.key("n", { ctrlKey: true });
  f.key("P", { ctrlKey: true });
  f.key("ArrowUp");
  f.key("ArrowDown");
  const ev = f.key("Enter");
  assert.ok(ev.prevented && ev.stopped);
  assert.deepEqual(f.picked.map((e) => e.label), ["two"]);
  assert.ok(!f.p.isOpen());
});

test("palette: Enter during composition is ignored; Esc closes", () => {
  const f = fake(E);
  f.p.open();
  f.key("Enter", { isComposing: true });
  assert.equal(f.picked.length, 0);
  assert.ok(f.key("Escape").stopped);
  assert.ok(!f.p.isOpen());
});

test("palette: onClose fires once per close, however it happens", () => {
  const f = fake(E);
  f.p.close();
  assert.equal(f.closed.length, 0);
  f.p.open();
  f.key("Escape");
  f.p.open();
  f.key("Enter");
  f.p.open();
  f.L.overlay.h.click({ target: f.overlay });
  assert.equal(f.closed.length, 3);
});

test("palette: click picks item, backdrop closes", () => {
  const f = fake(E);
  f.p.open();
  f.L.list.h.click({ target: { closest: () => ({ dataset: { i: "2" } }) } });
  assert.equal(f.picked[0].label, "three");
  f.p.open();
  f.L.overlay.h.click({ target: {} });
  assert.ok(f.p.isOpen());
  f.L.overlay.h.click({ target: f.overlay });
  assert.ok(!f.p.isOpen());
});

test("palette: combobox state follows the listbox", () => {
  const f = fake(E);
  f.p.open();
  assert.equal(f.input.attrs["aria-expanded"], "true");
  assert.equal(f.input.attrs["aria-activedescendant"], "cw-palette-opt-0");
  assert.ok(f.list.innerHTML.includes('role="option" id="cw-palette-opt-0" aria-selected="true"'));
  assert.ok(f.list.innerHTML.includes('id="cw-palette-opt-1" aria-selected="false"'));
  f.key("ArrowDown");
  assert.equal(f.input.attrs["aria-activedescendant"], "cw-palette-opt-1");
  f.input.value = "zzzz";
  f.L.input.h.input();
  assert.equal(f.input.attrs["aria-expanded"], "false");
  assert.ok(!("aria-activedescendant" in f.input.attrs));
  f.key("Escape");
  assert.equal(f.input.attrs["aria-expanded"], "false");
});

test("palette: labels are escaped; open() twice keeps the query", () => {
  const f = fake(pageEntries([{ title: "<img src=x onerror=alert(1)>", workspace: "docs" }]));
  f.p.open();
  assert.ok(!f.list.innerHTML.includes("<img"));
  f.input.value = "img";
  f.p.open();
  assert.equal(f.input.value, "img");
});

test("rankEntries: kind names are not searchable", () => {
  const e = buildEntries(pages, index, actions);
  assert.deepEqual(rankEntries(e, "heading"), []);
  assert.deepEqual(rankEntries(e, "docs").map((x) => x.kind), ["page", "page"]);
});

test("rankEntries: an empty query lists the actions first, even past 43 pages", () => {
  const many = Array.from({ length: 43 }, (_, i) => ({ title: `page ${i}`, href: `p${i}/`, workspace: "docs" }));
  const acts = [{ label: "toggle theme", action: "theme" }, { label: "single-key shortcuts", action: "single-keys" }];
  const r = rankEntries(buildEntries(many, null, acts), "");
  assert.deepEqual(r.slice(0, 2).map((x) => x.kind), ["action", "action"]);
  assert.equal(r.length, 40);
  assert.equal(r.filter((x) => x.kind === "action").length, 2);
});
