/*  Lzlib - Compression library for the lzip format
    Copyright (C) 2009-2017 Antonio Diaz Diaz.

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

#ifdef __cplusplus
extern "C" {
#endif

#define LZ_API_VERSION 1

static const char * const LZ_version_string = "1.9";

enum LZ_Errno { LZ_ok = 0,         LZ_bad_argument, LZ_mem_error,
                LZ_sequence_error, LZ_header_error, LZ_unexpected_eof,
                LZ_data_error,     LZ_library_error };


const char * LZ_version( void );
const char * LZ_strerror( const enum LZ_Errno lz_errno );

int LZ_min_dictionary_bits( void );
int LZ_min_dictionary_size( void );
int LZ_max_dictionary_bits( void );
int LZ_max_dictionary_size( void );
int LZ_min_match_len_limit( void );
int LZ_max_match_len_limit( void );


/*---------------------- Compression Functions ----------------------*/

struct LZ_Encoder;

struct LZ_Encoder * LZ_compress_open( const int dictionary_size,
                                      const int match_len_limit,
                                      const unsigned long long member_size );
int LZ_compress_close( struct LZ_Encoder * const encoder );

int LZ_compress_finish( struct LZ_Encoder * const encoder );
int LZ_compress_restart_member( struct LZ_Encoder * const encoder,
                                const unsigned long long member_size );
int LZ_compress_sync_flush( struct LZ_Encoder * const encoder );

int LZ_compress_read( struct LZ_Encoder * const encoder,
                      uint8_t * const buffer, const int size );
int LZ_compress_write( struct LZ_Encoder * const encoder,
                       const uint8_t * const buffer, const int size );
int LZ_compress_write_size( struct LZ_Encoder * const encoder );

enum LZ_Errno LZ_compress_errno( struct LZ_Encoder * const encoder );
int LZ_compress_finished( struct LZ_Encoder * const encoder );
int LZ_compress_member_finished( struct LZ_Encoder * const encoder );

unsigned long long LZ_compress_data_position( struct LZ_Encoder * const encoder );
unsigned long long LZ_compress_member_position( struct LZ_Encoder * const encoder );
unsigned long long LZ_compress_total_in_size( struct LZ_Encoder * const encoder );
unsigned long long LZ_compress_total_out_size( struct LZ_Encoder * const encoder );


/*--------------------- Decompression Functions ---------------------*/

struct LZ_Decoder;

struct LZ_Decoder * LZ_decompress_open( void );
int LZ_decompress_close( struct LZ_Decoder * const decoder );

int LZ_decompress_finish( struct LZ_Decoder * const decoder );
int LZ_decompress_reset( struct LZ_Decoder * const decoder );
int LZ_decompress_sync_to_member( struct LZ_Decoder * const decoder );

int LZ_decompress_read( struct LZ_Decoder * const decoder,
                        uint8_t * const buffer, const int size );
int LZ_decompress_write( struct LZ_Decoder * const decoder,
                         const uint8_t * const buffer, const int size );
int LZ_decompress_write_size( struct LZ_Decoder * const decoder );

enum LZ_Errno LZ_decompress_errno( struct LZ_Decoder * const decoder );
int LZ_decompress_finished( struct LZ_Decoder * const decoder );
int LZ_decompress_member_finished( struct LZ_Decoder * const decoder );

int LZ_decompress_member_version( struct LZ_Decoder * const decoder );
int LZ_decompress_dictionary_size( struct LZ_Decoder * const decoder );
unsigned LZ_decompress_data_crc( struct LZ_Decoder * const decoder );

unsigned long long LZ_decompress_data_position( struct LZ_Decoder * const decoder );
unsigned long long LZ_decompress_member_position( struct LZ_Decoder * const decoder );
unsigned long long LZ_decompress_total_in_size( struct LZ_Decoder * const decoder );
unsigned long long LZ_decompress_total_out_size( struct LZ_Decoder * const decoder );

#ifdef __cplusplus
}
#endif
