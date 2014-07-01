/* -------------------------------------------------------------------------------

Copyright (C) 1999-2006 Id Software, Inc. and contributors.
For a list of contributors, see the accompanying CONTRIBUTORS file.

This file is part of GtkRadiant.

GtkRadiant is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GtkRadiant is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GtkRadiant; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

----------------------------------------------------------------------------------

This code has been altered significantly from its original form, to support
several games based on the Quake III Arena engine, in the form of "Q3Map2."

------------------------------------------------------------------------------- */



/* marker */
#define IMAGE_C



/* dependencies */
#include "q3map2.h"



/* -------------------------------------------------------------------------------

this file contains image pool management with reference counting. note: it isn't
reentrant, so only call it from init/shutdown code or wrap calls in a mutex

------------------------------------------------------------------------------- */

/*
LoadDDSBuffer()
loads a dxtc (1, 3, 5) dds buffer into a valid rgba image
*/

static void LoadDDSBuffer( byte *buffer, int size, byte **pixels, int *width, int *height )
{
	int		w, h;
	ddsPF_t	pf;
	
	
	/* dummy check */
	if( buffer == NULL || size <= 0 || pixels == NULL || width == NULL || height == NULL )
		return;
	
	/* null out */
	*pixels = 0;
	*width = 0;
	*height = 0;
	
	/* get dds info */
	if( DDSGetInfo( (ddsBuffer_t*) buffer, &w, &h, &pf ) )
	{
		Sys_Printf( "WARNING: Invalid DDS texture\n" );
		return;
	}
	
	/* only certain types of dds textures are supported */
	if( pf != DDS_PF_ARGB8888 && pf != DDS_PF_DXT1 && pf != DDS_PF_DXT3 && pf != DDS_PF_DXT5 )
	{
		Sys_Printf( "WARNING: Only DDS texture formats ARGB8888, DXT1, DXT3, and DXT5 are supported (%d)\n", pf );
		return;
	}
	
	/* create image pixel buffer */
	*width = w;
	*height = h;
	*pixels = (byte *)safe_malloc( w * h * 4 );
	
	/* decompress the dds texture */
	DDSDecompress( (ddsBuffer_t*) buffer, *pixels );
}



/*
PNGReadData()
callback function for libpng to read from a memory buffer
note: this function is a total hack, as it reads/writes the png struct directly!
*/

typedef struct pngBuffer_s
{
	byte	*buffer;
	int		size, offset;
}
pngBuffer_t;

void PNGReadData( png_struct *png, png_byte *buffer, png_size_t size )
{
	pngBuffer_t		*pb = (pngBuffer_t*) png_get_io_ptr( png );
	
	
	if( (pb->offset + size) > pb->size )
		size = (pb->size - pb->offset);
	memcpy( buffer, &pb->buffer[ pb->offset ], size );
	pb->offset += (int)size;
	//%	Sys_Printf( "Copying %d bytes from 0x%08X to 0x%08X (offset: %d of %d)\n", size, &pb->buffer[ pb->offset ], buffer, pb->offset, pb->size );
}



/*
LoadPNGBuffer()
loads a png file buffer into a valid rgba image
*/

static void LoadPNGBuffer( byte *buffer, int size, byte **pixels, int *width, int *height )
{
	png_struct	*png;
	png_info	*info, *end;
	pngBuffer_t	pb;
	int			i, bitDepth, colorType, channels;
	png_uint_32	w, h;
	byte		**rowPointers;
	
	
	/* dummy check */
	if( buffer == NULL || size <= 0 || pixels == NULL || width == NULL || height == NULL )
		return;
	
	/* null out */
	*pixels = 0;
	*width = 0;
	*height = 0;
	
	/* determine if this is a png file */
	if( png_sig_cmp( buffer, 0, 8 ) != 0 )
	{
		Sys_Printf( "WARNING: Invalid PNG file\n" );
		return;
	}
	
	/* create png structs */
	png = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	if( png == NULL )
	{
		Sys_Printf( "WARNING: Unable to create PNG read struct\n" );
		return;
	}
	
	info = png_create_info_struct( png );
	if( info == NULL )
	{
		Sys_Printf( "WARNING: Unable to create PNG info struct\n" );
		png_destroy_read_struct( &png, NULL, NULL );
		return;
	}
	
	end = png_create_info_struct( png );
	if( end == NULL )
	{
		Sys_Printf( "WARNING: Unable to create PNG end info struct\n" );
		png_destroy_read_struct( &png, &info, NULL );
		return;
	}
	
	/* set read callback */
	pb.buffer = buffer;
	pb.size = size;
	pb.offset = 0;
	png_set_read_fn( png, &pb, PNGReadData );
	png->io_ptr = &pb; /* hack! */
	
	/* set error longjmp */
	if( setjmp( png->jmpbuf ) )
	{
		Sys_Printf( "WARNING: An error occurred reading PNG image\n" );
		png_destroy_read_struct( &png, &info, &end );
		return;
	}
	
	/* fixme: add proper i/o stuff here */

	/* read png info */
	png_read_info( png, info );
	
	/* read image header chunk */
	png_get_IHDR( png, info,
		&w, &h, &bitDepth, &colorType, NULL, NULL, NULL );
	
	/* read number of channels */
	channels = png_get_channels( png, info );
	
	/* the following will probably bork on certain types of png images, but hey... */

	/* force indexed/gray/trans chunk to rgb */
	if( (colorType == PNG_COLOR_TYPE_PALETTE && bitDepth <= 8) ||
		(colorType == PNG_COLOR_TYPE_GRAY && bitDepth <= 8) ||
		png_get_valid( png, info, PNG_INFO_tRNS ) )
		png_set_expand( png );
	
	/* strip 16bpc -> 8bpc */
	if( bitDepth == 16 )
		png_set_strip_16( png );
	
	/* pad rgb to rgba */
	if( bitDepth == 8 && colorType == PNG_COLOR_TYPE_RGB)
		png_set_filler( png, 255, PNG_FILLER_AFTER );
	
	/* create image pixel buffer */
	*width = w;
	*height = h;
	*pixels = (byte *)safe_malloc( w * h * 4 );
	
	/* create row pointers */
	rowPointers = (byte **)safe_malloc( h * sizeof( byte* ) );
	for( i = 0; i < h; i++ )
		rowPointers[ i ] = *pixels + (i * w * 4);
	
	/* read the png */
	png_read_image( png, rowPointers );
	
	/* clean up */
	free( rowPointers );
	png_destroy_read_struct( &png, &info, &end );
	
}

