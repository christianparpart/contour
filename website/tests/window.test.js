import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { PREF_KEYS, safeStorage, createPrefs, createWindow } from "../theme/assets/theme/js/window.js";

const mem = () => {
  const m = new Map();
  return { getItem: (k) => (m.has(k) ? m.get(k) : null), setItem: (k, v) => void m.set(k, String(v)) };
};
const boom = () => { throw new Error("denied"); };

test("PREF_KEYS match the head script", () => {
  assert.deepEqual(PREF_KEYS, {
    theme: "contour-site.theme", maximized: "contour-site.maximized", singleKeys: "contour-site.single-keys",
  });
});

test("every PREF_KEYS value appears in the inline head script of main.html", () => {
  const html = readFileSync(new URL("../theme/main.html", import.meta.url), "utf8");
  const head = html.slice(html.indexOf("<script>"), html.indexOf("</script>"));
  for (const [name, key] of Object.entries(PREF_KEYS)) assert.ok(head.includes(`"${key}"`), `${name}: ${key}`);
});

test("safeStorage never throws", () => {
  for (const backing of [{ getItem: boom, setItem: boom }, undefined]) {
    const s = safeStorage(backing);
    assert.equal(s.get("x"), null);
    assert.doesNotThrow(() => s.set("x", "1"));
  }
  const s = safeStorage(mem());
  s.set("a", "b");
  assert.equal(s.get("a"), "b");
});

test("createPrefs", () => {
  const mem2 = mem();
  const p = createPrefs(safeStorage(mem2));
  assert.equal(p.theme(), null);
  p.setTheme("dark");
  assert.equal(p.theme(), "dark");
  p.setMaximized(true);
  assert.equal(mem2.getItem(PREF_KEYS.maximized), "1");
  p.setMaximized(false);
  assert.equal(mem2.getItem(PREF_KEYS.maximized), "0");
  assert.equal(p.singleKeys(), true);
  p.setSingleKeys(false);
  assert.equal(p.singleKeys(), false);
  const b = safeStorage(mem());
  b.set(PREF_KEYS.theme, "blue");
  assert.equal(createPrefs(b).theme(), null);
});

const fakeDoc = (dark = false) => {
  const cls = (set) => ({ add: (c) => set.add(c), remove: (c) => set.delete(c), has: (c) => set.has(c) });
  const mk = () => {
    const s = new Set(), attrs = new Set();
    return { s, attrs, classList: cls(s), focused: 0, focus() { this.focused++; },
      setAttribute(k) { attrs.add(k); }, removeAttribute(k) { attrs.delete(k); } };
  };
  const html = { attrs: {}, getAttribute(k) { return this.attrs[k] ?? null; }, setAttribute(k, v) { this.attrs[k] = v; }, removeAttribute(k) { delete this.attrs[k]; }, hasAttribute(k) { return k in this.attrs; },
    toggleAttribute(k) { if (k in this.attrs) { delete this.attrs[k]; return false; } this.attrs[k] = ""; return true; } };
  const win = mk(), launcher = mk(), page = mk();
  return {
    documentElement: html, win, launcher, page,
    getElementById: (id) => ({ "cw-window": win, "cw-launcher": launcher, "cw-page": page }[id]),
    defaultView: { matchMedia: () => ({ matches: dark }) },
  };
};

test("createWindow: theme toggle stores and sets data-theme", () => {
  const doc = fakeDoc(true), prefs = createPrefs(safeStorage(mem()));
  const w = createWindow(doc, prefs);
  w.toggleTheme();
  assert.equal(doc.documentElement.attrs["data-theme"], "light");
  assert.equal(prefs.theme(), "light");
  w.toggleTheme();
  assert.equal(doc.documentElement.attrs["data-theme"], "dark");
  assert.equal(prefs.theme(), "dark");
});

test("createWindow: maximize toggles data-max and stores", () => {
  const store = mem(), doc = fakeDoc(), w = createWindow(doc, createPrefs(safeStorage(store)));
  w.toggleMaximize();
  assert.equal(doc.documentElement.attrs["data-max"], "");
  assert.equal(store.getItem(PREF_KEYS.maximized), "1");
  w.toggleMaximize();
  assert.equal(doc.documentElement.attrs["data-max"], undefined);
  assert.equal(store.getItem(PREF_KEYS.maximized), "0");
});

test("createWindow: close/open are not stored", () => {
  const store = mem(), doc = fakeDoc(), prefs = createPrefs(safeStorage(store));
  const w = createWindow(doc, prefs);
  w.close();
  assert.ok(doc.win.s.has("closed"));
  assert.ok(doc.launcher.s.has("on"));
  w.open();
  assert.ok(!doc.win.s.has("closed"));
  assert.ok(!doc.launcher.s.has("on"));
  assert.equal(store.getItem("contour-site.closed"), null);
});

test("createWindow: a closed window is inert and focus moves to the launcher and back", () => {
  const doc = fakeDoc(), w = createWindow(doc, createPrefs(safeStorage(mem())));
  w.close();
  assert.ok(doc.win.attrs.has("inert"));
  assert.equal(doc.launcher.focused, 1);
  w.open();
  assert.ok(!doc.win.attrs.has("inert"));
  assert.equal(doc.page.focused, 1);
});

test("toggleMaximize restores with throwing storage", () => {
  const doc = fakeDoc(), w = createWindow(doc, createPrefs(safeStorage({ getItem: boom, setItem: boom })));
  w.toggleMaximize();
  assert.ok("data-max" in doc.documentElement.attrs);
  w.toggleMaximize();
  assert.ok(!("data-max" in doc.documentElement.attrs));
});

test("singleKeys: only \"0\" is off", () => {
  const s = safeStorage(mem());
  s.set(PREF_KEYS.singleKeys, "garbage");
  assert.equal(createPrefs(s).singleKeys(), true);
  s.set(PREF_KEYS.singleKeys, "0");
  assert.equal(createPrefs(s).singleKeys(), false);
});
