#set page(
  paper: "us-letter",
  margin: (top: 2.5cm, bottom: 2.5cm, x: 2cm),
  
  header: context {
    let page_num = counter(page).get().first()
    if page_num > 1 [
      #grid(
        columns: (1fr, 1fr),
        align(left)[*SERVICE REPORT \#SR-99421*],
        align(right)[*Client:* Midwest Manufacturing Inc.]
      )
      #v(-4pt)
      #line(length: 100%, stroke: 0.5pt + rgb("#CBD5E0"))
    ]
  },
  
  footer: context [
    #align(center)[
      #text(8pt, fill: rgb("#718096"))[
        Page #counter(page).display() | Confidential Technical Field Service Documentation
      ]
    ]
  ]
)

#set text(font: "Liberation Sans", size: 10pt, fill: rgb("#2D3748"))

// Header Branding
#grid(
  columns: (1fr, 1fr),
  [
    #text(18pt, weight: "bold", fill: rgb("#2B6CB0"))[Nexus Industrial Systems] \
    #text(9pt, fill: rgb("#4A5568"))[Technical Field Services Division]
  ],
  align(right)[
    #text(20pt, weight: "bold", fill: rgb("#2B6CB0"))[SERVICE REPORT] \
    #v(2pt)
    *Report ID:* \#SR-99421
  ]
)

#v(1em)
#line(length: 100%, stroke: 1.5pt + rgb("#2B6CB0"))
#v(1em)

// Two-Column Metadata Box
#table(
  columns: (1fr, 1fr),
  fill: rgb("#EDF2F7"),
  stroke: 0.5pt + rgb("#CBD5E0"),
  inset: 8pt,
  [
    *Client:* Midwest Manufacturing Inc. \
    *Site Location:* Plant \#3 - 1400 Industrial Pkwy, Gary, IN \
    *Contact:* Robert Vance (Maintenance Mgr)
  ],
  [
    *Date of Service:* July 28, 2026 \
    *Technician:* David Miller (Tech ID: 8042) \
    *Asset / Serial ID:* CNC-Milling Unit \#04 (SN: 883-9021)
  ]
)

#v(1.5em)

// Narrative Section
== 1. Work Performed & Findings
#v(0.5em)
#rect(fill: rgb("#F7FAFC"), width: 100%, inset: 10pt, radius: 3pt, stroke: 0.5pt + rgb("#E2E8F0"))[
  Performed emergency diagnostic on main drive motor overheating issue. Replaced degraded thermal coupling, flushed cooling lines, and updated firmware to v4.2.1. System recalibrated and load-tested for 45 minutes under nominal factory conditions with zero thermal warnings.
]

#v(1.5em)

// Parts & Materials Table
== 2. Parts & Materials Used
#v(0.5em)
#table(
  columns: (90pt, 1fr, 40pt, 80pt),
  align: (left + horizon, left + horizon, center + horizon, right + horizon),
  fill: (col, row) => 
    if row == 0 { rgb("#2B6CB0") } 
    else if calc.even(row) { rgb("#F7FAFC") },
  stroke: 0.5pt + rgb("#E2E8F0"),
  inset: 7pt,

  [*#text(fill: white)[Part \#]*],
  [*#text(fill: white)[Description]*],
  [*#text(fill: white)[Qty]*],
  [*#text(fill: white)[Status]*],

  [TC-9042], [High-Temp Thermal Coupling], [1], [Replaced],
  [CL-1020], [Synthetic Coolant Fluid (1L)], [2], [Consumed],
  [FW-4200], [Firmware Update Flash Memory], [1], [Installed]
)

#v(1.5em)

// Status & Next Steps
== 3. Job Status & Follow-Up
#v(0.5em)
#grid(
  columns: (1fr, 1fr),
  gutter: 1em,
  rect(fill: rgb("#FEFCBF"), width: 100%, inset: 8pt, radius: 3pt, stroke: 0.5pt + rgb("#D69E2E"))[
    *Job Status:* Completed / Operable
  ],
  rect(fill: rgb("#EDF2F7"), width: 100%, inset: 8pt, radius: 3pt)[
    *Action Required:* Routine inspection recommended in 90 days.
  ]
)

#v(3em)

// Signatures Block
#grid(
  columns: (1fr, 1fr),
  gutter: 3cm,
  [
    #line(length: 100%, stroke: 0.5pt + rgb("#A0AEC0"))
    #v(-2pt)
    *Technician Signature:* David Miller
  ],
  [
    #line(length: 100%, stroke: 0.5pt + rgb("#A0AEC0"))
    #v(-2pt)
    *Client Sign-Off:* Robert Vance
  ]
)