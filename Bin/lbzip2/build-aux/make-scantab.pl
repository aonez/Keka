#!/usr/bin/perl -w
#-
# Copyright (C) 2011 Mikolaj Izdebski
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

#============================================================================#
#                                                                            #
# Generate transition tables for deterministic finite automatons (DFAs)      #
# used to detect the 48-bit magic bit sequence present at the beginning      #
# of every bzip2 compressed block.  These DFAs are already minimalistic.     #
#                                                                            #
# Each DFA has 49 states, numbered from 0 to 48.  Starting with the initial  #
# state 0, the DFA inputs one byte (big_dfa) or one bit (mini_dfa) at time   #
# and advances to a next state.  As soon as the bit pattern is detected,     #
# DFA moves to the accepting state (number 48) and remains there.            #
#                                                                            #
#============================================================================#


# The bit patern used in bzip2.
@x = split //, sprintf "%024b%024b", 0x314159, 0x265359;

# Use Knuth–Morris–Pratt algorithm to create the mini-DFA,
# which inputs only one bit at time.
$i = 0;
$j = $p[0] = -1;
while ($i < @x-1) {
  while ($j > -1 && $x[$i] != $x[$j]) {
    $j = $p[$j];
  }
  $i++;
  $j++;
  if ($x[$i] == $x[$j]) {
    $p[$i] = $p[$j];
  } else {
    $p[$i] = $j;
  }
}

# Create non-accepting states of the mini-DFA.
for $s (0..47) {
  $d[$s][0] = ($x[$s]?$p[$s]:$s)+1;
  $d[$s][1] = ($x[$s]?$s:$p[$s])+1;
}

# Create the big, 8-bit DFA, which inputs 8 bits at time.
for $s0 (0..47) {
  for $c (0..255) {
    $s = $s0;
    for $k (0..7) {
      $b = (($c << $k) & 0x80) >> 7;
      $s = $d[$s][$b];
      last if $s == 48;
    }
    $t[$s0][$c] = $s;
  }
}
for $c (0..255) {
  $t[48][$c] = 48;
}

# Dump the mini DFA.
@mini=();
for $s (0..47) {
  @vec = ();
  for $c (0..1) {
    $y = $d[$s][$c];
    push @vec,"$y,";
  }
  push @mini, '{' . join(' ',@vec) . '},'
}

# Dump the big DFA.
@big=();
for $s (0..48) {
  @vec = ();
  for $c (0..255) {
    $y = $t[$s][$c];
    push @vec,"$y,";
  }
  push @big, '{' . join(' ',@vec) . '},'
}


use Text::Wrap;
$Text::Wrap::columns = 80;
open F, ">src/scantab.h" or die;
printf F q(/* This file was generated automatically by make-scantab.pl.
   For comments refer to the generator script -- make-scantab.pl. */

static const unsigned char ACCEPT = 48;

static const unsigned char mini_dfa[48][2] = {
%s
};

static const unsigned char big_dfa[49][256] = {
%s
};
), wrap('  ','  ',join(' ',@mini)), wrap('  ','  ',join(' ',@big));
