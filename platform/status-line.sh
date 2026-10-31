# Status line at the bottom of the terminal for the dependency build scripts.
# Build output scrolls above it. Source this file, then call:
#
#   status_begin <status file> <steps...>
#   status_step <name>      (at the start of each step)
#   status_phase <phase>    (within a step)
#   status_total            (prints the total elapsed time)

STATUS_PID=
STATUS_STEP=
STATUS_STEP_NUM=0

function format_time() {
    printf '%d:%02d' $(($1 / 60)) $(($1 % 60))
}

function status_total() {
    format_time $(($(date +%s) - STATUS_START))
}

function status_draw() {
    local size rows cols label step_start now
    size=$(stty size </dev/tty) || return 0
    rows=${size% *}
    cols=${size#* }
    if [ "$size" != "$STATUS_SIZE" ]; then
        # Scroll the cursor off the last row before excluding it from the scroll region.
        STATUS_SIZE=$size
        printf '\e7\e[r\e8\eD\eM\e7\e[1;%dr\e8' $((rows - 1))
    fi
    { read -r label; read -r step_start; } < $STATUS_FILE
    now=$(date +%s)
    printf '\e7\e[%d;1H\e[7m%-*.*s\e[0m\e8' $rows $cols $cols \
        " $label   $(format_time $((now - step_start)))   (total $(format_time $((now - STATUS_START))))"
}

function status_write() {
    [ -n "$STATUS_PID" ] || return 0
    printf '%s\n%s\n' "$1" $2 > $STATUS_FILE.tmp
    mv $STATUS_FILE.tmp $STATUS_FILE
}

function status_phase() {
    status_write "[$STATUS_STEP_NUM/$STATUS_NUM_STEPS] $STATUS_STEP: $1" $STATUS_STEP_START
}

function status_step() {
    STATUS_STEP=$1
    STATUS_STEP_NUM=$((STATUS_STEP_NUM + 1))
    STATUS_STEP_START=$(date +%s)
    echo "=== $1 ==="
    status_phase start
}

function status_end() {
    local code=$? size
    if [ -n "$STATUS_PID" ]; then
        kill $STATUS_PID 2>/dev/null
        wait $STATUS_PID 2>/dev/null || true
        size=$(stty size </dev/tty)
        printf '\e7\e[r\e[%d;1H\e[2K\e8' ${size% *}
    fi
    if [ $code -ne 0 ]; then
        echo "*** Failed: $STATUS_STEP (exit code $code, total $(status_total))"
    fi
}

function status_begin() {
    STATUS_FILE=$1
    shift
    STATUS_NUM_STEPS=$#
    STATUS_START=$(date +%s)
    trap status_end EXIT
    [ -t 1 ] || return 0
    printf '%s\n%s\n' "Starting" $STATUS_START > $STATUS_FILE
    # Only this process draws, so status updates do not interleave with each other.
    (while kill -0 $$ 2>/dev/null; do status_draw; sleep 1; done) &
    STATUS_PID=$!
}
