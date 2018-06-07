/*-
  timeout.c - execute a command with one minute timeout

  Copyright (C) 2011 Mikolaj Izdebski

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with lbzip2.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  This program basically does the same thing as the following one-liner:

     perl -e'alarm 60; exec @ARGV or die $!'

  It's written in C because we don't want to depend on perl.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <unistd.h>             /* execv() */
#include <stdio.h>              /* perror() */

int
main(int argc, char **argv)
{
  alarm(60);

  argv++;
  execv(*argv, argv);

  perror("execv");
  return 1;
}
