#!/bin/sh
# SPDX-License-Identifier: MIT
# GNU binutils needed; no DOS compiler, C runtime, network or libraries needed.
set -eu
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
as --32 -o "$tmp/capture.o" capture.S
ld -m elf_i386 -Ttext 0x100 --oformat binary -e _start -o CAPTURE.COM "$tmp/capture.o"
printf 'Built %s bytes; NOT executed on original hardware.\n' "$(wc -c < CAPTURE.COM)"
