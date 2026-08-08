#let bar-chart(d, color: "#2563eb", title: none, x-label: none, y-label: none) = {
  let is-blank(val) = {
    if val == none { true }
    else if type(val) == "string" and val == "" { true }
    else { false }
  }

  let values = d.map(p => p.at(1))
  let raw-max = calc.max(..values)
  let max-val = if raw-max == 0 { 10 } else { calc.max(10, calc.ceil(raw-max / 10) * 10) }
  
  let has-title = not is-blank(title)
  let has-x-label = not is-blank(x-label)
  let has-y-label = not is-blank(y-label)

  let svg-width = 440
  let svg-height = if has-x-label { 225 } else { 200 }
  let x-start = if has-y-label { 65 } else { 50 }
  let x-end = 415
  let plot-width = x-end - x-start
  let y-bottom = 150
  let y-top = 20
  let plot-height = y-bottom - y-top
  
  let num-items = d.len()
  let slot-width = plot-width / num-items
  let bar-width = calc.min(35, slot-width * 0.7)
  
  let palette = ("#2563eb", "#16a34a", "#d97706", "#dc2626", "#7c3aed", "#06b6d4", "#db2777", "#4f46e5")
  
  let bars-arr = ()
  let i = 0
  
  for item in d {
    let label = item.at(0)
    let val = item.at(1)
    let slot-center = x-start + (i * slot-width) + (slot-width / 2)
    let x = slot-center - (bar-width / 2)
    let h = (val / max-val) * plot-height
    let y = y-bottom - h
    
    let bar-color = if color == "random" {
      palette.at(calc.rem(i, palette.len()))
    } else {
      color
    }
    
    bars-arr.push("<rect x='" + str(x) + "' y='" + str(y) + "' width='" + str(bar-width) + "' height='" + str(h) + "' fill='" + bar-color + "'/><text x='" + str(slot-center) + "' y='" + str(y - 6) + "' font-size='10' text-anchor='middle' fill='" + bar-color + "'>" + str(val) + "</text><text x='" + str(slot-center) + "' y='" + str(y-bottom + 20) + "' font-size='11' text-anchor='middle' fill='#666'>" + label + "</text>")
    i += 1
  }
  
  let grid-svg = range(5).map(idx => {
    let val = (max-val / 4) * (4 - idx)
    let y = y-top + (idx * (plot-height / 4))
    "<line x1='" + str(x-start) + "' y1='" + str(y) + "' x2='" + str(x-end) + "' y2='" + str(y) + "' stroke='#e5e7eb' stroke-width='1'/><text x='" + str(x-start - 8) + "' y='" + str(y + 4) + "' font-size='10' text-anchor='end' fill='#666'>" + str(calc.round(val)) + "</text>"
  }).join("")

  let y-label-svg = if has-y-label {
    let cy = y-top + (plot-height / 2)
    "<text x='20' y='" + str(cy) + "' transform='rotate(-90, 20, " + str(cy) + ")' font-size='11' text-anchor='middle' fill='#333'>" + str(y-label) + "</text>"
  } else {
    ""
  }

  let x-label-svg = if has-x-label {
    let cx = x-start + (plot-width / 2)
    "<text x='" + str(cx) + "' y='" + str(y-bottom + 42) + "' font-size='11' text-anchor='middle' fill='#333'>" + str(x-label) + "</text>"
  } else {
    ""
  }
  
  let svg-string = "<svg viewBox='0 0 " + str(svg-width) + " " + str(svg-height) + "' xmlns='http://www.w3.org/2000/svg'><rect width='100%' height='100%' fill='white'/>" + grid-svg + "<line x1='" + str(x-start) + "' y1='" + str(y-bottom) + "' x2='" + str(x-end) + "' y2='" + str(y-bottom) + "' stroke='#333' stroke-width='2'/><line x1='" + str(x-start) + "' y1='" + str(y-top) + "' x2='" + str(x-start) + "' y2='" + str(y-bottom) + "' stroke='#333' stroke-width='2'/>" + bars-arr.join("") + y-label-svg + x-label-svg + "</svg>"
  
  let chart-content = image(bytes(svg-string), format: "svg", width: 80%)

  align(center, if has-title {
    stack(
      spacing: 8pt,
      if type(title) == "function" { title() } else { title },
      chart-content
    )
  } else {
    chart-content
  })
}

#bar-chart((("Prod A", 45), ("Prod B", 110), ("Prod C", 75), ("Prod D", 95)), color: "#2563eb", title: [*Product Performance Overview*], x-label: "Products", y-label: "Revenue ($)")

