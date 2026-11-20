// Run with: node tests/model.test.js
const assert = require("assert")
const fs = require("fs")
const path = require("path")
const m = require("../../../tests/load")("shell/kinetic/Model.js")

assert.strictEqual(m.parseDescribe("nope"), null)
assert.strictEqual(m.parseDescribe(""), null)
assert.ok(m.parseDescribe('{"config":[],"library":[]}'))
assert.strictEqual(m.parseDescribe("x".repeat(600 * 1024)), null)

assert.strictEqual(m.bitsLabel(12, 9), "23")
assert.strictEqual(m.bitsLabel(0, 9), "none")
assert.strictEqual(m.toggleBit(12, 6), 76)
assert.strictEqual(m.toggleBit(76, 6), 12)
assert.ok(m.hasBit(8, 3) && !m.hasBit(8, 2))

assert.strictEqual(m.formatValue({ type: "int", value: 7.6 }), "8")
assert.strictEqual(m.formatValue({ type: "float", value: 0.5, step: 0.05, min: 0, max: 1 }), "0.5")
assert.strictEqual(m.formatValue({ type: "choice", value: 1, options: ["a", "b"] }), "b")
assert.strictEqual(m.formatValue({ type: "choice", value: "throttle", options: ["play", "throttle"] }), "throttle")
assert.strictEqual(m.formatValue({ type: "bool", value: 1 }), "on")
assert.strictEqual(m.formatValue({ type: "text", value: "terminal kitty" }), "terminal kitty")
assert.strictEqual(m.formatNumber({ type: "float", step: 1 }, 10), "10")
assert.strictEqual(m.formatNumber({ type: "float", step: 0.05 }, 2.5), "2.5")
assert.strictEqual(m.formatNumber({ type: "float", step: 0.1 }, 0), "0")

assert.strictEqual(m.normalizeHex("ABCDEF"), "#abcdef")
assert.strictEqual(m.normalizeHex("#12345"), "")
assert.strictEqual(m.normalizeHex("<img src=x>"), "")

assert.ok(m.safeArg("Day & Night") && m.safeArg("@fps") && m.safeArg("life") && m.safeArg("#a1b2c3"))
assert.ok(!m.safeArg("a;rm -rf") && !m.safeArg("$(x)") && !m.safeArg(""))

const g = m.groupConfig([{ key: "a", group: "X" }, { key: "b", group: "Y" }, { key: "c", group: "X" }])
assert.deepStrictEqual(g.map(x => x.group), ["X", "Y"])
assert.deepStrictEqual(g[0].items.map(x => x.key), ["a", "c"])

// markup-shaped names stay plain data: the panel renders them as PlainText
const d = m.parseDescribe('{"config":[],"library":[{"id":"x","name":"<img src=https://e.invalid/t>"}]}')
assert.strictEqual(d.library[0].name, "<img src=https://e.invalid/t>")

const lib = [
  { id: "life", name: "Game of Life", description: "cells" },
  { id: "garden", name: "Garden", description: "plants grow and bloom" },
  { id: "sand", name: "Sandbox", description: "seeds grow on wet sand" },
]
assert.strictEqual(m.filterLibrary(lib, "").length, 3)
assert.strictEqual(m.filterLibrary(lib, "   ").length, 3)
assert.deepStrictEqual(m.filterLibrary(lib, "LIFE").map(x => x.id), ["life"])
assert.deepStrictEqual(m.filterLibrary(lib, "grow").map(x => x.id), ["garden", "sand"])
assert.deepStrictEqual(m.filterLibrary(lib, "sand").map(x => x.id), ["sand"])
assert.deepStrictEqual(m.filterLibrary(lib, "seeds wet").map(x => x.id), ["sand"])
assert.deepStrictEqual(m.filterLibrary(lib, "garden bloom").map(x => x.id), ["garden"])
assert.deepStrictEqual(m.filterLibrary(lib, "zzz"), [])
// a name match ranks above a description match
assert.deepStrictEqual(m.filterLibrary([{ id: "a", name: "Pond", description: "garden" }, lib[1]], "garden").map(x => x.id), ["garden", "a"])

const f = { type: "float", min: 0, max: 3, step: 0.05 }, n = { type: "int", min: 1, max: 8 }
assert.strictEqual(m.parseNumberInput(f, "1.25"), 1.25)
assert.strictEqual(m.parseNumberInput(f, " 0,5 "), 0.5)
assert.strictEqual(m.parseNumberInput(f, ".5"), 0.5)
assert.strictEqual(m.parseNumberInput(f, "7"), 3)
assert.strictEqual(m.parseNumberInput(f, "-1"), 0)
assert.strictEqual(m.parseNumberInput(n, "4.6"), 5)
assert.strictEqual(m.parseNumberInput(n, "0"), 1)
assert.strictEqual(m.parseNumberInput(f, "abc"), null)
assert.strictEqual(m.parseNumberInput(f, ""), null)
assert.strictEqual(m.parseNumberInput(f, "1e9"), null)
assert.strictEqual(m.parseNumberInput(f, "1.2.3"), null)
assert.strictEqual(m.parseNumberInput({ type: "float", min: -2, max: 2 }, "-1.5"), -1.5)

// shell.json for the Bar tab
assert.deepStrictEqual(m.parseShell('{"bar":{"id":"kinetic.bar","background":"pill","pill":{"style":"glass"}}}'),
  { id: "kinetic.bar", background: "pill", pill: { style: "glass" } })
assert.deepStrictEqual(m.parseShell('{"bar":{"transparent":true,"pill":[1]}}'), { id: "omarchy.bar", background: "none", pill: {} })
assert.strictEqual(m.parseShell("{oops"), null)

// looks
assert.deepStrictEqual(m.parseLooks('[{"id":"ink"},{"id":"../x"},{"id":"A"},null]').map(x => x.id), ["ink"])
assert.deepStrictEqual(m.parseLooks("nope"), [])
assert.strictEqual(m.lookId("  My Look! 2 "), "my-look-2")
assert.strictEqual(m.lookId("$(rm)"), "rm")
assert.strictEqual(m.lookId("!!!"), "")

assert.ok(m.isSpecial({ special: "full", value: "full" }))
assert.ok(!m.isSpecial({ special: "full", value: 12 }) && !m.isSpecial({ value: "full" }))

assert.deepStrictEqual(m.parseMotion('{"enabled":true,"options":{"parallax":0.3}}'), { enabled: true, options: { parallax: 0.3 } })
assert.deepStrictEqual(m.parseMotion('{"options":[1]}'), { enabled: true, options: {} })
assert.deepStrictEqual(m.parseMotion(""), { enabled: false, options: {} })
assert.deepStrictEqual(m.parseMotion("{bad"), { enabled: false, options: {} })

console.log("model tests passed")
