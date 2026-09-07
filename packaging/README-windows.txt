qMDviewer - a lightweight Markdown viewer
========================================

Portable build for 64-bit Windows 10 and 11. Nothing needs to be installed:
unzip the folder anywhere and run qmdviewer.exe. Everything it needs, including
the Qt libraries, is in this folder.

Getting started
---------------
  * Double-click qmdviewer.exe, then press Ctrl+O to open a file.
  * Or drag a .md file onto the window (or onto qmdviewer.exe).
  * Or from a command prompt: qmdviewer.exe notes.md

Open sample.md in this folder to see what the renderer supports.

Handy keys
----------
  Ctrl+O                     Open a file
  F5 / Ctrl+R                Reload now
  Ctrl+F, then F3/Shift+F3   Find, next / previous match
                             (Enter jumps to the match and closes the box)
  Ctrl++ / Ctrl+- / Ctrl+0   Zoom in / out / reset, preview and editor
                             (also Ctrl+mouse wheel)
  Alt+Left / Alt+Right       Back / forward between linked files
  Ctrl+Shift+O               Toggle the outline panel
  Ctrl+M                     Hide or show the menu bar
  Ctrl+Shift+B               Keep or re-flow single line breaks
  Ctrl+P                     Print
  Ctrl+E                     Show or hide the source editor
  Ctrl+Shift+V               Toggle vim mode in the editor
  Ctrl+S                     Save

Right-click the document for the same commands when the menu bar is hidden.
The menu bar, toolbar, status bar and outline panel can all be hidden from the
View menu, and the choice is remembered.

Fenced code is syntax coloured when the fence names a language. File > Export
as PDF writes a contents page plus a real outline, so the headings appear in
your PDF reader's sidebar.

Ctrl+E opens a source editor next to the preview, which updates as you type;
Ctrl+S saves. Ctrl+Shift+V turns on vim mode (normal/insert/visual modes,
operators, registers, /search and :w, :q, :s/// commands).

With vim mode on, both panes run the same keys. The preview gets a real cursor
(a block in vim mode, a bar otherwise): h/j/k/l and w/b/e move it, 0/^/$ work
on the line, {/} step through paragraphs, [[ and ]] through headings, gg/G jump
to the ends, Ctrl+D/Ctrl+U move half a page and Ctrl+F/Ctrl+B a page, v/V then
y selects and copies, * and # search for the word under the cursor, and n/N
repeat. Note that Ctrl+F pages down in vim mode rather than opening the find
bar - press / to search instead, then n, N or F3 to repeat.


The open file is watched on disk: edit it in your editor and the view updates
immediately, keeping your place. Turn that off under File > Reload on Change.

Notes
-----
  * Windows may show a "Windows protected your PC" SmartScreen prompt because
    the executable is not code-signed. Choose More info > Run anyway.
  * Settings (window size, zoom, theme, recent files) are stored in the
    registry under HKEY_CURRENT_USER\Software\qMDviewer.
  * Do not move qmdviewer.exe out of this folder on its own; it needs the DLLs
    and the platforms\ folder next to it.
