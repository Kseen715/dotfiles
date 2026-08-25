#!/bin/sh
# polybar/scripts/swap.sh — swap in use, or nothing at all when there is none.
#
# "if exist" is the whole point, and it is why this is a script: polybar has no
# way to hide an internal module conditionally. internal/memory can print
# %swap_used%, but on a machine with no swap that is a permanent "0G" taking up
# bar width and implying a resource that does not exist. Printing an empty line
# makes polybar render the module as nothing.
#
# Reads /proc/meminfo rather than free(1): no fork, and the units are fixed
# (kB), so there is no locale-dependent parsing to get wrong.
set -u

[ -r /proc/meminfo ] || exit 0

_total=$(awk '/^SwapTotal:/ { print $2; exit }' /proc/meminfo)
_free=$(awk '/^SwapFree:/  { print $2; exit }' /proc/meminfo)
[ -n "${_total:-}" ] && [ -n "${_free:-}" ] || exit 0
[ "$_total" -gt 0 ] 2>/dev/null || exit 0

_used=$((_total - _free))
# One decimal of GiB, computed in integer arithmetic: 0.4G says more than "0G"
# on a machine that has only just started touching swap.
_tenths=$(( _used * 10 / 1048576 ))
printf '%d.%dG\n' "$((_tenths / 10))" "$((_tenths % 10))"
