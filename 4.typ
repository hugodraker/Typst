// =============================================================
// 10-newsletter-template.typ — Professional Newsletter Template
// =============================================================
// A template for creating a modern, multi-column newsletter.
//
// What you'll learn:
//   - Complex page layouts with columns
//   - Custom masthead design with #place() and #grid()
//   - Defining helper functions for pull-quotes and sidebars
//   - Using #columns() for multi-column text flow
//   - Advanced styling with gradients and custom shapes
// =============================================================

#let newsletter-template(
  title: "Newsletter Title",
  issue: "Issue #1",
  date: datetime.today().display("[month repr:long] [year]"),
  accent-color: rgb("#1e3a5f"),
  body,
) = {

  // ---- Page setup ----
  set page(
    paper: "a4",
    margin: (x: 1.5cm, y: 2cm),
    footer: context {
      set text(8pt, fill: luma(120))
      grid(
        columns: (1fr, 1fr, 1fr),
        align(left, [#title — #issue]),
        align(center, [Page #counter(page).display()]),
        align(right, [Acme Widget Co. Confidential]),
      )
    }
  )

  // ---- Typography ----
  set text(font: "New Computer Modern", size: 10pt)
  set par(justify: true, leading: 0.6em)

  // ---- Heading Styles ----
  show heading: set text(font: "New Computer Modern", weight: "bold", fill: accent-color)
  show heading.where(level: 1): it => {
    set text(18pt)
    v(12pt, weak: true)
    it
    v(8pt, weak: true)
  }

  // ---- Masthead (The Header) ----
  block(width: 100%, inset: (bottom: 20pt), {
    grid(
      columns: (1fr, auto),
      gutter: 10pt,
      align(bottom + left)[
        #text(32pt, weight: "black", fill: accent-color, font: "New Computer Modern")[
          #upper(title)
        ]
        #v(-10pt)
        #line(length: 100%, stroke: 2pt + accent-color)
        #text(12pt, weight: "bold", fill: luma(100), font: "New Computer Modern")[
          #upper(issue) | #upper(date)
        ]
      ],
      align(bottom + right)[
        #box(
          fill: accent-color,
          inset: 8pt,
          radius: 4pt,
          text(white, weight: "bold", size: 14pt, font: "New Computer Modern")[ACME]
        )
      ]
    )
  })

  // Start the actual newsletter content in columns
  columns(2, gutter: 20pt)[
    #body
  ]
}

// ---- Reusable Components ----

#let pull-quote(content) = {
  v(10pt)
  block(
    width: 100%,
    inset: (x: 20pt, y: 10pt),
    stroke: (left: 3pt + rgb("#1e3a5f")),
    text(14pt, style: "italic", weight: "medium", fill: rgb("#1e3a5f"), content)
  )
  v(10pt)
}

#let sidebar(title: "In This Issue", fill: luma(245), content) = {
  block(
    fill: fill,
    width: 100%,
    inset: 12pt,
    radius: 4pt,
    stroke: luma(200),
    {
      set text(font: "New Computer Modern")
      text(12pt, weight: "bold", fill: rgb("#1e3a5f"), upper(title))
      v(8pt)
      content
    }
  )
}

#let article-tag(name) = {
  box(
    fill: rgb("#1e3a5f").lighten(90%),
    inset: (x: 4pt, y: 2pt),
    radius: 2pt,
    text(8pt, weight: "bold", fill: rgb("#1e3a5f"), upper(name))
  )
}