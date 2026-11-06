Typst-Like C Rendering Engine & Viewer
A standalone, dependency-free C implementation of a Typst-subset document layout engine, PDF exporter, and interactive Windows GDI previewer.
The engine parses Typst-style markup, builds an Abstract Syntax Tree (AST), computes page layouts and line wrapping, produces PDF 1.4 documents from scratch (without external libraries like Cairo or HarfBuzz), and maintains a GDI display list for real-time document viewing.
---
1. Features
Direct PDF Generation: Emits raw PDF-1.4 file structures (Catalog, Pages, Type1 fonts, XRef tables, and content streams) directly into memory and files.
Interactive Windows GDI Viewer:
Double-buffered canvas with smooth drag panning (`Pan: ON/OFF`).
Zoom modes: Fit Page, Fit Width, and custom zoom scale steps.
Interactive page-by-page rendering with jump to page (`|<`, `<`, `>`, `>|`).
In-memory text search with page navigation (`Srch Prev`, `Srch Next`).
Toggleable floating overlay toolbar (toggle with canvas click).
Responsive layout re-flow mode ("Match Window").
Built-in Barcode Generators:
Code 128: High-density 1D alphanumeric barcodes with modulo 103 checksum calculation.
UPC-A: Standard retail barcodes with automatic check-digit calculation.
DataMatrix (ECC 200): 2D matrix symbology complete with Galois field ($GF(256)$) Reed-Solomon error correction and module placement.
QR Code: 2D matrix symbology (Versions 1–10) with automatic version discovery, byte-mode packing, alignment patterns, and error correction.
PDF417: 2D stacked barcode with cluster lookup tables, CW padding, and Reed-Solomon ECC.
Charting Primitives:
Bar charts and line charts with optional linear trend line regression (`trend-line: true`).
Pie charts with bezier-arc wedge construction and side legend.
Typst Markup Subset:
Document & page setup (`#set page(...)`, `#set text(...)`).
Document headers & footers with dynamic page counters (`#counter(page).display()`).
Macros & function declarations (`#let ...`).
Grids & Tables (`#grid(...)`, `#table(...)`) supporting fractional column widths (`fr`), alternating row fills, custom cell alignment, and borders.
Layout primitives: `#rect`, `#box`, `#block`, `#line`, `#v()`, `#align()`, and lists (`- `).
Inline styling: bold (`*text*`), escape characters, font size, and color overrides.
---
2. Compilation
Win32 Target (GCC / MinGW)
Build the standalone single executable using MinGW-w64 or classic MinGW:
```bash
gcc -Os -s -o typst.exe typst.c -lm -lgdi32 -luser32 -lcomdlg32
```
(Ensure `pdf417_clusters.h` is located in your include search path).
Win16 Target (Windows 3.1x / Open Watcom / Borland C++)
Running this engine under 16-bit Windows requires addressing the 64 KB segment architecture, Win16 GDI function naming, and memory limits.
Key Porting Differences:
Window Longs: Replace `SetWindowLongPtr` and `GWLP_WNDPROC` with `SetWindowLong` and `GWL_WNDPROC`.
Text Metrics & Drawing:
`GetTextExtentPoint32` is replaced with `GetTextExtent`.
Win16 does not support font escapement/rotation in standard GDI without TrueType mapping; rotated labels fallback to unrotated or bitmap blits.
Memory Allocations:
The Win32 static arrays (`source[MAX_STR_LEN * 20]`, `dl_items[32768]`, stream buffers) exceed the 64 KB near-data segment (`DGROUP`).
All large tables must be allocated on the global heap using `GlobalAllocPtr` / `GlobalFreePtr` (`windowsx.h`), or moved to huge pointers.
Toolchain Directives:
Must use the Large (`-ml`) or Huge memory model.
Compiling with Open Watcom (16-bit):
```bash
wcl -q -bt=windows -l=windows -ml -bcl=windows typst16.c commdlg.lib
```
Compiling with Borland C++ 3.1 / 4.52:
```bash
bcc -W -ml -2 -O2 -w- typst16.c commdlg.lib
```
---
3. CLI & GUI Usage
Command Line Mode
```bash
# Render a Typst file to PDF and launch the GDI previewer:
typst.exe document.typ

# Specify a custom output PDF path:
typst.exe document.typ output.pdf
```
If launched directly without arguments (or double-clicked from File Manager/Explorer), the program opens in interactive viewer mode with an empty workspace.
Interactive Viewer Controls
Toggle Toolbar: Left-click anywhere on the canvas background.
Pan Canvas: Left-click and drag across the page when `Pan: ON` is selected.
Keyboard Navigation:
`Right Arrow` / `Down Arrow` / `Page Down` / `Space`: Next Page.
`Left Arrow` / `Up Arrow` / `Page Up`: Previous Page.
Search: Enter text in the search input box and press `Enter` or click `Srch Next` / `Srch Prev`.
Zoom & View Adjustment:
`Fit Page`: Scales the document to fit inside the viewport.
`Fit Width`: Expands the document horizontally to match the view width.
`Match Window`: Re-flows the document markup using the current window size as the page size.
`+` / `-`: Incremental zoom adjustments.
---
4. Supported Markup Examples
Document Setup & Page Counters
```typst
#set page(
  paper: "a4",
  margin: (top: 2cm, bottom: 2cm, x: 1.5cm),
  header: [#text(size: 8pt)[Department of Radiology] #h(1fr) #counter(page).display()],
  footer: [Page #counter(page).display() of %d]
)
#set text(size: 10pt, fill: "#2D3748")
```
Tables with Fractional Widths (`fr`)
```typst
#table(
  columns: (2fr, 1fr, 1fr),
  align: (left, center, right),
  fill: "col-accent",
  stroke: 0.5pt + "#CBD5E0",
  [Examination], [Status], [Score],
  [Lumbar Spine L1-L4], [Normal], [0.848],
  [Femoral Neck], [Osteopenia], [-1.42]
)
```
Charting
```typst
#bar-chart(
  title: "Annual Revenue",
  x-label: "Quarter",
  y-label: "USD (Thousands)",
  fill: "#2563eb",
  ("Q1", 45), ("Q2", 68), ("Q3", 85), ("Q4", 92)
)

#pie-chart(
  title: "Department Budget Distribution",
  ("Research", 40),
  ("Operations", 25),
  ("Marketing", 20),
  ("Legal", 15)
)
```
Barcodes
```typst
// Code 128
#barcode(type: "code128", data: "PATIENT-98402", width: 200pt, height: 40pt)

// 2D QR Code
#barcode(type: "qr", data: "https://example.org/records/98402", width: 80pt, height: 80pt)

// DataMatrix
#barcode(type: "datamatrix", data: "DM-SECURE-DATA-50", width: 70pt, height: 70pt)

// PDF417
#barcode(type: "pdf417", data: "ID:98402|SURNAME:DOE|GIVEN:JOHN|DOB:1980-01-01", width: 250pt, height: 50pt)
```
---
5. Technical Limitations & Bounds
Parameter	Limit	Note
`MAX_NODES`	8,000	Maximum syntax elements in AST
`MAX_PAGES`	256	Maximum generated PDF document pages
`MAX_TABLE_COLS`	16	Maximum columns per table or grid
`MAX_GRID_ROWS`	128	Maximum rows per table or grid
`MAX_STR_LEN`	16,384	Internal text run / token buffer size
`STREAM_BUF_SIZE`	1,048,576	Initial per-page PDF stream buffer
Display List Cap	32,768 items	Win32 GDI drawing commands buffer
---
6. License
This software is released into the Public Domain. You are free to use, modify, distribute, or incorporate it into commercial and non-commercial products without restriction or warranty.
