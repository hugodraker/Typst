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
#pie-chart((("Prod A", 45), ("Prod B", 110)), title: [*Product Performance Overview*])
#pie-chart((("Prod A", 45)), title: [*Product Performance Overview*])
