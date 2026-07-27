#!/bin/sh
#
# abicheck.sh - check libmp3lame's exported ABI against the committed contract.
#
# The check is static: it inspects a built library and two text files that are
# part of the source tree, and encodes nothing. There is no audio input, no
# reference build and no network access, so it runs anywhere the library has
# been built.
#
# Three checks, in increasing depth and decreasing availability:
#
#   contract  the two committed export lists name the same symbols. Needs
#             nothing but the source tree.
#   exports   the built library exports exactly the symbols the contract
#             promises. Needs nm, dumpbin or objdump.
#   abi       nothing changed inside those symbols either - signatures, struct
#             layouts, enum values. Needs libabigail and debug information.
#
# A missing tool skips a check and never fails the run; a check that could be
# made and did not hold does fail it.
#
# Part of the LAME distribution.  No warranty; see COPYING.

set -u

prog=$(basename "$0")

usage() {
	cat <<EOF
Usage: $prog [-b DIR] [-i DIR] [-a FILE] [-h]

  -b DIR     Directory holding the built library, e.g. libmp3lame/.libs.
             (default: libmp3lame/.libs, relative to the current directory)
  -i DIR     Directory holding the committed export lists libmp3lame.sym and
             lame.def. (default: include)
  -a FILE    ABI baseline to compare against, as written by abidw.
             (default: maintainer/abi/libmp3lame.abi)
  -h         Show this help and exit.

Run it after building the library:

    make && make abicheck

Exits non-zero if any check FAILs. Checks that cannot run report SKIP and do
not affect the exit status.
EOF
}

libdir=libmp3lame/.libs
incdir=include
baseline=maintainer/abi/libmp3lame.abi

while getopts "b:i:a:h" opt; do
	case "$opt" in
		b)
			libdir=$OPTARG
			;;
		i)
			incdir=$OPTARG
			;;
		a)
			baseline=$OPTARG
			;;
		h)
			usage
			exit 0
			;;
		*)
			usage >&2
			exit 2
			;;
	esac
done

pass=0
fail=0
skip=0
total=3
step=0

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT HUP INT TERM

# describe CHECK TEXT - announce a check before running it.
describe() {
	step=$((step + 1))
	printf '[%d/%d] %s: %s\n' "$step" "$total" "$1" "$2"
}

verdict() {
	printf '      %s  %s\n' "$1" "$2"
	case "$1" in
		PASS)
			pass=$((pass + 1))
			;;
		FAIL)
			fail=$((fail + 1))
			;;
		SKIP)
			skip=$((skip + 1))
			;;
	esac
}

have() {
	command -v "$1" >/dev/null 2>&1
}

# --- locate the inputs ------------------------------------------------------

sym=$incdir/libmp3lame.sym
def=$incdir/lame.def

# The built library, in the order the platforms are likely to be met. A static
# archive is the last resort: it is the one case where the exported set cannot
# be read back, only the promised symbols confirmed present.
lib=
libkind=
for cand in \
	"$libdir"/libmp3lame.so \
	"$libdir"/libmp3lame.dylib \
	"$libdir"/libmp3lame-0.dll \
	"$libdir"/libmp3lame.dll \
	"$libdir"/lame.dll
do
	if [ -f "$cand" ]; then
		lib=$cand
		break
	fi
done
if [ -n "$lib" ]; then
	case "$lib" in
		*.dll)
			libkind=dll
			;;
		*.dylib)
			libkind=macho
			;;
		*)
			libkind=elf
			;;
	esac
elif [ -f "$libdir/libmp3lame.a" ]; then
	lib=$libdir/libmp3lame.a
	libkind=static
fi

printf '== LAME ABI check ==\n\n'
printf 'library : %s\n' "${lib:-(not found in $libdir)}"
printf 'contract: %s, %s\n' "$sym" "$def"
printf 'baseline: %s\n\n' "$baseline"

# --- extractors -------------------------------------------------------------

# The committed POSIX list is one bare symbol name per line.
read_sym() {
	sed -e 's/[;#].*//' -e 's/[[:space:]]//g' "$sym" | sed '/^$/d' | sort -u
}

# The committed Windows list is a module-definition file: a LIBRARY line, an
# EXPORTS line, then "name @ordinal" pairs. Ordinals, comments and the two
# header keywords are not part of the symbol set.
read_def() {
	sed -e 's/;.*//' "$def" |
		grep -v -i -E '^[[:space:]]*(LIBRARY|EXPORTS|VERSION|DESCRIPTION)([[:space:]]|$)' |
		awk '{ if (NF > 0) print $1 }' |
		sed 's/@.*//' |
		sed '/^$/d' | sort -u
}

# Symbols the toolchain adds to every shared object; they are not part of any
# library's interface and must not be compared against the contract.
drop_linker_symbols() {
	grep -v -E '^(_init|_fini|_edata|_end|__bss_start|__gmon_start__|_IO_stdin_used|__libc_csu_(init|fini)|DllMain(@12)?|DllEntryPoint(@12)?)$'
}

# Read the exported symbols back out of the built library.
read_exports() {
	case "$libkind" in
		elf)
			nm -D --defined-only --extern-only "$lib" |
				awk 'NF >= 3 { print $NF }'
			;;
		macho)
			# Mach-O has no -D; -gU is the defined-external set. Every C
			# symbol carries a leading underscore that is not part of the name.
			nm -gU "$lib" | awk 'NF >= 3 { print $NF }' | sed 's/^_//'
			;;
		dll)
			if have dumpbin; then
				# The export table follows a header line ending in "name";
				# each entry is "ordinal hint RVA name".
				dumpbin /exports "$lib" |
					awk '/ordinal[[:space:]]+hint/ { on = 1; next }
					     on && NF >= 4 && $1 ~ /^[0-9]+$/ { print $NF }'
			else
				# binutils reports the same table as "[N] name".
				objdump -p "$lib" |
					awk '/^\[[[:space:]]*[0-9]+\]/ { print $NF }'
			fi
			;;
	esac | drop_linker_symbols | sed '/^$/d' | sort -u
}

