/*
** Copyright (C) 2000 Albert L. Faber
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#pragma once

#include <stdbool.h>
#include "mpglibDBL_common.h"
#include "gain_analysis.h"

extern Float_t *lSamp;
extern Float_t *rSamp;
extern Float_t *maxSamp;
extern unsigned char *maxGain;
extern unsigned char *minGain;
extern bool maxAmpOnly;
extern int procSamp;

bool InitMP3(PMPSTR mp);
int decodeMP3(PMPSTR mp, const unsigned char *inmemory, int inmemsize, int *done);
void ExitMP3(PMPSTR mp);
void remove_buf(PMPSTR mp);
