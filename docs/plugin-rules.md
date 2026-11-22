# The plugin rules

These are the rules the cart's menu and the plugins site expect a plugin
to follow, and most of them come down to one line of CMake:

```cmake
cvcart_plugin(target ID developer.pluginname NAME "As The Menu Shows It" VERSION 1.2.3
              [BY "Developer"] [HOME /FOLDER] [OPENS .ext ...] [USES usb-host ...]
              [ICON icon.txt] [FILE_ICON icon.txt] [README README.md]
              [FILE NAME.CVP] [HEAP] [XIP])
```

## The ID: `developer.pluginname`

- An ID uses lowercase letters, digits, `.`, `-` and `_`. It has at
  least one dot and up to 32 characters, like `vexed.nes` or
  `jane-doe.chess`.
- The ID is permanent. The menu and the plugins site use it to tell
  plugins apart. Two files with the same ID count as the same plugin, and
  the menu uses the newer version and ignores the other. A new version
  with the same ID replaces the old one in the cart's flash. If you
  change the ID, it becomes a different plugin.
- The part before the first dot is your developer name. Use the same one
  for all your plugins, and pick one that nobody else uses. IDs that
  start with `parrotcart.` are for the plugins we make, so please pick
  your own prefix, like your name or GitHub username.
- The name (`NAME`, up to 32 characters) can change between versions.
  When two plugins have the same name, the menu shows the developer's
  name (`BY`, up to 32 characters) next to it.

## Versions: `major.minor.patch`

- A version is three numbers, like `1.0.3`. The menu compares them as
  numbers, so `1.10.0` is newer than `1.9.0`.
- Each release needs a new version, and its git tag is `v` followed by
  the version (`v1.0.3`). The release workflow stops if the tag doesn't
  match the version in the `.CVP`.
- Before `1.0.0`, you can change anything. After that, bump the major
  version when saves or files from the old version stop working.

## The API version

The SDK marks each `.CVP` with the plugin API version it was built for,
which is 1 today. If a cart's menu is older than a plugin's API, it won't
start the plugin and shows `NEEDS NEWER CART FIRMWARE` instead. Newer
carts run older plugins. You don't set the API version yourself. It
changes when you update `sdk/` (`tools/update-sdk.sh`) to an SDK with a
new API. If a release needs a newer cart, say so in your release notes.

## Size: 1 MB slots

The cart copies each plugin into its flash, in ten 1 MB slots, so the
plugins you used most recently start straight away. A plugin over 1 MB
takes two slots side by side. This sets a few limits:

- The image can be up to 2,093,056 bytes, which is 2 MB minus a 4 KB
  record. Up to 1,044,480 bytes (1 MB minus the record) it takes one
  slot. The `.CVP` adds a 512-byte header and the README to that.
- A plugin over 1 MB needs an up-to-date menu on the cart. An older menu
  says it's too big.
- Most plugins run from RAM, where the cart's bus timing is safe. The
  code, data and variables together have to fit below `0x2007EC00`,
  about 500 KB. If they don't, the link fails with a clear message.
- A plugin that's too big for RAM (the SCUMM plugin is 900 KB) is built
  with `XIP` and runs from its flash slot. The code that talks to the
  console still has to be in RAM. `sdk/.../cvplugin.cmake` and
  `cvxip.h` explain how.
- Keep big data on the card instead of in the plugin, and load it into
  the 8 MB PSRAM (`USES psram`).
- To use `malloc`, add `HEAP` to `cvcart_plugin()`. Without it, the build
  fails if anything links `malloc`. With `HEAP`, call `cvheap_reserve()`
  first (`cvheap.h`).

## The home folder

`HOME /NES` names the folder on the card for the plugin's files. It can
be up to 32 characters and starts with `/`. The menu and your users both
go by it, so games for a NES emulator go in `/NES`. Keep saves and
settings there too, or next to the file they belong to, and out of the
card's top folder. A home folder is optional.

## Extensions

