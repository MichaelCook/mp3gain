#pragma once

int ReadMP3GainID3Tag(char *filename, struct MP3GainTagInfo *info);

int WriteMP3GainID3Tag(char *filename, struct MP3GainTagInfo *info, int saveTimeStamp);

int RemoveMP3GainID3Tag(char *filename, int saveTimeStamp);