#let line-chart(d, color: "#2563eb", title: none, x-label: none, y-label: none, trend-line: false) = {
  let is-blank(val) = {
    if val == none { true }
    else if type(val) == "string" and val == "" { true }
    else { false }
  }

  let values = d.map(p => p.at(1))
  let raw-max = calc.max(..values)
  let max-val = if raw-max == 0 { 10 } else { calc.max(10, calc.ceil(raw-max / 10) * 10) }
  
  let has-title = not is-blank(title)
  let has-x-label = not is-blank(x-label)
  let has-y-label = not is-blank(y-label)

  let svg-width = 440
  let svg-height = if has-x-label { 225 } else { 200 }
  let x-start = if has-y-label { 65 } else { 50 }
  let x-end = 415
  let plot-width = x-end - x-start
  let y-bottom = 150
  let y-top = 20
  let plot-height = y-bottom - y-top
  
  let num-items = d.len()
  let slot-width = plot-width / num-items
  
  let points-arr = ()
  let elements-arr = ()
  let i = 0
  
  for item in d {
    let label = item.at(0)
    let val = item.at(1)
    let slot-center = x-start + (i * slot-width) + (slot-width / 2)
    let x = slot-center
    let h = (val / max-val) * plot-height
    let y = y-bottom - h
    
    points-arr.push(str(x) + "," + str(y))
    
    elements-arr.push("<circle cx='" + str(x) + "' cy='" + str(y) + "' r='4' fill='" + color + "'/><text x='" + str(slot-center) + "' y='" + str(y - 8) + "' font-size='10' text-anchor='middle' fill='" + color + "'>" + str(val) + "</text><text x='" + str(slot-center) + "' y='" + str(y-bottom + 20) + "' font-size='11' text-anchor='middle' fill='#666'>" + label + "</text>")
    i += 1
  }
  
  let polyline-svg = "<polyline points='" + points-arr.join(" ") + "' fill='none' stroke='" + color + "' stroke-width='2.5'/>"
  
  let trend-svg = if trend-line and num-items > 1 {
    let sum-x = 0
    let sum-y = 0
    let sum-xx = 0
    let sum-xy = 0
    let idx = 0
    for item in d {
      let x-val = idx
      let y-val = item.at(1)
      sum-x += x-val
      sum-y += y-val
      sum-xx += x-val * x-val
      sum-xy += x-val * y-val
      idx += 1
    }
    let denom = (num-items * sum-xx) - (sum-x * sum-x)
    if denom != 0 {
      let m = (num-items * sum-xy - sum-x * sum-y) / denom
      let b = (sum-y - m * sum-x) / num-items
      
      let y1-val = m * 0 + b
      let x1-px = x-start + (0 * slot-width) + (slot-width / 2)
      let y1-px = y-bottom - ((y1-val / max-val) * plot-height)
      
      let y2-val = m * (num-items - 1) + b
      let x2-px = x-start + ((num-items - 1) * slot-width) + (slot-width / 2)
      let y2-px = y-bottom - ((y2-val / max-val) * plot-height)
      
      "<line x1='" + str(x1-px) + "' y1='" + str(y1-px) + "' x2='" + str(x2-px) + "' y2='" + str(y2-px) + "' stroke='#dc2626' stroke-width='2' stroke-dasharray='4'/>"
    } else {
      ""
    }
  } else {
    ""
  }
  
  let grid-svg = range(5).map(idx => {
    let val = (max-val / 4) * (4 - idx)
    let y = y-top + (idx * (plot-height / 4))
    "<line x1='" + str(x-start) + "' y1='" + str(y) + "' x2='" + str(x-end) + "' y2='" + str(y) + "' stroke='#e5e7eb' stroke-width='1'/><text x='" + str(x-start - 8) + "' y='" + str(y + 4) + "' font-size='10' text-anchor='end' fill='#666'>" + str(calc.round(val)) + "</text>"
  }).join("")

  let y-label-svg = if has-y-label {
    let cy = y-top + (plot-height / 2)
    "<text x='20' y='" + str(cy) + "' transform='rotate(-90, 20, " + str(cy) + ")' font-size='11' text-anchor='middle' fill='#333'>" + str(y-label) + "</text>"
  } else {
    ""
  }

  let x-label-svg = if has-x-label {
    let cx = x-start + (plot-width / 2)
    "<text x='" + str(cx) + "' y='" + str(y-bottom + 42) + "' font-size='11' text-anchor='middle' fill='#333'>" + str(x-label) + "</text>"
  } else {
    ""
  }
  
  let svg-string = "<svg viewBox='0 0 " + str(svg-width) + " " + str(svg-height) + "' xmlns='http://www.w3.org/2000/svg'><rect width='100%' height='100%' fill='white'/>" + grid-svg + "<line x1='" + str(x-start) + "' y1='" + str(y-bottom) + "' x2='" + str(x-end) + "' y2='" + str(y-bottom) + "' stroke='#333' stroke-width='2'/><line x1='" + str(x-start) + "' y1='" + str(y-top) + "' x2='" + str(x-start) + "' y2='" + str(y-bottom) + "' stroke='#333' stroke-width='2'/>" + trend-svg + polyline-svg + elements-arr.join("") + y-label-svg + x-label-svg + "</svg>"
  
  let chart-content = image(bytes(svg-string), format: "svg", width: 80%)

  align(center, if has-title {
    stack(
      spacing: 8pt,
      if type(title) == "function" { title() } else { title },
      chart-content
    )
  } else {
    chart-content
  })
}

