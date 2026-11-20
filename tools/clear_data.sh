#!/bin/sh
# Removes what the setup screen imported, for testing the first run again: the
# per-user folder's `data` (the game in data/game, the MT-32 ROMs in data/mt32).
# Saved games, the Hall of Fame and settings.ini are kept.
#
#   tools/clear_data.sh              asks before removing
#   tools/clear_data.sh -y           doesn't ask
#   tools/clear_data.sh --settings   also drops game_data and mt32_roms from
#                                    settings.ini (the rest of it is kept)
#
# The per-user folder: COVERT_REACTION_USER_DIR if it's set, else
#   macOS    ~/Library/Application Support/Covert Reaction
#   Linux    ${XDG_DATA_HOME:-~/.local/share}/Covert Reaction
#   Windows  %APPDATA%\Covert Reaction (Git Bash, MSYS2, Cygwin)
#
# Note: with a game folder elsewhere that Covert Reaction also finds (such as
# resources/Covert in this repository, next to a build), the game starts from
# that one instead of showing the setup screen; --scene picker shows it anyway.
set -eu

yes=0
settings=0
for a in "$@"; do
  case "$a" in
    -y|--yes) yes=1 ;;
    --settings) settings=1 ;;
    -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $a (see --help)" >&2; exit 2 ;;
  esac
done

if [ -n "${COVERT_REACTION_USER_DIR:-}" ]; then
  dir=$COVERT_REACTION_USER_DIR
else
  case "$(uname -s)" in
    Darwin) dir="$HOME/Library/Application Support/Covert Reaction" ;;
    MINGW*|MSYS*|CYGWIN*) dir="${APPDATA:?APPDATA is not set}/Covert Reaction" ;;
    *) dir="${XDG_DATA_HOME:-$HOME/.local/share}/Covert Reaction" ;;
  esac
fi

data="$dir/data"
ini="$dir/settings.ini"
have_data=0
[ -e "$data" ] && have_data=1
have_keys=0
if [ "$settings" = 1 ] && [ -f "$ini" ] && grep -Eq '^[[:space:]]*(game_data|mt32_roms)[[:space:]]*=' "$ini"; then
  have_keys=1
fi

if [ "$have_data" = 0 ] && [ "$have_keys" = 0 ]; then
  echo "Nothing to clear in $dir"
  exit 0
fi

echo "In $dir:"
if [ "$have_data" = 1 ]; then
  echo "  remove data/ ($(du -sh "$data" 2>/dev/null | cut -f1)):"
  for sub in "$data"/* "$data"/.[!.]*; do
    [ -e "$sub" ] && echo "    $(basename "$sub")"
  done
fi
[ "$have_keys" = 1 ] && echo "  drop game_data and mt32_roms from settings.ini"

if [ "$yes" = 0 ]; then
  printf "Go ahead? [y/N] "
  read -r answer
  case "$answer" in
    y|Y|yes|YES) ;;
    *) echo "Nothing removed."; exit 1 ;;
  esac
fi

if [ "$have_data" = 1 ]; then
  rm -rf "$data"
  echo "Removed $data"
fi
if [ "$have_keys" = 1 ]; then
  tmp="$ini.tmp.$$"
  grep -Ev '^[[:space:]]*(game_data|mt32_roms)[[:space:]]*=' "$ini" > "$tmp" || true
  mv "$tmp" "$ini"
  echo "Dropped game_data and mt32_roms from $ini"
fi
