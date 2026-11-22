#!/bin/sh
# sim/lua.sh SCRIPT.lua [options] -- a Lua script in the simulator: the Lua
# plugin (lua/) built for this computer and started as if the script had
# been opened from the cart's menu (docs/lua.md).
#
#   sim/lua.sh lua/examples/snake.lua     the script, in the editor: 4 runs it
#   sim/lua.sh game.lua --run             the script, run at once
#   sim/lua.sh mygame/src --run           a folder: its main.lua
#   sim/lua.sh game.lua --usb host        the simulator's own options follow
#
# The script's folder is the SD card, so require, file.read and saves find
# their files beside it, as they do in a folder on the real card. Give
# --card FOLDER to use another folder as the card: the script is then
# opened by its path from there. To type in the editor, press F2 twice: the
# keyboard is then the cart's USB keyboard. The other options are the
# simulator's (sim/run.sh src --help lists them).
set -e
case "$1" in
    ""|-h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac
kit=$(cd "$(dirname "$0")/.." && pwd)
script=$1
shift
[ -d "$script" ] && script=$script/main.lua
if [ "$1" = --run ]; then shift; export LUA_RUN=1; fi
[ -f "$script" ] || { echo "sim/lua.sh: no script at $script" >&2; exit 2; }
exec "$kit/sim/run.sh" "$kit/lua" --open "$script" "$@"
