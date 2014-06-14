/*
 *  bat_inflate.h -- internal uncompression state
 *  Mar. 25th. 2014
 */
#ifndef _BAT_INFLATE_H
#define _BAT_INFLATE_H

#include "bat_conf.h"
#include "bat_zlz.h"
#include "bat_infextra.h"	//should follow the zlz.h

#ifndef NO_GZIP
#define NO_GZIP
#endif

#ifndef NO_GZIP
#define GUNZIP
#endif

//possible inflate modes between infalte() calls
typedef enum inflate_mode_e{
    HEAD,       /* i: waiting for magic header */
    FLAGS,      /* i: waiting for method and flags (gzip) */
    TIME,       /* i: waiting for modification time (gzip) */
    OS,         /* i: waiting for extra flags and operating system (gzip) */
    EXLEN,      /* i: waiting for extra length (gzip) */
    EXTRA,      /* i: waiting for extra bytes (gzip) */
    NAME,       /* i: waiting for end of file name (gzip) */
    COMMENT,    /* i: waiting for end of comment (gzip) */
    HCRC,       /* i: waiting for header crc (gzip) */
    DICTID,     /* i: waiting for dictionary check value */
    DICT,       /* waiting for inflateSetDictionary() call */
        TYPE,       /* i: waiting for type bits, including last-flag bit */
        TYPEDO,     /* i: same, but skip check to exit inflate on new block */
        STORED,     /* i: waiting for stored size (length and complement) */
        COPY_,      /* i/o: same as COPY below, but only first time in */
        COPY,       /* i/o: waiting for input or output to copy stored block */
        TABLE,      /* i: waiting for dynamic block table lengths */
        LENLENS,    /* i: waiting for code length code lengths */
        CODELENS,   /* i: waiting for length/lit and distance code lengths */
            LEN_,       /* i: same as LEN below, but only first time in */
            LEN,        /* i: waiting for length/lit/eob code */
            LENEXT,     /* i: waiting for length extra bits */
            DIST,       /* i: waiting for distance code */
            DISTEXT,    /* i: waiting for distance extra bits */
            MATCH,      /* o: waiting for output space to copy string */
            LIT,        /* o: waiting for output space to write literal */
    CHECK,      /* i: waiting for 32-bit check value */
    LENGTH,     /* i: waiting for 32-bit length (gzip) */
    DONE,       /* finished check, done -- remain here until reset */
    BAD,        /* got a data error -- remain here until reset */
    MEM,        /* got an inflate() memory error -- remain here until reset */
    SYNC        /* looking for synchronization bytes to restart inflate() */
} inflate_mode;

/*
	state transitions between above modes -
	(most modes can go to BAD or MEM on error -- not shown for clarity)
	
	Process header:
		HEAD	-> (gzip) or (zlib) or (raw)
		(gzip)	-> FLAGS->TIME->OS->EXLEN->EXTRA->NAME->COMMENT->HCRC->TYPE
		(zlib)	-> DICTID or TYPE
		DICTID	-> DITC	-> TYPE
		(raw)	-> TYPEDO
	Read deflate blocks:
		TYPE	-> TYPEDO -> STORED or TABLE or LEN_ or CHECK
		STORED	-> COPY_ -> COPY -> TYPE
		TABLE	-> LENLENS -> CODELENGS -> LEN_
		LEN_	-> LEN
	Read deflate codes in fixed or dynamic block:
		LEN	-> LENEXT or LIT or TYPE
		LENEXT	-> DIST -> DISTEXT -> MATCH -> LEN
		LIT	-> LEN
	Process trailer:
		CHECK	-> LENGTH -> DONE
 */

//state maintained bwtween infalte() calls. Approximately 10K bytes. 
//This would NOT be allowed in GPU-implementations
typedef struct inflate_state_s{
	unsigned char *input;	/* input buffer for compressed data */
	unsigned char *output;	/* output buffer for plain text */
	int avail_in;
	int avail_out;		
	unsigned long adler;

	inflate_mode mode;          /* current inflate mode */
	int last;                   /* true if processing last block */
    int wrap;                   /* bit 0 true for zlib, bit 1 true for gzip */
    int havedict;               /* true if dictionary provided */
    int flags;                  /* gzip header method and flags (0 if zlib) */
    unsigned dmax;              /* zlib header max distance (INFLATE_STRICT) */
    unsigned long check;        /* protected copy of check value */
    unsigned long total;        /* protected copy of output count */
//	gz_headerp head;            /* where to save gzip header information */
        /* sliding window */
    unsigned wbits;             /* log base 2 of requested window size */
    unsigned wsize;             /* window size or zero if not using window */
    unsigned whave;             /* valid bytes in the window */
    unsigned wnext;             /* window write index */
    unsigned char *window;  /* allocated sliding window, if needed */
        /* bit accumulator */
	unsigned long hold;         /* input bit accumulator */
	unsigned bits;              /* number of bits in "in" */
        /* for string and stored block copying */
	unsigned length;            /* literal or length of data to copy */
	unsigned offset;            /* distance back to copy string from */
		/* for table and code decoding */
	unsigned extra;		/* extra bits needed */
		/* fixed and dynamic code tables */
	code const *lencode;	/* starting table for length/literal codes */
	code const *distcode;	/* starting table for distance codes */
	unsigned lenbits;	/* index bits for lencode */
	unsigned distbits;	/* index bits for distcode */
		/* dynamic table building */
	unsigned ncode;		/* number of code length code lengths */
	unsigned nlen;		/* number of length code lengths */
	unsigned ndist;		/* number of distance code lengths */
	unsigned have;		/* number of code lengths in lens[] */
	code *next;		/* next available space in codes[] */
	unsigned short lens[320];	/* temporary storage for code lengths */
	unsigned short work[288];	/* work area for code table building */
	code codes[ENOUGH];		/* space for code tables */
	int sane;			/* if false, allow invalid distance too far */
	int back;			/* bits back of last unprocessed length/lit */
	unsigned was;			/* initial length of match */	
}inflate_state;


#endif	//_HT_INFLATE_H
