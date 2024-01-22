/*
 *  mp3gain.c - analyzes mp3 files, determines the perceived volume,
 *      and adjusts the volume of the mp3 accordingly
 *
 *  Copyright (C) 2001-2009 Glen Sawyer
 *  AAC support (C) 2004-2009 David Lasker, Altos Design, Inc.
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
 *
 *  coding by Glen Sawyer (mp3gain@hotmail.com) 735 W 255 N, Orem, UT 84057-4505 USA
 *    -- go ahead and laugh at me for my lousy coding skillz, I can take it :)
 *       Just do me a favor and let me know how you improve the code.
 *       Thanks.
 *
 *  Unix-ification by Stefan Partheymüller
 *  (other people have made Unix-compatible alterations-- I just ended up using
 *   Stefan's because it involved the least re-work)
 *
 *  DLL-ification by John Zitterkopf (zitt@hotmail.com)
 *
 *  Additional tweaks by Artur Polaczynski, Mark Armbrust, and others
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "apetag.h"
#include "id3tag.h"
#include <sys/stat.h>
#include <utime.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include "mpglibDBL_interface.h"
#include "gain_analysis.h"
#include "mp3gain.h"
#include "rg_error.h"

#define HEADERSIZE 4

#define CRC16_POLYNOMIAL 0x8005

#define BUFFERSIZE 3000000
#define WRITEBUFFERSIZE 100000

#define FULL_RECALC 1
#define AMP_RECALC 2
#define MIN_MAX_GAIN_RECALC 4

typedef struct
{
    unsigned long fileposition;
    unsigned char val[2];
} wbuffer;

static wbuffer writebuffer[WRITEBUFFERSIZE];
static unsigned long writebuffercnt;
static unsigned char buffer[BUFFERSIZE];
static bool gQuiet = false;
static bool gUsingTemp = false;
static bool gNowWriting = false;
static double lastfreq = -1.0;
static int whichChannel = 0;
static bool gBadLayer = false;
static int LayerSet = 0;
static int Reckless = 0;
static int wrapGain = 0;
static int undoChanges = 0;
static bool gSkipTag = false;
static bool gDeleteTag = false;
static bool gForceRecalculateTag = false;
static bool gForceUpdateTag = false;
static bool gCheckTagOnly = false;
static bool gUseId3 = false;
static bool gSuccess;
static long inbuffer;
static unsigned long bitidx;
static unsigned char *wrdpntr;
static unsigned char *curframe;
static const char *curfilename;
static FILE *inf;
static FILE *outf;
static bool gSaveTime;
static unsigned long filepos;
static const char *gProgramName;

static const double bitrate[4][16] =
{
    { 1,  8, 16, 24, 32, 40, 48, 56,  64,  80,  96, 112, 128, 144, 160, 1 },
    { 1,  1,  1,  1,  1,  1,  1,  1,   1,   1,   1,   1,   1,   1,   1, 1 },
    { 1,  8, 16, 24, 32, 40, 48, 56,  64,  80,  96, 112, 128, 144, 160, 1 },
    { 1, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 1 }
};

static const double frequency[4][4] =
{
    { 11.025, 12,  8,  1 },
    {      1,  1,  1,  1 },
    {  22.05, 24, 16,  1 },
    {   44.1, 48, 32,  1 }
};

static long arrbytesinframe[16];

/* instead of writing each byte change, I buffer them up */
static void flushWriteBuff()
{
    unsigned long i;
    for (i = 0; i < writebuffercnt; i++)
    {
        fseek(inf, writebuffer[i].fileposition, SEEK_SET);
        fwrite(writebuffer[i].val, 1, 2, inf);
    }
    writebuffercnt = 0;
}

static void addWriteBuff(unsigned long pos, unsigned char *vals)
{
    if (writebuffercnt >= WRITEBUFFERSIZE)
    {
        flushWriteBuff();
        fseek(inf, filepos, SEEK_SET);
    }
    writebuffer[writebuffercnt].fileposition = pos;
    writebuffer[writebuffercnt].val[0] = *vals;
    writebuffer[writebuffercnt].val[1] = vals[1];
    writebuffercnt++;
}

/* Fill the mp3 buffer.  Return the number of bytes that were added */
static unsigned long fillBuffer(long savelastbytes)
{
    unsigned long i;
    unsigned long skip;
    unsigned long skipbuf;

    skip = 0;
    if (savelastbytes < 0)
    {
        skip = -savelastbytes;
        savelastbytes = 0;
    }

    if (gUsingTemp && gNowWriting)
    {
        if (fwrite(buffer, 1, inbuffer - savelastbytes, outf) != (size_t)(inbuffer - savelastbytes))
        {
            return 0;
        }
    }

    if (savelastbytes != 0) /* save some of the bytes at the end of the buffer */
    {
        memmove(buffer, (buffer + inbuffer - savelastbytes), savelastbytes);
    }

    while (skip > 0)   /* skip some bytes from the input file */
    {
        skipbuf = skip > BUFFERSIZE ? BUFFERSIZE : skip;

        i = fread(buffer, 1, skipbuf, inf);
        if (i != skipbuf)
        {
            return 0;
        }

        if (gUsingTemp && gNowWriting)
        {
            if (fwrite(buffer, 1, skipbuf, outf) != skipbuf)
            {
                return 0;
            }
        }
        filepos += i;
        skip -= skipbuf;
    }
    i = fread(buffer + savelastbytes, 1, BUFFERSIZE - savelastbytes, inf);

    filepos = filepos + i;
    inbuffer = i + savelastbytes;
    return i;
}

static const unsigned char maskLeft8bits[8] =
{
    0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE
};

static const unsigned char maskRight8bits[8] =
{
    0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01
};

static void set8Bits(unsigned short val)
{
    val <<= (8 - bitidx);
    wrdpntr[0] &= maskLeft8bits[bitidx];
    wrdpntr[0] |= (val  >> 8);
    wrdpntr[1] &= maskRight8bits[bitidx];
    wrdpntr[1] |= (val  & 0xFF);

    if (!gUsingTemp)
    {
        addWriteBuff(filepos - (inbuffer - (wrdpntr - buffer)), wrdpntr);
    }
}

static void skipBits(int nbits)
{
    bitidx += nbits;
    wrdpntr += (bitidx >> 3);
    bitidx &= 7;
}

static unsigned char peek8Bits()
{
    unsigned short rval;

    rval = wrdpntr[0];
    rval <<= 8;
    rval |= wrdpntr[1];
    rval >>= (8 - bitidx);

    return (rval & 0xFF);
}

static bool skipID3v2()
{
    /*
     *  An ID3v2 tag can be detected with the following pattern:
     *    $49 44 33 yy yy xx zz zz zz zz
     *  Where yy is less than $FF, xx is the 'flags' byte and zz is less than
     *  $80.
     */
    unsigned long ID3Size;

    bool ok = true;

    if (wrdpntr[0] == 'I' && wrdpntr[1] == 'D' && wrdpntr[2] == '3'
        && wrdpntr[3] < 0xFF && wrdpntr[4] < 0xFF)
    {

        ID3Size = (long)(wrdpntr[9]) | ((long)(wrdpntr[8]) << 7) |
                  ((long)(wrdpntr[7]) << 14) | ((long)(wrdpntr[6]) << 21);

        ID3Size += 10;

        wrdpntr = wrdpntr + ID3Size;

        if ((wrdpntr + HEADERSIZE - buffer) > inbuffer)
        {
            ok = fillBuffer(inbuffer - (wrdpntr - buffer)) != 0;
            wrdpntr = buffer;
        }
    }

    return ok;
}

void passError(MMRESULT lerrnum, int numStrings, ...)
{
    va_list marker;
    va_start(marker, numStrings);
    size_t totalStrLen = 0;
    for (int i = 0; i < numStrings; i++)
    {
        totalStrLen += strlen(va_arg(marker, const char *));
    }
    va_end(marker);

    char *errstr = malloc(totalStrLen + 3);
    errstr[0] = '\0';

    va_start(marker, numStrings);
    for (int i = 0; i < numStrings; i++)
    {
        strcat(errstr, va_arg(marker, const char *));
    }
    va_end(marker);

    fprintf(stderr, "%s: %s", gProgramName, errstr);
    gSuccess = false;
    free(errstr);
}