/*
WritePNG
writes out PNG file
code was taken from Darkplaces engine sourcecode
Copyright (C)2006 Serge "(515)" Ziryukin, Forest "LordHavoc" Hale, Pavel [VorteX] Timofeyev
*/

// ICC sRGB profile
unsigned char icc_profile_sRGB_IEC61966_2_1[] =
{
	0x00,0x00,0x0C,0x48,0x4C,0x69,0x6E,0x6F,0x02,0x10,0x00,
	0x00,0x6D,0x6E,0x74,0x72,0x52,0x47,0x42,0x20,0x58,0x59,
	0x5A,0x20,0x07,0xCE,0x00,0x02,0x00,0x09,0x00,0x06,0x00,
	0x31,0x00,0x00,0x61,0x63,0x73,0x70,0x4D,0x53,0x46,0x54,
	0x00,0x00,0x00,0x00,0x49,0x45,0x43,0x20,0x73,0x52,0x47,
	0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01,0x00,0x00,0xF6,0xD6,0x00,0x01,0x00,0x00,0x00,
	0x00,0xD3,0x2D,0x48,0x50,0x20,0x20,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x11,
	0x63,0x70,0x72,0x74,0x00,0x00,0x01,0x50,0x00,0x00,0x00,
	0x33,0x64,0x65,0x73,0x63,0x00,0x00,0x01,0x84,0x00,0x00,
	0x00,0x6C,0x77,0x74,0x70,0x74,0x00,0x00,0x01,0xF0,0x00,
	0x00,0x00,0x14,0x62,0x6B,0x70,0x74,0x00,0x00,0x02,0x04,
	0x00,0x00,0x00,0x14,0x72,0x58,0x59,0x5A,0x00,0x00,0x02,
	0x18,0x00,0x00,0x00,0x14,0x67,0x58,0x59,0x5A,0x00,0x00,
	0x02,0x2C,0x00,0x00,0x00,0x14,0x62,0x58,0x59,0x5A,0x00,
	0x00,0x02,0x40,0x00,0x00,0x00,0x14,0x64,0x6D,0x6E,0x64,
	0x00,0x00,0x02,0x54,0x00,0x00,0x00,0x70,0x64,0x6D,0x64,
	0x64,0x00,0x00,0x02,0xC4,0x00,0x00,0x00,0x88,0x76,0x75,
	0x65,0x64,0x00,0x00,0x03,0x4C,0x00,0x00,0x00,0x86,0x76,
	0x69,0x65,0x77,0x00,0x00,0x03,0xD4,0x00,0x00,0x00,0x24,
	0x6C,0x75,0x6D,0x69,0x00,0x00,0x03,0xF8,0x00,0x00,0x00,
	0x14,0x6D,0x65,0x61,0x73,0x00,0x00,0x04,0x0C,0x00,0x00,
	0x00,0x24,0x74,0x65,0x63,0x68,0x00,0x00,0x04,0x30,0x00,
	0x00,0x00,0x0C,0x72,0x54,0x52,0x43,0x00,0x00,0x04,0x3C,
	0x00,0x00,0x08,0x0C,0x67,0x54,0x52,0x43,0x00,0x00,0x04,
	0x3C,0x00,0x00,0x08,0x0C,0x62,0x54,0x52,0x43,0x00,0x00,
	0x04,0x3C,0x00,0x00,0x08,0x0C,0x74,0x65,0x78,0x74,0x00,
	0x00,0x00,0x00,0x43,0x6F,0x70,0x79,0x72,0x69,0x67,0x68,
	0x74,0x20,0x28,0x63,0x29,0x20,0x31,0x39,0x39,0x38,0x20,
	0x48,0x65,0x77,0x6C,0x65,0x74,0x74,0x2D,0x50,0x61,0x63,
	0x6B,0x61,0x72,0x64,0x20,0x43,0x6F,0x6D,0x70,0x61,0x6E,
	0x79,0x00,0x00,0x64,0x65,0x73,0x63,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x12,0x73,0x52,0x47,0x42,0x20,0x49,0x45,
	0x43,0x36,0x31,0x39,0x36,0x36,0x2D,0x32,0x2E,0x31,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x12,
	0x73,0x52,0x47,0x42,0x20,0x49,0x45,0x43,0x36,0x31,0x39,
	0x36,0x36,0x2D,0x32,0x2E,0x31,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x58,0x59,0x5A,0x20,0x00,0x00,0x00,0x00,0x00,0x00,
	0xF3,0x51,0x00,0x01,0x00,0x00,0x00,0x01,0x16,0xCC,0x58,
	0x59,0x5A,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x58,0x59,0x5A,
	0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x6F,0xA2,0x00,0x00,
	0x38,0xF5,0x00,0x00,0x03,0x90,0x58,0x59,0x5A,0x20,0x00,
	0x00,0x00,0x00,0x00,0x00,0x62,0x99,0x00,0x00,0xB7,0x85,
	0x00,0x00,0x18,0xDA,0x58,0x59,0x5A,0x20,0x00,0x00,0x00,
	0x00,0x00,0x00,0x24,0xA0,0x00,0x00,0x0F,0x84,0x00,0x00,
	0xB6,0xCF,0x64,0x65,0x73,0x63,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x16,0x49,0x45,0x43,0x20,0x68,0x74,0x74,0x70,
	0x3A,0x2F,0x2F,0x77,0x77,0x77,0x2E,0x69,0x65,0x63,0x2E,
	0x63,0x68,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x16,0x49,0x45,0x43,0x20,0x68,0x74,0x74,0x70,
	0x3A,0x2F,0x2F,0x77,0x77,0x77,0x2E,0x69,0x65,0x63,0x2E,
	0x63,0x68,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x64,0x65,0x73,0x63,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x2E,0x49,0x45,0x43,0x20,0x36,0x31,
	0x39,0x36,0x36,0x2D,0x32,0x2E,0x31,0x20,0x44,0x65,0x66,
	0x61,0x75,0x6C,0x74,0x20,0x52,0x47,0x42,0x20,0x63,0x6F,
	0x6C,0x6F,0x75,0x72,0x20,0x73,0x70,0x61,0x63,0x65,0x20,
	0x2D,0x20,0x73,0x52,0x47,0x42,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x2E,0x49,0x45,0x43,0x20,
	0x36,0x31,0x39,0x36,0x36,0x2D,0x32,0x2E,0x31,0x20,0x44,
	0x65,0x66,0x61,0x75,0x6C,0x74,0x20,0x52,0x47,0x42,0x20,
	0x63,0x6F,0x6C,0x6F,0x75,0x72,0x20,0x73,0x70,0x61,0x63,
	0x65,0x20,0x2D,0x20,0x73,0x52,0x47,0x42,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x64,0x65,0x73,
	0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2C,0x52,0x65,
	0x66,0x65,0x72,0x65,0x6E,0x63,0x65,0x20,0x56,0x69,0x65,
	0x77,0x69,0x6E,0x67,0x20,0x43,0x6F,0x6E,0x64,0x69,0x74,
	0x69,0x6F,0x6E,0x20,0x69,0x6E,0x20,0x49,0x45,0x43,0x36,
	0x31,0x39,0x36,0x36,0x2D,0x32,0x2E,0x31,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2C,0x52,0x65,
	0x66,0x65,0x72,0x65,0x6E,0x63,0x65,0x20,0x56,0x69,0x65,
	0x77,0x69,0x6E,0x67,0x20,0x43,0x6F,0x6E,0x64,0x69,0x74,
	0x69,0x6F,0x6E,0x20,0x69,0x6E,0x20,0x49,0x45,0x43,0x36,
	0x31,0x39,0x36,0x36,0x2D,0x32,0x2E,0x31,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x76,0x69,0x65,0x77,0x00,0x00,0x00,0x00,0x00,0x13,
	0xA4,0xFE,0x00,0x14,0x5F,0x2E,0x00,0x10,0xCF,0x14,0x00,
	0x03,0xED,0xCC,0x00,0x04,0x13,0x0B,0x00,0x03,0x5C,0x9E,
	0x00,0x00,0x00,0x01,0x58,0x59,0x5A,0x20,0x00,0x00,0x00,
	0x00,0x00,0x4C,0x09,0x56,0x00,0x50,0x00,0x00,0x00,0x57,
	0x1F,0xE7,0x6D,0x65,0x61,0x73,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,
	0x8F,0x00,0x00,0x00,0x02,0x73,0x69,0x67,0x20,0x00,0x00,
	0x00,0x00,0x43,0x52,0x54,0x20,0x63,0x75,0x72,0x76,0x00,
	0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x05,
	0x00,0x0A,0x00,0x0F,0x00,0x14,0x00,0x19,0x00,0x1E,0x00,
	0x23,0x00,0x28,0x00,0x2D,0x00,0x32,0x00,0x37,0x00,0x3B,
	0x00,0x40,0x00,0x45,0x00,0x4A,0x00,0x4F,0x00,0x54,0x00,
	0x59,0x00,0x5E,0x00,0x63,0x00,0x68,0x00,0x6D,0x00,0x72,
	0x00,0x77,0x00,0x7C,0x00,0x81,0x00,0x86,0x00,0x8B,0x00,
	0x90,0x00,0x95,0x00,0x9A,0x00,0x9F,0x00,0xA4,0x00,0xA9,
	0x00,0xAE,0x00,0xB2,0x00,0xB7,0x00,0xBC,0x00,0xC1,0x00,
	0xC6,0x00,0xCB,0x00,0xD0,0x00,0xD5,0x00,0xDB,0x00,0xE0,
	0x00,0xE5,0x00,0xEB,0x00,0xF0,0x00,0xF6,0x00,0xFB,0x01,
	0x01,0x01,0x07,0x01,0x0D,0x01,0x13,0x01,0x19,0x01,0x1F,
	0x01,0x25,0x01,0x2B,0x01,0x32,0x01,0x38,0x01,0x3E,0x01,
	0x45,0x01,0x4C,0x01,0x52,0x01,0x59,0x01,0x60,0x01,0x67,
	0x01,0x6E,0x01,0x75,0x01,0x7C,0x01,0x83,0x01,0x8B,0x01,
	0x92,0x01,0x9A,0x01,0xA1,0x01,0xA9,0x01,0xB1,0x01,0xB9,
	0x01,0xC1,0x01,0xC9,0x01,0xD1,0x01,0xD9,0x01,0xE1,0x01,
	0xE9,0x01,0xF2,0x01,0xFA,0x02,0x03,0x02,0x0C,0x02,0x14,
	0x02,0x1D,0x02,0x26,0x02,0x2F,0x02,0x38,0x02,0x41,0x02,
	0x4B,0x02,0x54,0x02,0x5D,0x02,0x67,0x02,0x71,0x02,0x7A,
	0x02,0x84,0x02,0x8E,0x02,0x98,0x02,0xA2,0x02,0xAC,0x02,
	0xB6,0x02,0xC1,0x02,0xCB,0x02,0xD5,0x02,0xE0,0x02,0xEB,
	0x02,0xF5,0x03,0x00,0x03,0x0B,0x03,0x16,0x03,0x21,0x03,
	0x2D,0x03,0x38,0x03,0x43,0x03,0x4F,0x03,0x5A,0x03,0x66,
	0x03,0x72,0x03,0x7E,0x03,0x8A,0x03,0x96,0x03,0xA2,0x03,
	0xAE,0x03,0xBA,0x03,0xC7,0x03,0xD3,0x03,0xE0,0x03,0xEC,
	0x03,0xF9,0x04,0x06,0x04,0x13,0x04,0x20,0x04,0x2D,0x04,
	0x3B,0x04,0x48,0x04,0x55,0x04,0x63,0x04,0x71,0x04,0x7E,
	0x04,0x8C,0x04,0x9A,0x04,0xA8,0x04,0xB6,0x04,0xC4,0x04,
	0xD3,0x04,0xE1,0x04,0xF0,0x04,0xFE,0x05,0x0D,0x05,0x1C,
	0x05,0x2B,0x05,0x3A,0x05,0x49,0x05,0x58,0x05,0x67,0x05,
	0x77,0x05,0x86,0x05,0x96,0x05,0xA6,0x05,0xB5,0x05,0xC5,
	0x05,0xD5,0x05,0xE5,0x05,0xF6,0x06,0x06,0x06,0x16,0x06,
	0x27,0x06,0x37,0x06,0x48,0x06,0x59,0x06,0x6A,0x06,0x7B,
	0x06,0x8C,0x06,0x9D,0x06,0xAF,0x06,0xC0,0x06,0xD1,0x06,
	0xE3,0x06,0xF5,0x07,0x07,0x07,0x19,0x07,0x2B,0x07,0x3D,
	0x07,0x4F,0x07,0x61,0x07,0x74,0x07,0x86,0x07,0x99,0x07,
	0xAC,0x07,0xBF,0x07,0xD2,0x07,0xE5,0x07,0xF8,0x08,0x0B,
	0x08,0x1F,0x08,0x32,0x08,0x46,0x08,0x5A,0x08,0x6E,0x08,
	0x82,0x08,0x96,0x08,0xAA,0x08,0xBE,0x08,0xD2,0x08,0xE7,
	0x08,0xFB,0x09,0x10,0x09,0x25,0x09,0x3A,0x09,0x4F,0x09,
	0x64,0x09,0x79,0x09,0x8F,0x09,0xA4,0x09,0xBA,0x09,0xCF,
	0x09,0xE5,0x09,0xFB,0x0A,0x11,0x0A,0x27,0x0A,0x3D,0x0A,
	0x54,0x0A,0x6A,0x0A,0x81,0x0A,0x98,0x0A,0xAE,0x0A,0xC5,
	0x0A,0xDC,0x0A,0xF3,0x0B,0x0B,0x0B,0x22,0x0B,0x39,0x0B,
	0x51,0x0B,0x69,0x0B,0x80,0x0B,0x98,0x0B,0xB0,0x0B,0xC8,
	0x0B,0xE1,0x0B,0xF9,0x0C,0x12,0x0C,0x2A,0x0C,0x43,0x0C,
	0x5C,0x0C,0x75,0x0C,0x8E,0x0C,0xA7,0x0C,0xC0,0x0C,0xD9,
	0x0C,0xF3,0x0D,0x0D,0x0D,0x26,0x0D,0x40,0x0D,0x5A,0x0D,
	0x74,0x0D,0x8E,0x0D,0xA9,0x0D,0xC3,0x0D,0xDE,0x0D,0xF8,
	0x0E,0x13,0x0E,0x2E,0x0E,0x49,0x0E,0x64,0x0E,0x7F,0x0E,
	0x9B,0x0E,0xB6,0x0E,0xD2,0x0E,0xEE,0x0F,0x09,0x0F,0x25,
	0x0F,0x41,0x0F,0x5E,0x0F,0x7A,0x0F,0x96,0x0F,0xB3,0x0F,
	0xCF,0x0F,0xEC,0x10,0x09,0x10,0x26,0x10,0x43,0x10,0x61,
	0x10,0x7E,0x10,0x9B,0x10,0xB9,0x10,0xD7,0x10,0xF5,0x11,
	0x13,0x11,0x31,0x11,0x4F,0x11,0x6D,0x11,0x8C,0x11,0xAA,
	0x11,0xC9,0x11,0xE8,0x12,0x07,0x12,0x26,0x12,0x45,0x12,
	0x64,0x12,0x84,0x12,0xA3,0x12,0xC3,0x12,0xE3,0x13,0x03,
	0x13,0x23,0x13,0x43,0x13,0x63,0x13,0x83,0x13,0xA4,0x13,
	0xC5,0x13,0xE5,0x14,0x06,0x14,0x27,0x14,0x49,0x14,0x6A,
	0x14,0x8B,0x14,0xAD,0x14,0xCE,0x14,0xF0,0x15,0x12,0x15,
	0x34,0x15,0x56,0x15,0x78,0x15,0x9B,0x15,0xBD,0x15,0xE0,
	0x16,0x03,0x16,0x26,0x16,0x49,0x16,0x6C,0x16,0x8F,0x16,
	0xB2,0x16,0xD6,0x16,0xFA,0x17,0x1D,0x17,0x41,0x17,0x65,
	0x17,0x89,0x17,0xAE,0x17,0xD2,0x17,0xF7,0x18,0x1B,0x18,
	0x40,0x18,0x65,0x18,0x8A,0x18,0xAF,0x18,0xD5,0x18,0xFA,
	0x19,0x20,0x19,0x45,0x19,0x6B,0x19,0x91,0x19,0xB7,0x19,
	0xDD,0x1A,0x04,0x1A,0x2A,0x1A,0x51,0x1A,0x77,0x1A,0x9E,
	0x1A,0xC5,0x1A,0xEC,0x1B,0x14,0x1B,0x3B,0x1B,0x63,0x1B,
	0x8A,0x1B,0xB2,0x1B,0xDA,0x1C,0x02,0x1C,0x2A,0x1C,0x52,
	0x1C,0x7B,0x1C,0xA3,0x1C,0xCC,0x1C,0xF5,0x1D,0x1E,0x1D,
	0x47,0x1D,0x70,0x1D,0x99,0x1D,0xC3,0x1D,0xEC,0x1E,0x16,
	0x1E,0x40,0x1E,0x6A,0x1E,0x94,0x1E,0xBE,0x1E,0xE9,0x1F,
	0x13,0x1F,0x3E,0x1F,0x69,0x1F,0x94,0x1F,0xBF,0x1F,0xEA,
	0x20,0x15,0x20,0x41,0x20,0x6C,0x20,0x98,0x20,0xC4,0x20,
	0xF0,0x21,0x1C,0x21,0x48,0x21,0x75,0x21,0xA1,0x21,0xCE,
	0x21,0xFB,0x22,0x27,0x22,0x55,0x22,0x82,0x22,0xAF,0x22,
	0xDD,0x23,0x0A,0x23,0x38,0x23,0x66,0x23,0x94,0x23,0xC2,
	0x23,0xF0,0x24,0x1F,0x24,0x4D,0x24,0x7C,0x24,0xAB,0x24,
	0xDA,0x25,0x09,0x25,0x38,0x25,0x68,0x25,0x97,0x25,0xC7,
	0x25,0xF7,0x26,0x27,0x26,0x57,0x26,0x87,0x26,0xB7,0x26,
	0xE8,0x27,0x18,0x27,0x49,0x27,0x7A,0x27,0xAB,0x27,0xDC,
	0x28,0x0D,0x28,0x3F,0x28,0x71,0x28,0xA2,0x28,0xD4,0x29,
	0x06,0x29,0x38,0x29,0x6B,0x29,0x9D,0x29,0xD0,0x2A,0x02,
	0x2A,0x35,0x2A,0x68,0x2A,0x9B,0x2A,0xCF,0x2B,0x02,0x2B,
	0x36,0x2B,0x69,0x2B,0x9D,0x2B,0xD1,0x2C,0x05,0x2C,0x39,
	0x2C,0x6E,0x2C,0xA2,0x2C,0xD7,0x2D,0x0C,0x2D,0x41,0x2D,
	0x76,0x2D,0xAB,0x2D,0xE1,0x2E,0x16,0x2E,0x4C,0x2E,0x82,
	0x2E,0xB7,0x2E,0xEE,0x2F,0x24,0x2F,0x5A,0x2F,0x91,0x2F,
	0xC7,0x2F,0xFE,0x30,0x35,0x30,0x6C,0x30,0xA4,0x30,0xDB,
	0x31,0x12,0x31,0x4A,0x31,0x82,0x31,0xBA,0x31,0xF2,0x32,
	0x2A,0x32,0x63,0x32,0x9B,0x32,0xD4,0x33,0x0D,0x33,0x46,
	0x33,0x7F,0x33,0xB8,0x33,0xF1,0x34,0x2B,0x34,0x65,0x34,
	0x9E,0x34,0xD8,0x35,0x13,0x35,0x4D,0x35,0x87,0x35,0xC2,
	0x35,0xFD,0x36,0x37,0x36,0x72,0x36,0xAE,0x36,0xE9,0x37,
	0x24,0x37,0x60,0x37,0x9C,0x37,0xD7,0x38,0x14,0x38,0x50,
	0x38,0x8C,0x38,0xC8,0x39,0x05,0x39,0x42,0x39,0x7F,0x39,
	0xBC,0x39,0xF9,0x3A,0x36,0x3A,0x74,0x3A,0xB2,0x3A,0xEF,
	0x3B,0x2D,0x3B,0x6B,0x3B,0xAA,0x3B,0xE8,0x3C,0x27,0x3C,
	0x65,0x3C,0xA4,0x3C,0xE3,0x3D,0x22,0x3D,0x61,0x3D,0xA1,
	0x3D,0xE0,0x3E,0x20,0x3E,0x60,0x3E,0xA0,0x3E,0xE0,0x3F,
	0x21,0x3F,0x61,0x3F,0xA2,0x3F,0xE2,0x40,0x23,0x40,0x64,
	0x40,0xA6,0x40,0xE7,0x41,0x29,0x41,0x6A,0x41,0xAC,0x41,
	0xEE,0x42,0x30,0x42,0x72,0x42,0xB5,0x42,0xF7,0x43,0x3A,
	0x43,0x7D,0x43,0xC0,0x44,0x03,0x44,0x47,0x44,0x8A,0x44,
	0xCE,0x45,0x12,0x45,0x55,0x45,0x9A,0x45,0xDE,0x46,0x22,
	0x46,0x67,0x46,0xAB,0x46,0xF0,0x47,0x35,0x47,0x7B,0x47,
	0xC0,0x48,0x05,0x48,0x4B,0x48,0x91,0x48,0xD7,0x49,0x1D,
	0x49,0x63,0x49,0xA9,0x49,0xF0,0x4A,0x37,0x4A,0x7D,0x4A,
	0xC4,0x4B,0x0C,0x4B,0x53,0x4B,0x9A,0x4B,0xE2,0x4C,0x2A,
	0x4C,0x72,0x4C,0xBA,0x4D,0x02,0x4D,0x4A,0x4D,0x93,0x4D,
	0xDC,0x4E,0x25,0x4E,0x6E,0x4E,0xB7,0x4F,0x00,0x4F,0x49,
	0x4F,0x93,0x4F,0xDD,0x50,0x27,0x50,0x71,0x50,0xBB,0x51,
	0x06,0x51,0x50,0x51,0x9B,0x51,0xE6,0x52,0x31,0x52,0x7C,
	0x52,0xC7,0x53,0x13,0x53,0x5F,0x53,0xAA,0x53,0xF6,0x54,
	0x42,0x54,0x8F,0x54,0xDB,0x55,0x28,0x55,0x75,0x55,0xC2,
	0x56,0x0F,0x56,0x5C,0x56,0xA9,0x56,0xF7,0x57,0x44,0x57,
	0x92,0x57,0xE0,0x58,0x2F,0x58,0x7D,0x58,0xCB,0x59,0x1A,
	0x59,0x69,0x59,0xB8,0x5A,0x07,0x5A,0x56,0x5A,0xA6,0x5A,
	0xF5,0x5B,0x45,0x5B,0x95,0x5B,0xE5,0x5C,0x35,0x5C,0x86,
	0x5C,0xD6,0x5D,0x27,0x5D,0x78,0x5D,0xC9,0x5E,0x1A,0x5E,
	0x6C,0x5E,0xBD,0x5F,0x0F,0x5F,0x61,0x5F,0xB3,0x60,0x05,
	0x60,0x57,0x60,0xAA,0x60,0xFC,0x61,0x4F,0x61,0xA2,0x61,
	0xF5,0x62,0x49,0x62,0x9C,0x62,0xF0,0x63,0x43,0x63,0x97,
	0x63,0xEB,0x64,0x40,0x64,0x94,0x64,0xE9,0x65,0x3D,0x65,
	0x92,0x65,0xE7,0x66,0x3D,0x66,0x92,0x66,0xE8,0x67,0x3D,
	0x67,0x93,0x67,0xE9,0x68,0x3F,0x68,0x96,0x68,0xEC,0x69,
	0x43,0x69,0x9A,0x69,0xF1,0x6A,0x48,0x6A,0x9F,0x6A,0xF7,
	0x6B,0x4F,0x6B,0xA7,0x6B,0xFF,0x6C,0x57,0x6C,0xAF,0x6D,
	0x08,0x6D,0x60,0x6D,0xB9,0x6E,0x12,0x6E,0x6B,0x6E,0xC4,
	0x6F,0x1E,0x6F,0x78,0x6F,0xD1,0x70,0x2B,0x70,0x86,0x70,
	0xE0,0x71,0x3A,0x71,0x95,0x71,0xF0,0x72,0x4B,0x72,0xA6,
	0x73,0x01,0x73,0x5D,0x73,0xB8,0x74,0x14,0x74,0x70,0x74,
	0xCC,0x75,0x28,0x75,0x85,0x75,0xE1,0x76,0x3E,0x76,0x9B,
	0x76,0xF8,0x77,0x56,0x77,0xB3,0x78,0x11,0x78,0x6E,0x78,
	0xCC,0x79,0x2A,0x79,0x89,0x79,0xE7,0x7A,0x46,0x7A,0xA5,
	0x7B,0x04,0x7B,0x63,0x7B,0xC2,0x7C,0x21,0x7C,0x81,0x7C,
	0xE1,0x7D,0x41,0x7D,0xA1,0x7E,0x01,0x7E,0x62,0x7E,0xC2,
	0x7F,0x23,0x7F,0x84,0x7F,0xE5,0x80,0x47,0x80,0xA8,0x81,
	0x0A,0x81,0x6B,0x81,0xCD,0x82,0x30,0x82,0x92,0x82,0xF4,
	0x83,0x57,0x83,0xBA,0x84,0x1D,0x84,0x80,0x84,0xE3,0x85,
	0x47,0x85,0xAB,0x86,0x0E,0x86,0x72,0x86,0xD7,0x87,0x3B,
	0x87,0x9F,0x88,0x04,0x88,0x69,0x88,0xCE,0x89,0x33,0x89,
	0x99,0x89,0xFE,0x8A,0x64,0x8A,0xCA,0x8B,0x30,0x8B,0x96,
	0x8B,0xFC,0x8C,0x63,0x8C,0xCA,0x8D,0x31,0x8D,0x98,0x8D,
	0xFF,0x8E,0x66,0x8E,0xCE,0x8F,0x36,0x8F,0x9E,0x90,0x06,
	0x90,0x6E,0x90,0xD6,0x91,0x3F,0x91,0xA8,0x92,0x11,0x92,
	0x7A,0x92,0xE3,0x93,0x4D,0x93,0xB6,0x94,0x20,0x94,0x8A,
	0x94,0xF4,0x95,0x5F,0x95,0xC9,0x96,0x34,0x96,0x9F,0x97,
	0x0A,0x97,0x75,0x97,0xE0,0x98,0x4C,0x98,0xB8,0x99,0x24,
	0x99,0x90,0x99,0xFC,0x9A,0x68,0x9A,0xD5,0x9B,0x42,0x9B,
	0xAF,0x9C,0x1C,0x9C,0x89,0x9C,0xF7,0x9D,0x64,0x9D,0xD2,
	0x9E,0x40,0x9E,0xAE,0x9F,0x1D,0x9F,0x8B,0x9F,0xFA,0xA0,
	0x69,0xA0,0xD8,0xA1,0x47,0xA1,0xB6,0xA2,0x26,0xA2,0x96,
	0xA3,0x06,0xA3,0x76,0xA3,0xE6,0xA4,0x56,0xA4,0xC7,0xA5,
	0x38,0xA5,0xA9,0xA6,0x1A,0xA6,0x8B,0xA6,0xFD,0xA7,0x6E,
	0xA7,0xE0,0xA8,0x52,0xA8,0xC4,0xA9,0x37,0xA9,0xA9,0xAA,
	0x1C,0xAA,0x8F,0xAB,0x02,0xAB,0x75,0xAB,0xE9,0xAC,0x5C,
	0xAC,0xD0,0xAD,0x44,0xAD,0xB8,0xAE,0x2D,0xAE,0xA1,0xAF,
	0x16,0xAF,0x8B,0xB0,0x00,0xB0,0x75,0xB0,0xEA,0xB1,0x60,
	0xB1,0xD6,0xB2,0x4B,0xB2,0xC2,0xB3,0x38,0xB3,0xAE,0xB4,
	0x25,0xB4,0x9C,0xB5,0x13,0xB5,0x8A,0xB6,0x01,0xB6,0x79,
	0xB6,0xF0,0xB7,0x68,0xB7,0xE0,0xB8,0x59,0xB8,0xD1,0xB9,
	0x4A,0xB9,0xC2,0xBA,0x3B,0xBA,0xB5,0xBB,0x2E,0xBB,0xA7,
	0xBC,0x21,0xBC,0x9B,0xBD,0x15,0xBD,0x8F,0xBE,0x0A,0xBE,
	0x84,0xBE,0xFF,0xBF,0x7A,0xBF,0xF5,0xC0,0x70,0xC0,0xEC,
	0xC1,0x67,0xC1,0xE3,0xC2,0x5F,0xC2,0xDB,0xC3,0x58,0xC3,
	0xD4,0xC4,0x51,0xC4,0xCE,0xC5,0x4B,0xC5,0xC8,0xC6,0x46,
	0xC6,0xC3,0xC7,0x41,0xC7,0xBF,0xC8,0x3D,0xC8,0xBC,0xC9,
	0x3A,0xC9,0xB9,0xCA,0x38,0xCA,0xB7,0xCB,0x36,0xCB,0xB6,
	0xCC,0x35,0xCC,0xB5,0xCD,0x35,0xCD,0xB5,0xCE,0x36,0xCE,
	0xB6,0xCF,0x37,0xCF,0xB8,0xD0,0x39,0xD0,0xBA,0xD1,0x3C,
	0xD1,0xBE,0xD2,0x3F,0xD2,0xC1,0xD3,0x44,0xD3,0xC6,0xD4,
	0x49,0xD4,0xCB,0xD5,0x4E,0xD5,0xD1,0xD6,0x55,0xD6,0xD8,
	0xD7,0x5C,0xD7,0xE0,0xD8,0x64,0xD8,0xE8,0xD9,0x6C,0xD9,
	0xF1,0xDA,0x76,0xDA,0xFB,0xDB,0x80,0xDC,0x05,0xDC,0x8A,
	0xDD,0x10,0xDD,0x96,0xDE,0x1C,0xDE,0xA2,0xDF,0x29,0xDF,
	0xAF,0xE0,0x36,0xE0,0xBD,0xE1,0x44,0xE1,0xCC,0xE2,0x53,
	0xE2,0xDB,0xE3,0x63,0xE3,0xEB,0xE4,0x73,0xE4,0xFC,0xE5,
	0x84,0xE6,0x0D,0xE6,0x96,0xE7,0x1F,0xE7,0xA9,0xE8,0x32,
	0xE8,0xBC,0xE9,0x46,0xE9,0xD0,0xEA,0x5B,0xEA,0xE5,0xEB,
	0x70,0xEB,0xFB,0xEC,0x86,0xED,0x11,0xED,0x9C,0xEE,0x28,
	0xEE,0xB4,0xEF,0x40,0xEF,0xCC,0xF0,0x58,0xF0,0xE5,0xF1,
	0x72,0xF1,0xFF,0xF2,0x8C,0xF3,0x19,0xF3,0xA7,0xF4,0x34,
	0xF4,0xC2,0xF5,0x50,0xF5,0xDE,0xF6,0x6D,0xF6,0xFB,0xF7,
	0x8A,0xF8,0x19,0xF8,0xA8,0xF9,0x38,0xF9,0xC7,0xFA,0x57,
	0xFA,0xE7,0xFB,0x77,0xFC,0x07,0xFC,0x98,0xFD,0x29,0xFD,
	0xBA,0xFE,0x4B,0xFE,0xDC,0xFF,0x6D,0xFF,0xFF
};

