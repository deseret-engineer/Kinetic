.pragma library

// Pure helpers for the Kinetic panel. Everything here is data in,
// data out, so it can be tested with plain node.

var MAX_DESCRIBE = 512 * 1024

function parseDescribe(text) {
  text = String(text || "")
  if (text.length === 0 || text.length > MAX_DESCRIBE) return null
  try {
    var d = JSON.parse(text)
    if (!d || typeof d !== "object" || !Array.isArray(d.config)) return null
    if (!Array.isArray(d.library)) d.library = []
    if (!Array.isArray(d.palette)) d.palette = []
    return d
  } catch (e) {
    return null
  }
}

function decimalsFor(step) {
  step = Number(step) || 0
  if (step <= 0) return 2
  var s = String(step)
  var dot = s.indexOf(".")
  return dot < 0 ? 0 : Math.min(4, s.length - dot - 1)
}

// A slider step for a parameter that may not declare one.
function stepFor(item) {
  if (item.step > 0) return item.step
  if (item.type === "int") return 1
  var range = Math.abs((item.max || 1) - (item.min || 0))
  return range > 0 ? range / 100 : 0.01
}

function formatNumber(item, v) {
  v = Number(v)
  if (!isFinite(v)) return ""
  if (item.type === "int") return String(Math.round(v))
  var text = v.toFixed(decimalsFor(stepFor(item)))
  return text.indexOf(".") >= 0 ? text.replace(/\.?0+$/, "") : text
}

function formatValue(item) {
  if (!item) return ""
  switch (item.type) {
  case "bool": return item.value === true || Number(item.value) !== 0 ? "on" : "off"
  case "choice":
    if (typeof item.value === "string") return item.value
    return (item.options && item.options[item.value] !== undefined) ? String(item.options[item.value]) : String(item.value)
  case "bits": return bitsLabel(item.value, item.bits)
  case "color": return String(item.value)
  case "text": return String(item.value)
  default: return formatNumber(item, item.value)
  }
}

function isOn(item) { return item.value === true || item.value === "true" || Number(item.value) > 0 }

function isChanged(item) {
  if (!item) return false
  if (item.changed !== undefined) return !!item.changed
  return String(item.value) !== String(item.default)
}

function hasBit(mask, n) { return ((Math.round(Number(mask) || 0) >> n) & 1) === 1 }

function toggleBit(mask, n) { return (Math.round(Number(mask) || 0) ^ (1 << n)) >>> 0 }

// Life-style digits: mask 12 with 9 bits -> "23"
function bitsLabel(mask, bits) {
  var out = ""
  for (var i = 0; i < (bits || 9); i++) if (hasBit(mask, i)) out += String(i)
  return out === "" ? "none" : out
}

function choiceIndex(item) {
  if (typeof item.value === "string") return Math.max(0, (item.options || []).indexOf(item.value))
  return Math.round(Number(item.value) || 0)
}

function choiceOptions(item) {
  var opts = item.options || []
  var out = []
  for (var i = 0; i < opts.length; i++) out.push({ value: String(opts[i]), label: titleCase(String(opts[i])) })
  return out
}

function choiceValue(item) {
  var opts = item.options || []
  if (typeof item.value === "string") return item.value
  var i = choiceIndex(item)
  return i >= 0 && i < opts.length ? String(opts[i]) : ""
}

function titleCase(s) { return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }

function groupConfig(config) {
  var groups = []
  var index = {}
  for (var i = 0; i < (config || []).length; i++) {
    var c = config[i]
    if (!(c.group in index)) {
      index[c.group] = groups.length
      groups.push({ group: c.group, items: [] })
    }
    groups[index[c.group]].items.push(c)
  }
  return groups
}

var HEX = /^#?[0-9a-fA-F]{6}$/
function normalizeHex(text) {
  text = String(text || "").trim()
  if (!HEX.test(text)) return ""
  return (text.charAt(0) === "#" ? text : "#" + text).toLowerCase()
}

