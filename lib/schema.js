.pragma library

// A module's options, resolved against its schema.json (the bar's pill, the
// motion module's animations). Shared by the bar (which draws with normalize())
// and the panel (which lists items()), so nothing can disagree about a default.
// lib/schema.py is the same logic for the command line; tests keep them equal.
// Pure functions: data in, data out.

var HEX = /^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$/

function isObject(v) { return v !== null && typeof v === "object" && !Array.isArray(v) }

function has(o, k) { return isObject(o) && Object.prototype.hasOwnProperty.call(o, k) }

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

// An option's default, given the options resolved before it.
function resolveDefault(opt, resolved) {
  var d = opt["default"]
  if (!isObject(d)) return d
  if (d.by !== undefined) {
    var key = resolved[d.by]
    if (has(d.values, key)) return d.values[key]
    return d["else"]
  }
  if (d.from !== undefined) {
    var base = Number(resolved[d.from])
    if (!isFinite(base)) base = 0
    return clamp(base * (d.factor === undefined ? 1 : d.factor), d.min === undefined ? -Infinity : d.min,
                 d.max === undefined ? Infinity : d.max)
  }
  return undefined
}

// One value checked against its option; undefined when it does not fit.
function coerce(opt, v) {
  if (v === undefined || v === null) return undefined
  switch (opt.type) {
  case "choice":
    return opt.options.indexOf(v) >= 0 ? v : undefined
  case "bool":
    if (v === true || v === "true") return true
    if (v === false || v === "false") return false
    return undefined
  case "color":
    var s = String(v).trim()
    if ((opt.options || []).indexOf(s) >= 0) return s
    if (s.length && s.charAt(0) !== "#") s = "#" + s
    return HEX.test(s) ? s.toLowerCase() : undefined
  case "int":
  case "float":
    if (opt.special !== undefined && v === opt.special) return v
    if (v === "" || typeof v === "boolean") return undefined
    var n = Number(v)
    if (!isFinite(n)) return undefined
    n = clamp(n, opt.min, opt.max)
    return opt.type === "int" ? Math.round(n) : n
  }
  return undefined
}

// Every option with a valid value: the user's where it fits, else the default.
function normalize(raw, schema) {
  var out = {}
  var opts = schema.options
  for (var i = 0; i < opts.length; i++) {
    var opt = opts[i]
    var v = has(raw, opt.key) ? coerce(opt, raw[opt.key]) : undefined
    out[opt.key] = v !== undefined ? v : resolveDefault(opt, out)
  }
  return out
}

// Rows for a settings UI, in the shape the panel's SettingRow takes.
function items(raw, schema) {
  var eff = normalize(raw, schema)
  var resolved = {}
  var rows = []
  for (var i = 0; i < schema.options.length; i++) {
    var opt = schema.options[i]
    resolved[opt.key] = eff[opt.key]
    var def = resolveDefault(opt, resolved)
    rows.push({
      key: opt.key, group: opt.group, type: opt.type, label: opt.label || opt.key,
      description: opt.description || "", options: opt.options, special: opt.special,
      min: opt.min, max: opt.max, step: opt.step,
      value: eff[opt.key], "default": def, changed: has(raw, opt.key)
    })
  }
  return rows
}

function groups(rows, schema) {
  var out = []
  for (var g = 0; g < schema.groups.length; g++) {
    var list = rows.filter(function(r) { return r.group === schema.groups[g] })
    if (list.length) out.push({ group: schema.groups[g], items: list })
  }
  return out
}

function option(schema, key) {
  for (var i = 0; i < schema.options.length; i++) if (schema.options[i].key === key) return schema.options[i]
  return null
}

// A value typed or clicked in a UI, as the text `kinetic bar set` takes; "" if invalid.
function valueText(schema, key, v) {
  var opt = option(schema, key)
  if (!opt) return ""
  var c = coerce(opt, v)
  return c === undefined ? "" : String(c)
}

if (typeof module !== "undefined") module.exports = {
  resolveDefault: resolveDefault, coerce: coerce, normalize: normalize, items: items, groups: groups,
  option: option, valueText: valueText
}
