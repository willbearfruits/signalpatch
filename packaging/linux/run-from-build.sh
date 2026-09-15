#!/bin/sh
# Launch the app from a source checkout the way the docs recommend on a
# PipeWire desktop: through pw-jack with the graph forced to 128 samples.
# A .desktop entry can point here (see README "Build"); arguments pass through
# (a patch or project zip), with none the autosave comes back.
here=$(cd "$(dirname "$0")" && pwd)
app="$here/../../build/signalpatch_artefacts/RelWithDebInfo/SignalPatch"
[ -x "$app" ] || app="$here/../../build/signalpatch_artefacts/Release/SignalPatch"
export PIPEWIRE_QUANTUM="${PIPEWIRE_QUANTUM:-128/48000}"
if command -v pw-jack >/dev/null 2>&1; then
    exec pw-jack "$app" "$@"
fi
exec "$app" "$@"
