/*
 *  Copyright (C) 2002 John Zitterkopf (zitt@bigfoot.com)
 *                     (http://www.zittware.com)
 *
 *  These comments must remain intact in all copies of the source code.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once

#define MP3GAIN_VERSION "1.5.2"

#define M3G_ERR_CANT_MODIFY_FILE -1
#define M3G_ERR_CANT_MAKE_TMP -2
#define M3G_ERR_NOT_ENOUGH_TMP_SPACE -3
#define M3G_ERR_RENAME_TMP -4
#define M3G_ERR_FILEOPEN   -5
#define M3G_ERR_READ       -6
#define M3G_ERR_WRITE      -7
#define M3G_ERR_TAGFORMAT  -8

void passError(int numStrings, ...);

typedef enum
{
    storeTime,
    setStoredTime
} timeAction;

/* Get/Set file datetime stamp */
void fileTime(const char *filename, timeAction action);
