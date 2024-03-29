/*****************************************************************************
 * mxpeg_helper.h: MXPEG helper functions
 *****************************************************************************
 * Copyright (C) 2012 the VideoLAN team
 * $Id$
 *
 * Authors: Sébastien Escudier
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/**
 * Finds FF XX in the first size byte of data
 */
static uint8_t find_jpeg_marker(int *position, const uint8_t *data, int size)
{
    for (int i = *position; i + 1 < size; i++) {
        if (data[i] != 0xff)
            continue;
        if (data[i + 1] != 0xff) {
            *position = i + 2;
            return data[i + 1];
        }
    }
    return 0xff;
}

/*
 * Mxpeg frame format : http://developer.mobotix.com/docs/mxpeg_frame.html
 * */
static bool IsMxpeg(stream_t *s)
{
    const uint8_t *header;
    int size = stream_Peek(s, &header, 256);
    int position = 0;

    if (find_jpeg_marker(&position, header, size) != 0xd8)
        return false;
    if (find_jpeg_marker(&position, header, position + 2) != 0xe0)
        return false;

    if (position + 2 > size)
        return false;

    /* Skip this jpeg header */
    uint32_t header_size = GetWBE(&header[position]);
    position += header_size;
    if (position + 4 > size)
    {
        size = position + 4;
        if( stream_Peek (s, &header, size) < size )
            return false;
    }

    /* Skip the comment header */
    if ( !(header[position] == 0xFF && header[position+1] == 0xFE) )
        return false;

    position += 2;
    header_size = GetWBE (&header[position]);

    /* Find the MXF header */
    size = position + header_size + 8; //8 = FF FE 00 00 M X F 00
    if (stream_Peek(s, &header, position + header_size + 8 ) < size)
        return false;

    position += header_size;
    if ( !(header[position] == 0xFF && header[position+1] == 0xFE) )
        return false;

    position += 4;

    if (memcmp (&header[position], "MXF\0", 4) )
        return false;

    return true;
}
