#!/bin/bash
# Launches the real app headless on every screen, plus the clicks and scrolls that once broke it, and fails if Qt
# prints any QML warning (for example "Column will not function", which Qt 6.8 reports and Qt 6.4 does not).
# Needs a build with -DTC_DEV_TOOLS=ON. Usage: qml_warnings.sh /path/to/tenchar
EXE="$1"; FAIL=0; TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
run() {   # name, args...
    local name="$1"; shift
    rm -rf "$TMP/d" "$TMP/dc"
    local out; out="$(XDG_DATA_HOME="$TMP/d" XDG_CONFIG_HOME="$TMP/dc" timeout 60 "$EXE" "$@" --grab "$TMP/$name.png" 2>&1 \
        | grep -E "qrc:|will not function|Binding loop|ReferenceError|TypeError|is not a type|Cannot " )"
    if [ -n "$out" ]; then echo "FAIL [$name]: $out"; FAIL=1; fi
    if [ ! -s "$TMP/$name.png" ] || [ "$(stat -c %s "$TMP/$name.png")" -lt 15000 ]; then echo "FAIL [$name]: no screenshot"; FAIL=1; fi
}
for s in setup unlock unlockfail pair pairnotice entercode waiting verify settings about keysreset chat chatempty chatoffline chatbn; do
    run "screen-$s" --preview "$s"
done
# the interactions that used to break the layout: open a dialog, press Create with empty fields, pick a radio, scroll
run click-relay-row   --size 440x1500 --click 220,660
run click-create      --size 440x1500 --click 220,1448
run click-radio       --size 440x1500 --click 220,1073
run wheel             --wheel -240
run type-passphrase   --keys "correct horse battery staple~Tcorrect horse battery staple~E" --delay 4500
[ "$FAIL" = 0 ] && echo "no QML warnings on any screen or interaction"
exit $FAIL