static bool frameSearch(int startup)
{
    static int startfreq;
    static int startmpegver;
    long tempmpegver;
    double bitbase;
    int i;

    bool done = false;
    bool ok = true;

    if ((wrdpntr + HEADERSIZE - buffer) > inbuffer)
    {
        ok = fillBuffer(inbuffer - (wrdpntr - buffer)) != 0;
        wrdpntr = buffer;
        if (!ok)
        {
            done = true;
        }
    }

    while (!done)
    {

        done = true;

        if ((wrdpntr[0] & 0xFF) != 0xFF)
        {
            done = false;    /* first 8 bits must be '1' */
        }
        else if ((wrdpntr[1] & 0xE0) != 0xE0)
        {
            done = false;    /* next 3 bits are also '1' */
        }
        else if ((wrdpntr[1] & 0x18) == 0x08)
        {
            done = false;    /* invalid MPEG version */
        }
        else if ((wrdpntr[2] & 0xF0) == 0xF0)
        {
            done = false;    /* bad bitrate */
        }
        else if ((wrdpntr[2] & 0xF0) == 0x00)
        {
            done = false;    /* we'll just completely ignore "free format" bitrates */
        }
        else if ((wrdpntr[2] & 0x0C) == 0x0C)
        {
            done = false;    /* bad sample frequency */
        }
        else if ((wrdpntr[1] & 0x06) != 0x02)   /* not Layer III */
        {
            if (!LayerSet)
            {
                switch (wrdpntr[1] & 0x06)
                {
                case 0x06:
                    gBadLayer = true;
                    passError(MP3GAIN_FILEFORMAT_NOTSUPPORTED, 2,
                              curfilename, " is an MPEG Layer I file, not a layer III file\n");
                    return 0;
                case 0x04:
                    gBadLayer = true;
                    passError(MP3GAIN_FILEFORMAT_NOTSUPPORTED, 2,
                              curfilename, " is an MPEG Layer II file, not a layer III file\n");
                    return 0;
                }
            }
            done = false; /* probably just corrupt data, keep trying */
        }
        else if (startup)
        {
            startmpegver = wrdpntr[1] & 0x18;
            startfreq = wrdpntr[2] & 0x0C;
            tempmpegver = startmpegver >> 3;
            if (tempmpegver == 3)
            {
                bitbase = 1152.0;
            }
            else
            {
                bitbase = 576.0;
            }

            for (i = 0; i < 16; i++)
            {
                arrbytesinframe[i] = (long)(floor(floor((bitbase * bitrate[tempmpegver][i]) / frequency[tempmpegver][startfreq >> 2]) /
                                                  8.0));
            }

        }
        else
        {
            /* !startup -- if MPEG version or frequency is different,
               then probably not correctly synched yet */
            if ((wrdpntr[1] & 0x18) != startmpegver)
            {
                done = false;
            }
            else if ((wrdpntr[2] & 0x0C) != startfreq)
            {
                done = false;
            }
            else if ((wrdpntr[2] & 0xF0) == 0) /* bitrate is "free format" probably just
                                                  corrupt data if we've already found
                                                  valid frames */
            {
                done = false;
            }
        }

        if (!done)
        {
            wrdpntr++;
        }

        if ((wrdpntr + HEADERSIZE - buffer) > inbuffer)
        {
            ok = fillBuffer(inbuffer - (wrdpntr - buffer)) != 0;
            wrdpntr = buffer;
            if (!ok)
            {
                done = true;
            }
        }
    }

    if (ok)
    {
        if (inbuffer - (wrdpntr - buffer) < (arrbytesinframe[(wrdpntr[2] >> 4) & 0x0F] + ((wrdpntr[2] >> 1) & 0x01)))
        {
            ok = fillBuffer(inbuffer - (wrdpntr - buffer)) != 0;
            wrdpntr = buffer;
        }
        bitidx = 0;
        curframe = wrdpntr;
    }
    return ok;
}

static int crcUpdate(int value, int crc)
{
    value <<= 8;
    for (int i = 0; i < 8; i++)
    {
        value <<= 1;
        crc <<= 1;

        if (((crc ^ value) & 0x10000))
        {
            crc ^= CRC16_POLYNOMIAL;
        }
    }
    return crc;
}

static void crcWriteHeader(int headerlength, char *header)
{
    int crc = 0xffff; /* (jo) init crc16 for error_protection */
    crc = crcUpdate(((unsigned char *)header)[2], crc);
    crc = crcUpdate(((unsigned char *)header)[3], crc);
    for (int i = 6; i < headerlength; i++)
    {
        crc = crcUpdate(((unsigned char *)header)[i], crc);
    }

    header[4] = crc >> 8;
    header[5] = crc & 255;
}

static long getSizeOfFile(const char *filename)
{
    long size = 0;
    FILE *file;

    file = fopen(filename, "rb");
    if (file)
    {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        fclose(file);
    }

    return size;
}

static int deleteFile(const char *filename)
{
    return remove(filename);
}

static int moveFile(const char *currentfilename, const char *newfilename)
{
    return rename(currentfilename, newfilename);
}

/* Get File size and datetime stamp */

void fileTime(const char *filename, timeAction action)
{
    static int timeSaved = 0;
    static struct stat savedAttributes;

    if (action == storeTime)
    {
        timeSaved = (stat(filename, &savedAttributes) == 0);
    }
    else
    {
        if (timeSaved)
        {
            struct utimbuf setTime;

            setTime.actime = savedAttributes.st_atime;
            setTime.modtime = savedAttributes.st_mtime;
            timeSaved = 0;

            utime(filename, &setTime);
        }
    }
}

static unsigned long reportPercentWritten(unsigned long percent,
        unsigned long bytes)
{
    fprintf(stderr, "                                                \r"
            " %2lu%% of %lu bytes written\r",
            percent, bytes);
    return 1;
}

static int numFiles, totFiles;

static unsigned long reportPercentAnalyzed(unsigned long percent,
        unsigned long bytes)
{
    char fileDivFiles[21];
    fileDivFiles[0] = '\0';

    if (totFiles - 1)   /* if 1 file then don't show [x/n] */
    {
        sprintf(fileDivFiles, "[%d/%d]", numFiles, totFiles);
    }

    fprintf(stderr, "                                           \r"
            "%s %2lu%% of %lu bytes analyzed\r",
            fileDivFiles, percent, bytes);
    return 1;
}

static void scanFrameGain()
{
    int gr, ch;
    int gain;

    int mpegver = (curframe[1] >> 3) & 0x03;
    int crcflag = curframe[1] & 0x01;
    int mode = (curframe[3] >> 6) & 0x03;
    int nchan = (mode == 3) ? 1 : 2;

    if (!crcflag)
    {
        wrdpntr = curframe + 6;
    }
    else
    {
        wrdpntr = curframe + 4;
    }

    bitidx = 0;

    if (mpegver == 3)   /* 9 bit main_data_begin */
    {
        wrdpntr++;
        bitidx = 1;

        if (mode == 3)
        {
            skipBits(5);    /* private bits */
        }
        else
        {
            skipBits(3);    /* private bits */
        }

        skipBits(nchan * 4);  /* scfsi[ch][band] */
        for (gr = 0; gr < 2; gr++)
        {
            for (ch = 0; ch < nchan; ch++)
            {
                skipBits(21);
                gain = peek8Bits();
                if (*minGain > gain)
                {
                    *minGain = gain;
                }
                if (*maxGain < gain)
                {
                    *maxGain = gain;
                }
                skipBits(38);
            }
        }
    }
    else     /* mpegver != 3 */
    {
        wrdpntr++; /* 8 bit main_data_begin */

        if (mode == 3)
        {
            skipBits(1);
        }
        else
        {
            skipBits(2);
        }

        /* only one granule, so no loop */
        for (ch = 0; ch < nchan; ch++)
        {
            skipBits(21);
            gain = peek8Bits();
            if (*minGain > gain)
            {
                *minGain = gain;
            }
            if (*maxGain < gain)
            {
                *maxGain = gain;
            }
            skipBits(42);
        }
    }
}

