import test from "node:test";
import assert from "node:assert/strict";
import { classifyLink, scrollKey, createRouter, revealOsPanel, scrollToHash } from "../theme/assets/theme/js/router.js";
import { readModel, allPages } from "../theme/assets/theme/js/model.js";

const ROOT = "https://x/";
const CUR = "https://x/install/";
const c = (href, extra = {}) => classifyLink({ href, ...extra }, CUR, ROOT);

test("classifyLink: router, hash and browser", () => {
  for (const h of ["https://x/features/", "https://x/features.html", "https://x/vt-extensions/index.html"])
    assert.equal(c(h), "router", h);
  assert.equal(c("https://x/install/#flatpak"), "hash");
  for (const h of ["https://github.com/a/b", "https://x/screenshots/a.png", "https://x/videos/a.mp4",
    "https://x/videos/a.webm", "https://x/search/search_index.json"])
    assert.equal(c(h), "browser", h);
  for (const e of [{ target: "_blank" }, { download: true }, { ctrlKey: true }, { metaKey: true },
    { shiftKey: true }, { altKey: true }, { button: 1 }])
    assert.equal(c("https://x/features/", e), "browser", JSON.stringify(e));
});

test("classifyLink: outside the site root", () => {
  assert.equal(classifyLink({ href: "https://x/other/" }, "https://x/site/a/", "https://x/site/"), "browser");
  assert.equal(classifyLink({ href: "https://x/site/b/" }, "https://x/site/a/", "https://x/site/"), "router");
});

test("scrollKey drops the hash", () => assert.equal(scrollKey("https://x/a/#h"), "https://x/a/"));

const el = (id) => ({ id, scrollTop: 0, replaced: null, replaceWith(n) { this.replaced = n; }, focus() { this.focused = true; } });
function setup({ fetchPage, parsed } = {}) {
  const els = Object.fromEntries(["cw-strip", "cw-tabs", "cw-tabsel", "cw-page", "cw-aside", "cw-loc", "cw-title"].map((i) => [i, el(i)]));
  const doc = { title: "old", getElementById: (i) => els[i] ?? null, defaultView: null };
  const calls = { assign: [], push: [], replace: [], nav: [] };
  const history = { state: null, scrollRestoration: "auto",
    pushState: (s, t, u) => { calls.push.push([s, u]); history.state = s; if (u) location.href = u; }, replaceState: (s, t, u) => calls.replace.push([s, u]) };
  var location = { href: CUR, assign: (u) => calls.assign.push(u) };
  const next = parsed === undefined
    ? { title: "new", getElementById: (i) => ({ id: i }) } : parsed;
  const router = createRouter({ doc, history, location, siteRoot: ROOT,
    fetchPage: fetchPage ?? (async () => ({ ok: true, text: async () => "x" })),
    parse: () => next, onNavigated: (u) => calls.nav.push(u) });
  return { els, doc, calls, router, history };
}

test("navigate: rejected fetch falls back to assign", async () => {
  const s = setup({ fetchPage: async () => { throw new Error("offline"); } });
  await s.router.navigate("https://x/a/", "push");
  assert.deepEqual(s.calls.assign, ["https://x/a/"]);
  assert.equal(s.calls.push.length, 0);
});

test("navigate: not ok falls back to assign", async () => {
  const s = setup({ fetchPage: async () => ({ ok: false, text: async () => "" }) });
  await s.router.navigate("https://x/a/", "push");
  assert.deepEqual(s.calls.assign, ["https://x/a/"]);
});

test("navigate: redirect stub without #page falls back to assign", async () => {
  const s = setup({ parsed: { title: "r", getElementById: () => null } });
  await s.router.navigate("https://x/a/", "push");
  assert.deepEqual(s.calls.assign, ["https://x/a/"]);
  assert.equal(s.calls.push.length, 0);
});

