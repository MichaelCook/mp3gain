/*
 *      Machine dependent defines/includes for LAME.
 *
 *      Copyright (c) 1999 A.L. Faber
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

/*
 * 3 different types of pow() functions:
 *   - table lookup
 *   - pow()
 *   - exp()   on some machines this is claimed to be faster than pow()
 */

#define POW20(x)  pow20[x]
//#define POW20(x)  pow(2.0,((double)(x)-210)*.25)
//#define POW20(x)  exp( ((double)(x)-210)*(.25*LOG2) )

#define IPOW20(x)  ipow20[x]
//#define IPOW20(x)  exp( -((double)(x)-210)*.1875*LOG2 )
//#define IPOW20(x)  pow(2.0,-((double)(x)-210)*.1875)

/*
 * FLOAT    for variables which require at least 32 bits
 * FLOAT8   for variables which require at least 64 bits
 *
 * On some machines, 64 bit will be faster than 32 bit.  Also, some math
 * routines require 64 bit float, so setting FLOAT=float will result in a
 * lot of conversions.
 */

/* sample_t must be floating point, at least 32 bits */
typedef float     sample_t;
typedef sample_t  stereo_t [2];
