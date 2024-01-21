#pragma once

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#define         SBLIMIT                 32
#define         SSLIMIT                 18

#define         MPG_MD_STEREO           0
#define         MPG_MD_JOINT_STEREO     1
#define         MPG_MD_DUAL_CHANNEL     2
#define         MPG_MD_MONO             3

#define MAXFRAMESIZE 1792

/* AF: ADDED FOR LAYER1/LAYER2 */
#define         SCALE_BLOCK             12

/* Pre Shift fo 16 to 8 bit converter table */
#define AUSHIFT (3)

struct frame {
    int stereo;
    int jsbound;
    int single;
    int lsf;
    int mpeg25;
    int header_change;
    int lay;
    int error_protection;
    int bitrate_index;
    int sampling_frequency;
    int padding;
    int extension;
    int mode;
    int mode_ext;
    int copyright;
    int original;
    int emphasis;
    int framesize; /* computed framesize */
};

struct gr_info_s {
      int scfsi;
      unsigned part2_3_length;
      unsigned big_values;
      unsigned scalefac_compress;
      unsigned block_type;
      unsigned mixed_block_flag;
      unsigned table_select[3];
      unsigned subblock_gain[3];
      unsigned maxband[3];
      unsigned maxbandl;
      unsigned maxb;
      unsigned region1start;
      unsigned region2start;
      unsigned preflag;
      unsigned scalefac_scale;
      unsigned count1table_select;
      double *full_gain[3];
      double *pow2gain;
};

struct III_sideinfo
{
  unsigned main_data_begin;
  unsigned private_bits;
  struct {
    struct gr_info_s gr[2];
  } ch[2];
};