static int changeGain(const char *filename,
                      int leftgainchange,
                      int rightgainchange)
{
    int mode;
    int crcflag;
    unsigned char *Xingcheck;
    int nchan;
    int ch;
    int gr;
    unsigned char gain;
    int bitridx;
    long bytesinframe;
    int sideinfo_len;
    int mpegver;
    long gFilesize = 0;
    int gainchange[2];
    int singlechannel;
    long outlength, inlength; /* size checker when using Temp files */

    char *outfilename = NULL;
    unsigned long frame = 0;
    gBadLayer = false;
    LayerSet = Reckless;

    gNowWriting = true;

    if (leftgainchange == 0 && rightgainchange == 0)
    {
        return 0;
    }

    gainchange[0] = leftgainchange;
    gainchange[1] = rightgainchange;
    singlechannel = leftgainchange != rightgainchange;

    if (gSaveTime)
    {
        fileTime(filename, storeTime);
    }

    gFilesize = getSizeOfFile(filename);

    if (gUsingTemp)
    {
        fflush(stdout);
        outlength = (long)strlen(filename);
        outfilename = malloc(outlength + 5);
        strcpy(outfilename, filename);
        if ((filename[outlength - 3] == 'T' || filename[outlength - 3] == 't') &&
            (filename[outlength - 2] == 'M' || filename[outlength - 2] == 'm') &&
            (filename[outlength - 1] == 'P' || filename[outlength - 1] == 'p'))
        {
            strcat(outfilename, ".TMP");
        }
        else
        {
            outfilename[outlength - 3] = 'T';
            outfilename[outlength - 2] = 'M';
            outfilename[outlength - 1] = 'P';
        }

        inf = fopen(filename, "r+b");

        if (inf != NULL)
        {
            outf = fopen(outfilename, "wb");

            if (outf == NULL)
            {
                fclose(inf);
                inf = NULL;
                passError(MP3GAIN_UNSPECIFED_ERROR, 3,
                          "\nCan't open ", outfilename, " for temp writing\n");
                gNowWriting = false;
                free(outfilename);
                return M3G_ERR_CANT_MAKE_TMP;
            }

        }
    }
    else
    {
        inf = fopen(filename, "r+b");
    }

    if (inf == NULL)
    {
        if (gUsingTemp && (outf != NULL))
        {
            fclose(outf);
            outf = NULL;
        }
        passError(MP3GAIN_UNSPECIFED_ERROR, 3,
                  "\nCan't open ", filename, " for modifying\n");
        gNowWriting = false;
        free(outfilename);
        return M3G_ERR_CANT_MODIFY_FILE;
    }
    else
    {
        writebuffercnt = 0;
        inbuffer = 0;
        filepos = 0;
        bitidx = 0;
        bool ok = fillBuffer(0);
        if (ok)
        {

            wrdpntr = buffer;

            ok = skipID3v2();
            // TODO: why are we ignoring `ok` here?

            ok = frameSearch(true);
            if (!ok)
            {
                if (!gBadLayer)
                    passError(MP3GAIN_UNSPECIFED_ERROR, 3,
                              "Can't find any valid MP3 frames in file ", filename, "\n");
            }
            else
            {
                LayerSet = 1; /* We've found at least one valid layer 3 frame.
                                 Assume any later layer 1 or 2 frames are just
                                 bitstream corruption */
                mode = (curframe[3] >> 6) & 3;

                if ((curframe[1] & 0x08) == 0x08) /* MPEG 1 */
                {
                    sideinfo_len = (mode == 3) ? 4 + 17 : 4 + 32;
                }
                else                /* MPEG 2 */
                {
                    sideinfo_len = (mode == 3) ? 4 + 9 : 4 + 17;
                }

                if (!(curframe[1] & 0x01))
                {
                    sideinfo_len += 2;
                }

                Xingcheck = curframe + sideinfo_len;

                //LAME CBR files have "Info" tags, not "Xing" tags
                if ((Xingcheck[0] == 'X' && Xingcheck[1] == 'i' && Xingcheck[2] == 'n' && Xingcheck[3] == 'g') ||
                    (Xingcheck[0] == 'I' && Xingcheck[1] == 'n' && Xingcheck[2] == 'f' && Xingcheck[3] == 'o'))
                {
                    bitridx = (curframe[2] >> 4) & 0x0F;
                    if (bitridx == 0)
                    {
                        passError(MP3GAIN_FILEFORMAT_NOTSUPPORTED, 2,
                                  filename, " is free format (not currently supported)\n");
                        ok = false;
                    }
                    else
                    {
                        mpegver = (curframe[1] >> 3) & 0x03;

                        bytesinframe = arrbytesinframe[bitridx] + ((curframe[2] >> 1) & 0x01);

                        wrdpntr = curframe + bytesinframe;

                        ok = frameSearch(0);
                    }
                }

                frame = 1;
            } /* if (!ok) else */

            while (ok)
            {
                bitridx = (curframe[2] >> 4) & 0x0F;
                if (singlechannel)
                {
                    if ((curframe[3] >> 6) & 0x01)   /* if mode is NOT stereo
                                                        or dual channel */
                    {
                        passError(MP3GAIN_FILEFORMAT_NOTSUPPORTED, 2,
                                  filename, ": Can't adjust single channel for mono or joint stereo\n");
                        ok = false;
                    }
                }
                if (bitridx == 0)
                {
                    passError(MP3GAIN_FILEFORMAT_NOTSUPPORTED, 2,
                              filename, " is free format (not currently supported)\n");
                    ok = false;
                }
                if (ok)
                {
                    mpegver = (curframe[1] >> 3) & 0x03;
                    crcflag = curframe[1] & 0x01;

                    bytesinframe = arrbytesinframe[bitridx] + ((curframe[2] >> 1) & 0x01);
                    mode = (curframe[3] >> 6) & 0x03;
                    nchan = (mode == 3) ? 1 : 2;

                    if (!crcflag) /* we DO have a crc field */
                    {
                        wrdpntr = curframe + 6;    /* 4-byte header, 2-byte CRC */
                    }
                    else
                    {
                        wrdpntr = curframe + 4;    /* 4-byte header */
                    }

                    bitidx = 0;

                    if (mpegver == 3)   /* 9 bit main_data_begin */
                    {
                        wrdpntr++;
                        bitidx = 1;

                        if (mode == 3)
                        {
                            skipBits(5);    /* private bits */
                        }
                        else
                        {
                            skipBits(3);    /* private bits */
                        }

                        skipBits(nchan * 4); /* scfsi[ch][band] */
                        for (gr = 0; gr < 2; gr++)
                            for (ch = 0; ch < nchan; ch++)
                            {
                                skipBits(21);
                                gain = peek8Bits();
                                if (wrapGain)
                                {
                                    gain += (unsigned char)(gainchange[ch]);
                                }
                                else
                                {
                                    if (gain != 0)
                                    {
                                        if ((int)(gain) + gainchange[ch] > 255)
                                        {
                                            gain = 255;
                                        }
                                        else if ((int)gain + gainchange[ch] < 0)
                                        {
                                            gain = 0;
                                        }
                                        else
                                        {
                                            gain += (unsigned char)(gainchange[ch]);
                                        }
                                    }
                                }
                                set8Bits(gain);
                                skipBits(38);
                            }
                        if (!crcflag)
                        {
                            if (nchan == 1)
                            {
                                crcWriteHeader(23, (char *)curframe);
                            }
                            else
                            {
                                crcWriteHeader(38, (char *)curframe);
                            }
                            /* WRITETOFILE */
                            if (!gUsingTemp)
                            {
                                addWriteBuff(filepos - (inbuffer - (curframe + 4 - buffer)), curframe + 4);
                            }
                        }
                    }
                    else   /* mpegver != 3 */
                    {
                        wrdpntr++; /* 8 bit main_data_begin */

                        if (mode == 3)
                        {
                            skipBits(1);
                        }
                        else
                        {
                            skipBits(2);
                        }

                        /* only one granule, so no loop */
                        for (ch = 0; ch < nchan; ch++)
                        {
                            skipBits(21);
                            gain = peek8Bits();
                            if (wrapGain)
                            {
                                gain += (unsigned char)(gainchange[ch]);
                            }
                            else
                            {
                                if (gain != 0)
                                {
                                    if ((int)(gain) + gainchange[ch] > 255)
                                    {
                                        gain = 255;
                                    }
                                    else if ((int)gain + gainchange[ch] < 0)
                                    {
                                        gain = 0;
                                    }
                                    else
                                    {
                                        gain += (unsigned char)(gainchange[ch]);
                                    }
                                }
                            }
                            set8Bits(gain);
                            skipBits(42);
                        }
                        if (!crcflag)
                        {
                            if (nchan == 1)
                            {
                                crcWriteHeader(15, (char *)curframe);
                            }
                            else
                            {
                                crcWriteHeader(23, (char *)curframe);
                            }
                            /* WRITETOFILE */
                            if (!gUsingTemp)
                            {
                                addWriteBuff(filepos - (inbuffer - (curframe + 4 - buffer)), curframe + 4);
                            }
                        }

                    }
                    if (!gQuiet)
                    {
                        frame++;
                        if (frame % 200 == 0)
                        {
                            ok = reportPercentWritten((unsigned long)(((double)(filepos - (inbuffer - (curframe + bytesinframe - buffer))) *
                                                      100.0) / gFilesize), gFilesize);
                            if (!ok)
                            {
                                return ok;
                            }
                        }
                    }
                    wrdpntr = curframe + bytesinframe;
                    ok = frameSearch(0);
                }
            }
        }

        if (!gQuiet)
        {
            fprintf(stderr, "                                                   \r");
        }
        fflush(stdout);
        if (gUsingTemp)
        {
            while (fillBuffer(0));
            fflush(outf);
            fseek(outf, 0, SEEK_END);
            fseek(inf, 0, SEEK_END);
            outlength = ftell(outf);
            inlength = ftell(inf);
            fclose(outf);
            fclose(inf);
            inf = NULL;
            outf = NULL;

            if (outlength != inlength)
            {
                deleteFile(outfilename);
                passError(MP3GAIN_UNSPECIFED_ERROR, 3,
                          "Not enough temp space on disk to modify ", filename,
                          "\nEither free some space, or do not use \"temp file\" option\n");
                gNowWriting = false;
                return M3G_ERR_NOT_ENOUGH_TMP_SPACE;
            }
            else
            {

                if (deleteFile(filename))
                {
                    deleteFile(outfilename); //try to delete tmp file
                    passError(MP3GAIN_UNSPECIFED_ERROR, 3,
                              "Can't open ", filename, " for modifying\n");
                    gNowWriting = false;
                    return M3G_ERR_CANT_MODIFY_FILE;
                }
                if (moveFile(outfilename, filename))
                {
                    passError(MP3GAIN_UNSPECIFED_ERROR, 9,
                              "Problem re-naming ", outfilename, " to ", filename,
                              "\nThe mp3 was correctly modified, but you will need to re-name ",
                              outfilename, " to ", filename,
                              " yourself.\n");
                    gNowWriting = false;
                    return M3G_ERR_RENAME_TMP;
                };
                if (gSaveTime)
                {
                    fileTime(filename, setStoredTime);
                }
            }
            free(outfilename);
        }
        else
        {
            flushWriteBuff();
            fclose(inf);
            inf = NULL;
            if (gSaveTime)
            {
                fileTime(filename, setStoredTime);
            }
        }
    }

    gNowWriting = false;

    return 0;
}

static void WriteMP3GainTag(const char *filename,
                            struct MP3GainTagInfo *info,
                            struct FileTagsStruct *fileTags,
                            bool saveTimeStamp)
{
    if (gUseId3)
    {
        /* Write ID3 tag; remove stale APE tag if it exists. */
        if (WriteMP3GainID3Tag(filename, info, saveTimeStamp) >= 0)
        {
            RemoveMP3GainAPETag(filename, saveTimeStamp);
        }
    }
    else
    {
        /* Write APE tag */
        WriteMP3GainAPETag(filename, info, fileTags, saveTimeStamp);
    }
}

static void changeGainAndTag(const char *filename,
                             int leftgainchange,
                             int rightgainchange,
                             struct MP3GainTagInfo *tag,
                             struct FileTagsStruct *fileTag)
{
    double dblGainChange;
    int curMin;
    int curMax;

