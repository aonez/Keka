#!/usr/bin/perl
#-
# Copyright (C) 2011, 2012 Mikolaj Izdebski
# Copyright (C) 2008, 2009, 2010 Laszlo Ersek
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

$il = 2;   # indentation level
$lw = 19;  # label width
$sw = 80;  # screen width

$ind = ' 'x$il;
undef $Text::Wrap::unexpand;

while (<DATA>) {
  if (!/^@(.*)/) { $s.=$_; next }
  $label=$1;
  @d=();
  while (<DATA>) {
    last unless /./;
    chop;
    push @d,$_;
  }
  $desc = join " ", @d;

  $Text::Wrap::columns = $lw+$il;
  $s .= wrap $ind,$ind,$label;
  $s =~ s/\n([^\n]*)$/\n/; $t=$1;
  $t .= ' 'x($il+$lw-length$t);
  $Text::Wrap::columns = $sw;
  $s .= wrap "$t: ",' 'x($il+$lw+2),$desc;
  $s .= "\n";
}

@c = map {s/\n/\\n/g; $_} grep {/./} split /([^\0]{509})/, $s;
unshift @c,'%s'x@c;
$_ = join '", "', @c;
$_ = "#define USAGE_STRING \"$_\"";

$s = "";
while (1) {
  if (length($_) <= 77) {
    $s.="$_\n";
    last;
  }
  /^(.{77}[^\\]?)(.*)$/;
  $s.="$1\\\n";
  $_="$2";
}

open F, "src/main.c" or die;
undef $/; $_=<F>;
s/#define USAGE_STRING.*(.*\\\n)*.*[^\\]\n/$s\n/ or die;
open F, ">src/main.c" or die;
print F;


__END__
Usage:
1. PROG [-n WTHRS] [-k|-c|-t] [-d|-z] [-1 .. -9] [-f] [-v] [-S] {FILE}
2. PROG -h|-V

Recognized PROG names:

@bunzip2, lbunzip2
Decompress. Forceable with `-d'.

@bzcat, lbzcat
Decompress to stdout. Forceable with `-cd'.

@<otherwise>
Compress. Forceable with `-z'.


Environment variables:

@LBZIP2, BZIP2, BZIP
Insert arguments between PROG and the rest of the command line. Tokens are
separated by spaces and tabs; no escaping.


Options:

@-n WTHRS
Set the number of (de)compressor threads to WTHRS, where WTHRS is a positive
integer.

@-k, --keep
Don't remove FILE operands. Open regular input files with more than one link.

@-c, --stdout
Write output to stdout even with FILE operands. Implies `-k'. Incompatible
with `-t'.

@-t, --test
Test decompression; discard output instead of writing it to files or stdout.
Implies `-k'. Incompatible with `-c'.

@-d, --decompress
Force decompression over the selection by PROG.

@-z, --compress
Force compression over the selection by PROG.

@-1 .. -9
Set the compression block size to 100K .. 900K.

@--fast
Alias for `-1'.

@--best
Alias for `-9'. This is the default.

@-f, --force
Open non-regular input files. Open input files with more than one link. Try to
remove each output file before opening it. With `-cd' copy files not in bzip2
format.

@-s, --small
Reduce memory usage at cost of performance.

@-u, --sequential
Perform splitting input blocks sequentially. This may improve compression ratio
and decrease CPU usage, but will degrade scalability.

@-v, --verbose
Log each (de)compression start to stderr. Display compression ratio and space
savings. Display progress information if stderr is connected to a terminal.

@-S
Print condition variable statistics to stderr.

@-q, --quiet, --repetitive-fast, --repetitive-best, --exponential
Accepted for compatibility, otherwise ignored.

@-h, --help
Print this help to stdout and exit.

@-L, --license, -V, --version
Print version information to stdout and exit.


Operands:

@FILE
Specify files to compress or decompress. If no FILE is given, work as a
filter. FILEs with `.bz2', `.tbz', `.tbz2' and `.tz2' name suffixes will be
skipped when compressing. When decompressing, `.bz2' suffixes will be removed
in output filenames; `.tbz', `.tbz2' and `.tz2' suffixes will be replaced by
`.tar'; other filenames will be suffixed with `.out'.
