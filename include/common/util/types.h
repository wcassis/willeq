#pragma once

#include <cstdint>

typedef uint8_t byte;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned char uchar;

#ifdef _WINDOWS
	#if (!defined(_MSC_VER) || (defined(_MSC_VER) && _MSC_VER < 1900))
		#define snprintf	_snprintf
	#endif
	#define strncasecmp	_strnicmp
	#define strcasecmp	_stricmp
#endif

#define safe_delete(d) if(d) { delete d; d=nullptr; }
#define safe_delete_array(d) if(d) { delete[] d; d=nullptr; }

#ifndef WIN32
// More WIN32 compatability
	typedef unsigned long DWORD;
	typedef unsigned char BYTE;
	typedef char CHAR;
	typedef unsigned short WORD;
	typedef float FLOAT;
	typedef FLOAT *PFLOAT;
	typedef BYTE *PBYTE,*LPBYTE;
	typedef int *PINT,*LPINT;
	typedef WORD *PWORD,*LPWORD;
	typedef long *LPLONG, LONG;
	typedef DWORD *PDWORD,*LPDWORD;
	typedef int INT;
	typedef unsigned int UINT,*PUINT,*LPUINT;
#endif

// htonll and ntohll already defined on windows
#ifndef WIN32
#	if defined(__linux__)
#		include <endian.h>
#	elif defined(__FreeBSD__) || defined(__NetBSD__)
#		include <sys/endian.h>
#	elif defined (__OpenBSD__)
#		include <sys/types.h>
#		define be16toh(x) betoh16(x)
#		define be32toh(x) betoh32(x)
#		define be64toh(x) betoh64(x)
#	endif
#	define htonll(x) htobe64(x)
#	define ntohll(x) be64toh(x)
#endif
