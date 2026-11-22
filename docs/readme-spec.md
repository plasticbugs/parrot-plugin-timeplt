# The README

Your repository's `README.md` is also your plugin's manual. The cart
shows it on the plugin's details page, a page at a time, 30 characters
wide, in the menu's uppercase font. On the plugins site, your plugin's
page shows the same cart text, plus the `README.md` itself formatted for
the web.

You write one file in ordinary GitHub-flavored Markdown.
`cvcart_plugin(... README README.md)` converts it when you build the
plugin and stores the text in the `.CVP`.

Release notes are separate. They're the text of the GitHub release, and
only the plugins site shows them.

## What to write

The cart shows the README from the top, and 30 columns is narrow, so
write the top part for the person holding the controller:

1. One or two sentences about what it is.
2. The controls, as a list with one control per line.
3. Anything else they need to use it, like where files go on the card
   and what to plug in.
4. Credits for every emulator core, library, font, tune and picture that
   isn't yours, each with its license and a link to its source.
5. Your license, and a link to your source code.

After that, add whatever else you like, such as build steps, history or
thanks. The cart shows that part too, further down.

This repository's `README.md` follows that pattern, and each example has
a short README too.

## How Markdown becomes cart text

The SDK converts it with `sdk/firmware/revc/sdk/cvreadme.py`. To see what
the cart will show, run:

```sh
python3 sdk/firmware/revc/sdk/cvreadme.py README.md
```

| You write | The cart shows |
|---|---|
| a heading | the heading in capitals, with a blank line before and after |
| a paragraph | the text, rewrapped to 30 columns |
| a list (`-`, `*`, `+`, `1.`) | each item starting with `- ` or `1. `, with wrapped lines lined up under its first word. Nested lists are indented further |
| a link, `[text](https://...)` | the text, then the URL. A URL that doesn't fit on the rest of the line starts a new line |
| a link inside the repository (`LICENSE`, `#controls`) | just the text |
| `code` | just the text |
| a code block | the block as it is, with each line cut at 30 columns |
| `**bold**`, `*italic*`, `~~struck~~` | just the text |
| an image | `[image: its alt text]` |
| a table | a `header: value` line for each cell, with a blank line between rows |
| a block quote | the text, indented 2 columns |
| HTML | the text, without the tags |
| curly quotes, dashes, accents | plain ASCII look-alikes. Any other character becomes `?` |

Here are some tips that follow from that:

- Put each link at the end of its sentence or list item, or on a line
  by itself as `<https://...>`. A URL in the middle of a sentence shows
  up in the middle of the sentence on the cart too.
- Use lists instead of tables for controls. A table's rows turn into
  pairs like "Control: Stick / Does: moves", which take more room and
  aren't any clearer.
- Keep code blocks to 30 columns, or put them further down. The cart
  cuts longer lines instead of wrapping them.
- Write alt text for images, because it's all the cart shows of them.
- Badges and banners at the top turn into `[image: ...]` lines on the
  cart, so put them lower down or leave them out.
- The cart's font has uppercase letters, digits and `! # & ' ( ) * + , -
  . / : ?`. Other characters are stored as you wrote them, and the
  plugins site shows them, but they appear as `?` on the TV.

## The limits

- The converted text can be up to 16,384 bytes, which is about 500
  lines. Anything longer is cut at the end of a line, with a last line
  that says `[The README is cut here]`. For a long manual, put it on the
  web and link to it.
- The text can only have printable ASCII and line ends, and no line can
  be longer than 30 characters. The converter makes sure of both.

## In the file

If you write a tool that reads `.CVP` files, the README comes after the
image. The header's 32-bit little-endian word at offset 280 is the
README's offset from the start of the file (0 means there's no README),
and the word at 284 is its length in bytes. The image and its SHA-256
stay the same with or without a README.

If you give `README` a `.txt` file, the SDK treats it as finished cart
text. It checks the file against the limits and doesn't convert it.
