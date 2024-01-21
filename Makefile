SOURCES = \
 mp3gain.c \
 apetag.c \
 id3tag.c \
 gain_analysis.c \
 rg_error.c \
 mpglibDBL_common.c \
 mpglibDBL_dct64_i386.c \
 mpglibDBL_decode_i386.c \
 mpglibDBL_interface.c \
 mpglibDBL_layer3.c \
 mpglibDBL_tabinit.c \

.PHONY: test
test: mp3gain
	./test

mp3gain: $(SOURCES)
	astyle --options=.astylerc $(SOURCES)
	gcc -Wall -Werror -O3 -s -o mp3gain $(SOURCES) -lm
