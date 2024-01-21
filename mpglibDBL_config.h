/* The number of bytes in a double.  */
#define SIZEOF_DOUBLE 8

/* The number of bytes in a float.  */
#define SIZEOF_FLOAT 4

/* The number of bytes in a int.  */
#define SIZEOF_INT 4

/* The number of bytes in a long.  */
#define SIZEOF_LONG 4

/* The number of bytes in a long double.  */
#define SIZEOF_LONG_DOUBLE 12

/* The number of bytes in a short.  */
#define SIZEOF_SHORT 2

/* The number of bytes in a unsigned int.  */
#define SIZEOF_UNSIGNED_INT 4

/* The number of bytes in a unsigned long.  */
#define SIZEOF_UNSIGNED_LONG 4

/* The number of bytes in a unsigned short.  */
#define SIZEOF_UNSIGNED_SHORT 2

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H

/* Name of package */
#define PACKAGE "lame"

/* Version number of package */
#define VERSION "3.90"

/* Define if compiler has function prototypes */
#define PROTOTYPES 1

/* enable VBR bitrate histogram */
#define BRHIST 1

/* IEEE754 compatible machine */
#define TAKEHIRO_IEEE754_HACK 1

/* #define HAVE_STRCHR */

typedef long double ieee854_float80_t;
typedef double      ieee754_float64_t;
typedef float       ieee754_float32_t;

#define LAME_LIBRARY_BUILD