test("navigate: success swaps panes, pushes once, saves scroll", async () => {
  const s = setup();
  s.els["cw-page"].scrollTop = 120;
  await s.router.navigate("https://x/a/", "push");
  assert.equal(s.calls.push.length, 1);
  assert.deepEqual(s.calls.push[0], [{ scroll: 0 }, "https://x/a/"]);
  assert.equal(s.calls.replace[0][0].scroll, 120);
  assert.equal(s.els["cw-page"].replaced.id, "cw-page");
  assert.equal(s.doc.title, "new");
  assert.deepEqual(s.calls.nav, ["https://x/a/"]);
  assert.equal(s.calls.assign.length, 0);
});

test("navigate: pop restores stored scroll and does not push", async () => {
  const s = setup();
  s.history.state = { scroll: 77 };
  const page = { id: "cw-page", scrollTop: 0, replaceWith() {}, focus() {} };
  s.doc.getElementById = (i) => (i === "cw-page" ? page : null);
  await s.router.navigate("https://x/a/", "pop");
  assert.equal(s.calls.push.length, 0);
  assert.equal(page.scrollTop, 77);
});

const tab = (t, href, group, cur) => ({ textContent: t, dataset: { group }, getAttribute: () => href, hasAttribute: (a) => a === "aria-current" && !!cur });
test("readModel and allPages", () => {
  const docs = { title: "docs", getAttribute: () => "install/", dataset: { ws: "docs" }, hasAttribute: () => true };
  const home = { getAttribute: () => "./", dataset: { ws: "home" }, hasAttribute: () => false };
  const doc = {};
  doc.querySelectorAll = (q) => q.includes("a.ws") ? [home, docs] : [
    { dataset: { ws: "home" }, querySelectorAll: () => [tab("start", "./", "", false)] },
    { dataset: { ws: "docs" }, querySelectorAll: () => [tab("install", "install/", "", true), tab("keys", "k/", "g", false)] }];
  const m = readModel(doc);
  assert.equal(m.workspaces.length, 2);
  assert.deepEqual(m.current, { workspace: "docs", href: "install/" });
  assert.deepEqual(allPages(m).map((p) => [p.title, p.workspace]), [["start", "home"], ["install", "docs"], ["keys", "docs"]]);
  assert.equal(m.workspaces[1].tabs[1].group, "g");
});

function livePage(setup) {
  const page = { id: "cw-page", scrollTop: 0, replaceWith() {}, focus() {} };
  const s = setup;
  s.doc.getElementById = (i) => (i === "cw-page" ? page : null);
  return page;
}

test("scroll is remembered for forward and back", async () => {
  const s = setup();
  const page = livePage(s);
  page.scrollTop = 500;
  await s.router.navigate("https://x/b/", "push");
  page.scrollTop = 300;
  await s.router.navigate("https://x/install/", "pop");
  assert.equal(page.scrollTop, 500);
  await s.router.navigate("https://x/b/", "pop");
  assert.equal(page.scrollTop, 300);
});

test("malformed hash does not throw after the swap", async () => {
  const s = setup();
  livePage(s);
  await s.router.navigate("https://x/a/#%E0%A4%A", "push");
  assert.deepEqual(s.calls.nav, ["https://x/a/#%E0%A4%A"]);
});

test("redirected response pushes the final url", async () => {
  const s = setup({ fetchPage: async () => ({ ok: true, redirected: true, url: "https://x/final/", text: async () => "" }) });
  await s.router.navigate("https://x/a/", "push");
  assert.equal(s.calls.push[0][1], "https://x/final/");
});

test("failed pop reloads instead of assigning", async () => {
  const s = setup({ fetchPage: async () => { throw new Error("x"); } });
  let reloaded = 0;
  s.router = createRouter({ doc: s.doc, history: s.history, location: { href: CUR, assign() {}, reload: () => reloaded++ },
    siteRoot: ROOT, fetchPage: async () => { throw new Error("x"); } });
  await s.router.navigate("https://x/a/", "pop");
  assert.equal(reloaded, 1);
});