// this struct is only used for status information during loading
static struct
{
	const unsigned char	*tmpBuf;
	int            tmpBuflength;
	int            tmpi;
	unsigned int   FRowBytes;
	unsigned char **FRowPtrs;
	unsigned char *Data;
	int            BitDepth;
	int            BytesPerPixel;
	int            ColorType;
	unsigned long  Height;
	unsigned long  Width;
	int            Interlace;
	int            Compression;
	int            Filter;
	FILE          *outfile;
} my_png;

static void PNGFlushData(png_structp png)
{
}

static void PNGWriteData(png_structp png, png_bytep data, png_size_t length)
{
	fwrite(data, length, 1, my_png.outfile);
}

static void PNGWriteError(png_structp png, png_const_charp message)
{
	Sys_Printf("WritePNG: error: %s\n", message);
}

static void PNGWriteWarning(png_structp png, png_const_charp message)
{
	Sys_Printf("WritePNG: warning: %s\n", message);
}

void WritePNG(const char *filename, byte *data, int width, int height, qboolean hasAlpha, qboolean sRGB) 
{
	png_structp png;
	png_infop pnginfo;
	unsigned char ioBuffer[8192];
	unsigned int linesize;
	int passes, i, j;
	FILE *file;

	png = png_create_write_struct( PNG_LIBPNG_VER_STRING, 0, PNGWriteError, PNGWriteWarning );
	if (!png)
	{
		Sys_Printf("WritePNG: png_create_write_struct failed\n");
		return;
	}
	pnginfo = png_create_info_struct(png);
	if(!pnginfo)
	{
		Sys_Printf("WritePNG: png_create_info_struct failed\n");
		png_destroy_write_struct(&png, NULL);
		return;
	}

	memset(&my_png, 0, sizeof(my_png)); // FIXME: not thread-safe
#ifdef __cplusplus
#ifdef WIN64
	if (setjmp((_JBTYPE *)png))
#elif defined(MACOSX) || defined(WIN32)
	if (setjmp((int *)png))
#elif defined(__ANDROID__)
	if (setjmp((long *)png))
#else
	if (setjmp((__jmp_buf_tag *)png))
#endif
#else
	if (setjmp(png))
#endif
	{
		Sys_Printf("WritePNG: setjmp failed\n");
		png_destroy_write_struct(&png, &pnginfo);
		return;
	}

	file = fopen(filename, "wb");
	if (!file)
	{
		Sys_Printf("WritePNG: failed to write %s\n", filename);
		return;
	}
	my_png.outfile = file;
	png_set_write_fn(png, ioBuffer, PNGWriteData, PNGFlushData);
	png_set_compression_level(png, Z_BEST_SPEED);
	png_set_IHDR(png, pnginfo, width, height, 8, hasAlpha ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB, PNG_INTERLACE_ADAM7, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	if (sRGB)
	{
		png_set_iCCP(png, pnginfo, "sRGB_IEC61966-2.1", PNG_COMPRESSION_TYPE_BASE, (char *)icc_profile_sRGB_IEC61966_2_1, sizeof(icc_profile_sRGB_IEC61966_2_1));
		png_set_sRGB(png, pnginfo, 3);
	}
	png_set_filter(png, 0, PNG_FILTER_SUB);
	png_write_info(png, pnginfo);
	png_set_packing(png);
	passes = png_set_interlace_handling(png);
	linesize = width * (hasAlpha ? 4 : 3);
	for(i = 0; i < passes; ++i)
		for(j = 0; j < height; ++j)
			png_write_row(png, &data[j * linesize]);
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &pnginfo);
	fclose(file);
}


