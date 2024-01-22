SOURCES = \
 mp3gain.c \
 apetag.c \
 id3tag.c \
 gain_analysis.c \
 mpglibDBL_common.c \
 mpglibDBL_dct64_i386.c \
 mpglibDBL_decode_i386.c \
 mpglibDBL_interface.c \
 mpglibDBL_layer3.c \
 mpglibDBL_tabinit.c \

HEADERS = \
 apetag.h \
 gain_analysis.h \
 id3tag.h \
 mp3gain.h \
 mpglibDBL_common.h \
 mpglibDBL_config.h \
 mpglibDBL_dct64_i386.h \
 mpglibDBL_decode_i386.h \
 mpglibDBL_encoder.h \
 mpglibDBL_huffman.h \
 mpglibDBL_interface.h \
 mpglibDBL_lame.h \
 mpglibDBL_layer3.h \
 mpglibDBL_machine.h \
 mpglibDBL_mpg123.h \
 mpglibDBL_mpglib.h \
 mpglibDBL_tabinit.h \
 mpglibDBL_VbrTag.h \

CFLAGS = -O3 -s

.PHONY: test
test: mp3gain
	./test

mp3gain: $(SOURCES) $(HEADERS)
	astyle --options=.astylerc $(SOURCES) $(HEADERS)
	gcc -Wall -Werror $(CFLAGS) -o mp3gain $(SOURCES) -lm
