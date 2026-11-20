// lib/schema.js and lib/schema.py must agree on every input: the panel and the
// bar use one, the command line the other. Run with: node tests/schema.test.js
const assert = require("assert")
const { execFileSync } = require("child_process")
const path = require("path")
const load = require("./load")
const js = load("lib/schema.js")

const schemas = {
  bar: require("../modules/bar/schema.json"),
  motion: require("../modules/motion/schema.json"),
}

// values worth trying for any option: valid, edge, wrong type, hostile
function candidates(o) {
  const out = [undefined, null, "", "x", true, false, -1, 0, 0.5, 1, 7.5, 1e9, "12", "#abcdef", "ABCDEF", "<b>", "full"]
  if (o.options) out.push(...o.options)
  if (o.min !== undefined) out.push(o.min, o.max, (o.min + o.max) / 2, o.min - 1, o.max + 1, o.min + 0.5)
  return out
}

let seed = 42
const rnd = () => (seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648

const cases = []
for (const [name, s] of Object.entries(schemas)) {
  cases.push({ name, raw: {} }, { name, raw: null })
  for (const p of Object.values(s.presets)) cases.push({ name, raw: p })
  for (let i = 0; i < 150; i++) {
    const raw = {}
    for (const o of s.options) {
      if (rnd() < 0.4) continue
      const c = candidates(o)
      const v = c[Math.floor(rnd() * c.length)]
      if (v !== undefined) raw[o.key] = v
    }
    cases.push({ name, raw })
  }
}

const py = `
import json, sys
sys.path.insert(0, ${JSON.stringify(path.join(__dirname, "..", "lib"))})
import schema as S
schemas = json.loads(sys.argv[1]); cases = json.load(sys.stdin)
print(json.dumps([S.normalize(c["raw"], schemas[c["name"]]) for c in cases]))
`
const fromPy = JSON.parse(execFileSync("python3", ["-c", py, JSON.stringify(schemas)], { input: JSON.stringify(cases) }))
cases.forEach((c, i) => assert.deepStrictEqual(fromPy[i], js.normalize(c.raw, schemas[c.name]), `${c.name} ${JSON.stringify(c.raw)}`))

// motion schema: well formed, presets valid, every curve named exists
const m = schemas.motion
for (const o of m.options) for (const v of (o.key.endsWith("Curve") ? o.options : [])) assert.ok(m.curves[v], `curve ${v}`)
for (const [name, p] of Object.entries(m.presets))
  for (const [k, v] of Object.entries(p)) assert.notStrictEqual(js.coerce(js.option(m, k), v), undefined, `motion preset ${name}: ${k}`)

console.log(`schema tests passed (${cases.length} cases, JS and Python agree)`)