test("the first of two navigations resolving last is dropped", async () => {
  const gates = {};
  const s = setup({ fetchPage: (u) => new Promise((r) => { gates[u] = () => r({ ok: true, text: async () => "" }); }) });
  livePage(s);
  const first = s.router.navigate("https://x/a/", "push");
  const second = s.router.navigate("https://x/b/", "push");
  gates["https://x/b/"]();
  await second;
  gates["https://x/a/"]();
  await first;
  assert.deepEqual(s.calls.nav, ["https://x/b/"]);
  assert.equal(s.calls.push.length, 1);
});

// A pymdownx.tabbed set: radios first, then the labels, then the blocks in the same order.
function tabbed(checked = 0) {
  const radios = [0, 1, 2].map((i) => ({ checked: i === checked }));
  const blocks = [{}, {}, {}];
  const parent = { children: blocks };
  const set = { querySelectorAll: (q) => (q === ":scope > input" ? radios : []) };
  blocks.forEach((b, i) => { b.parentElement = parent; b.closest = (q) => (q === ".tabbed-block" ? b : q === ".tabbed-set" ? set : null); });
  const inside = (i) => ({ closest: (q) => (q === ".tabbed-block" ? blocks[i] : null) });
  return { radios, inside };
}

test("revealOsPanel checks the radio of a hidden tab panel, once", () => {
  const t = tabbed(0);
  assert.equal(revealOsPanel({}, t.inside(2)), true);
  assert.equal(t.radios[2].checked, true);
  assert.equal(revealOsPanel({}, t.inside(2)), false);
  assert.equal(revealOsPanel({}, t.inside(0)), false);
  assert.equal(revealOsPanel({}, { closest: () => null }), false);
  assert.equal(revealOsPanel({}, null), false);
  assert.equal(revealOsPanel({}, undefined), false);
});

test("scrollToHash reveals, scrolls and decodes; malformed or empty hashes are harmless", () => {
  const t = tabbed(0);
  const scrolled = [];
  const target = { ...t.inside(1), scrollIntoView: () => scrolled.push("x") };
  const doc = { getElementById: (id) => ({ "a b": target, "%E0%A4%A": target })[id] ?? null };
  assert.equal(scrollToHash(doc, "#a%20b"), true);
  assert.equal(t.radios[1].checked, true);
  assert.equal(scrollToHash(doc, "%E0%A4%A"), true);
  assert.equal(scrolled.length, 2);
  assert.equal(scrollToHash(doc, "#"), false);
  assert.equal(scrollToHash(doc, ""), false);
  assert.equal(scrollToHash(doc, "#missing"), false);
});

test("follow: same page replaces, another page pushes, and a failure falls back to a full load", async () => {
  const s = setup();
  livePage(s);
  await s.router.follow("https://x/install/#a");
  assert.equal(s.calls.push.length, 0);
  assert.deepEqual(s.calls.replace.at(-1), [{ scroll: 0 }, "https://x/install/#a"]);
  await s.router.follow("https://x/b/");
  assert.deepEqual(s.calls.push.at(-1), [{ scroll: 0 }, "https://x/b/"]);
  const bad = setup({ fetchPage: async () => { throw new Error("offline"); } });
  await bad.router.follow("https://x/c/");
  assert.deepEqual(bad.calls.assign, ["https://x/c/"]);
});

test("revealOsPanel opens every enclosing tabbed set, not just the innermost", () => {
  const mk = (n) => {
    const radios = Array.from({ length: n }, (_, i) => ({ checked: i === 0 }));
    return { radios, blocks: Array.from({ length: n }, () => ({})), set: { querySelectorAll: () => radios } };
  };
  const outer = mk(2), inner = mk(2);
  const wire = (g, outerBlock) => g.blocks.forEach((b) => {
    b.parentElement = { children: g.blocks, closest: (q) => (q === ".tabbed-block" ? outerBlock ?? null : null) };
    b.closest = (q) => (q === ".tabbed-block" ? b : q === ".tabbed-set" ? g.set : null);
  });
  wire(outer);
  wire(inner, outer.blocks[1]);
  const target = { closest: (q) => (q === ".tabbed-block" ? inner.blocks[1] : null) };
  assert.equal(revealOsPanel({}, target), true);
  assert.equal(inner.radios[1].checked, true);
  assert.equal(outer.radios[1].checked, true);
});