/*
ImageInit()
implicitly called by every function to set up image list
*/

static void ImageInit( void )
{
	int		i;
	
	
	if( numImages <= 0 )
	{
		/* clear images (fixme: this could theoretically leak) */
		memset( images, 0, sizeof( images ) );
		
		/* generate *bogus image */
		images[ 0 ].name = (char *)safe_malloc( strlen( DEFAULT_IMAGE ) + 1 );
		strcpy( images[ 0 ].name, DEFAULT_IMAGE );
		images[ 0 ].filename = (char *)safe_malloc( strlen( DEFAULT_IMAGE ) + 1 );
		strcpy( images[ 0 ].filename, DEFAULT_IMAGE );
		images[ 0 ].width = 64;
		images[ 0 ].height = 64;
		images[ 0 ].refCount = 1;
		images[ 0 ].pixels = (byte *)safe_malloc( 64 * 64 * 4 );
		for( i = 0; i < (64 * 64 * 4); i++ )
			images[ 0 ].pixels[ i ] = 255;
	}
}



/*
ImageFree()
frees an rgba image
*/

void ImageFree( image_t *image )
{
	/* dummy check */
	if( image == NULL )
		return;
	
	/* decrement refcount */
	image->refCount--;
	
	/* free? */
	if( image->refCount <= 0 )
	{
		if( image->name != NULL )
			free( image->name );
		image->name = NULL;
		if( image->filename != NULL )
			free( image->filename );
		image->filename = NULL;
		free( image->pixels );
		image->width = 0;
		image->height = 0;
		numImages--;
	}
}



