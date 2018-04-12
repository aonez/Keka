/*  Arg_parser - POSIX/GNU command line argument parser. (C version)
    Copyright (C) 2006-2017 Antonio Diaz Diaz.

    This library is free software. Redistribution and use in source and
    binary forms, with or without modification, are permitted provided
    that the following conditions are met:

    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

/*  Arg_parser reads the arguments in 'argv' and creates a number of
    option codes, option arguments and non-option arguments.

    In case of error, 'ap_error' returns a non-null pointer to an error
    message.

    'options' is an array of 'struct ap_Option' terminated by an element
    containing a code which is zero. A null name means a short-only
    option. A code value outside the unsigned char range means a
    long-only option.

    Arg_parser normally makes it appear as if all the option arguments
    were specified before all the non-option arguments for the purposes
    of parsing, even if the user of your program intermixed option and
    non-option arguments. If you want the arguments in the exact order
    the user typed them, call 'ap_init' with 'in_order' = true.

    The argument '--' terminates all options; any following arguments are
    treated as non-option arguments, even if they begin with a hyphen.

    The syntax for optional option arguments is '-<short_option><argument>'
    (without whitespace), or '--<long_option>=<argument>'.
*/

#ifdef __cplusplus
extern "C" {
#endif

enum ap_Has_arg { ap_no, ap_yes, ap_maybe };

struct ap_Option
  {
  int code;			/* Short option letter or code ( code != 0 ) */
  const char * name;		/* Long option name (maybe null) */
  enum ap_Has_arg has_arg;
  };


struct ap_Record
  {
  int code;
  char * argument;
  };


struct Arg_parser
  {
  struct ap_Record * data;
  char * error;
  int data_size;
  int error_size;
  };


char ap_init( struct Arg_parser * const ap,
              const int argc, const char * const argv[],
              const struct ap_Option options[], const char in_order );

void ap_free( struct Arg_parser * const ap );

const char * ap_error( const struct Arg_parser * const ap );

    /* The number of arguments parsed (may be different from argc) */
int ap_arguments( const struct Arg_parser * const ap );

    /* If ap_code( i ) is 0, ap_argument( i ) is a non-option.
       Else ap_argument( i ) is the option's argument (or empty). */
int ap_code( const struct Arg_parser * const ap, const int i );

const char * ap_argument( const struct Arg_parser * const ap, const int i );

#ifdef __cplusplus
}
#endif