    if (leftgainchange != 0 || rightgainchange != 0)
    {
        if (!changeGain(filename, leftgainchange, rightgainchange))
        {
            if (!tag->haveUndo)
            {
                tag->undoLeft = 0;
                tag->undoRight = 0;
            }
            tag->dirty = true;
            tag->undoRight -= rightgainchange;
            tag->undoLeft -= leftgainchange;
            tag->undoWrap = wrapGain;

            /* if undo == 0, then remove Undo tag */
            tag->haveUndo = true;
            /* on second thought, don't remove it. Shortening the tag causes
               full file copy, which is slow so we avoid it if we can

               tag->haveUndo =
                 ((tag->undoRight != 0) ||
                  (tag->undoLeft != 0));
            */

            if (leftgainchange == rightgainchange)   /* don't screw around
                                                        with other fields if
                                                        mis-matched
                                                        left/right */
            {
                dblGainChange = leftgainchange * 1.505; /* approx. 5 * log10(2) */
                if (tag->haveTrackGain)
                {
                    tag->trackGain -= dblGainChange;
                }
                if (tag->haveTrackPeak)
                {
                    tag->trackPeak *= pow(2.0, (double)(leftgainchange) / 4.0);
                }
                if (tag->haveAlbumGain)
                {
                    tag->albumGain -= dblGainChange;
                }
                if (tag->haveAlbumPeak)
                {
                    tag->albumPeak *= pow(2.0, (double)(leftgainchange) / 4.0);
                }
                if (tag->haveMinMaxGain)
                {
                    curMin = tag->minGain;
                    curMax = tag->maxGain;
                    curMin += leftgainchange;
                    curMax += leftgainchange;
                    if (wrapGain)
                    {
                        if (curMin < 0 || curMin > 255 || curMax < 0 || curMax > 255)
                        {
                            /* we've lost the "real" min or max because of wrapping */
                            tag->haveMinMaxGain = 0;
                        }
                    }
                    else
                    {
                        tag->minGain = tag->minGain == 0 ? 0 : curMin < 0 ? 0 : curMin > 255 ? 255 : curMin;
                        tag->maxGain = curMax < 0 ? 0 : curMax > 255 ? 255 : curMax;
                    }
                }
                if (tag->haveAlbumMinMaxGain)
                {
                    curMin = tag->albumMinGain;
                    curMax = tag->albumMaxGain;
                    curMin += leftgainchange;
                    curMax += leftgainchange;
                    if (wrapGain)
                    {
                        if (curMin < 0 || curMin > 255 || curMax < 0 || curMax > 255)
                        {
                            /* we've lost the "real" min or max because of wrapping */
                            tag->haveAlbumMinMaxGain = 0;
                        }
                    }
                    else
                    {
                        tag->albumMinGain = tag->albumMinGain == 0 ? 0 : curMin < 0 ? 0 : curMin > 255 ? 255 : curMin;
                        tag->albumMaxGain = curMax < 0 ? 0 : curMax > 255 ? 255 : curMax;
                    }
                }
            } // if (leftgainchange == rightgainchange ...
            WriteMP3GainTag(filename, tag, fileTag, gSaveTime);
        } // if (!changeGain(filename ...
    }// if (leftgainchange !=0 ...
}

static int queryUserForClipping(char *argv_mainloop, int intGainChange)
{
    fprintf(stderr, "%s: WARNING: %s may clip with mp3 gain change %d\n",
            gProgramName, argv_mainloop, intGainChange);

    int ch = 0;
    fflush(stdout);
    while ((ch != 'Y') && (ch != 'N'))
    {
        fprintf(stderr, "Make change? [y/n]:");
        ch = getchar();
        if (ch == EOF)
        {
            ch = 'N';
        }
        ch = toupper(ch);
    }
    if (ch == 'N')
    {
        return 0;
    }

    return 1;
}

#if 0
static void wrapExplanation()
{
    printf("Here's the problem:\n"
           "The \"global gain\" field that mp3gain adjusts is an 8-bit unsigned integer, so\n"
           "the possible values are 0 to 255.\n"
           "\n"
           "MOST mp3 files (in fact, ALL the mp3 files I've examined so far) don't go\n"
           "over 230. So there's plenty of headroom on top-- you can increase the gain\n"
           "by 37dB (multiplying the amplitude by 76) without a problem.\n"
           "\n"
           "The problem is at the bottom of the range. Some encoders create frames with\n"
           "0 as the global gain for silent frames.\n"
           "What happens when you _lower_ the global gain by 1?\n"
           "Well, in the past, mp3gain always simply wrapped the result up to 255.\n"
           "That way, if you lowered the gain by any amount and then raised it by the\n"
           "same amount, the mp3 would always be _exactly_ the same.\n"
           "\n"
           "There are a few encoders out there, unfortunately, that create 0-gain frames\n"
           "with other audio data in the frame.\n"
           "As long as the global gain is 0, you'll never hear the data.\n"
           "But if you lower the gain on such a file, the global gain is suddenly _huge_.\n"
           "If you play this modified file, there might be a brief, very loud blip.\n"
           "\n"
           "So now the default behavior of mp3gain is to _not_ wrap gain changes.\n"
           "In other words,\n"
           "1) If the gain change would make a frame's global gain drop below 0,\n"
           "   then the global gain is set to 0.\n"
           "2) If the gain change would make a frame's global gain grow above 255,\n"
           "   then the global gain is set to 255.\n"
           "3) If a frame's global gain field is already 0, it is not changed, even if\n"
           "   the gain change is a positive number\n"
           "\n"
           "To use the original \"wrapping\" behavior, use -w");
    exit(0);
}
#endif

static void errUsage()
{
    fprintf(stderr,
            "Usage: %s [options] <infile> [<infile 2> ...]\n"
            "  --use -? or -h for a full list of options\n"
            "Uses mpglib, which can be found at http://www.mpg123.de\n",
            gProgramName);
    exit(1);
}

static void fullUsage()
{
    printf("copyright(c) 2001-2009 by Glen Sawyer\n"
           "uses mpglib, which can be found at http://www.mpg123.de\n"
           "Usage: %s [options] <infile> [<infile 2> ...]\n"
           "options:\n"
           "\t-v - show version number\n"
           "\t-g <i>  - apply gain i without doing any analysis\n"
           "\t-l 0 <i> - apply gain i to channel 0 (left channel)\n"
           "\t          without doing any analysis (ONLY works for STEREO files,\n"
           "\t          not Joint Stereo)\n"
           "\t-l 1 <i> - apply gain i to channel 1 (right channel)\n"
           "\t-e - skip Album analysis, even if multiple files listed\n"
           "\t-r - apply Track gain automatically (all files set to equal loudness)\n"
           "\t-k - automatically lower Track/Album gain to not clip audio\n"
           "\t-a - apply Album gain automatically (files are all from the same\n"
           "\t              album: a single gain change is applied to all files, so\n"
           "\t              their loudness relative to each other remains unchanged,\n"
           "\t              but the average album loudness is normalized)\n"
           "\t-m <i> - modify suggested MP3 gain by integer i\n"
           "\t-d <n> - modify suggested dB gain by floating-point n\n"
           "\t-c - ignore clipping warning when applying gain\n"
           "\t-o - output is a database-friendly tab-delimited list\n"
           "\t-t - writes modified data to temp file, then deletes original\n"
           "\t     instead of modifying bytes in original file\n"
           "\t-q - Quiet mode: no status messages\n"
           "\t-p - Preserve original file timestamp\n"
           "\t-x - Only find max. amplitude of file\n"
           "\t-f - Assume input file is an MPEG 2 Layer III file\n"
           "\t     (i.e. don't check for mis-named Layer I or Layer II files)\n"
           "\t-? or -h - show this message\n"
           "\t-s c - only check stored tag info (no other processing)\n"
           "\t-s d - delete stored tag info (no other processing)\n"
           "\t-s s - skip (ignore) stored tag info (do not read or write tags)\n"
           "\t-s r - force re-calculation (do not read tag info)\n"
           "\t-s i - use ID3v2 tag for MP3 gain info\n"
           "\t-s a - use APE tag for MP3 gain info (default)\n"
           "\t-u - undo changes made (based on stored tag info)\n"
           "\t-w - \"wrap\" gain change if gain+change > 255 or gain+change < 0\n"
           "\t      (use \"-? wrap\" switch for a complete explanation)\n"
           "If you specify -r and -a, only the second one will work\n"
           "If you do not specify -c, the program will stop and ask before\n"
           "     applying gain change to a file that might clip\n",
           gProgramName);
    exit(0);
}