#line-chart((("Prod A", 45), ("Prod B", 110), ("Prod C", 75), ("Prod D", 95)), color: "#2563eb", title: [*Product Performance Overview*], x-label: "Products", y-label: "Revenue ($)", trend-line: true)

#let pie-chart(d, title: none) = {
  let is-blank(val) = {
    if val == none { true }
    else if type(val) == "string" and val == "" { true }
    else { false }
  }

  let has-title = not is-blank(title)
  let values = d.map(p => p.at(1))
  let total = values.fold(0, (acc, v) => acc + v)
  if total == 0 { total = 1 }

  let svg-width = 440
  let svg-height = 240
  let cx = 140
  let cy = 120
  let r = 85

  let palette = ("#2563eb", "#16a34a", "#d97706", "#dc2626", "#7c3aed", "#06b6d4", "#db2777", "#4f46e5")

  let slices-arr = ()
  let legend-arr = ()
  let current-angle = -90deg
  let i = 0

  for item in d {
    let label = item.at(0)
    let val = item.at(1)
    let slice-angle = (val / total) * 360deg
    let start-angle = current-angle
    let end-angle = current-angle + slice-angle
    
    let color = palette.at(calc.rem(i, palette.len()))

    let x1 = cx + r * calc.cos(start-angle)
    let y1 = cy + r * calc.sin(start-angle)
    let x2 = cx + r * calc.cos(end-angle)
    let y2 = cy + r * calc.sin(end-angle)

    let large-arc = if slice-angle > 180deg { 1 } else { 0 }

    let path = if calc.abs(slice-angle - 360deg) < 0.1deg {
      "<circle cx='" + str(cx) + "' cy='" + str(cy) + "' r='" + str(r) + "' fill='" + color + "'/>"
    } else {
      "<path d='M " + str(cx) + " " + str(cy) + " L " + str(x1) + " " + str(y1) + " A " + str(r) + " " + str(r) + " 0 " + str(large-arc) + " 1 " + str(x2) + " " + str(y2) + " Z' fill='" + color + "' stroke='white' stroke-width='1.5'/>"
    }
    slices-arr.push(path)

    let legend-y = 50 + (i * 25)
    let percentage = calc.round((val / total) * 100, digits: 1)
    let legend-element = "<rect x='270' y='" + str(legend-y) + "' width='14' height='14' fill='" + color + "' rx='2'/><text x='295' y='" + str(legend-y + 11) + "' font-size='11' fill='#333'>" + label + " (" + str(val) + " - " + str(percentage) + "%)" + "</text>"
    legend-arr.push(legend-element)

    current-angle = end-angle
    i += 1
  }

  let svg-string = "<svg viewBox='0 0 " + str(svg-width) + " " + str(svg-height) + "' xmlns='http://www.w3.org/2000/svg'><rect width='100%' height='100%' fill='white'/>" + slices-arr.join("") + legend-arr.join("") + "</svg>"

  let chart-content = image(bytes(svg-string), format: "svg", width: 80%)

  align(center, if has-title {
    stack(
      spacing: 8pt,
      if type(title) == "function" { title() } else { title },
      chart-content
    )
  } else {
    chart-content
  })
}

#pie-chart((("Prod A", 45), ("Prod B", 110), ("Prod C", 75), ("Prod D", 95)), title: [*Product Performance Overview*])
#pie-chart((("Prod A", 45), ("Prod B", 110), ("Prod C", 75)), title: [*Product Performance Overview*])