`OPENS .nes .fds` lists the file types your plugin opens. You can list up
to 8, each a dot followed by 1 to 7 letters or digits, in any case. When
someone presses A on one of those files, the menu starts your plugin with
the file in its launch block (`examples/open-file`).

- When two plugins open the same extension, the menu shows an Open with
  list so you can choose. Please only list the types your plugin really
  reads.
- The menu opens ColecoVision games and movies itself: `.rom` and `.col`
  files that start with the ColecoVision header (`$AA55` or `$55AA`).
  Every other type, `.gb` and `.gbc` too, opens with whichever plugins
  list it.
- A plugin can list `.rom` too, because other machines' cartridges use it
  (an MSX cartridge starts with `AB`). It gets the `.rom` files that don't
  have the ColecoVision header. Menus from before November 19, 2026 hide
  those files, so on an older cart people need to rename them to another
  extension your plugin lists.
- `FILE_ICON` is the icon those files get in the menu (`docs/icons.md`).
- A plugin with no extensions runs on its own. The menu lists it on
  Home with the other apps (in Apps, or in the group of the folder it's
  in, such as `/PLUGINS/GAMES`), and A starts it. An older menu starts
  every plugin from its Plugins page instead.
- A plugin with extensions starts only with one of its files, unless it
  also has `USES standalone`. Then it is listed with the apps too and can
  start with no file, so it needs something to show without one, such as
  an empty page or a demo. In the Plugin Manager, A on a plugin that needs
  a file opens its `HOME` folder.

## What it uses

The menu shows `USES usb-host usb-device psram` on the plugin's details
page, and the plugins site shows it too, so people know what to plug in.
Use `usb-host` for pads, keyboards and mice, which need a powered hub.
Use `usb-device` when the cart plugs into a computer, and `psram` when
your plugin uses the PSRAM. `standalone` isn't something it uses: it says
the plugin can start without a file (above).

## What your plugin can use while it runs

Your plugin has all of the chip's RAM below `0x2007EC00`, the PSRAM, the
USB port (as a host or a device, one at a time), the card and the
console to itself.

## What every plugin needs to do

- Start the console side first, with `cvk_start()` or `cvf_start()` at
  the top of `main()`. Until then, the console is parked, waiting for a
  cartridge.
- Call `cvk_task()` (or `cvf_task()`) on every pass of your main loop,
  and keep each pass short. The task function checks the console's power
  and RESET, and takes the cart back to the menu when it's time. One long
  step at the start is fine, such as starting the card, which can take up
  to a second and a half. Avoid loops that wait, because RESET only
  works while the task function gets to run.
- Leave a note for the menu when there's something to say, like
  `cvk_note("Saved")`. A note can be up to 32 characters, and the menu
  shows the first 30.
- Open the launched file through the launch block
  (`cvsd_open_launched()`) instead of by its path, because a name with
  characters outside ASCII has `?` in the path.
- Be careful with the card. Your plugin can read anything, but it should
  only write to files it made (`fs_create()`, at their final size) and
  to the launched file if it edits that file. Please don't delete or
  overwrite other files, since they belong to the person using the cart.

## Things to avoid

- Writing to the cart's flash. The menu, the other plugins and the cart's
  settings are stored there. A broken plugin can't brick the cart,
  because its USB recovery is in ROM, but it could mean reinstalling the
  menu.
- Making the console wait, for example with `sleep_ms()` in the loop or
  by waiting for USB. The task function needs to run often to keep RESET
  working.
- Using both of the SDK's kernels, or calling TinyUSB's host functions
  from the loop while `cvpad` is running (`cvpad.h`).

## Sharing a plugin

Plugins are free downloads, separate from the cart. Nobody bundles them
with a cart or sells them. Every release has credits and a link to its
source (`docs/release-checklist.md`).

You choose your plugin's license. The template and the SDK are MIT
(`LICENSE`, `THIRD-PARTY.md`), so you can release your plugin under any
license you like, including the GPL. If you use an emulator core, its
license often decides for you. Whatever you pick, the release is free,
its README has credits, and it links to the source. If your plugin
contains GPL or LGPL code, that license requires the source link, and
the link has to point to the source of the exact version you released.
