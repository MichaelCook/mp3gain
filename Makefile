SOURCES = \
 mp3gain.c \
 apetag.c \
 id3tag.c \
 gain_analysis.c \
 rg_error.c \
 mpglibDBL/common.c \
 mpglibDBL/dct64_i386.c \
 mpglibDBL/decode_i386.c \
 mpglibDBL/interface.c \
 mpglibDBL/layer3.c \
 mpglibDBL/tabinit.c \

.PHONY: test
test: mp3gain
	./test

mp3gain: $(SOURCES)
	gcc -Wall -O3 -DHAVE_MEMCPY -s -o mp3gain $(SOURCES) -lm