// Names that reach the command line must look like identifiers or presets.
var SAFE = /^[A-Za-z0-9@#._&' -]{1,64}$/
function safeArg(s) { return SAFE.test(String(s)) }

// A number typed into a setting's value box: accepts a comma as the decimal
// point, rounds integers, clamps into the setting's range. null if it is not a number.
function parseNumberInput(item, text) {
  var s = String(text || "").trim().replace(",", ".")
  if (!/^[-+]?(\d+\.?\d*|\.\d+)$/.test(s)) return null
  var v = Number(s)
  if (!isFinite(v)) return null
  if (item && item.type === "int") v = Math.round(v)
  if (item && item.min !== undefined && v < Number(item.min)) v = Number(item.min)
  if (item && item.max !== undefined && v > Number(item.max)) v = Number(item.max)
  return v
}

// The library, narrowed to wallpapers matching every word of the query (in the
// name, id or description, ignoring case). Name matches sort first.
function filterLibrary(library, query) {
  library = library || []
  var words = String(query || "").toLowerCase().split(/\s+/).filter(function(w) { return w.length > 0 })
  if (words.length === 0) return library
  var hits = []
  for (var i = 0; i < library.length; i++) {
    var w = library[i]
    var name = String(w.name || "").toLowerCase(), id = String(w.id || "").toLowerCase()
    var hay = name + " " + id + " " + String(w.description || "").toLowerCase()
    var ok = true, inName = true
    for (var k = 0; k < words.length && ok; k++) {
      ok = hay.indexOf(words[k]) >= 0
      inName = inName && (name.indexOf(words[k]) >= 0 || id.indexOf(words[k]) >= 0)
    }
    if (ok) hits.push({ w: w, rank: inName ? 0 : 1, i: i })
  }
  hits.sort(function(a, b) { return a.rank - b.rank || a.i - b.i })
  return hits.map(function(h) { return h.w })
}

function stateLabel(w) {
  if (!w) return "not running"
  var parts = [w.state === "running" ? "running" : "paused (" + w.state + ")"]
  if (w.state === "running" && w.gpu_ms > 0) parts.push(w.gpu_ms.toFixed(2) + " ms GPU")
  parts.push((w.fps > 0 ? Math.round(w.fps) + " fps" : "on change"))
  return parts.join(" · ")
}

// shell.json, as far as the Bar tab cares.
var MAX_SHELL = 1024 * 1024
function parseShell(text) {
  text = String(text || "")
  if (text.length === 0 || text.length > MAX_SHELL) return null
  try {
    var c = JSON.parse(text)
    var bar = c && typeof c.bar === "object" && c.bar ? c.bar : {}
    return {
      id: String(bar.id || "omarchy.bar"),
      background: String(bar.background || (bar.transparent === true ? "none" : "opaque")),
      pill: bar.pill && typeof bar.pill === "object" && !Array.isArray(bar.pill) ? bar.pill : {}
    }
  } catch (e) {
    return null
  }
}

// `kinetic preset list --json`
function parsePresets(text) {
  text = String(text || "")
  if (text.length === 0 || text.length > MAX_DESCRIBE) return []
  try {
    var l = JSON.parse(text)
    if (!Array.isArray(l)) return []
    return l.filter(function(x) { return x && typeof x.id === "string" && /^[a-z0-9][a-z0-9-]{0,47}$/.test(x.id) })
            .slice(0, 200)
  } catch (e) {
    return []
  }
}

// What a typed preset name becomes as an id: "My Preset!" -> "my-preset"
function presetId(name) {
  return String(name || "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 48)
}

// ~/.config/kinetic/motion.json as the Motion tab needs it.
function parseMotion(text) {
  text = String(text || "")
  try {
    var c = text.length > 0 && text.length < MAX_SHELL ? JSON.parse(text) : null
    if (!c || typeof c !== "object") return { enabled: false, options: {} }
    var o = c.options && typeof c.options === "object" && !Array.isArray(c.options) ? c.options : {}
    return { enabled: c.enabled !== false, options: o }
  } catch (e) {
    return { enabled: false, options: {} }
  }
}

// A bar option row whose value is its special word ("full" radius).
function isSpecial(item) { return !!item && item.special !== undefined && item.value === item.special }

if (typeof module !== "undefined") module.exports = {
  parseShell: parseShell, parsePresets: parsePresets, parseMotion: parseMotion, presetId: presetId, isSpecial: isSpecial,
  parseDescribe: parseDescribe, decimalsFor: decimalsFor, stepFor: stepFor, formatValue: formatValue,
  formatNumber: formatNumber, isOn: isOn, isChanged: isChanged, hasBit: hasBit, toggleBit: toggleBit,
  bitsLabel: bitsLabel, choiceIndex: choiceIndex, choiceOptions: choiceOptions, choiceValue: choiceValue,
  groupConfig: groupConfig, normalizeHex: normalizeHex, safeArg: safeArg, stateLabel: stateLabel,
  filterLibrary: filterLibrary, parseNumberInput: parseNumberInput
}
