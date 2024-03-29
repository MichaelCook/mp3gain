#include "mpglibDBL_encoder.h"

struct buf
{
    unsigned char *pnt;
    long size;
    long pos;
    struct buf *next;
    struct buf *prev;
};

struct framebuf
{
    struct buf *buf;
    long pos;
    struct frame *next;
    struct frame *prev;
};

typedef struct mpstr_tag
{
    struct buf *head, *tail;
    int vbr_header;               /* 1 if valid Xing vbr header detected */
    int num_frames;               /* set if vbr header present */
    int header_parsed;
    int side_parsed;
    int data_parsed;
    int free_format;             /* 1 = free format frame */
    int old_free_format;        /* 1 = last frame was free format */
    int bsize;
    int framesize;
    int ssize;
    int dsize;
    int fsizeold;
    int fsizeold_nopadding;
    struct frame fr;
    unsigned char bsspace[2][MAXFRAMESIZE + 512]; /* MAXFRAMESIZE */
    double hybrid_block[2][2][SBLIMIT * SSLIMIT];
    int hybrid_blc[2];
    unsigned long header;
    int bsnum;
    double synth_buffs[2][2][0x110];
    int  synth_bo;
    int  sync_bitstream;

} MPSTR, *PMPSTR;

#define MP3_ERR -1
#define MP3_OK  0
#define MP3_NEED_MORE 1
