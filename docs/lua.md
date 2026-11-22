# Lua

You can write programs for the cart in [Lua](https://www.lua.org), a small
language that's easy to learn. There are two ways to use it.

- **On the console.** The Lua plugin (`LUA.CVP`) has a text editor. Plug in
  a USB keyboard, type a script, and press Ctrl+R to run it.
- **With this kit.** Write a script on your computer, try it in the
  simulator, and build it into a plugin with a name and icon in the
  cart's menu.

Both run the same code, and a script written one way works the other way.

## A first script

```lua
local x = 0

function update()
  x = (x + 1) % 256
end

function draw()
  cls()
  print("Hello", 100, 80, YELLOW)
  circfill(x, 120, 6, LIGHT_GREEN)
end
```

On the console, start Lua from Apps on Home (on an older menu, the
Plugins page). It lists the scripts on
the card. Press Esc to leave the list and Ctrl+N for a new script, type
this in, and press Ctrl+R (or 4 on the keypad). Lua asks for a name the
first time and saves the script in the card's `/LUA` folder.

On your computer, save it as `first.lua` and run:

```sh
sim/lua.sh first.lua
```

That opens the script in the same editor, in a window. Press 4 to run it.
Add `--run` to skip the editor and start the script at once. The simulator
needs SDL3 and a C compiler (`docs/simulator.md`).

To put a script on the card yourself, copy it into `/LUA`. Then open it
from the cart's menu like any other file.

## The keys

In the editor:

| Key | What it does |
|---|---|
| Ctrl+R, or keypad 4 | saves the script and runs it |
| Ctrl+S, or keypad 1 | saves |
| Ctrl+O, or keypad 2 | lists the scripts in `/LUA` to open one |
| Ctrl+N, or keypad 3 | starts a new script |

While a script runs:

| Key | What it does |
|---|---|
| Esc | stops the script and goes back to the editor |
| keypad `*`, held for two seconds | the same, without a keyboard |
| RESET | goes back to the cart's menu |

When a script has a mistake, Lua stops it and goes back to the editor. The
cursor is on the line, and the message is at the top and bottom of the
screen until you press a key.

## How a script runs

Lua runs your file once from top to bottom. Then it calls three functions,
if you wrote them:

| Function | When Lua calls it |
|---|---|
| `init()` | once, after the file has run |
| `update()` | 60 times a second. Move things and read the controls here |
| `draw(cleared)` | each time the console is ready for a new picture. Draw here |

A script needs `update()` or `draw()`, or both.

Draw the whole picture in `draw()` every time. The cart compares it with
what the TV already shows and sends the console only the difference, so
redrawing costs little. `cleared` is true when the screen was wiped and
nothing of your last picture is left. A script that builds its picture up
bit by bit uses it to know when to start over (`lua/examples/colours.lua`).

## The screen

The screen is 256 pixels wide and 192 high. `x` runs from 0 at the left to
255, and `y` from 0 at the top to 191. Drawing off the edge is fine.

There are 15 colours. Each has a number and a name:

| | | | |
|---|---|---|---|
| 1 `BLACK` | 2 `GREEN` | 3 `LIGHT_GREEN` | 4 `DARK_BLUE` |
| 5 `BLUE` | 6 `DARK_RED` | 7 `CYAN` | 8 `RED` |
| 9 `LIGHT_RED` | 10 `DARK_YELLOW` | 11 `YELLOW` | 12 `DARK_GREEN` |
| 13 `MAGENTA` | 14 `GRAY` | 15 `WHITE` | |

Colour 0 is the video chip's "see-through", which shows as black here.

The console can show two colours in each group of 8 pixels along a row
(pixels 0 to 7, 8 to 15, and so on), so a third colour in a group turns
into one of the other two. Drawing on a grid of 8 pixels keeps colours
apart, and sprites have no such limit.

When you leave a colour out, the function uses the last colour you gave.

### Drawing

| Function | What it does |
|---|---|
| `cls(colour)` | fills the screen. Without a colour, black |
| `pset(x, y, colour)` | sets one pixel |
| `pget(x, y)` | gives the colour of a pixel, or 0 off the screen |
| `line(x1, y1, x2, y2, colour)` | draws a line |
| `rect(x, y, w, h, colour)` | draws the outline of a box `w` wide and `h` high |
| `rectfill(x, y, w, h, colour)` | draws a filled box |
| `circ(x, y, r, colour)` | draws a circle around `x, y` |
| `circfill(x, y, r, colour)` | draws a filled circle |
| `print(text, x, y, colour, scale)` | writes text. A letter is 6 pixels wide and 8 high, and `scale` 2 doubles that. It gives back the `x` after the last letter |
| `print(text)` | writes on the next line down, starting at the top after `cls()`. In the simulator it also goes to the terminal, which is handy for finding mistakes |
| `image(rows)` | makes a small picture from a list of strings. Each character is a pixel: `0` to `9` and `a` to `f` are colours 0 to 15, and `.` is see-through |
| `blit(img, x, y, flip_x, flip_y, colour)` | draws a picture made by `image`. `flip_x` and `flip_y` mirror it. With a colour, every pixel is drawn in that colour |

A picture from `image` has `img.w` and `img.h`, its width and height.

### Sprites

A sprite is a small shape in one colour that the console draws on top of
the picture. Moving a sprite is quick, and it never rubs out what's under
it, so sprites are the way to make things move smoothly.

| Function | What it does |
|---|---|
| `shape(n, rows)` | defines shape `n` from 8 strings of 8 characters, or 16 of 16. A `.` is see-through and anything else is a pixel |
| `sprite(n, x, y, shape, colour)` | shows sprite `n` (1 to 32) at `x, y` with that shape |
| `sprite(n)` | hides sprite `n` |
| `zoom(true)` | shows every sprite at twice its size. `zoom(false)` goes back |

There are 32 sprites, and sprite 1 is in front. Shapes are all 8x8 (up to
256 of them) or all 16x16 (up to 64). The console shows 4 sprites on any
one row of the TV picture. A fifth on the same row doesn't show there.

```lua
shape(1, {
  "..####..",
  ".######.",
  "########",
  "########",
  "########",
  "########",
  ".######.",
  "..####..",
})
sprite(1, 120, 90, 1, RED)
```

## Sound

The console's sound chip has three voices that play tones and one that
plays noise. Volume goes from 0 (silent) to 15, and 15 is what you get if
you leave it out. `time` is how long the sound lasts, counted in updates,
so 60 is one second. Without it the sound plays until you change it.

| Function | What it does |
|---|---|
| `tone(voice, hz, volume, time)` | plays a tone on voice 1, 2 or 3. The lowest the chip can play is 110 Hz |
| `note(voice, name, volume, time)` | plays a note by name, like `"C4"`, `"F#3"` or `"Bb5"`. `"C4"` is middle C. A number is a MIDI note number |
| `noise(kind, volume, time)` | plays noise. Kinds 1 to 3 are a hiss from high to low, and 4 is a hiss at the pitch of voice 3. Kinds 5 to 8 are the same as a buzz |
| `mute()` | stops all four |

## The controller

`btn` and `btnp` read the console's controller. If a USB game pad is
plugged in, it counts too. Its d-pad or left stick is the stick, B is
`fire`, A is `fire2`, and X and Y are the keypad's `*` and `#`.

| Function | What it does |
|---|---|
| `btn(name)` | true while that button is held. The names are `"up"`, `"down"`, `"left"`, `"right"`, `"fire"` (the left button) and `"fire2"` (the right one) |
| `btnp(name)` | true only on the update when it was pressed |
| `keypad()` | the keypad key that's held, as `"0"` to `"9"`, `"*"` or `"#"`, or `nil` |
| `keypadp()` | the keypad key that was just pressed, or `nil` |

A script started from the editor stops when `*` is held for two seconds.

## USB

USB devices plug into a powered hub, and the hub into the cart.

| Function | What it does |
|---|---|
| `key(name)` | true while that key of a USB keyboard is held. A key's name is its character (`"a"`, `"1"`, `"/"`) or a word: `"space"`, `"return"`, `"backspace"`, `"tab"`, `"up"`, `"down"`, `"left"`, `"right"`, `"lshift"`, `"lctrl"`, `"f1"` and so on |
| `pad(n)` | USB game pad 1 or 2 as a table, or `nil` if it isn't plugged in. The buttons are true or false: `up`, `down`, `left`, `right`, `south`, `east`, `west`, `north`, `l`, `r`, `zl`, `zr`, `minus`, `plus`, `home`, `capture`, `lstick`, `rstick`. The sticks are `lx`, `ly`, `rx`, `ry`, from -2048 to 2047 with up as positive. `kind` is the pad's type |
| `mouse()` | a USB mouse as a table, or `nil`. `x` and `y` are a pointer that stays on the screen, `dx` and `dy` are how far it moved since the last update, `left`, `right` and `middle` are its buttons, and `wheel` is the wheel |
| `midi.send(byte, ...)` | sends a MIDI message to a MIDI device on the hub. It gives back true if it went. A string is sent as it is |
| `usb.status()` | a table that counts what's plugged in: `pads`, `keyboards`, `mice`, and `midi` (true or false) |
| `usb.mode()` | `"host"` when the port is ready for devices |

Buttons on a pad are named by where they are. `south` is the bottom one,
which is B on a Nintendo pad and A on an Xbox pad.

Lua also calls these functions, if you wrote them:

| Function | When Lua calls it |
|---|---|
| `keypressed(key, char)` | a key went down. `char` is the character it types, or `nil` for a key like an arrow |
| `keyreleased(key)` | a key came up |
| `midimessage(status, a, b)` | a MIDI message came in from a MIDI keyboard |

Esc stops a script that was started from the editor, so a script doesn't
see that key.

## Time and memory

| Function | What it does |
|---|---|
| `time()` | seconds since the script started |
| `frame()` | how many times `update()` has run |
| `fps()` | pictures sent to the console in the last second |
| `mem()` | two numbers: bytes of memory in use, and bytes free |
| `menu_note(text)` | leaves a line for the cart's menu to show when you go back to it. Up to 32 characters |

`WIDTH` is 256 and `HEIGHT` is 192.

## Files on the card

A name without a folder means a file next to your script. A name that
starts with `/` is a path from the top of the card.

| Function | What it does |
|---|---|
| `file.save(name, text)` | keeps a string in a file, for a high score or a saved game. The file is made the first time, with room for 4000 bytes. `file.save(name, text, room)` makes it with a different amount of room |
| `file.load(name)` | gives the string back, or `nil` if there's no save yet |
| `file.read(name, offset, count)` | reads a file as a string. Without `offset` and `count`, all of it |
| `file.size(name)` | its size in bytes, or `nil` if there's no such file |
| `file.list(folder)` | the folder's entries. Each is a table with `name`, `size` and `folder` (true for a folder) |
| `file.create(name, size)` | makes a file of that size, full of zeros, if it isn't there yet. It gives back true if it made it |
| `file.write(name, offset, text)` | writes over part of a file |

A file on the card can't grow after it's made. That's why `file.save`
makes the file with room to spare, and why `file.write` only writes over
what's there. A script reads up to 4 MB at a time and makes files up to
1 MB.

The card may be missing, so a save can fail. A game that should keep going
wraps it:

```lua
pcall(file.save, "SCORE.SAV", tostring(best))
```

## More than one file

| Function | What it does |
|---|---|
| `require("name")` | runs `name.lua` from the script's folder, once, and gives back what it returned |
| `dofile(name)` | runs a file every time it's called |
| `loadfile(name)` | reads a file and gives it back as a function |

## Lua on the cart

This is Lua 5.4 with these libraries: the basic functions, `string`,
`table`, `math` and `coroutine`. There is no `io`, `os`, `debug`, `utf8` or
`package`. The Lua manual is at <https://www.lua.org/manual/5.4/>.

Numbers are 32 bits. Whole numbers go up to 2,147,483,647, and other
numbers keep about 7 digits. The cart's chip does 32-bit arithmetic in one
step, and each value takes half the memory that 64-bit numbers would.

Brackets, function calls from C and `pcall` can nest 30 deep. A function
that calls itself can go much deeper than that, because Lua keeps those
calls on the heap, not on the chip's stack.

## When something goes wrong

- A mistake in the script stops it. From the editor you go back to the
  line. A script built into a plugin shows the message and the line on
  the screen, and the fire button runs it again.
- A function that runs for 5 seconds without finishing is stopped the same
  way. The message has the line it was on.
- RESET works at any time, even in a loop that never ends.

## Speed

On the cart, Lua runs about 3 million simple steps a second, which is
about 50,000 in each TV frame. Some figures from `lua/test/onarm.sh`:

| | Time on the cart |
|---|---|
| 200,000 turns of a loop that adds numbers | 0.20 s |
| moving 100 things with `x`, `y`, `dx`, `dy` fields | 0.7 ms |
| `update()` for a game with 20 things, each checked against the others | 1.0 ms |
| `fib(22)`, 57,000 function calls | 0.11 s |
| filling, adding up and sorting a list of 5,000 numbers | 0.15 s |

A TV frame is 16.7 ms, so there's room for a game with a few dozen things
on the screen. Lua is too slow to write an emulator in.

If `update()` takes longer than a frame, Lua calls it less often and
catches up by up to 4 calls when it can.

The picture is a separate limit. The console can take about 1,900 bytes
of changes in a frame.

| What changes | Pictures a second |
|---|---|
| sprites only, or a few small things | 60 |
| a full line of text | 30 to 60 |
| the whole screen | 4 to 8 |

So move things with sprites, and change the background a little at a time.
`fps()` tells you what a script is getting.

## Memory

A script has two kinds of memory, and Lua uses both without your help.

- About 60 KB of the chip's memory, which is fast. Small things go
  here: numbers, short strings, small tables.
- 8 MB of slower memory. Anything of 4 KB or more goes here, and so does
  everything else once the fast memory is full.

`mem()` gives the totals.

## A script as a plugin

A Lua app can be a `.CVP` with a name and icon. The script is built
into the plugin, so there's nothing else to copy to the card. It starts at
once, with no editor.

```cmake
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(mygame C CXX ASM)
parrotcart_init()

add_executable(mygame)
cvcart_features(mygame PSRAM CARD PADS)
parrotcart_lua(mygame MAIN src/main.lua FILES src/enemy.lua src/level1.txt HOME /MYGAME)
cvcart_plugin(mygame ID yourname.mygame NAME "My Game" VERSION 0.1.0 BY "Your Name"
              HOME /MYGAME ICON src/icon.txt README README.md)
```

- `MAIN` is the script the plugin starts.
- `FILES` are the rest. `require` and `file.read` find them by their path
  from `MAIN`'s folder, so `src/enemy.lua` is `require("enemy")`.
- `HOME` is the folder on the card for the app's saves. Give
  `cvcart_plugin` the same one. The folder has to be on the card already.
- `cvcart_features` always needs `PSRAM` and `CARD`. Add `PADS`,
  `KEYBOARD`, `MOUSE` or `MIDI_IN` for the USB devices your script uses
  (`docs/features.md`). A function whose feature is off stops the script
  with a message that says so. Each feature you leave out gives Lua more
  fast memory.

Build it the usual way (`docs/getting-started.md`) and copy the `.CVP`
into `/PLUGINS`. Say in your README's credits that the plugin contains Lua
(MIT, <https://www.lua.org>).

To try the app on your computer before you build it:

```sh
sim/lua.sh src --run
```

## Examples

These are in `lua/examples`. Copy them into `/LUA` on the card to try them
on the console.

| Script | What it shows |
|---|---|
| `hello.lua` | text, a shape that moves, the stick, a sound |
| `sprites.lua` | twelve sprites bouncing around |
| `colours.lua` | a pattern in all 15 colours, drawn a little at a time |
| `snake.lua` | a small game with a score that's saved on the card |

## For people working on the runtime

The Lua plugin is in `lua/`. `lua/main.c` starts with a description of how
it works.

```sh
cmake -S lua -B build-lua -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build-lua                  # build-lua/LUA.CVP
lua/test/run.sh                     # the runtime on the console's model
lua/test/onarm.sh                   # the cart's build on an emulated chip
sim/test.sh lua                     # the editor and scripts in the simulator
```

`lua/test/onarm.sh` needs Python with `unicorn` and `pyelftools`. It runs
the speed test above, checks that the deepest nesting fits the stack, and
runs the API test on the cart's code.
