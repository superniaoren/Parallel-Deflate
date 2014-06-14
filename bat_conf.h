#ifndef _BAT_CONF_H
#define _BAT_CONF_H

#ifndef local
#define local static
#endif

typedef unsigned char	Byte;
typedef Byte	Bytef;
typedef unsigned int	uInt;	
typedef unsigned long	uLong;

typedef unsigned char	uch;
typedef uch 		uchf;
typedef unsigned short	ush;
typedef ush ushf;
typedef unsigned long	ulg;
typedef char 		charf;
typedef int		intf;

//the 3 kinds of block type
#define STORED_BLOCK	0
#define STATIC_TREES	1
#define DYN_TREES	2

//the minimum and maximum match lengths
//这里的MAX_MATCH不是搜索操作中返回数值，find-longest-match中的
//返回长度数值不设限制；只需要根据这里的MAX_MATCH进行分割即可；
#define BAT_MIN_MATCH	4
#define BAT_MAX_MATCH	259		/*暂且沿用deflate当中数值，以防制表出错*/

#define BAT_BINARY   0
#define BAT_TEXT     1
#define BAT_ASCII    BAT_TEXT   /* for compatibility with 1.2.2 and earlier */
#define BAT_UNKNOWN  2
/* Possible values of the data_type field (though see inflate()) */

#define BAT_FILTERED            1
#define BAT_HUFFMAN_ONLY        2
#define BAT_RLE                 3
#define BAT_FIXED               4
#define BAT_DEFAULT_STRATEGY    0
/* compression strategy; see deflateInit2() below for details */

#endif	//_BAT_CONF_H
