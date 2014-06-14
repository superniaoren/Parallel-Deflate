/* bat_inffast.c -- fast decoding
 * Mar. 25th. 2014
 */
#include "bat_zlz.h"
#include "bat_infextra.h"
#include "bat_inflate.h"
#include <stdio.h>

#ifndef ASMINF

/* Allow machine dependent optimization for post-increment or pre-increment.
   Based on testing to date,
   Pre-increment preferred for:
   - PowerPC G3 (Adler)
   - MIPS R5000 (Randers-Pehrson)
   Post-increment preferred for:
   - none
   No measurable difference:
   - Pentium III (Anderson)
   - M68060 (Nikl)
 */
#  define OFF 0
#  define PUP(a) *(a)++

/*
   Decode literal, length, and distance codes and write out the resulting
   literal and match bytes until either not enough input or output is
   available, an end-of-block is encountered, or a data error is encountered.
   When large enough input and output buffers are supplied to inflate(), for
   example, a 16K input buffer and a 64K output buffer, more than 95% of the
   inflate execution time is spent in this routine.

   Entry assumptions:

        state->mode == LEN
        strm->avail_in >= 6
        strm->avail_out >= 258
        start >= strm->avail_out
        state->bits < 8

   On return, state->mode is one of:

        LEN -- ran out of enough output space or enough available input
        TYPE -- reached end of block code, inflate() to interpret next block
        BAD -- error in block data

   Notes:

    - The maximum input bits used by a length/distance pair is 15 bits for the
      length code, 5 bits for the length extra, 15 bits for the distance code,
      and 13 bits for the distance extra.  This totals 48 bits, or six bytes.
      Therefore if strm->avail_in >= 6, then there is enough input to avoid
      checking for available input while decoding.

    - The maximum bytes that a single length/distance pair can output is 258
      bytes, which is the maximum length that can be coded.  inflate_fast()
      requires strm->avail_out >= 258 for each loop to avoid checking for
      output space.
    @param start :  inflate()'s starting value for strm->avail_out
 */
