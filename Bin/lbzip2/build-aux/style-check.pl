#!/usr/bin/perl -w
#-
# Copyright (C) 2011, 2012 Mikolaj Izdebski
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# Hardcoded files to consider.
@ARGV=qw(
src/common.h
src/signals.c
src/encode.h
src/decode.c
src/encode.c
src/process.c
src/compress.c
src/expand.c
src/parse.c
src/signals.h
src/main.c
src/divbwt.c
src/decode.h
src/process.h
src/main.h
) if !@ARGV;  # The user knows better.

sub msg { print "$f: @_\n"; ++$cnt }

for $f (@ARGV) {
  open F, $f or msg "file doesn't exist" and next;
  undef $/; $_=<F>;
  ++$nf;

  # ASCII chars, whitespaces, line length.
  /([^\x20-\x7e\n])/ and msg "contains prohibited chars";
  /[^\n]\n$/ or msg "doesn't end with a single NL";
  / \n/ and msg "has trailing whitespace before NL";
  /\n{4}/ and msg "has more than 2 consec blank lines";
  /\n[^\n]{80}/ and msg "has line longer than 79 chars";

  # C specific stuff.
  m{^/\*-([^*]|\*[^/])*Copyright[^*]+2012([^*]|\*[^/])*\*/}
      or msg "has missing or outdated copyright notice";
  $f ne "src/common.h" xor /\n *# *include *<config\.h>\n/
      or msg "missing or excessive #include <config.h>";
}

$nf='No' if !$nf;
$cnt='no' if !$cnt;
print "$nf file(s) checked, $cnt warning(s).\n";
