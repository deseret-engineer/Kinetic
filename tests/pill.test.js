// Run with: node tests/pill.test.js
const assert = require("assert")
const load = require("./load")
const schema = require("../modules/bar/schema.json")
const p = load("lib/schema.js")

// every option is well formed, and a default never depends on a later option
const seen = new Set()
for (const o of schema.options) {
  assert.ok(schema.groups.includes(o.group), `${o.key}: unknown group ${o.group}`)
  assert.ok(["choice", "bool", "color", "int", "float"].includes(o.type), `${o.key}: type ${o.type}`)
  if (o.type === "int" || o.type === "float") assert.ok(o.min < o.max && o.step > 0, `${o.key}: range`)
  const d = o.default
  if (d && typeof d === "object") assert.ok(seen.has(d.by || d.from), `${o.key}: default reads a later option`)
  seen.add(o.key)
}

// defaults: per style, and derived
const liquid = p.normalize(null, schema)
assert.strictEqual(liquid.style, "liquid")
assert.strictEqual(liquid.opacity, 0.16)
assert.strictEqual(liquid.glass, "auto")
assert.strictEqual(liquid.radius, "full")
assert.strictEqual(liquid.blurThreshold, 0.08)
const solid = p.normalize({ style: "solid" }, schema)
assert.strictEqual(solid.opacity, 1)
assert.strictEqual(solid.glass, "none")
assert.strictEqual(solid.borderOpacity, 0.14)
assert.strictEqual(p.normalize({ style: "outline" }, schema).borderOpacity, 0.45)
assert.strictEqual(p.normalize({ opacity: 0.9 }, schema).blurThreshold, 0.3)
assert.strictEqual(p.normalize({ opacity: 0.01 }, schema).blurThreshold, 0.02)

// what the user set wins when it fits, and is clamped or ignored when it does not
const c = p.normalize({ style: "nope", margin: 4.6, radius: 500, tint: "ABCDEF", shadow: "true", opacity: "x",
                        sideMargin: -5, align: 7, borderColor: "<img src=x>" }, schema)
assert.strictEqual(c.style, "liquid")
assert.strictEqual(c.margin, 5)
assert.strictEqual(c.radius, 40)
assert.strictEqual(c.tint, "#abcdef")
assert.strictEqual(c.shadow, true)
assert.strictEqual(c.opacity, 0.16)
assert.strictEqual(c.sideMargin, 0)
assert.strictEqual(c.align, "center")
assert.strictEqual(c.borderColor, "auto")
assert.strictEqual(p.normalize({ radius: "full" }, schema).radius, "full")
assert.strictEqual(p.normalize({ radius: 12 }, schema).radius, 12)

// rows for the panel: effective values, defaults that follow the style, and what is set
const rows = p.items({ style: "glass", frost: 1 }, schema)
const row = k => rows.find(r => r.key === k)
assert.strictEqual(row("opacity").value, 0.38)
assert.strictEqual(row("opacity").default, 0.38)
assert.strictEqual(row("opacity").changed, false)
assert.strictEqual(row("frost").value, 1)
assert.strictEqual(row("frost").default, 2.5)
assert.strictEqual(row("frost").changed, true)
assert.strictEqual(row("radius").special, "full")
const groups = p.groups(rows, schema)
assert.deepStrictEqual(groups.map(g => g.group), schema.groups)
assert.strictEqual(groups.reduce((n, g) => n + g.items.length, 0), schema.options.length)

assert.strictEqual(p.valueText(schema, "margin", "7.4"), "7")
assert.strictEqual(p.valueText(schema, "glass", "wallpaper"), "wallpaper")
assert.strictEqual(p.valueText(schema, "glass", "x"), "")
assert.strictEqual(p.valueText(schema, "nope", "1"), "")

// presets only name real options with valid values
for (const [name, preset] of Object.entries(schema.presets))
  for (const [k, v] of Object.entries(preset)) {
    const o = p.option(schema, k)
    assert.ok(o, `preset ${name}: unknown option ${k}`)
    assert.notStrictEqual(p.coerce(o, v), undefined, `preset ${name}: bad ${k}`)
  }

console.log("pill tests passed")
