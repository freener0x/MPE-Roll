/* LAME build configuration for MPE Roll (the library only: no frontend, no decoder, no assembly).
   LAME's own configure script writes this file on Unix and configMS.h is its Windows version; this
   one works for every compiler MPE Roll is built with. */
#ifndef MPEROLL_LAME_CONFIG_H
#define MPEROLL_LAME_CONFIG_H

#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1
#define PACKAGE "lame"
#define LAME_LIBRARY_BUILD 1

#include <stdint.h>

typedef long double ieee854_float80_t;
typedef double ieee754_float64_t;
typedef float ieee754_float32_t;

#endif
