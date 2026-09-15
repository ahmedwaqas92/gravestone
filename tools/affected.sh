#!/bin/sh
# affected.sh
#
# Prints the test binaries a change can reach.
#
# Nothing here is guessed. With no argument the question is put to make,
# which already holds the whole dependency graph including the header
# lists the compiler writes out, so the answer is exactly the set of test
# binaries that are now out of date.
#
# Given a revision or a list of files, the same graph is read from the
# other direction. Each test's link line names every object it uses, and
# each object has a list of every header it read, so a file is matched
# against those lists. A header moving into another module needs no edit
# here either way.
set -e

cd "$(dirname "$0")/.."

TESTS=$(make -s print-tests)

# No argument. Ask make what is out of date, which is what an edit since
# the last build means.
if [ "$#" -eq 0 ] || [ -z "$1" ]; then
    for t in $TESTS; do
        if make -q "$t" >/dev/null 2>&1; then
            continue                 # up to date, nothing reached it
        fi
        echo "$t"
    done
    exit 0
fi

# A revision, or file names. Anything that exists on disk is taken as a
# file name, and everything else is handed to git as a revision.
if [ -e "$1" ]; then
    CHANGED=$(printf '%s\n' "$@")
else
    since=$1
    CHANGED=$( { git diff --name-only "$since" 2>/dev/null || true
                 git ls-files --others --exclude-standard 2>/dev/null || true
               } | sed '/^$/d' | sort -u )
fi
[ -n "$CHANGED" ] || exit 0

LINKS=$(make --dry-run --always-make $TESTS 2>/dev/null |
        grep -E -- '-o +build/[a-z_]+_test ' || true)
if [ -z "$LINKS" ]; then
    printf '%s\n' $TESTS         # never built, so everything is reachable
    exit 0
fi

printf '%s\n' "$LINKS" | while IFS= read -r line; do
    bin=$(printf '%s' "$line" | sed -n 's/.* -o \(build\/[a-z_]*_test\) .*/\1/p')
    [ -n "$bin" ] || continue

    lists="$bin.d"
    for obj in $(printf '%s' "$line" | tr ' ' '\n' | grep -E '^build/.*\.o$'); do
        lists="$lists ${obj%.o}.d"
    done

    for f in $CHANGED; do
        case " $line " in
        *" $f "*) echo "$bin"; break;;
        esac
        found=0
        for d in $lists; do
            [ -f "$d" ] || continue
            if grep -qF -- " $f" "$d" || grep -qF -- "	$f" "$d"; then
                found=1
                break
            fi
        done
        if [ "$found" = 1 ]; then
            echo "$bin"
            break
        fi
    done
done | sort -u
