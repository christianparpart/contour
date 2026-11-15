import test from "node:test";
import assert from "node:assert/strict";
import { KEY_TABLE, statusHints, resolveKey, isTypingTarget } from "../theme/assets/theme/js/keys.js";

const ev = (key, m = {}) => ({ key, ctrlKey: false, metaKey: false, altKey: false, ...m });
const on = { singleKeys: true };

test("table: single flags and unique chords", () => {
  assert.deepEqual(KEY_TABLE.filter((r) => !r.single).map((r) => r.chord), ["Ctrl+k", "Escape"]);
  assert.equal(new Set(KEY_TABLE.map((r) => r.chord)).size, KEY_TABLE.length);
});

test("statusHints: the table drives the status line, in its display order", () => {
  const shown = statusHints().map(({ keys, sep, text, fallback }) => ({ keys, sep, text, fallback }));
  assert.deepEqual(shown, [
    { keys: ["Ctrl", "k"], sep: "", text: "palette", fallback: true },
    { keys: ["?"], sep: "", text: "keys", fallback: false },
    { keys: [":"], sep: "", text: "palette", fallback: false },
    { keys: ["g", "t"], sep: "", text: "next tab", fallback: false },
    { keys: ["1", "3"], sep: "–", text: "workspace", fallback: false },
  ]);
});

test("resolveKey: single keys and g prefix", () => {
  assert.deepEqual(resolveKey("", ev("1"), on), { action: "workspace-1", pending: "" });
  assert.deepEqual(resolveKey("", ev("g"), on), { action: null, pending: "g" });
  assert.equal(resolveKey("g", ev("t"), on).action, "tab-next");
  assert.equal(resolveKey("g", ev("T"), on).action, "tab-prev");
  assert.equal(resolveKey("g", ev("g"), on).action, "top");
  assert.deepEqual(resolveKey("g", ev("x"), on), { action: null, pending: "" });
  assert.equal(resolveKey("", ev("G"), on).action, "bottom");
  for (const [k, a] of [["j", "scroll-down"], ["k", "scroll-up"], ["d", "half-down"], ["u", "half-up"],
    [":", "palette"], ["/", "palette"], ["?", "help"], ["t", "theme"], ["m", "maximize"], ["Escape", "close"]]) {
    assert.equal(resolveKey("", ev(k), on).action, a, k);
  }
});

test("resolveKey: modifiers", () => {
  assert.equal(resolveKey("", ev("k", { ctrlKey: true }), on).action, "palette");
  assert.equal(resolveKey("", ev("k", { metaKey: true }), on).action, "palette");
  assert.equal(resolveKey("", ev("j", { ctrlKey: true }), on).action, null);
  assert.equal(resolveKey("", ev("1", { altKey: true }), on).action, null);
});

test("resolveKey: single keys disabled", () => {
  const off = { singleKeys: false };
  assert.equal(resolveKey("", ev("1"), off).action, null);
  assert.deepEqual(resolveKey("", ev("g"), off), { action: null, pending: "" });
  assert.equal(resolveKey("", ev("Escape"), off).action, "close");
  assert.equal(resolveKey("", ev("k", { ctrlKey: true }), off).action, "palette");
});

test("isTypingTarget", () => {
  assert.equal(isTypingTarget({ tagName: "INPUT", type: "text" }), true);
  assert.equal(isTypingTarget({ tagName: "INPUT" }), true);
  assert.equal(isTypingTarget({ tagName: "TEXTAREA" }), true);
  assert.equal(isTypingTarget({ tagName: "SELECT" }), true);
  assert.equal(isTypingTarget({ tagName: "DIV", isContentEditable: true }), true);
  assert.equal(isTypingTarget({ tagName: "INPUT", type: "checkbox" }), false);
  assert.equal(isTypingTarget({ tagName: "INPUT", type: "radio" }), false);
  assert.equal(isTypingTarget({ tagName: "A" }), false);
  assert.equal(isTypingTarget({ tagName: "BUTTON" }), false);
  assert.equal(isTypingTarget(null), false);
});

test("Ctrl/Meta + uppercase K opens the palette", () => {
  assert.equal(resolveKey("", ev("K", { ctrlKey: true, shiftKey: true }), on).action, "palette");
  assert.equal(resolveKey("", ev("K", { metaKey: true }), on).action, "palette");
});

test("resolveKey: modifier-only keys keep the pending prefix, so g T is typeable", () => {
  for (const k of ["Shift", "Control", "Alt", "Meta", "CapsLock", "AltGraph"])
    assert.deepEqual(resolveKey("g", ev(k, { shiftKey: k === "Shift" }), on), { action: null, pending: "g" }, k);
  assert.deepEqual(resolveKey("", ev("Shift"), on), { action: null, pending: "" });
  let r = resolveKey("", ev("g"), on);
  r = resolveKey(r.pending, ev("Shift", { shiftKey: true }), on);
  assert.deepEqual(resolveKey(r.pending, ev("T", { shiftKey: true }), on), { action: "tab-prev", pending: "" });
});
