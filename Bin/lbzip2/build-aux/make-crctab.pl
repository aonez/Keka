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

use Text::Wrap;
open F, ">src/crctab.c" or die;
printf F q(/* This file was generated automatically by make-crctab.pl.
   For comments refer to the generator script -- make-crctab.pl. */

#include "common.h"
#include "encode.h"

uint32_t crc_table[256] = {
%s
};
), wrap '  ','  ',map {
  $c = $_<<24;
  for (1..8) { $c = ($c<<1) & 0xFFFFFFFF ^ 0x04C11DB7 & -($c>>31); }
  sprintf "0x%08lX,",$c
} 0..255
