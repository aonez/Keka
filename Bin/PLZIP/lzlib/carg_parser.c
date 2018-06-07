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

#include <stdlib.h>
#include <string.h>

#include "carg_parser.h"


/* assure at least a minimum size for buffer 'buf' */
static void * ap_resize_buffer( void * buf, const int min_size )
  {
  if( buf ) buf = realloc( buf, min_size );
  else buf = malloc( min_size );
  return buf;
  }


static char push_back_record( struct Arg_parser * const ap,
                              const int code, const char * const argument )
  {
  const int len = strlen( argument );
  struct ap_Record * p;
  void * tmp = ap_resize_buffer( ap->data,
                 ( ap->data_size + 1 ) * sizeof (struct ap_Record) );
  if( !tmp ) return 0;
  ap->data = (struct ap_Record *)tmp;
  p = &(ap->data[ap->data_size]);
  p->code = code;
  p->argument = 0;
  tmp = ap_resize_buffer( p->argument, len + 1 );
  if( !tmp ) return 0;
  p->argument = (char *)tmp;
  strncpy( p->argument, argument, len + 1 );
  ++ap->data_size;
  return 1;
  }


static char add_error( struct Arg_parser * const ap, const char * const msg )
  {
  const int len = strlen( msg );
  void * tmp = ap_resize_buffer( ap->error, ap->error_size + len + 1 );
  if( !tmp ) return 0;
  ap->error = (char *)tmp;
  strncpy( ap->error + ap->error_size, msg, len + 1 );
  ap->error_size += len;
  return 1;
  }


static void free_data( struct Arg_parser * const ap )
  {
  int i;
  for( i = 0; i < ap->data_size; ++i ) free( ap->data[i].argument );
  if( ap->data ) { free( ap->data ); ap->data = 0; }
  ap->data_size = 0;
  }


static char parse_long_option( struct Arg_parser * const ap,
                               const char * const opt, const char * const arg,
                               const struct ap_Option options[],
                               int * const argindp )
  {
  unsigned len;
  int index = -1, i;
  char exact = 0, ambig = 0;

  for( len = 0; opt[len+2] && opt[len+2] != '='; ++len ) ;

  /* Test all long options for either exact match or abbreviated matches. */
  for( i = 0; options[i].code != 0; ++i )
    if( options[i].name && strncmp( options[i].name, &opt[2], len ) == 0 )
      {
      if( strlen( options[i].name ) == len )	/* Exact match found */
        { index = i; exact = 1; break; }
      else if( index < 0 ) index = i;		/* First nonexact match found */
      else if( options[index].code != options[i].code ||
               options[index].has_arg != options[i].has_arg )
        ambig = 1;		/* Second or later nonexact match found */
      }

  if( ambig && !exact )
    {
    add_error( ap, "option '" ); add_error( ap, opt );
    add_error( ap, "' is ambiguous" );
    return 1;
    }

  if( index < 0 )		/* nothing found */
    {
    add_error( ap, "unrecognized option '" ); add_error( ap, opt );
    add_error( ap, "'" );
    return 1;
    }

  ++*argindp;

  if( opt[len+2] )		/* '--<long_option>=<argument>' syntax */
    {
    if( options[index].has_arg == ap_no )
      {
      add_error( ap, "option '--" ); add_error( ap, options[index].name );
      add_error( ap, "' doesn't allow an argument" );
      return 1;
      }
    if( options[index].has_arg == ap_yes && !opt[len+3] )
      {
      add_error( ap, "option '--" ); add_error( ap, options[index].name );
      add_error( ap, "' requires an argument" );
      return 1;
      }
    return push_back_record( ap, options[index].code, &opt[len+3] );
    }

  if( options[index].has_arg == ap_yes )
    {
    if( !arg || !arg[0] )
      {
      add_error( ap, "option '--" ); add_error( ap, options[index].name );
      add_error( ap, "' requires an argument" );
      return 1;
      }
    ++*argindp;
    return push_back_record( ap, options[index].code, arg );
    }

  return push_back_record( ap, options[index].code, "" );
  }


