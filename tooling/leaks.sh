#!/bin/sh
# Checks that ca leaks nothing from libc or std into user code; called by
# `make leaks`, which sets CXX, CXXFLAGS (with -I<lib>) and LIB.
#
#   headers   every header under src/lib, with everything it includes, stays
#             inside src/lib: no system or std header reaches a user. What the
#             compiler injects into every translation unit (gcc's
#             stdc-predef.h) is not ours and is subtracted.
#   symbols   every external symbol LIB defines is mangled inside namespace
#             ca (functions, inline variables, guards, thread-locals), except
#             the C functions ca provides in libc's place (C_SYMBOLS) and the weak UBSan
#             handlers (__ubsan_handle_*, 3-system/sanitizer/ubsan.cpp).
#   helpers   no compiler runtime helper (libgcc's __popcountdi2, __udivti3, ...):
#             programs linked without libc link no libgcc either.
#   layer 1   every object of 1-primitives references only symbols inside ca:
#             the freestanding layer calls nothing from libc (memcpy, ...), not
#             even the C_SYMBOLS it defines itself, which would recurse.

status=0
inside_ca='^_Z(N[rVKRO]*|GVN|ZN[rVKRO]*|THN|TWN|TVN)2ca'
C_SYMBOLS='memcpy memmove memset memcmp bcmp memchr strlen'
flags=$(printf '%s\n' $CXXFLAGS | grep -v -x -e -MMD -e -MP | tr '\n' ' ')

deps() {
	$CXX $flags -x c++ -M "$1" 2>&1 | tr ' \\' '\n\n' | grep -v '^$' | sed 1d | grep -v -x "$1"
}
baseline=$(deps /dev/null)

for header in $(find src/lib -name '*.hpp' | sort); do
	outside=$(deps "$header" | grep -v '^src/lib/' | grep -v -x -F "${baseline:-/}")
	if [ -n "$outside" ]; then
		printf '  LEAK  %s includes:\n' "$header"
		printf '%s\n' "$outside" | sed 's/^/          /'
		status=1
	fi
done

symbols=$(nm -g --defined-only "$LIB" 2>/dev/null | awk 'NF == 3 { print $3 }' \
	| grep -v -E "$inside_ca" | grep -v -x -F "$(printf '%s\n' $C_SYMBOLS)" | grep -v -E '^__ubsan_handle_[a-z0-9_]+$')
if [ -n "$symbols" ]; then
	printf '  LEAK  %s defines symbols outside ca:\n' "$LIB"
	printf '%s\n' "$symbols" | sed 's/^/          /'
	status=1
fi

helpers=$(nm -u "$LIB" 2>/dev/null | awk '{ print $2 }' | grep -E '^__[a-z0-9]+[dt]i[0-9]$|^__(u)?(div|mod)ti3$|^__cpu_(model|indicator)' | sort -u)
if [ -n "$helpers" ]; then
	printf '  LEAK  %s references compiler runtime (libgcc) helpers, absent without it:\n' "$LIB"
	printf '%s\n' "$helpers" | sed 's/^/          /'
	status=1
fi

for object in $(find "$(dirname "$LIB")/src/lib/1-primitives" -name '*.o' 2>/dev/null | sort); do
	outside=$( (nm -u "$object" | awk '{ print $2 }'; readelf -rW "$object" | awk '{ print $5 }' \
		| grep -x -F "$(printf '%s\n' $C_SYMBOLS)") | grep -v -E "$inside_ca" | sort -u)
	if [ -n "$outside" ]; then
		printf '  LEAK  %s (layer 1) references symbols outside ca:\n' "$object"
		printf '%s\n' "$outside" | sed 's/^/          /'
		status=1
	fi
done

if [ $status -eq 0 ]; then
	echo "leaks: none ($(find src/lib -name '*.hpp' | wc -l) headers, $(nm -g --defined-only "$LIB" | awk 'NF == 3' | wc -l) symbols, $CXX)"
fi
exit $status
