# Releasing a plugin

A release is a git tag, `vX.Y.Z`, and a GitHub release with the `.CVP`
and up to three screenshots attached. We read your releases for the
plugins site, and we never build anything from them.

## Before you tag

- [ ] The ID is yours and final (`docs/plugin-rules.md`), not
      `yourname.hello`.
- [ ] `VERSION` in `CMakeLists.txt` is the new version, higher than the
      last release's.
- [ ] `README.md` starts with what the plugin is, its controls and its
      credits (`docs/readme-spec.md`). To see it the way the cart will
      show it, run `python3 sdk/firmware/revc/sdk/cvreadme.py README.md`
- [ ] The credits list every core, library, font, sound and picture you
      didn't make, with its author, its license and a link to its
      source. A plugin built with this kit contains the Parrot Cart SDK
      (MIT) and the Raspberry Pi Pico SDK (BSD 3-Clause), plus TinyUSB
      (MIT) if it uses USB.
- [ ] The README links to your source code. For GPL or LGPL code, link
      to the exact version you released (the tag).
- [ ] `LICENSE` is your plugin's license. The template's is MIT, and
      yours can be anything (`docs/plugin-rules.md`, "Sharing a
      plugin"). If you change it, keep `THIRD-PARTY.md` and the MIT text
      for the SDK's part.
- [ ] The icons read well in magenta and green on black
      (`docs/icons.md`).
- [ ] The screenshots are in `screenshots/` as `screenshot-1.png` to
      `screenshot-3.png`, each exactly 256x192 and under 100 KB
      (`docs/screenshots.md`). Check them with
      `python3 tools/png256.py check screenshots/*.png`
- [ ] It builds from a clean checkout, and `test/run.sh` passes if you
      use it.
- [ ] It has run on a real console from the card. Start it from the
      Plugins page, and by opening a file if it opens any. Press RESET to
      go back to the menu and check its note. Switch the console off
      while it runs, and on again. `docs/testing.md` has the full list.
- [ ] The release notes are in `RELEASE_NOTES.md`. They're optional. The
      workflow uses the file as the release's text, and the plugins site
      shows it. Say what changed, and whether the release needs newer
      cart firmware. Without the file, GitHub lists the commits.

## Tag it

```sh
git tag v1.0.0
git push origin v1.0.0
```

Then `.github/workflows/release.yml` builds the plugin on GitHub, checks
that the tag matches the version inside the `.CVP`, checks the
screenshots, and makes the release with the `.CVP` and the screenshots
attached. Download the `.CVP` from the release and run that exact file
on your console once.

To do the same by hand (the workflow does nothing more than this):

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk && ninja -C build
gh release create v1.0.0 build/MYPLUGIN.CVP screenshots/screenshot-*.png \
    --title "My Plugin 1.0.0" --notes-file RELEASE_NOTES.md
```

## Submit it to the plugins site

The plugin registry is <https://github.com/plasticbugs/parrot-cart-plugins>.
Its website, <https://plasticbugs.github.io/parrot-cart-plugins/>, lists
every plugin and has the icon editor.

To get a plugin listed, open an issue in the registry with your
repository as `owner/repo`. You only do this once. After that, we pick
up your new releases. Here's what we take from each release:

| From | On the site |
|---|---|
| the `.CVP` attached to the release | the download, plus the name, version, developer, ID, icons, the files it opens, what it uses, and the README's cart text, all read from the file |
| `screenshot-1.png` .. `screenshot-3.png` attached to the release | the screenshots |
| the release's text | the release notes |
| `README.md` at the tag | the text of the plugin's page |

Each release has one `.CVP`. Plugins are free, and people download them
from your release. They're never bundled with a cart.
