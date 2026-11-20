// Loads one of Kinetic's QML-side JS libraries into node.
const fs = require("fs")
const path = require("path")
module.exports = function load(rel) {
  const src = fs.readFileSync(path.join(__dirname, "..", rel), "utf8")
    .replace(/^\.pragma library$/m, "").replace(/^\.import .*$/mg, "")
  const m = {}
  new Function("module", src)({ set exports(v) { Object.assign(m, v) } })
  return m
}
