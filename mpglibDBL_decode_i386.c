/*
 * Mpeg Layer-1,2,3 audio decoder
 * ------------------------------
 * copyright (c) 1995,1996,1997 by Michael Hipp, All rights reserved.
 * See also 'README'
 *
 * slighlty optimized for machines without autoincrement/decrement.
 * The performance is highly compiler dependend. Maybe
 * the decode.c version for 'normal' processor may be faster
 * even for Intel processors.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mpglibDBL_decode_i386.h"
#include "mpglibDBL_dct64_i386.h"
#include "mpglibDBL_tabinit.h"
#include "mpglibDBL_interface.h"

Float_t *lSamp;
Float_t *rSamp;
Float_t *maxSamp;
bool maxAmpOnly;

int procSamp;

int synth_1to1_mono(PMPSTR mp, double *bandPtr, int *pnt)
{
    int ret;
    int pnt1 = 0;

    ret = synth_1to1(mp, bandPtr, 0, &pnt1);
    *pnt += 64;

    return ret;
}

int synth_1to1(PMPSTR mp, double *bandPtr, int channel, int *pnt)
{
    /*  static const int step = 2; */
    int bo;
    Float_t *dsamp;
    double mSamp = 0;

    double *b0, (*buf)[0x110];
    int clip = 0;
    int bo1;

    bo = mp->synth_bo;

    if (!channel)
    {
        dsamp = lSamp;
        bo--;
        bo &= 0xf;
        buf = mp->synth_buffs[0];
    }
    else
    {
        dsamp = rSamp;
        buf = mp->synth_buffs[1];
    }

    if (bo & 0x1)
    {
        b0 = buf[0];
        bo1 = bo;
        dct64(buf[1] + ((bo + 1) & 0xf), buf[0] + bo, bandPtr);
    }
    else
    {
        b0 = buf[1];
        bo1 = bo + 1;
        dct64(buf[0] + bo, buf[1] + bo + 1, bandPtr);
    }

    mp->synth_bo = bo;
    if (maxAmpOnly)
    {
        {
            register int j;
            double *window = decwin + 16 - bo1;

            for (j = 16; j; j--, b0 += 0x10, window += 0x20)
            {
                double sum;
                sum  = window[0x0] * b0[0x0];
                sum -= window[0x1] * b0[0x1];
                sum += window[0x2] * b0[0x2];
                sum -= window[0x3] * b0[0x3];
                sum += window[0x4] * b0[0x4];
                sum -= window[0x5] * b0[0x5];
                sum += window[0x6] * b0[0x6];
                sum -= window[0x7] * b0[0x7];
                sum += window[0x8] * b0[0x8];
                sum -= window[0x9] * b0[0x9];
                sum += window[0xA] * b0[0xA];
                sum -= window[0xB] * b0[0xB];
                sum += window[0xC] * b0[0xC];
                sum -= window[0xD] * b0[0xD];
                sum += window[0xE] * b0[0xE];
                sum -= window[0xF] * b0[0xF];

                if (sum > mSamp)
                {
                    mSamp = sum;
                }
                else if ((-sum) > mSamp)
                {
                    mSamp = (-sum);
                }
            }

            {
                double sum;
                sum  = window[0x0] * b0[0x0];
                sum += window[0x2] * b0[0x2];
                sum += window[0x4] * b0[0x4];
                sum += window[0x6] * b0[0x6];
                sum += window[0x8] * b0[0x8];
                sum += window[0xA] * b0[0xA];
                sum += window[0xC] * b0[0xC];
                sum += window[0xE] * b0[0xE];

                if (sum > mSamp)
                {
                    mSamp = sum;
                }
                else if ((-sum) > mSamp)
                {
                    mSamp = (-sum);
                }
                b0 -= 0x10, window -= 0x20;
            }
            window += bo1 << 1;

            for (j = 15; j; j--, b0 -= 0x10, window -= 0x20)
            {
                double sum;
                sum = -window[-0x1] * b0[0x0];
                sum -= window[-0x2] * b0[0x1];
                sum -= window[-0x3] * b0[0x2];
                sum -= window[-0x4] * b0[0x3];
                sum -= window[-0x5] * b0[0x4];
                sum -= window[-0x6] * b0[0x5];
                sum -= window[-0x7] * b0[0x6];
                sum -= window[-0x8] * b0[0x7];
                sum -= window[-0x9] * b0[0x8];
                sum -= window[-0xA] * b0[0x9];
                sum -= window[-0xB] * b0[0xA];
                sum -= window[-0xC] * b0[0xB];
                sum -= window[-0xD] * b0[0xC];
                sum -= window[-0xE] * b0[0xD];
                sum -= window[-0xF] * b0[0xE];
                sum -= window[-0x0] * b0[0xF];

                if (sum > mSamp)
                {
                    mSamp = sum;
                }
                else if ((-sum) > mSamp)
                {
                    mSamp = (-sum);
                }
            }
        }
    }
    else
    {

        {
            register int j;
            double *window = decwin + 16 - bo1;

            for (j = 16; j; j--, b0 += 0x10, window += 0x20)
            {
                double sum;
                sum  = window[0x0] * b0[0x0];
                sum -= window[0x1] * b0[0x1];
                sum += window[0x2] * b0[0x2];
                sum -= window[0x3] * b0[0x3];
                sum += window[0x4] * b0[0x4];
                sum -= window[0x5] * b0[0x5];
                sum += window[0x6] * b0[0x6];
                sum -= window[0x7] * b0[0x7];
                sum += window[0x8] * b0[0x8];
                sum -= window[0x9] * b0[0x9];
                sum += window[0xA] * b0[0xA];
                sum -= window[0xB] * b0[0xB];
                sum += window[0xC] * b0[0xC];
                sum -= window[0xD] * b0[0xD];
                sum += window[0xE] * b0[0xE];
                sum -= window[0xF] * b0[0xF];

                *dsamp++ = (Float_t)sum;
                procSamp++;
                if (sum > mSamp)
                {
                    mSamp = sum;
                }
                else if ((-sum) > mSamp)
                {
                    mSamp = (-sum);
                }
            }

            {
                double sum;
                sum  = window[0x0] * b0[0x0];
                sum += window[0x2] * b0[0x2];
                sum += window[0x4] * b0[0x4];
                sum += window[0x6] * b0[0x6];
                sum += window[0x8] * b0[0x8];
                sum += window[0xA] * b0[0xA];
                sum += window[0xC] * b0[0xC];
                sum += window[0xE] * b0[0xE];
                *dsamp++ = (Float_t)sum;
                procSamp++;
                if (sum > mSamp)
                {
                    mSamp = sum;
                }
                else if ((-sum) > mSamp)
                {
                    mSamp = (-sum);
                }
                b0 -= 0x10, window -= 0x20;
            }
            window += bo1 << 1;

            for (j = 15; j; j--, b0 -= 0x10, window -= 0x20)
            {
                double sum;
                sum = -window[-0x1] * b0[0x0];
                sum -= window[-0x2] * b0[0x1];
                sum -= window[-0x3] * b0[0x2];
                sum -= window[-0x4] * b0[0x3];
                sum -= window[-0x5] * b0[0x4];
                sum -= window[-0x6] * b0[0x5];
                sum -= window[-0x7] * b0[0x6];
                sum -= window[-0x8] * b0[0x7];
                sum -= window[-0x9] * b0[0x8];
                sum -= window[-0xA] * b0[0x9];
                sum -= window[-0xB] * b0[0xA];
                sum -= window[-0xC] * b0[0xB];
                sum -= window[-0xD] * b0[0xC];
                sum -= window[-0xE] * b0[0xD];
                sum -= window[-0xF] * b0[0xE];
                sum -= window[-0x0] * b0[0xF];

                *dsamp++ = (Float_t)sum;
                procSamp++;
                if (sum > mSamp)
                {
                    mSamp = sum;
                }
                else if ((-sum) > mSamp)
                {
                    mSamp = (-sum);
                }
            }
        }
    }
    *pnt += 128;

    if ((Float_t)mSamp > *maxSamp)
    {
        *maxSamp = mSamp;
    }

    if (!channel)
    {
        lSamp = dsamp;
    }
    else
    {
        rSamp = dsamp;
    }

    return clip;
}
