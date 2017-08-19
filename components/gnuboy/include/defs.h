


#ifndef __DEFS_H__
#define __DEFS_H__

#include <stdint.h>


#ifdef IS_LITTLE_ENDIAN
#define LO 0
#define HI 1
#else
#define LO 1
#define HI 0
#endif


typedef uint8_t byte;

typedef uint8_t un8;
typedef uint16_t un16;
typedef uint32_t un32;

typedef int8_t n8;
typedef int16_t n16;
typedef int32_t n32;

typedef un16 word;
typedef word addr;

/* stuff from main.c ... */
void die(char *fmt, ...);


#endif

