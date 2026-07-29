// Set document metadata
#set title("My First Typst Document")
#set author("Jane Doe")
#set date(today())

= Introduction to Typst

Typst is a modern, fast, and user-friendly typesetting system.
It uses a clean syntax and compiles instantly to PDF.

== Features
- Simple markup
- Fast compilation
- Built-in math support: $E = mc^2$
- Easy styling

== Example Table
#table(
  columns: 3,
  [Name], [Age], [Role],
  ["Alice"], [30], ["Engineer"],
  ["Bob"], [25], ["Designer"]
)

== Example Image
#image("example.png", width: 50%)

== Conclusion
This is a minimal example. You can extend it with custom styles, templates, and more.