static char parse_short_option( struct Arg_parser * const ap,
                                const char * const opt, const char * const arg,
                                const struct ap_Option options[],
                                int * const argindp )
  {
  int cind = 1;			/* character index in opt */

  while( cind > 0 )
    {
    int index = -1, i;
    const unsigned char code = opt[cind];
    char code_str[2];
    code_str[0] = code; code_str[1] = 0;

    if( code != 0 )
      for( i = 0; options[i].code; ++i )
        if( code == options[i].code )
          { index = i; break; }

    if( index < 0 )
      {
      add_error( ap, "invalid option -- '" ); add_error( ap, code_str );
      add_error( ap, "'" );
      return 1;
      }

    if( opt[++cind] == 0 ) { ++*argindp; cind = 0; }	/* opt finished */

    if( options[index].has_arg != ap_no && cind > 0 && opt[cind] )
      {
      if( !push_back_record( ap, code, &opt[cind] ) ) return 0;
      ++*argindp; cind = 0;
      }
    else if( options[index].has_arg == ap_yes )
      {
      if( !arg || !arg[0] )
        {
        add_error( ap, "option requires an argument -- '" );
        add_error( ap, code_str ); add_error( ap, "'" );
        return 1;
        }
      ++*argindp; cind = 0;
      if( !push_back_record( ap, code, arg ) ) return 0;
      }
    else if( !push_back_record( ap, code, "" ) ) return 0;
    }
  return 1;
  }


char ap_init( struct Arg_parser * const ap,
              const int argc, const char * const argv[],
              const struct ap_Option options[], const char in_order )
  {
  const char ** non_options = 0;	/* skipped non-options */
  int non_options_size = 0;		/* number of skipped non-options */
  int argind = 1;			/* index in argv */
  int i;

  ap->data = 0;
  ap->error = 0;
  ap->data_size = 0;
  ap->error_size = 0;
  if( argc < 2 || !argv || !options ) return 1;

  while( argind < argc )
    {
    const unsigned char ch1 = argv[argind][0];
    const unsigned char ch2 = ch1 ? argv[argind][1] : 0;

    if( ch1 == '-' && ch2 )		/* we found an option */
      {
      const char * const opt = argv[argind];
      const char * const arg = ( argind + 1 < argc ) ? argv[argind+1] : 0;
      if( ch2 == '-' )
        {
        if( !argv[argind][2] ) { ++argind; break; }	/* we found "--" */
        else if( !parse_long_option( ap, opt, arg, options, &argind ) ) return 0;
        }
      else if( !parse_short_option( ap, opt, arg, options, &argind ) ) return 0;
      if( ap->error ) break;
      }
    else
      {
      if( in_order )
        { if( !push_back_record( ap, 0, argv[argind++] ) ) return 0; }
      else
        {
        void * tmp = ap_resize_buffer( non_options,
                       ( non_options_size + 1 ) * sizeof *non_options );
        if( !tmp ) return 0;
        non_options = (const char **)tmp;
        non_options[non_options_size++] = argv[argind++];
        }
      }
    }
  if( ap->error ) free_data( ap );
  else
    {
    for( i = 0; i < non_options_size; ++i )
      if( !push_back_record( ap, 0, non_options[i] ) ) return 0;
    while( argind < argc )
      if( !push_back_record( ap, 0, argv[argind++] ) ) return 0;
    }
  if( non_options ) free( non_options );
  return 1;
  }


void ap_free( struct Arg_parser * const ap )
  {
  free_data( ap );
  if( ap->error ) { free( ap->error ); ap->error = 0; }
  ap->error_size = 0;
  }


const char * ap_error( const struct Arg_parser * const ap )
  { return ap->error; }


int ap_arguments( const struct Arg_parser * const ap )
  { return ap->data_size; }


int ap_code( const struct Arg_parser * const ap, const int i )
  {
  if( i >= 0 && i < ap_arguments( ap ) ) return ap->data[i].code;
  else return 0;
  }


const char * ap_argument( const struct Arg_parser * const ap, const int i )
  {
  if( i >= 0 && i < ap_arguments( ap ) ) return ap->data[i].argument;
  else return "";
  }