/*
ImageFind()
finds an existing rgba image and returns a pointer to the image_t struct or NULL if not found
*/

image_t *ImageFind( const char *filename )
{
	int			i;
	char		name[ MAX_OS_PATH ];
	
	
	/* init */
	ImageInit();
	
	/* dummy check */
	if( filename == NULL || filename[ 0 ] == '\0' )
		return NULL;
	
	/* strip file extension off name */
	strcpy( name, filename );
	StripExtension( name );
	
	/* search list */
	for( i = 0; i < MAX_IMAGES; i++ )
	{
		if( images[ i ].name != NULL && !strcmp( name, images[ i ].name ) )
			return &images[ i ];
	}
	
	/* no matching image found */
	return NULL;
}



/*
ImageLoad()
loads an rgba image and returns a pointer to the image_t struct or NULL if not found
*/

image_t *ImageLoad( const char *filename )
{
	int			i;
	image_t		*image;
	char		name[ MAX_OS_PATH ];
	int			size;
	byte		*buffer = NULL;

	
	/* init */
	ImageInit();
	
	/* dummy check */
	if( filename == NULL || filename[ 0 ] == '\0' )
		return NULL;
	
	/* strip file extension off name */
	strcpy( name, filename );
	StripExtension( name );
	
	/* try to find existing image */
	image = ImageFind( name );
	if( image != NULL )
	{
		image->refCount++;
		return image;
	}
	
	/* none found, so find first non-null image */
	image = NULL;
	for( i = 0; i < MAX_IMAGES; i++ )
	{
		if( images[ i ].name == NULL )
		{
			image = &images[ i ];
			break;
		}
	}
	
	/* too many images? */
	if( image == NULL )
		Error( "MAX_IMAGES (%d) exceeded, there are too many image files referenced by the map.", MAX_IMAGES );
	
	/* set it up */
	image->name = (char *)safe_malloc( strlen( name ) + 1 );
	strcpy( image->name, name );
	
	/* attempt to load tga */
	StripExtension( name );
	strcat( name, ".tga" );
	size = vfsLoadFile( (const char*) name, (void**) &buffer, 0 );
	if( size > 0 )
		LoadTGABuffer( buffer, buffer + size, &image->pixels, &image->width, &image->height );
	else
	{
		/* attempt to load png */
		StripExtension( name );
		strcat( name, ".png" );
		size = vfsLoadFile( (const char*) name, (void**) &buffer, 0 );
		if( size > 0 )
			LoadPNGBuffer( buffer, size, &image->pixels, &image->width, &image->height );
		else
		{
			/* attempt to load jpg */
			StripExtension( name );
			strcat( name, ".jpg" );
			size = vfsLoadFile( (const char*) name, (void**) &buffer, 0 );
			if( size > 0 )
			{
				if( LoadJPGBuff( buffer, size, &image->pixels, &image->width, &image->height ) == -1 && image->pixels != NULL )
					Sys_Printf( "WARNING: LoadJPGBuff: %s\n", (unsigned char*) image->pixels );
			}
			else
			{
				/* attempt to load dds */
				StripExtension( name );
				strcat( name, ".dds" );
				size = vfsLoadFile( (const char*) name, (void**) &buffer, 0 );
				if( size > 0 )
				{
					LoadDDSBuffer( buffer, size, &image->pixels, &image->width, &image->height );
					
					/* debug code */
					#if 1
					{
						ddsPF_t	pf;
						DDSGetInfo( (ddsBuffer_t*) buffer, NULL, NULL, &pf );
						Sys_Printf( "pf = %d\n", pf );
						if( image->width > 0 )
						{
							StripExtension( name );
							strcat( name, "_converted.tga" );
							WriteTGA( "C:\\games\\quake3\\baseq3\\textures\\rad\\dds_converted.tga", image->pixels, image->width, image->height );
						}
					}
					#endif
				}
			}
		}
	}
	
	/* free file buffer */
	free( buffer );
	
	/* make sure everything's kosher */
	if( size <= 0 || image->width <= 0 || image->height <= 0 || image->pixels == NULL )
	{
		//%	Sys_Printf( "size = %d  width = %d  height = %d  pixels = 0x%08x (%s)\n",
		//%		size, image->width, image->height, image->pixels, name );
		free( image->name );
		image->name = NULL;
		return NULL;
	}
	
	/* set filename */
	image->filename = (char *)safe_malloc( strlen( name ) + 1 );
	strcpy( image->filename, name );
	
	/* set count */
	image->refCount = 1;
	numImages++;
	
	/* return the image */
	return image;
}


