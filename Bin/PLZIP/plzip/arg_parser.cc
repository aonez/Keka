/*  Arg_parser - POSIX/GNU command line argument parser. (C++ version)
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

#include <cstring>
#include <string>
#include <vector>

#include "arg_parser.h"


bool Arg_parser::parse_long_option( const char * const opt, const char * const arg,
                                    const Option options[], int & argind )
  {
  unsigned len;
  int index = -1;
  bool exact = false, ambig = false;

  for( len = 0; opt[len+2] && opt[len+2] != '='; ++len ) ;

  // Test all long options for either exact match or abbreviated matches.
  for( int i = 0; options[i].code != 0; ++i )
    if( options[i].name && std::strncmp( options[i].name, &opt[2], len ) == 0 )
      {
      if( std::strlen( options[i].name ) == len )	// Exact match found
        { index = i; exact = true; break; }
      else if( index < 0 ) index = i;		// First nonexact match found
      else if( options[index].code != options[i].code ||
               options[index].has_arg != options[i].has_arg )
        ambig = true;		// Second or later nonexact match found
      }

  if( ambig && !exact )
    {
    error_ = "option '"; error_ += opt; error_ += "' is ambiguous";
    return false;
    }

  if( index < 0 )		// nothing found
    {
    error_ = "unrecognized option '"; error_ += opt; error_ += '\'';
    return false;
    }

  ++argind;
  data.push_back( Record( options[index].code ) );

  if( opt[len+2] )		// '--<long_option>=<argument>' syntax
    {
    if( options[index].has_arg == no )
      {
      error_ = "option '--"; error_ += options[index].name;
      error_ += "' doesn't allow an argument";
      return false;
      }
    if( options[index].has_arg == yes && !opt[len+3] )
      {
      error_ = "option '--"; error_ += options[index].name;
      error_ += "' requires an argument";
      return false;
      }
    data.back().argument = &opt[len+3];
    return true;
    }

  if( options[index].has_arg == yes )
    {
    if( !arg || !arg[0] )
      {
      error_ = "option '--"; error_ += options[index].name;
      error_ += "' requires an argument";
      return false;
      }
    ++argind; data.back().argument = arg;
    return true;
    }

  return true;
  }


bool Arg_parser::parse_short_option( const char * const opt, const char * const arg,
                                     const Option options[], int & argind )
  {
  int cind = 1;			// character index in opt

  while( cind > 0 )
    {
    int index = -1;
    const unsigned char c = opt[cind];

    if( c != 0 )
      for( int i = 0; options[i].code; ++i )
        if( c == options[i].code )
          { index = i; break; }

    if( index < 0 )
      {
      error_ = "invalid option -- '"; error_ += c; error_ += '\'';
      return false;
      }

    data.push_back( Record( c ) );
    if( opt[++cind] == 0 ) { ++argind; cind = 0; }	// opt finished

    if( options[index].has_arg != no && cind > 0 && opt[cind] )
      {
      data.back().argument = &opt[cind]; ++argind; cind = 0;
      }
    else if( options[index].has_arg == yes )
      {
      if( !arg || !arg[0] )
        {
        error_ = "option requires an argument -- '"; error_ += c;
        error_ += '\'';
        return false;
        }
      data.back().argument = arg; ++argind; cind = 0;
      }
    }
  return true;
  }


Arg_parser::Arg_parser( const int argc, const char * const argv[],
                        const Option options[], const bool in_order )
  {
  if( argc < 2 || !argv || !options ) return;

  std::vector< const char * > non_options;	// skipped non-options
  int argind = 1;				// index in argv

  while( argind < argc )
    {
    const unsigned char ch1 = argv[argind][0];
    const unsigned char ch2 = ch1 ? argv[argind][1] : 0;

    if( ch1 == '-' && ch2 )		// we found an option
      {
      const char * const opt = argv[argind];
      const char * const arg = ( argind + 1 < argc ) ? argv[argind+1] : 0;
      if( ch2 == '-' )
        {
        if( !argv[argind][2] ) { ++argind; break; }	// we found "--"
        else if( !parse_long_option( opt, arg, options, argind ) ) break;
        }
      else if( !parse_short_option( opt, arg, options, argind ) ) break;
      }
    else
      {
      if( in_order ) data.push_back( Record( argv[argind++] ) );
      else non_options.push_back( argv[argind++] );
      }
    }
  if( error_.size() ) data.clear();
  else
    {
    for( unsigned i = 0; i < non_options.size(); ++i )
      data.push_back( Record( non_options[i] ) );
    while( argind < argc )
      data.push_back( Record( argv[argind++] ) );
    }
  }


Arg_parser::Arg_parser( const char * const opt, const char * const arg,
                        const Option options[] )
  {
  if( !opt || !opt[0] || !options ) return;

  if( opt[0] == '-' && opt[1] )		// we found an option
    {
    int argind = 1;			// dummy
    if( opt[1] == '-' )
      { if( opt[2] ) parse_long_option( opt, arg, options, argind ); }
    else
      parse_short_option( opt, arg, options, argind );
    if( error_.size() ) data.clear();
    }
  else data.push_back( Record( opt ) );
  }