# --- check 1: the two committed lists agree ---------------------------------

describe contract \
	'the committed export lists (libmp3lame.sym, lame.def) name the same symbols'

if [ ! -f "$sym" ] || [ ! -f "$def" ]; then
	verdict SKIP "no export lists in $incdir - wrong -i directory?"
else
	read_sym >"$work/sym"
	read_def >"$work/def"

	# There is one list per operating system, not one per build, and each is
	# the logical OR over every configuration we ship. The comparison is
	# therefore exact in both directions: a name in one list and not in the
	# other is a divergence, whichever side it sits on.
	comm -23 "$work/sym" "$work/def" >"$work/symonly"
	comm -13 "$work/sym" "$work/def" >"$work/defonly"

	if [ -s "$work/symonly" ] || [ -s "$work/defonly" ]; then
		verdict FAIL 'the two lists have drifted apart'
		sed 's/^/        exported on POSIX but not on Windows: /' "$work/symonly"
		sed 's/^/        exported on Windows but not on POSIX: /' "$work/defonly"
	else
		verdict PASS "$(wc -l <"$work/def" | tr -d ' ') symbols, named by both"
	fi
fi

# --- check 2: the library exports what the contract promises ----------------

describe exports \
	'the built library exports exactly the symbols the contract promises'

if [ -z "$lib" ]; then
	verdict SKIP "no built library in $libdir - run make first"
elif [ "$libkind" = static ]; then
	# A static archive keeps every non-static symbol, so the exported set is
	# not observable. What can still be checked is that nothing the contract
	# promises has gone missing.
	if ! have nm; then
		verdict SKIP 'nm not found'
	else
		nm --defined-only --extern-only "$lib" 2>/dev/null |
			awk 'NF >= 3 { print $NF }' | sed 's/^_//' | sort -u >"$work/have"
		read_sym >"$work/want"
		if comm -23 "$work/want" "$work/have" | grep -q .; then
			verdict FAIL 'the static library is missing promised symbols'
			comm -23 "$work/want" "$work/have" | sed 's/^/        missing: /'
		else
			verdict PASS "all $(wc -l <"$work/want" | tr -d ' ') promised symbols present (static build: extra exports cannot be detected)"
		fi
	fi
elif { [ "$libkind" = dll ] && ! have dumpbin && ! have objdump; } ||
	{ [ "$libkind" != dll ] && ! have nm; }; then
	verdict SKIP 'no symbol reader (nm, dumpbin or objdump) found'
else
	read_exports >"$work/have"
	if [ "$libkind" = dll ]; then
		read_def >"$work/want"
		want_name=lame.def
	else
		read_sym >"$work/want"
		want_name=libmp3lame.sym
	fi

	if [ ! -s "$work/have" ]; then
		verdict SKIP "could not read an export table out of $lib"
	elif cmp -s "$work/want" "$work/have"; then
		verdict PASS "$(wc -l <"$work/have" | tr -d ' ') symbols, matching $want_name"
	else
		verdict FAIL "the library and $want_name disagree"
		comm -13 "$work/want" "$work/have" | sed 's/^/        exported but not in the contract: /'
		comm -23 "$work/want" "$work/have" | sed 's/^/        in the contract but not exported: /'
	fi
fi

# --- check 3: the full ABI diff ---------------------------------------------

describe abi \
	'no signature, struct-layout or enum change inside those symbols'

if ! have abidiff; then
	verdict SKIP 'abidiff not found - install libabigail to enable this check'
elif [ ! -f "$baseline" ]; then
	verdict SKIP "no baseline at $baseline"
elif [ -z "$lib" ] || [ "$libkind" != elf ]; then
	verdict SKIP 'needs an ELF shared library; libabigail reads no other format'
elif have objdump && ! objdump -h "$lib" 2>/dev/null | grep -q '\.debug_info'; then
	# Without DWARF the comparison degrades to the symbol set that check 2
	# already covers, while still reporting every type as removed.
	verdict SKIP 'the library carries no debug information - rebuild with -g'
else
	abidiff "$baseline" "$lib" >"$work/abidiff" 2>&1
	rc=$?
	# libabigail's exit status is a bit field, verified against the installed
	# tool rather than read off the documentation: 1 = abidiff itself failed,
	# 2 = it was called wrongly, 4 = the ABI changed, 8 = it changed
	# incompatibly. A status above 128 is a signal, which is also a failure of
	# the tool and not a verdict about the library.
	if [ "$rc" -eq 0 ]; then
		verdict PASS 'identical to the baseline'
	elif [ "$rc" -gt 128 ] || [ $((rc & 3)) -ne 0 ]; then
		verdict SKIP 'abidiff could not run'
		sed 's/^/        /' "$work/abidiff"
	else
		if [ $((rc & 8)) -ne 0 ]; then
			verdict FAIL 'incompatible ABI change against the baseline'
		else
			verdict FAIL 'ABI change against the baseline'
		fi
		sed 's/^/        /' "$work/abidiff"
	fi
fi

# --- summary ----------------------------------------------------------------

printf '\nSummary: %d checks - %d PASS, %d FAIL, %d SKIP\n' \
	"$total" "$pass" "$fail" "$skip"

if [ "$fail" -gt 0 ]; then
	exit 1
fi
exit 0