int main(int argc, char **argv)
{
    MPSTR mp;
    int mode;
    unsigned char *Xingcheck;
    unsigned long frame;
    int nchan;
    int bitridx;
    int freqidx;
    long bytesinframe;
    double dBchange;
    double dblGainChange;
    int intGainChange = 0;
    int intAlbumGainChange = 0;
    int nprocsamp;
    int first = 1;
    Float_t maxsample;
    Float_t lsamples[1152];
    Float_t rsamples[1152];
    unsigned char maxgain;
    unsigned char mingain;
    int ignoreClipWarning = 0;
    int autoClip = 0;
    bool applyTrack = false;
    bool applyAlbum = false;
    int analysisTrack = 0;
    bool analysisError = false;
    int databaseFormat = 0;
    int i;
    int *fileok;
    int goAhead;
    bool directGain = false;
    bool directSingleChannelGain = false;
    int directGainVal = 0;
    int mp3GainMod = 0;
    double dBGainMod = 0;
    int mpegver;
    int sideinfo_len;
    long gFilesize = 0;
    int decodeSuccess;
    struct MP3GainTagInfo *tagInfo;
    struct MP3GainTagInfo *curTag;
    struct FileTagsStruct *fileTags;
    int albumRecalc;
    double curAlbumGain = 0;
    double curAlbumPeak = 0;
    unsigned char curAlbumMinGain = 0;
    unsigned char curAlbumMaxGain = 0;

    gSuccess = true;
    gProgramName = basename(argv[0]);

    if (argc < 2)
    {
        errUsage();
    }

    bool ok = true;
    maxAmpOnly = false;
    gSaveTime = false;
    int fileStart = 1;
    numFiles = 0;

    for (i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (arg[0] != '-' || strlen(arg) != 2)
        {
            continue;
        }
        fileStart++;
        switch (arg[1])
        {
        case 'a':
        case 'A':
            applyTrack = false;
            applyAlbum = true;
            break;

        case 'c':
        case 'C':
            ignoreClipWarning = true;
            break;

        case 'd':
        case 'D':
            if (arg[2] != '\0')
            {
                dBGainMod = atof(arg + 2);
                break;
            }
            if (i + 1 >= argc)
            {
                errUsage();
            }
            dBGainMod = atof(argv[i + 1]);
            i++;
            fileStart++;
            break;

        case 'f':
        case 'F':
            Reckless = 1;
            break;

        case 'g':
        case 'G':
            directGain = true;
            directSingleChannelGain = false;
            if (arg[2] != '\0')
            {
                directGainVal = atoi(arg + 2);
                break;
            }
            if (i + 1 >= argc)
            {
                errUsage();
            }
            directGainVal = atoi(argv[i + 1]);
            i++;
            fileStart++;
            break;

        case 'h':
        case 'H':
        case '?':
            fullUsage();
            break;

        case 'k':
        case 'K':
            autoClip = true;
            break;

        case 'l':
        case 'L':
            directSingleChannelGain = true;
            directGain = false;
            if (arg[2] != '\0')
            {
                whichChannel = atoi(arg + 2);
                if (i + 1 >= argc)
                {
                    errUsage();
                }
                directGainVal = atoi(argv[i + 1]);
                i++;
                fileStart++;
                break;
            }
            if (i + 2 >= argc)
            {
                errUsage();
            }
            whichChannel = atoi(argv[i + 1]);
            i++;
            fileStart++;
            directGainVal = atoi(argv[i + 1]);
            i++;
            fileStart++;
            break;

        case 'm':
        case 'M':
            if (arg[2] != '\0')
            {
                mp3GainMod = atoi(arg + 2);
                break;
            }
            if (i + 1 >= argc)
            {
                errUsage();
            }
            mp3GainMod = atoi(argv[i + 1]);
            i++;
            fileStart++;
            break;

        case 'o':
        case 'O':
            databaseFormat = true;
            break;

        case 'p':
        case 'P':
            gSaveTime = true;
            break;

        case 'q':
        case 'Q':
            gQuiet = true;
            break;

        case 'r':
        case 'R':
            applyTrack = true;
            applyAlbum = false;
            break;

        case 's':
        case 'S':
        {
            char c = 0;
            if (arg[2] == '\0')
            {
                if (i + 1 >= argc)
                {
                    errUsage();
                }
                i++;
                fileStart++;
                c = arg[0];
            }
            else
            {
                c = arg[2];
            }
            switch (c)
            {
            case 'c':
            case 'C':
                gCheckTagOnly = true;
                break;
            case 'd':
            case 'D':
                gDeleteTag = true;
                break;
            case 's':
            case 'S':
                gSkipTag = true;
                break;
            case 'u':
            case 'U':
                gForceUpdateTag = true;
                break;
            case 'r':
            case 'R':
                gForceRecalculateTag = true;
                break;
            case 'i':
            case 'I':
                gUseId3 = true;
                break;
            case 'a':
            case 'A':
                gUseId3 = false;
                break;
            default:
                errUsage();
            }
            break;
        }

        case 't':
        case 'T':
            gUsingTemp = true;
            break;

        case 'u':
        case 'U':
            undoChanges = true;
            break;

        case 'v':
        case 'V':
            printf("%s version %s (%s %s)\n", gProgramName, MP3GAIN_VERSION,
                   __DATE__, __TIME__);
            exit(0);

        case 'w':
        case 'W':
            wrapGain = true;
            break;

        case 'x':
        case 'X':
            maxAmpOnly = true;
            break;

        case 'e':
        case 'E':
            analysisTrack = true;
            break;

        default:
            fprintf(stderr, "%s: unknown option '%s'\n", gProgramName, arg);
            exit(EXIT_FAILURE);
        }
    }

    /* now stored in tagInfo---  maxsample = malloc(sizeof(Float_t) * argc); */
    fileok = malloc(sizeof(int) * argc);
    /* now stored in tagInfo---  maxgain = malloc(sizeof(unsigned char) * argc); */
    /* now stored in tagInfo---  mingain = malloc(sizeof(unsigned char) * argc); */
    tagInfo = calloc(argc, sizeof(struct MP3GainTagInfo));
    fileTags = malloc(sizeof(struct FileTagsStruct) * argc);

    if (databaseFormat)
    {
        if (gCheckTagOnly)
        {
            printf("File\tMP3 gain\tdB gain\tMax Amplitude\tMax global_gain\t"
                   "Min global_gain\tAlbum gain\tAlbum dB gain\tAlbum Max Amplitude\t"
                   "Album Max global_gain\tAlbum Min global_gain\n");
        }
        else if (undoChanges)
        {
            printf("File\tleft global_gain change\tright global_gain change\n");
        }
        else
        {
            printf("File\tMP3 gain\tdB gain\tMax Amplitude\tMax global_gain\t"
                   "Min global_gain\n");
        }
        fflush(stdout);
    }

    /* read all the tags first */
    totFiles = argc - fileStart;
    for (int argi = fileStart; argi < argc; argi++)
    {
        fileok[argi] = 0;
        curfilename = argv[argi];
        fileTags[argi].apeTag = NULL;
        fileTags[argi].lyrics3tag = NULL;
        fileTags[argi].id31tag = NULL;
        tagInfo[argi].dirty = gForceUpdateTag;
        tagInfo[argi].haveAlbumGain = 0;
        tagInfo[argi].haveAlbumPeak = 0;
        tagInfo[argi].haveTrackGain = 0;
        tagInfo[argi].haveTrackPeak = 0;
        tagInfo[argi].haveUndo = 0;
        tagInfo[argi].haveMinMaxGain = 0;
        tagInfo[argi].haveAlbumMinMaxGain = 0;
        tagInfo[argi].recalc = 0;

        if (!gSkipTag && !gDeleteTag)
        {
            {
                ReadMP3GainAPETag(curfilename, &(tagInfo[argi]), &(fileTags[argi]));
                if (gUseId3)
                {
                    if (tagInfo[argi].haveTrackGain || tagInfo[argi].haveAlbumGain ||
                        tagInfo[argi].haveMinMaxGain || tagInfo[argi].haveAlbumMinMaxGain ||
                        tagInfo[argi].haveUndo)
                    {
                        /* Mark the file dirty to force upgrade to ID3v2 */
                        tagInfo[argi].dirty = 1;
                    }
                    ReadMP3GainID3Tag(curfilename, &(tagInfo[argi]));
                }
            }
#if 0
            printf("Read previous tags from %s\n", curfilename);
            dumpTaginfo(&(tagInfo[argi]));
#endif
            if (gForceRecalculateTag)
            {
                if (tagInfo[argi].haveAlbumGain)
                {
                    tagInfo[argi].dirty = true;
                    tagInfo[argi].haveAlbumGain = 0;
                }
                if (tagInfo[argi].haveAlbumPeak)
                {
                    tagInfo[argi].dirty = true;
                    tagInfo[argi].haveAlbumPeak = 0;
                }
                if (tagInfo[argi].haveTrackGain)
                {
                    tagInfo[argi].dirty = true;
                    tagInfo[argi].haveTrackGain = 0;
                }
                if (tagInfo[argi].haveTrackPeak)
                {
                    tagInfo[argi].dirty = true;
                    tagInfo[argi].haveTrackPeak = 0;
                }
#if 0 // NOT Undo information!
                if (tagInfo[argi].haveUndo)
                {
                    tagInfo[argi].dirty = true;
                    tagInfo[argi].haveUndo = 0;
                }
#endif
                if (tagInfo[argi].haveMinMaxGain)
                {
                    tagInfo[argi].dirty = true;
                    tagInfo[argi].haveMinMaxGain = 0;
                }
                if (tagInfo[argi].haveAlbumMinMaxGain)
                {
                    tagInfo[argi].dirty = true;
                    tagInfo[argi].haveAlbumMinMaxGain = 0;
                }
            }
        }
    }

    /* check if we need to actually process the file(s) */
    albumRecalc = gForceRecalculateTag || gSkipTag ? FULL_RECALC : 0;
    if (!gSkipTag && !gDeleteTag && !gForceRecalculateTag)
    {
        /* we're not automatically recalculating, so check if we already have
           all the information */
        if (argc - fileStart > 1)
        {
            curAlbumGain = tagInfo[fileStart].albumGain;
            curAlbumPeak = tagInfo[fileStart].albumPeak;
            curAlbumMinGain = tagInfo[fileStart].albumMinGain;
            curAlbumMaxGain = tagInfo[fileStart].albumMaxGain;
        }
        for (int argi = fileStart; argi < argc; argi++)
        {
            if (!maxAmpOnly)   /* we don't care about these things if we're
                                  only looking for max amp */
            {
                if (argc - fileStart > 1 && !applyTrack &&
                    !analysisTrack)   /* only check album stuff if more than
                                         one file in the list */
                {
                    if (!tagInfo[argi].haveAlbumGain)
                    {
                        albumRecalc |= FULL_RECALC;
                    }
                    else if (tagInfo[argi].albumGain != curAlbumGain)
                    {
                        albumRecalc |= FULL_RECALC;
                    }
                }
                if (!tagInfo[argi].haveTrackGain)
                {
                    tagInfo[argi].recalc |= FULL_RECALC;
                }
            }
            if (argc - fileStart > 1 && !applyTrack &&
                !analysisTrack)   /* only check album stuff if more than one
                                     file in the list */
            {
                if (!tagInfo[argi].haveAlbumPeak)
                {
                    albumRecalc |= AMP_RECALC;
                }
                else if (tagInfo[argi].albumPeak != curAlbumPeak)
                {
                    albumRecalc |= AMP_RECALC;
                }
                if (!tagInfo[argi].haveAlbumMinMaxGain)
                {
                    albumRecalc |= MIN_MAX_GAIN_RECALC;
                }
                else if (tagInfo[argi].albumMaxGain != curAlbumMaxGain)
                {
                    albumRecalc |= MIN_MAX_GAIN_RECALC;
                }
                else if (tagInfo[argi].albumMinGain != curAlbumMinGain)
                {
                    albumRecalc |= MIN_MAX_GAIN_RECALC;
                }
            }
            if (!tagInfo[argi].haveTrackPeak)
            {
                tagInfo[argi].recalc |= AMP_RECALC;
            }
            if (!tagInfo[argi].haveMinMaxGain)
            {
                tagInfo[argi].recalc |= MIN_MAX_GAIN_RECALC;
            }
        }
    }

    for (int argi = fileStart; argi < argc; argi++)
    {
        memset(&mp, 0, sizeof(mp));

        // if the entire Album requires some kind of recalculation, then each
        // track needs it
        tagInfo[argi].recalc |= albumRecalc;

        curfilename = argv[argi];
        if (gCheckTagOnly)
        {
            curTag = tagInfo + argi;
            if (curTag->haveTrackGain)
            {
                dblGainChange = curTag->trackGain / (5.0 * log10(2.0));

                if (fabs(dblGainChange) - (double)((int)(fabs(dblGainChange))) < 0.5)
                {
                    intGainChange = (int)(dblGainChange);
                }
                else
                {
                    intGainChange = (int)(dblGainChange) + (dblGainChange < 0 ? -1 : 1);
                }
            }
            if (curTag->haveAlbumGain)
            {
                dblGainChange = curTag->albumGain / (5.0 * log10(2.0));

                if (fabs(dblGainChange) - (double)((int)(fabs(dblGainChange))) < 0.5)
                {
                    intAlbumGainChange = (int)(dblGainChange);
                }
                else
                {
                    intAlbumGainChange = (int)(dblGainChange) + (dblGainChange < 0 ? -1 : 1);
                }
            }
            if (!gQuiet && !databaseFormat)
            {
                printf("%s\n", argv[argi]);
                if (curTag->haveTrackGain)
                {
                    printf("Recommended \"Track\" dB change: %f\n", curTag->trackGain);
                    printf("Recommended \"Track\" mp3 gain change: %d\n", intGainChange);
                    if (curTag->haveTrackPeak)
                    {
                        if (curTag->trackPeak * (Float_t)(pow(2.0, (double)(intGainChange) / 4.0)) > 1.0)
                        {
                            printf("WARNING: some clipping may occur with this gain change!\n");
                        }
                    }
                }
                if (curTag->haveTrackPeak)
                {
                    printf("Max PCM sample at current gain: %f\n", curTag->trackPeak * 32768.0);
                }
                if (curTag->haveMinMaxGain)
                {
                    printf("Max mp3 global gain field: %d\n", curTag->maxGain);
                    printf("Min mp3 global gain field: %d\n", curTag->minGain);
                }
                if (curTag->haveAlbumGain)
                {
                    printf("Recommended \"Album\" dB change: %f\n", curTag->albumGain);
                    printf("Recommended \"Album\" mp3 gain change: %d\n", intAlbumGainChange);
                    if (curTag->haveTrackPeak)
                    {
                        if (curTag->trackPeak * (Float_t)(pow(2.0, (double)(intAlbumGainChange) / 4.0)) > 1.0)
                        {
                            printf("WARNING: some clipping may occur with this gain change!\n");
                        }
                    }
                }
                if (curTag->haveAlbumPeak)
                {
                    printf("Max Album PCM sample at current gain: %f\n", curTag->albumPeak * 32768.0);
                }
                if (curTag->haveAlbumMinMaxGain)
                {
                    printf("Max Album mp3 global gain field: %d\n", curTag->albumMaxGain);
                    printf("Min Album mp3 global gain field: %d\n", curTag->albumMinGain);
                }
                printf("\n");
            }
            else
            {
                printf("%s\t", argv[argi]);
                if (curTag->haveTrackGain)
                {
                    printf("%d\t", intGainChange);
                    printf("%f\t", curTag->trackGain);
                }
                else
                {
                    printf("NA\tNA\t");
                }
                if (curTag->haveTrackPeak)
                {
                    printf("%f\t", curTag->trackPeak * 32768.0);
                }
                else
                {
                    printf("NA\t");
                }
                if (curTag->haveMinMaxGain)
                {
                    printf("%d\t", curTag->maxGain);
                    printf("%d\t", curTag->minGain);
                }
                else
                {
                    printf("NA\tNA\t");
                }
                if (curTag->haveAlbumGain)
                {
                    printf("%d\t", intAlbumGainChange);
                    printf("%f\t", curTag->albumGain);
                }
                else
                {
                    printf("NA\tNA\t");
                }
                if (curTag->haveAlbumPeak)
                {
                    printf("%f\t", curTag->albumPeak * 32768.0);
                }
                else
                {
                    printf("NA\t");
                }
                if (curTag->haveAlbumMinMaxGain)
                {
                    printf("%d\t", curTag->albumMaxGain);
                    printf("%d\n", curTag->albumMinGain);
                }
                else
                {
                    printf("NA\tNA\n");
                }
                fflush(stdout);
            }
        }
        else if (undoChanges)
        {
            directGain = true; /* so we don't write the tag a second time */
            if ((tagInfo[argi].haveUndo) && (tagInfo[argi].undoLeft || tagInfo[argi].undoRight))
            {
                if (!gQuiet && !databaseFormat)
                {
                    printf("Undoing mp3gain changes (%d,%d) to %s...\n",
                           tagInfo[argi].undoLeft, tagInfo[argi].undoRight,
                           argv[argi]);
                }

                if (databaseFormat)
                {
                    printf("%s\t%d\t%d\n", argv[argi], tagInfo[argi].undoLeft, tagInfo[argi].undoRight);
                }

                changeGainAndTag(argv[argi],
                                 tagInfo[argi].undoLeft, tagInfo[argi].undoRight,
                                 tagInfo + argi, fileTags + argi);

            }
            else
            {
                if (databaseFormat)
                {
                    printf("%s\t0\t0\n", argv[argi]);
                }
                else if (!gQuiet)
                {
                    if (tagInfo[argi].haveUndo)
                    {
                        fprintf(stderr, "%s: No changes to undo in %s\n",
                                gProgramName, argv[argi]);
                    }
                    else
                    {
                        fprintf(stderr, "%s: No undo information in %s\n",
                                gProgramName, argv[argi]);
                    }
                    gSuccess = false;
                }
            }
        }
        else if (directSingleChannelGain)
        {
            if (!gQuiet)
            {
                printf("Applying gain change of %d to CHANNEL %d of %s...\n",
                       directGainVal, whichChannel, argv[argi]);
            }
            if (whichChannel)   /* do right channel */
            {
                if (gSkipTag)
                {
                    changeGain(argv[argi], 0, directGainVal);
                }
                else
                {
                    changeGainAndTag(argv[argi], 0, directGainVal, tagInfo + argi, fileTags + argi);
                }
            }
            else   /* do left channel */
            {
                if (gSkipTag)
                {
                    changeGain(argv[argi], directGainVal, 0);
                }
                else
                {
                    changeGainAndTag(argv[argi], directGainVal, 0, tagInfo + argi, fileTags + argi);
                }
            }
            if (!gQuiet && gSuccess)
            {
                printf("\ndone\n");
            }
        }
        else if (directGain)
        {
            if (!gQuiet)
            {
                printf("Applying gain change of %d to %s...\n",
                       directGainVal, argv[argi]);
            }
            if (gSkipTag)
            {
                changeGain(argv[argi], directGainVal, directGainVal);
            }
            else
            {
                changeGainAndTag(argv[argi],
                                 directGainVal, directGainVal, tagInfo + argi,
                                 fileTags + argi);
            }
            if (!gQuiet && gSuccess)
            {
                printf("\ndone\n");
            }
        }
        else if (gDeleteTag)
        {
            {
                RemoveMP3GainAPETag(argv[argi], gSaveTime);
                if (gUseId3)
                {
                    RemoveMP3GainID3Tag(argv[argi], gSaveTime);
                }
            }
            if (!gQuiet && !databaseFormat)
            {
                printf("Deleting tag info of %s...\n", argv[argi]);
            }
            if (databaseFormat)
            {
                printf("%s\tNA\tNA\tNA\tNA\tNA\n", argv[argi]);
            }
        }
        else
        {
            if (!databaseFormat && !gQuiet)
            {
                printf("%s\n", argv[argi]);
            }

            if (tagInfo[argi].recalc > 0)
            {
                gFilesize = getSizeOfFile(argv[argi]);

                inf = fopen(argv[argi], "rb");
            }

            if ((inf == NULL) && (tagInfo[argi].recalc > 0))
            {
                fprintf(stderr, "%s: Can't open %s for reading\n",
                        gProgramName, argv[argi]);
                gSuccess = false;
            }
            else
            {
                InitMP3(&mp);
                if (tagInfo[argi].recalc == 0)
                {
                    maxsample = tagInfo[argi].trackPeak * 32768.0;
                    maxgain = tagInfo[argi].maxGain;
                    mingain = tagInfo[argi].minGain;
                    ok = true;
                }
                else
                {
                    if (!((tagInfo[argi].recalc & FULL_RECALC) || (tagInfo[argi].recalc & AMP_RECALC))) /* only min/max rescan */
                    {
                        maxsample = tagInfo[argi].trackPeak * 32768.0;
                    }
                    else
                    {
                        maxsample = 0;
                    }
                    {
                        gBadLayer = false;
                        LayerSet = Reckless;
                        maxgain = 0;
                        mingain = 255;
                        inbuffer = 0;
                        filepos = 0;
                        bitidx = 0;
                        ok = fillBuffer(0);
                    }
                }
                if (ok)
                {
                    if (tagInfo[argi].recalc > 0)
                    {
                        wrdpntr = buffer;

                        ok = skipID3v2();
                        // TODO: why are we ignoring `ok` here?

                        ok = frameSearch(true);
                    }

                    if (!ok)
                    {
                        if (!gBadLayer)
                        {
                            fprintf(stderr, "%s: Can't find any valid MP3 frames in file %s\n",
                                    gProgramName, argv[argi]);
                            gSuccess = false;
                        }
                    }
                    else
                    {
                        LayerSet = 1; /* We've found at least one valid layer 3 frame.
                                         Assume any later layer 1 or 2 frames are just
                                         bitstream corruption */
                        fileok[argi] = true;
                        numFiles++;

                        if (tagInfo[argi].recalc > 0)
                        {
                            mode = (curframe[3] >> 6) & 3;

                            if ((curframe[1] & 0x08) == 0x08) /* MPEG 1 */
                            {
                                sideinfo_len = ((curframe[3] & 0xC0) == 0xC0) ? 4 + 17 : 4 + 32;
                            }
                            else                /* MPEG 2 */
                            {
                                sideinfo_len = ((curframe[3] & 0xC0) == 0xC0) ? 4 + 9 : 4 + 17;
                            }

                            if (!(curframe[1] & 0x01))
                            {
                                sideinfo_len += 2;
                            }

                            Xingcheck = curframe + sideinfo_len;
                            //LAME CBR files have "Info" tags, not "Xing" tags
                            if ((Xingcheck[0] == 'X' && Xingcheck[1] == 'i' && Xingcheck[2] == 'n' && Xingcheck[3] == 'g') ||
                                (Xingcheck[0] == 'I' && Xingcheck[1] == 'n' && Xingcheck[2] == 'f' && Xingcheck[3] == 'o'))
                            {
                                bitridx = (curframe[2] >> 4) & 0x0F;
                                if (bitridx == 0)
                                {
                                    fprintf(stderr, "%s: %s is free format (not currently supported)\n",
                                            gProgramName, curfilename);
                                    ok = false;
                                }
                                else
                                {
                                    mpegver = (curframe[1] >> 3) & 0x03;
                                    freqidx = (curframe[2] >> 2) & 0x03;

                                    bytesinframe = arrbytesinframe[bitridx] + ((curframe[2] >> 1) & 0x01);

                                    wrdpntr = curframe + bytesinframe;

                                    ok = frameSearch(0);
                                }
                            }

                            frame = 1;

                            if (!maxAmpOnly)
                            {
                                if (ok)
                                {
                                    mpegver = (curframe[1] >> 3) & 0x03;
                                    freqidx = (curframe[2] >> 2) & 0x03;

                                    if (first)
                                    {
                                        lastfreq = frequency[mpegver][freqidx];
                                        InitGainAnalysis((long)(lastfreq * 1000.0));
                                        analysisError = false;
                                        first = 0;
                                    }
                                    else
                                    {
                                        if (frequency[mpegver][freqidx] != lastfreq)
                                        {
                                            lastfreq = frequency[mpegver][freqidx];
                                            ResetSampleFrequency((long)(lastfreq * 1000.0));
                                        }
                                    }
                                }
                            }
                            else
                            {
                                analysisError = false;
                            }

                            while (ok)
                            {
                                bitridx = (curframe[2] >> 4) & 0x0F;
                                if (bitridx == 0)
                                {
                                    fprintf(stderr, "%s: %s is free format (not currently supported)\n",
                                            gProgramName, curfilename);
                                    ok = false;
                                }
                                else
                                {
                                    mpegver = (curframe[1] >> 3) & 0x03;
                                    freqidx = (curframe[2] >> 2) & 0x03;

                                    bytesinframe = arrbytesinframe[bitridx] + ((curframe[2] >> 1) & 0x01);
                                    mode = (curframe[3] >> 6) & 0x03;
                                    nchan = (mode == 3) ? 1 : 2;

                                    if (inbuffer >= bytesinframe)
                                    {
                                        lSamp = lsamples;
                                        rSamp = rsamples;
                                        maxSamp = &maxsample;
                                        maxGain = &maxgain;
                                        minGain = &mingain;
                                        procSamp = 0;
                                        if ((tagInfo[argi].recalc & AMP_RECALC) || (tagInfo[argi].recalc & FULL_RECALC))
                                        {
                                            decodeSuccess = decodeMP3(&mp, curframe, bytesinframe, &nprocsamp);
                                        }
                                        else
                                        {
                                            /* don't need to actually decode
                                               frame, just scan for min/max
                                               gain values */
                                            decodeSuccess = !MP3_OK;
                                            scanFrameGain();//curframe);
                                        }
                                        if (decodeSuccess == MP3_OK)
                                        {
                                            if (!maxAmpOnly && (tagInfo[argi].recalc & FULL_RECALC))
                                            {
                                                if (AnalyzeSamples(lsamples, rsamples, procSamp / nchan, nchan) == GAIN_ANALYSIS_ERROR)
                                                {
                                                    fprintf(stderr, "%s: Error analyzing further samples (max time reached)\n", gProgramName);
                                                    analysisError = true;
                                                    ok = false;
                                                }
                                            }
                                        }
                                    }

                                    if (!analysisError)
                                    {
                                        wrdpntr = curframe + bytesinframe;
                                        ok = frameSearch(0);
                                    }

                                    if (!gQuiet)
                                    {
                                        if (!(++frame % 200))
                                        {
                                            reportPercentAnalyzed((int)(((double)(filepos - (inbuffer - (curframe + bytesinframe - buffer))) * 100.0) / gFilesize),
                                                                  gFilesize);
                                        }
                                    }
                                }
                            }
                        }

                        if (!gQuiet)
                        {
                            fprintf(stderr, "                                                 \r");
                        }

                        if (tagInfo[argi].recalc & FULL_RECALC)
                        {
                            if (maxAmpOnly)
                            {
                                dBchange = 0;
                            }
                            else
                            {
                                dBchange = GetTitleGain();
                            }
                        }
                        else
                        {
                            dBchange = tagInfo[argi].trackGain;
                        }

                        if (dBchange == GAIN_NOT_ENOUGH_SAMPLES)
                        {
                            fprintf(stderr, "%s: Not enough samples in %s to do analysis\n",
                                    gProgramName, argv[argi]);
                            gSuccess = false;
                            numFiles--;
                        }
                        else
                        {
                            /* even if gSkipTag is on, we'll leave this part
                               running just to store the minpeak and
                               maxpeak */
                            curTag = tagInfo + argi;
                            if (!maxAmpOnly)
                            {
                                /* if we don't already have a tagged track
                                   gain OR we have it, but it doesn't match */
                                if (!curTag->haveTrackGain ||
                                    (curTag->haveTrackGain &&
                                     (fabs(dBchange - curTag->trackGain) >= 0.01))
                                   )
                                {
                                    curTag->dirty = true;
                                    curTag->haveTrackGain = 1;
                                    curTag->trackGain = dBchange;
                                }
                            }
                            if (!curTag->haveMinMaxGain || /* if minGain or
                                                              maxGain doesn't
                                                              match tag */
                                (curTag->haveMinMaxGain &&
                                 (curTag->minGain != mingain || curTag->maxGain != maxgain)))
                            {
                                curTag->dirty = true;
                                curTag->haveMinMaxGain = true;
                                curTag->minGain = mingain;
                                curTag->maxGain = maxgain;
                            }

                            if (!curTag->haveTrackPeak ||
                                (curTag->haveTrackPeak &&
                                 (fabs(maxsample - (curTag->trackPeak) * 32768.0) >= 3.3)))
                            {
                                curTag->dirty = true;
                                curTag->haveTrackPeak = true;
                                curTag->trackPeak = maxsample / 32768.0;
                            }
                            /* the TAG version of the suggested Track Gain
                               should ALWAYS be based on the 89dB standard.
                               So we don't modify the suggested gain change
                               until this point */

                            dBchange += dBGainMod;

                            dblGainChange = dBchange / (5.0 * log10(2.0));

                            if (fabs(dblGainChange) - (double)((int)(fabs(dblGainChange))) < 0.5)
                            {
                                intGainChange = (int)(dblGainChange);
                            }
                            else
                            {
                                intGainChange = (int)(dblGainChange) + (dblGainChange < 0 ? -1 : 1);
                            }
                            intGainChange += mp3GainMod;

                            if (databaseFormat)
                            {
                                printf("%s\t%d\t%f\t%f\t%d\t%d\n", argv[argi], intGainChange, dBchange, maxsample, maxgain, mingain);
                                fflush(stdout);
                            }
                            if (!applyTrack && !applyAlbum)
                            {
                                if (!databaseFormat)
                                {
                                    printf("Recommended \"Track\" dB change: %f\n", dBchange);
                                    printf("Recommended \"Track\" mp3 gain change: %d\n", intGainChange);
                                    if (maxsample * (Float_t)(pow(2.0, (double)(intGainChange) / 4.0)) > 32767.0)
                                    {
                                        printf("WARNING: some clipping may occur with this gain change!\n");
                                    }
                                    printf("Max PCM sample at current gain: %f\n", maxsample);
                                    printf("Max mp3 global gain field: %d\n", maxgain);
                                    printf("Min mp3 global gain field: %d\n", mingain);
                                    printf("\n");
                                }
                            }
                            else if (applyTrack)
                            {
                                first = true; /* don't keep track of Album gain */
                                if (inf)
                                {
                                    fclose(inf);
                                }
                                inf = NULL;
                                goAhead = true;

                                if (intGainChange == 0)
                                {
                                    printf("No changes to %s are necessary\n", argv[argi]);
                                    if (!gSkipTag && tagInfo[argi].dirty)
                                    {
                                        printf("...but tag needs update: Writing tag information for %s\n", argv[argi]);
                                        WriteMP3GainTag(argv[argi], tagInfo + argi, fileTags + argi, gSaveTime);
                                    }
                                }
                                else
                                {
                                    if (autoClip)
                                    {
                                        int intMaxNoClipGain = (int)(floor(4.0 * log10(32767.0 / maxsample) / log10(2.0)));
                                        if (intGainChange > intMaxNoClipGain)
                                        {
                                            printf("Applying auto-clipped mp3 gain change of %d to %s\n(Original suggested gain was %d)\n",
                                                   intMaxNoClipGain, argv[argi], intGainChange);
                                            intGainChange = intMaxNoClipGain;
                                        }
                                    }
                                    else if (!ignoreClipWarning)
                                    {
                                        if (maxsample * (Float_t)(pow(2.0, (double)(intGainChange) / 4.0)) > 32767.0)
                                        {
                                            if (queryUserForClipping(argv[argi], intGainChange))
                                            {
                                                if (!gQuiet)
                                                {
                                                    printf("Applying mp3 gain change of %d to %s...\n", intGainChange, argv[argi]);
                                                }
                                            }
                                            else
                                            {
                                                goAhead = 0;
                                            }
                                        }
                                    }
                                    if (goAhead)
                                    {
                                        if (!gQuiet)
                                        {
                                            printf("Applying mp3 gain change of %d to %s...\n", intGainChange, argv[argi]);
                                        }
                                        if (gSkipTag)
                                        {
                                            changeGain(argv[argi], intGainChange, intGainChange);
                                        }
                                        else
                                        {
                                            changeGainAndTag(argv[argi],
                                                             intGainChange, intGainChange, tagInfo + argi, fileTags + argi);
                                        }
                                    }
                                    else if (!gSkipTag && tagInfo[argi].dirty)
                                    {
                                        printf("Writing tag information for %s\n", argv[argi]);
                                        WriteMP3GainTag(argv[argi], tagInfo + argi, fileTags + argi, gSaveTime);
                                    }
                                }
                            }
                        }
                    }
                }

                ExitMP3(&mp);
                fflush(stdout);
                if (inf)
                {
                    fclose(inf);
                }
                inf = NULL;
            }
        }
    }

    if (numFiles > 0 && !applyTrack && !analysisTrack)
    {
        if (albumRecalc & FULL_RECALC)
        {
            if (maxAmpOnly)
            {
                dBchange = 0;
            }
            else
            {
                dBchange = GetAlbumGain();
            }
        }
        else
        {
            /* the following if-else is for the weird case where someone
               applies "Album" gain to a single file, but the file doesn't
               actually have an Album field */
            if (tagInfo[fileStart].haveAlbumGain)
            {
                dBchange = tagInfo[fileStart].albumGain;
            }
            else
            {
                dBchange = tagInfo[fileStart].trackGain;
            }
        }

        if (dBchange == GAIN_NOT_ENOUGH_SAMPLES)
        {
            fprintf(stderr, "%s: Not enough samples in mp3 files to do analysis\n",
                    gProgramName);
        }
        else
        {
            Float_t maxmaxsample;
            unsigned char maxmaxgain;
            unsigned char minmingain;
            maxmaxsample = 0;
            maxmaxgain = 0;
            minmingain = 255;
            for (int argi = fileStart; argi < argc; argi++)
            {
                if (fileok[argi])
                {
                    if (tagInfo[argi].trackPeak > maxmaxsample)
                    {
                        maxmaxsample = tagInfo[argi].trackPeak;
                    }
                    if (tagInfo[argi].maxGain > maxmaxgain)
                    {
                        maxmaxgain = tagInfo[argi].maxGain;
                    }
                    if (tagInfo[argi].minGain < minmingain)
                    {
                        minmingain = tagInfo[argi].minGain;
                    }
                }
            }

            if (!gSkipTag && (numFiles > 1 || applyAlbum))
            {
                for (int argi = fileStart; argi < argc; argi++)
                {
                    curTag = tagInfo + argi;
                    if (!maxAmpOnly)
                    {
                        /* if we don't already have a tagged track gain OR we
                           have it, but it doesn't match */
                        if (
                            !curTag->haveAlbumGain ||
                            (curTag->haveAlbumGain &&
                             (fabs(dBchange - curTag->albumGain) >= 0.01))
                        )
                        {
                            curTag->dirty = true;
                            curTag->haveAlbumGain = 1;
                            curTag->albumGain = dBchange;
                        }
                    }

                    /* if albumMinGain or albumMaxGain doesn't match tag... */
                    if (!curTag->haveAlbumMinMaxGain ||
                        (curTag->haveAlbumMinMaxGain &&
                         (curTag->albumMinGain != minmingain ||
                          curTag->albumMaxGain != maxmaxgain)))
                    {
                        curTag->dirty = true;
                        curTag->haveAlbumMinMaxGain = true;
                        curTag->albumMinGain = minmingain;
                        curTag->albumMaxGain = maxmaxgain;
                    }

                    if (!curTag->haveAlbumPeak ||
                        (curTag->haveAlbumPeak &&
                         (fabs(maxmaxsample - curTag->albumPeak) >= 0.0001)))
                    {
                        curTag->dirty = true;
                        curTag->haveAlbumPeak = true;
                        curTag->albumPeak = maxmaxsample;
                    }
                }
            }

            /* the TAG version of the suggested Album Gain should ALWAYS be
               based on the 89dB standard.  So we don't modify the suggested
               gain change until this point */

            dBchange += dBGainMod;

            dblGainChange = dBchange / (5.0 * log10(2.0));
            if (fabs(dblGainChange) - (double)((int)(fabs(dblGainChange))) < 0.5)
            {
                intGainChange = (int)(dblGainChange);
            }
            else
            {
                intGainChange = (int)(dblGainChange) + (dblGainChange < 0 ? -1 : 1);
            }
            intGainChange += mp3GainMod;

            if (databaseFormat)
            {
                printf("\"Album\"\t%d\t%f\t%f\t%d\t%d\n", intGainChange, dBchange, maxmaxsample * 32768.0, maxmaxgain,
                       minmingain);
                fflush(stdout);
            }

            if (!applyAlbum)
            {
                if (!databaseFormat)
                {
                    printf("\nRecommended \"Album\" dB change for all files: %f\n", dBchange);
                    printf("Recommended \"Album\" mp3 gain change for all files: %d\n", intGainChange);
                    for (int argi = fileStart; argi < argc; argi++)
                    {
                        if (fileok[argi])
                            if (tagInfo[argi].trackPeak * (Float_t)(pow(2.0, (double)(intGainChange) / 4.0)) > 1.0)
                            {
                                printf("WARNING: with this global gain change, some clipping may occur in file %s\n", argv[argi]);
                            }
                    }
                }
            }
            else
            {
                /*MAA*/
                if (autoClip)
                {
                    int intMaxNoClipGain = (int)(floor(-4.0 * log10(maxmaxsample) / log10(2.0)));
                    if (intGainChange > intMaxNoClipGain)
                    {
                        fprintf(stdout,
                                "Applying auto-clipped mp3 gain change of %d to album\n"
                                "(Original suggested gain was %d)\n",
                                intMaxNoClipGain,
                                intGainChange);
                        intGainChange = intMaxNoClipGain;

                    }

                }
                for (int argi = fileStart; argi < argc; argi++)
                {
                    if (fileok[argi])
                    {
                        goAhead = true;
                        if (intGainChange == 0)
                        {
                            printf("\nNo changes to %s are necessary\n", argv[argi]);
                            if (!gSkipTag && tagInfo[argi].dirty)
                            {
                                printf("...but tag needs update: Writing tag information for %s\n", argv[argi]);
                                WriteMP3GainTag(argv[argi], tagInfo + argi, fileTags + argi, gSaveTime);
                            }
                        }
                        else
                        {
                            if (!ignoreClipWarning)
                            {
                                if (tagInfo[argi].trackPeak * (Float_t)(pow(2.0, (double)(intGainChange) / 4.0)) > 1.0)
                                {
                                    goAhead = queryUserForClipping(argv[argi], intGainChange);
                                }
                            }
                            if (goAhead)
                            {
                                if (!gQuiet)
                                {
                                    printf("Applying mp3 gain change of %d to %s...\n", intGainChange, argv[argi]);
                                }
                                if (gSkipTag)
                                {
                                    changeGain(argv[argi], intGainChange, intGainChange);
                                }
                                else
                                {
                                    changeGainAndTag(argv[argi], intGainChange, intGainChange, tagInfo + argi,
                                                     fileTags + argi);
                                }
                            }
                            else if (!gSkipTag && tagInfo[argi].dirty)
                            {
                                printf("Writing tag information for %s\n", argv[argi]);
                                WriteMP3GainTag(argv[argi], tagInfo + argi, fileTags + argi, gSaveTime);
                            }
                        }
                    }
                }
            }
        }
    }

    /* update file tags */
    if (!applyTrack &&
        !applyAlbum &&
        !directGain &&
        !directSingleChannelGain &&
        !gDeleteTag &&
        !gSkipTag &&
        !gCheckTagOnly)
    {
        /* if we made changes, we already updated the tags */
        for (int argi = fileStart; argi < argc; argi++)
        {
            if (fileok[argi])
            {
                if (tagInfo[argi].dirty)
                {
                    WriteMP3GainTag(argv[argi], tagInfo + argi, fileTags + argi, gSaveTime);
                }
            }
        }
    }

    free(tagInfo);
    free(fileok);
    for (int argi = fileStart; argi < argc; argi++)
    {
        if (fileTags[argi].apeTag)
        {
            if (fileTags[argi].apeTag->otherFields)
            {
                free(fileTags[argi].apeTag->otherFields);
            }
            free(fileTags[argi].apeTag);
        }
        if (fileTags[argi].lyrics3tag)
        {
            free(fileTags[argi].lyrics3tag);
        }
        if (fileTags[argi].id31tag)
        {
            free(fileTags[argi].id31tag);
        }
    }
    free(fileTags);

    if (!gSuccess)
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
