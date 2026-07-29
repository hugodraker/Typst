#set page(
  paper: "us-letter",
  margin: (top: 2.5cm, bottom: 2.5cm, x: 2cm),
  
  // Conditional Header (only shows on Page 2 and beyond)
  header: context {
    let page_num = counter(page).get().first()
    if page_num > 1 [
      #grid(
        columns: (1fr, 1fr),
        align(left)[*INVOICE \#INV-2026-084*],
        align(right)[Page #counter(page).display()]
      )
      #v(-4pt)
      #line(length: 100%, stroke: 0.5pt + rgb("#CBD5E0"))
    ]
  },
  
  // Footer
  footer: context [
    #align(center)[
      #text(8pt, fill: rgb("#718096"))[
        Thank you for your business! | Payment due within 30 days
      ]
    ]
  ]
)

#set text(font: "Liberation Sans", size: 10pt, fill: rgb("#2D3748"))

// Brand Header Block
#grid(
  columns: (1fr, 1fr),
  [
    #text(18pt, weight: "bold", fill: rgb("#1A365D"))[Apex Cloud Solutions LLC] \
    #text(9pt, fill: rgb("#4A5568"))[
      100 Technology Way, Suite 400, Austin, TX 78701 \
      billing\@apexcloud.io | (512) 555-0199
    ]
  ],
  align(right)[
    #text(22pt, weight: "bold", fill: rgb("#1A365D"))[INVOICE] \
    #v(2pt)
    #text(9pt)[
      *Invoice \#:* INV-2026-084 \
      *Date:* July 28, 2026 \
      *Due Date:* August 27, 2026
    ]
  ]
)

#v(1em)
#line(length: 100%, stroke: 1.5pt + rgb("#1A365D"))
#v(1em)

// Billing Recipient Box
#rect(fill: rgb("#F7FAFC"), width: 100%, inset: 10pt, radius: 4pt, stroke: 0.5pt + rgb("#E2E8F0"))[
  #text(9pt, fill: rgb("#718096"))[*BILL TO:*] \
  #text(11pt, weight: "bold")[Global Logistics Corp] \
  500 Enterprise Blvd, Chicago, IL 60601 \
  ap\@globallogistics.com
]

#v(1.5em)

// Dynamic Line Items Table
#table(
  columns: (1fr, 50pt, 80pt, 80pt),
  align: (left + horizon, center + horizon, right + horizon, right + horizon),
  fill: (col, row) => 
    if row == 0 { rgb("#1A365D") } 
    else if calc.even(row) { rgb("#F7FAFC") },
  stroke: 0.5pt + rgb("#E2E8F0"),
  inset: 8pt,

  [*#text(fill: white)[Description]*],
  [*#text(fill: white)[Qty]*],
  [*#text(fill: white)[Unit Price]*],
  [*#text(fill: white)[Amount]*],

  [Database Migration & Schema Refactoring], [1], [\$1,850.00], [\$1,850.00],
  [Automated Typst Reporting Engine Setup], [1], [\$1,200.00], [\$1,200.00],
  [Monthly Cloud Infrastructure Optimization], [3], [\$450.00], [\$1,350.00]
)

#v(1em)

// Summary Calculation Block
#align(right)[
  #block(width: 45%)[
    #grid(
      columns: (1fr, 1fr),
      align: (left, right),
      row-gutter: 8pt,
      [Subtotal:], [\$4,400.00],
      [Tax (8.25%):], [\$363.00],
      line(length: 100%, stroke: 0.5pt + rgb("#CBD5E0")), line(length: 100%, stroke: 0.5pt + rgb("#CBD5E0")),
      [*Total Due:*], [*#text(fill: rgb("#27AE60"), size: 12pt)[\$4,763.00]*]
    )
  ]
]