//void inflate_fast_core(ht_streamp strm, unsigned start)
void inflate_fast_core(bat_streamp strm, void* state_void)
{
#ifdef ZEBUG
printf("----------[ZOO: use inflate_fast_core sub routine]----------\n");
#endif
	inflate_state *state = (inflate_state *)state_void;
//	struct inflate_state_s *state;
	unsigned char *in;      /* local strm->next_in */
	unsigned char *last;    /* have enough input while in < last */
	unsigned char *out;     /* local strm->next_out */
	unsigned char *end;     /* while out < end, enough space available */
	unsigned char *beg;     /* inflate()'s initial strm->next_out */
#ifdef INFLATE_STRICT
#endif
	unsigned wsize;             /* window size or zero if not using window */
	unsigned whave;             /* valid bytes in the window */
	unsigned wnext;             /* window write index */
	unsigned char *window;  /* allocated sliding window, if wsize != 0 */
    unsigned long hold;         /* local strm->hold */
    unsigned bits;              /* local strm->bits */
    code const *lcode;      /* local strm->lencode */
    code const *dcode;      /* local strm->distcode */
    unsigned lmask;             /* mask for first level of length codes */
    unsigned dmask;             /* mask for first level of distance codes */
    code here;                  /* retrieved table entry */
    unsigned op;                /* code bits, operation, extra bits, or */
                                /*  window position, window bytes to copy */
	unsigned len;               /* match length, unused bytes */
	unsigned dist;              /* match distance */
	unsigned char *from;    /* where to copy match from */

	/* copy state to local variables */
//	state = (struct inflate_state_s *)strm->state;
	in = state->input - OFF;
	last = in + (state->avail_in - 5);
	out = state->output - OFF;
	end = out + strm->compr_size; 
	beg = state->output;
//    end = out + (strm->avail_out - 257);
//    beg = out - (start - strm->avail_out);
#ifdef INFLATE_STRICT
#endif
    wsize = state->wsize;
    whave = state->whave;
    wnext = state->wnext;
    window = state->window;
	hold = state->hold;
	bits = state->bits;
	lcode = state->lencode;
	dcode = state->distcode;
	lmask = (1U << state->lenbits) - 1;
	dmask = (1U << state->distbits) - 1;
#ifdef ZEBUG
//if(state->lenbits != 9 || state->distbits != 6) 
 printf("line:%d, lenbits=%d, distbits=%d,\n",__LINE__, state->lenbits, state->distbits);
#endif

    /* decode literals and length/distances until end-of-block or not enough
       input data or output space */
	do {
#ifdef ZEBUG
printf("++++++++++avail0out = %d [compr-size=%d] ========\n", state->avail_out + (unsigned)(out-beg), strm->compr_size);
#endif
		if (bits < 15) {
			hold += (unsigned long)(PUP(in)) << bits;
			bits += 8;
			hold += (unsigned long)(PUP(in)) << bits;
			bits += 8;
		}
		here = lcode[hold & lmask];	//[Zoo]如此确定了here ??? yeah
	dolen:
		op = (unsigned)(here.bits);
		hold >>= op;
		bits -= op;
		op = (unsigned)(here.op);
		if (op == 0) {                          /* literal */
//            Tracevv((stderr, here.val >= 0x20 && here.val < 0x7f ?
//                    "inflate:         literal '%c'\n" :
//                    "inflate:         literal 0x%02x\n", here.val));
#ifdef ZEBUG
if(here.val >= 0x20 && here.val < 0x7f)
printf("inflate:	literal '%c'\n", here.val);
else
printf("inflate:	literal 0x%02x\n", here.val);
#endif
			PUP(out) = (unsigned char)(here.val);
		}
		else if (op & 16) {                     /* length base */
			len = (unsigned)(here.val);
			op &= 15;                           /* number of extra bits */
#ifdef ZEBUG
printf("inflate:	len_base=%3u, ", len);
#endif
			if (op) {
				if (bits < op) {
					hold += (unsigned long)(PUP(in)) << bits;
					bits += 8;
				}
				len += (unsigned)hold & ((1U << op) - 1);
#ifdef ZEBUG
printf("len_extra=%d, op=%d, ", (unsigned)hold & ((1U<<op)-1), op);
#endif
				hold >>= op;
				bits -= op;
			}
//            Tracevv((stderr, "inflate:         length %u\n", len));
			if (bits < 15) {
				hold += (unsigned long)(PUP(in)) << bits;
				bits += 8;
				hold += (unsigned long)(PUP(in)) << bits;
				bits += 8;
			}
			here = dcode[hold & dmask];
	dodist:
			op = (unsigned)(here.bits);
			hold >>= op;
			bits -= op;
			op = (unsigned)(here.op);
			if (op & 16) {                      /* distance base */
				dist = (unsigned)(here.val);
				op &= 15;                   /* number of extra bits */
				if (bits < op) {
					hold += (unsigned long)(PUP(in)) << bits;
					bits += 8;
					if (bits < op) {
						hold += (unsigned long)(PUP(in)) << bits;
						bits += 8;
					}
				}
				dist += (unsigned)hold & ((1U << op) - 1);
				hold >>= op;
				bits -= op;
//                Tracevv((stderr, "inflate:         distance %u\n", dist));
#ifdef ZEBUG
printf("   (off=%5u,len=%3u)\n",dist, len);
#endif

#if 1
				from = out - dist;
				do{	//[Zoo] the simplest way, not speed-efficient
					PUP(out) = PUP(from);
				}while(--len);
#else
                op = (unsigned)(out - beg);     /* max distance in output */
                if (dist > op) {                /* see if copy from window */
                    op = dist - op;             /* distance back in window */
                    if (op > whave) {
                        if (state->sane) {
//                            strm->msg = (char *)"invalid distance too far back";
                            state->mode = BAD;
                            break;
                        }
#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
#endif
                    }
                    from = window - OFF;
                    if (wnext == 0) {           /* very common case */
                        from += wsize - op;
                        if (op < len) {         /* some from window */
                            len -= op;
                            do {
                                PUP(out) = PUP(from);
                            } while (--op);
                            from = out - dist;  /* rest from output */
                        }
                    }
                    else if (wnext < op) {      /* wrap around window */
                        from += wsize + wnext - op;
                        op -= wnext;
                        if (op < len) {         /* some from end of window */
                            len -= op;
                            do {
                                PUP(out) = PUP(from);
                            } while (--op);
                            from = window - OFF;
                            if (wnext < len) {  /* some from start of window */
                                op = wnext;
                                len -= op;
                                do {
                                    PUP(out) = PUP(from);
                                } while (--op);
                                from = out - dist;      /* rest from output */
                            }
                        }
                    }
                    else {                      /* contiguous in window */
                        from += wnext - op;
                        if (op < len) {         /* some from window */
                            len -= op;
                            do {
                                PUP(out) = PUP(from);
                            } while (--op);
                            from = out - dist;  /* rest from output */
                        }
                    }
                    while (len > 2) {
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        len -= 3;
                    }
                    if (len) {
                        PUP(out) = PUP(from);
                        if (len > 1)
                            PUP(out) = PUP(from);
                    }
                }
                else {
                    from = out - dist;          /* copy direct from output */
                    do {                        /* minimum length is three */
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        len -= 3;
                    } while (len > 2);
                    if (len) {
                        PUP(out) = PUP(from);
                        if (len > 1)
                            PUP(out) = PUP(from);
                    }
                }
#endif
			}
			else if ((op & 64) == 0) {          /* 2nd level distance code */
				here = dcode[here.val + (hold & ((1U << op) - 1))];
				goto dodist;
			}
			else {
		//                strm->msg = (char *)"invalid distance code";
				state->mode = BAD;
				break;
			}
		}
		else if ((op & 64) == 0) {              /* 2nd level length code */
			here = lcode[here.val + (hold & ((1U << op) - 1))];
			goto dolen;
		}
		else if (op & 32) {                     /* end-of-block */
//            Tracevv((stderr, "inflate:         end of block\n"));
			state->mode = CHECK;	//reach the end of current block
		//	state->mode = TYPE;	//reach the end of current block
		//	state->mode = DONE;	//the current block is still not done, left adler.
#ifdef ZEBUG
printf("inflate:         END-OF block\n");
#endif
			break;
		}
		else {
//            strm->msg = (char *)"invalid literal/length code";
			state->mode = BAD;
			break;
		}
	} while (in < last && out < end);

	/* return unused bytes (on entry, bits < 8, so in won't go too far back) */
	len = bits >> 3;
	in -= len;
	bits -= len << 3;
	hold &= (1U << bits) - 1;
	
	/* update state and return */
	state->input = in + OFF;
	state->output = out + OFF;
	state->avail_in = (unsigned)(in < last ? 5 + (last - in) : 5 - (in - last));
	state->avail_out += (unsigned)(out - beg);
	state->hold = hold;
	state->bits = bits;
	return;
}

/*
   inflate_fast() speedups that turned out slower (on a PowerPC G3 750CXe):
   - Using bit fields for code structure
   - Different op definition to avoid & for extra bits (do & for table bits)
   - Three separate decoding do-loops for direct, window, and wnext == 0
   - Special case for distance > 1 copies to do overlapped load and store copy
   - Explicit branch predictions (based on measured branch probabilities)
   - Deferring match copy and interspersed it with decoding subsequent codes
   - Swapping literal/length else
   - Swapping window/direct else
   - Larger unrolled copy loops (three is about right)
   - Moving len -= 3 statement into middle of loop
 */

#endif /* !ASMINF */
