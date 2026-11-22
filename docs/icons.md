# Icons

A plugin has two icons. Each is 8x8 pixels with one bit per pixel, so a
pixel is either on or off. The menu sets the color.

| Icon | CMake | Where the menu shows it | Color |
|---|---|---|---|
| the plugin icon | `ICON icon.txt` | next to the plugin's name, on the Plugins page and anywhere it's listed as an app | **magenta**, meaning A starts it |
| the file icon | `FILE_ICON file_icon.txt` | next to each file the plugin opens (`OPENS`), in the card's folders | **light green**, meaning A opens it in another system |

Both are drawn on black. You can't choose the color, because the menu
uses color to show what pressing A will do. Yellow folders open, cyan
items play on the console itself, green files open with a plugin, and
magenta items start one. For that reason, every plugin icon is magenta
and every plugin's file is green.

If you leave out `ICON`, the plugin gets the menu's plug icon. If you
leave out `FILE_ICON`, its files get the plain file icon. An icon with no
pixels turned on counts as no icon.

## The file

An icon is a plain text file with 8 lines of 8 characters. Use `#` for a
pixel that's on and `.` for one that's off. The SDK skips blank lines,
and anything else stops the build with an error.

```
..####..
.#....#.
#..##..#
#......#
#..##..#
#..##..#
.#....#.
..####..
```

In the `.CVP`, each icon is 8 bytes, one byte per row from the top, with
the left pixel in bit 7. The plugin icon is at offset 264 of the header,
and the file icon is at 272.

## Drawing one

You can draw them with the icon editor on the plugins site. It shows
them in the menu's colors and gives you the text to paste:
<https://plasticbugs.github.io/parrot-cart-plugins/>

You can also type the 64 characters yourself.

Here are some tips for icons that read well on a TV at this size:

- Draw a filled silhouette instead of an outline with details. A single
  stray pixel looks like noise on a CRT.
- Make lines two pixels wide where you can, because one-pixel vertical
  lines shimmer on composite video.
- If the shape allows, leave the top row, the bottom row and the right column
  empty, as the menu's icons do. Then icons in a list don't run into each
  other or into the name beside them.
  Icons sit right next to the first letter of the name.
- Make the file icon look related to the plugin icon, such as the same
  shape on a page, so a folder of your files is easy to recognize.

The menu can show up to 7 different plugin icons and 6 different file
icons on one page. Beyond that, the rest of the page uses the plug and the plain file
icon.
