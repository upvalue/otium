---
id: lan-gbr8
status: open
deps: []
links: []
created: 2026-10-21T22:25:04Z
type: task
priority: 2
assignee: Phil
tags: [oct-21-cq, manse, ui]
---
# Stop Manse widgets copying otium.ui code

manse/widgets.ot:9 inset copies the private one in lib/otium/ui/gpu.ot:270; Item's paint (widgets.ot:53) copies gpu.ot:328 paint-box. Item is Row/Column plus :hover and :on-press. ui.ot:62-88 Row and Column declare the same 11 fields, and classes don't inherit fields, so one Box class with a direction flag (as Item does with :column?) could replace all three.


## Notes

**2026-10-21T22:51:22Z**

Strawman for discussion: keep public Row and Column constructors, share inset and box painting, and add hover/on-press to their common implementation. Replacing Row, Column, and Item with Box changes dispatch-type and user methods; confirm whether those public class identities must survive before consolidating them. Field inheritance would be a separate language change.
