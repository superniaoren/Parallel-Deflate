/*
 *  bat_inflate.c 
 *  ------------
 *  Mar. 25th. 2104
 */
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include "bat_inflate.h"    //provide bat_zlz.h, should before bat_infextra.h

//**************************************************************//
//			functions declarations			//
//**************************************************************//
local int  bat_inflateInit_stream(bat_streamp strm, int ifd, int ofd, int insize);
local int  bat_inflateInit_state(bat_streamp strm);
local int  bat_inflate_state_reset(inflate_state *state);

local void bat_inflateEnd_stream(bat_streamp strm);
local void bat_inflateEnd_state(bat_streamp strm);

local int  bat_uncompress(bat_streamp strm);

local int  bat_inflate_core(bat_streamp strm, inflate_state *s, int flush);

//**************************************************************//
local int bat_inflateInit_stream(bat_streamp strm, int ifd, int ofd, int insize){
	if(strm == BAT_NULL)
		return BAT_STREAM_ERROR;
	//reset stream and state context
	assert(insize > 0);
	strm->ifd = ifd;
	strm->ofd = ofd;
	strm->total_in = insize;
	strm->total_out = 0;
	strm->data_type = BAT_UNKNOWN;
	//allocate the input buffer and output buffer
	//here, when a outbuf is full, the plain-text should be flushed.
	//at first, all the compressed data is load into inbuf
	int block_size = BAT_BlockSize;
	int compr_size = BAT_predict_comp_len(block_size);
//	unsigned char *inbuf = (unsigned char*)malloc( compr_size * sizeof(unsigned char) );
	unsigned char *inbuf = (unsigned char*)malloc( insize * sizeof(unsigned char) );
	assert(inbuf != NULL);
	unsigned char *outbuf =(unsigned char*)malloc( block_size * sizeof(unsigned char) );
	assert(outbuf != NULL);
	//unsigned int *hash_table;
	strm->block_size	= block_size;
	strm->compr_size	= compr_size;
	strm->all_in_buf	= inbuf;
	strm->all_out_buf	= outbuf;
	
	return BAT_OK;
}

local int bat_inflateInit_state(bat_streamp strm){
	if(strm == BAT_NULL)
		return BAT_STREAM_ERROR;
	//allocate inflate state, 这里沿用了之前的做法，heap上的state，而不是stack中的
	struct inflate_state_s *state;
	state = (struct inflate_state_s *)calloc(1, sizeof(struct inflate_state_s));
	if(state == BAT_NULL) return BAT_MEM_ERROR;
	//force to convert to internal state!
	strm->inflate = (struct internal_state *)state;
	//init the key buffer pointers
	state->input = strm->all_in_buf;	//未来的对于inflate的扩展和映射就主要在此
	state->output = strm->all_out_buf;	//未来的对于inflate的扩展和映射就主要在此
	state->avail_in = strm->total_in;
	state->avail_out = 0;
	//very very important initializations HERE
#if 1
		/* sliding window */
	state->window = BAT_NULL;
	int windowBits = BAT_BlockLog;
	int wrap = (windowBits >> 4) + 1;
	if(windowBits < 8 || windowBits > 15)
		return BAT_STREAM_ERROR;
	state->wrap = wrap;	//bit 0 true for zlib, bit 1 true for gzip
	state->wbits = (unsigned)windowBits;
	state->wsize = 0;
	state->whave = 0;
	state->wnext = 0;
		/* common configurations */
	int ret = bat_inflate_state_reset(state);
#endif	
	return ret;
}

local int bat_inflate_state_reset(inflate_state * state){
		/* common configurations */
	state->mode = HEAD;	//current inflate mode
	if(state->wrap)		//normally, adler is initialized 1 here.
		state->adler = state->wrap & 1;	//if disable wrap, U can: adler = 1L;
	state->last = 0;	//true if processing last block
	state->total = 0;	//protected copy of output count
	state->dmax = 32768U;	//32K, zlib header max distance
	state->havedict = 0;	//true if dictionary provided
		/* bit accumulator  */
	state->hold = 0L;	//input bit accumulator
	state->bits = 0;	//number of bits in  'in'
		/* fixed and dynamic code tables  */
	state->lencode = state->distcode = state->next = state->codes;
		/* dynamic table building */
	state->sane = 1;	//if false, allow invalid distance too far
	state->back = -1;	//bits back of last uncompressed length/lit
	return BAT_OK;
}

//**************************************************************//
local void bat_inflateEnd_stream(bat_streamp strm){
	if(strm == BAT_NULL || strm->inflate == BAT_NULL)
		return;
	if(strm->all_in_buf){
		free(strm->all_in_buf);	strm->all_in_buf = BAT_NULL;
	}
	if(strm->all_out_buf){
		free(strm->all_out_buf);	strm->all_out_buf = BAT_NULL;
	}
	if(strm->ifd != -1)  close(strm->ifd);
	if(strm->ofd != -1)  close(strm->ofd);
	return;
}

local void bat_inflateEnd_state(bat_streamp strm){
	struct inflate_state_s *state = (struct inflate_state_s *)strm->inflate;
	if(strm != BAT_NULL && strm->inflate != BAT_NULL){
		if(state->window != BAT_NULL)
			free(state->window);
		free(strm->inflate);
		strm->inflate = BAT_NULL;
	}
	return;
}

//--------------------------------------------------------------//
//load registers with state in inflate_core() for speed
#define LOAD()				\
	do{				\
		put = state->output;	\
		left = state->avail_out;\
		next = state->input;	\
		have = state->avail_in;	\
		hold = state->hold;	\
		bits = state->bits;	\
	} while (0)

//restore state from registers in inflate()
#define RESTORE()			\
	do{				\
		state->output = put;	\
		state->avail_out = left;\
		state->input = next;	\
		state->avail_in= have;	\
		state->hold = hold;	\
		state->bits = bits;	\
	}while(0)

//Assure there are at least n bits in the bit accumulator.
//If there not enough available input, just return;
#define NEEDBITS(n)			\
	do{				\
		while(bits < (unsigned)(n))\
			PULLBYTE();	\
	} while (0)

//Get a byte of input into the bit accumulator, or return
//from inflate_core(), if there no input available.
#define PULLBYTE()			\
	do{ 				\
		if(have==0)		\
			goto inf_leave;	\
		have--;			\
		hold += (unsigned long)(*next++) << bits; \
		bits += 8;		\
	} while (0)

//Return the low n bits of the bit accumulator (n < 16)
#define BITS(n)				\
	( (unsigned)hold & ((1U << (n)) - 1) )

//Remove n bits from the bit accumulator
#define DROPBITS(n)			\
	do{				\
		hold >>= (n);		\
		bits -= (unsigned)(n);	\
	} while (0)
		
//clear the input bit accumulator
#define INITBITS()			\
	do { 				\
		hold = 0;		\
		bits = 0;		\
	} while (0)

//remove zero to seven bits as needed arrive at a byte boundary
#define BYTEBITS()			\
	do{				\
		hold >>= bits & 7;	\
		bits -= bits & 7;	\
	}while(0)

#define MSG(S)  printf("line:%d, %s\n",__LINE__, (S))

local int  bat_inflate_core(bat_streamp strm, inflate_state *state, int flush){
	unsigned char *next;		//next input
	unsigned char *put;		//next output
	unsigned long hold;		//bit buffer [CORE]
	unsigned have, left;		//available inpout and output
	unsigned bits;			//bits in bit buffer
	unsigned in, out;		//save starting available input and output
	unsigned char *from;		//where to copy match bytes from
	unsigned len;			//length to copy for repeats, bits to drop
	code here;			//current decoding TABLE ENTRY [CORE]
	code last;			//parent table entry
	unsigned copy;			//number of stored or match bytes to copy
	int ret ;
	static const unsigned short bl_order[19] = //permutation of code lengths
	{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	
	if(strm == BAT_NULL || state==BAT_NULL || state->output==BAT_NULL \
		|| (state->input==BAT_NULL && state->avail_in != 0) )
		return BAT_STREAM_ERROR;
	if(state->mode != HEAD){
		printf("First mode should be HEAD when decode a new BLOCK\n");
		return BAT_DATA_ERROR;
	}
	//load in the data
	LOAD();			//do {} while(0);
	in = have;		//==avail_in initially
	out = left;		//==avail_out initially
	ret = BAT_OK;
	//now enter the main loop
	for( ; ; ){
	switch(state->mode){	//the STATE MATCHINE
		case HEAD:
#ifdef ZEBUG
		MSG("HEAD ->");
#endif
			if(state->wrap == 0){//skip the head procession
				state->mode = TYPEDO;
				break;	//jump to create tables and decode data.
			}
			NEEDBITS(16);	//read the 1st 2 bytes
			if( ( (BITS(8)<<8) + (hold >> 8) ) % 31){
				//incorrect header check
				state->mode = BAD;
				break;
			}
			if( BITS(4) != BAT_DEFLATED ){
				//unknown compression method
				state->mode = BAD;
				break;
			}
			DROPBITS(4);		//drop the BAT_DEFALTED info
			len = BITS(4) + 8;	//recover the w_bits
			if(state->wbits == 0)
				state->wbits = len;
			else if(len > state->wbits){
				//invalid window(hash table) size
				state->mode = BAD;
				break;
			}
			state->dmax = 1U << len;
			//now, the zlib header is parsed successfully.
			state->adler = state->check = BAT_adler32(0L, BAT_NULL, 0);
			//check if the bit: PRESET_DICT  is set [DEPRECATED]
	//		state->mode = hold & 0x200 ? DICTID : TYPE;
			state->mode = TYPE;
			INITBITS();
			break;
		case TYPE:
#ifdef ZEBUG
		MSG("TYPE ->");
#endif
			if(flush != BAT_FINISH) goto inf_leave;
		case TYPEDO:	//type do:
#ifdef ZEBUG
		MSG("TYPEDO ->");
#endif
			if(state->last){	//if process the last block
				BYTEBITS();	
				state->mode = CHECK;
				//TODO  为什么会到达这里,last block为什么要分两个循环进行处理？
#ifdef ZEBUG
printf("line:%d, state->last = %d \n", __LINE__, state->last);
#endif
				break;
			}
			//对应tr_flush_block()当中的操作：
			NEEDBITS(3);	//send_bits(s, (DYN_TREES<<1)+last, 3)
			state->last = BITS(1);
			DROPBITS(1);
#ifdef ZEBUG
printf("TYPEDO : bits(2) = %u, state->last = %d\n", (unsigned)(BITS(2)), state->last);
#endif
			switch(BITS(2)){	//dyn, fixed, stored
			case 0:			//stored block
				state->mode = STORED;
				printf("mode=STORED is not supported now\n");
				goto inf_leave;
			case 1:			//fixed block
				state->mode = LEN_;
				printf("mode FOR fixed table is not supported now\n");
				goto inf_leave;
			case 2:			//dynamic block
				state->mode = TABLE;
				break;
			case 3:
				state->mode = BAD;
			}
			DROPBITS(2);		//now the bits accumulated is cleared.
			break;
		case TABLE:
#ifdef ZEBUG
		MSG("TABLE ->");
#endif
			NEEDBITS(14);		//see: send_all_trees();
			state->nlen = BITS(5) + 257;
			DROPBITS(5);
			state->ndist = BITS(5) + 1;
			DROPBITS(5);
			state->ncode = BITS(4) + 4;
			DROPBITS(4);
			if(state->nlen > 286 || state->ndist > 30){
				printf("[in TABLE] too many length or distance symbols\n");
				state->mode = BAD;
				break;
			}
			state->have = 0;	//cleared, to be a counter.
			state->mode = LENLENS;
		case LENLENS:			//the len/lit or dist encoded lengths with bl_tree
#ifdef ZEBUG
		MSG("LENLENS ->");
#endif
			while(state->have < state->ncode){//see send_trees() in send_all_trees()
				NEEDBITS(3);
				state->lens[bl_order[state->have++]] = (unsigned short)BITS(3);
#ifdef ZEBUG
printf("r=%2hd, ", (unsigned short)BITS(3));
#endif
				DROPBITS(3);
			}	
#ifdef ZEBUG
printf("\n");
#endif
			while(state->have < 19)
				state->lens[bl_order[state->have++]] = 0;
			//now having collected all the code-lengths
			state->next = state->codes;
			state->lencode = (const code *)(state->next);
			state->lenbits = 7;
			ret = inflate_table(CODES, state->lens, 19, &(state->next), \
						&(state->lenbits), state->work);
			if(ret){
				printf("[in LENLENS] invalid code lengths set\n");
				state->mode =BAD;
				break;
			}
			state->have = 0;
			state->mode = CODELENS;
		case CODELENS:			//now fetch the litlens and idists huff-tables
#ifdef ZEBUG
		MSG("CODELENS");
#endif
			while( state->have <  state->nlen + state->ndist){
				for( ; ; ){	//use table created in LENLENS,try to find one have enough bits
					here = state->lencode[BITS(state->lenbits)];
					if( (unsigned)(here.bits) <= bits) break;
					PULLBYTE();
				}
				//now, find a valid value with enough bits
				if(here.val < 16){	//the stored encoded data are in RLE form
					DROPBITS( here.bits );	//直接就是 (encoded) code lengths
					state->lens[state->have++] = here.val;
				}
				else{	//确定解析RLE中重复的次数
					if(here.val == 16){	//REP_3_6
						NEEDBITS(here.bits + 2);
						DROPBITS(here.bits);
						if(state->have == 0){
							//invalid bit length repeat
							state->mode = BAD;
							break;
						}
						len = state->lens[state->have - 1];
						copy = 3 + BITS(2);	//确定copy value
						DROPBITS(2);
					}
					else if(here.val == 17){  //REPZ_3_10 + 2bits times
						NEEDBITS( here.bits + 3);
						DROPBITS( here.bits);
						len = 0;
						copy = 3 + BITS(3);
						DROPBITS(3);
					}
					else{	//REPZ_11_138
						NEEDBITS( here.bits + 7);
						DROPBITS( here.bits);
						len = 0;
						copy = 11 + BITS(7);
						DROPBITS(7);
					}
					if(state->have + copy > state->nlen + state->ndist){
						printf("invalid bit length repeast\n");
						state->mode = BAD;
						break;
					}
					while(copy--)	//存储已经得到解码的长度值(而非被bl_tree编码后存储的)
						state->lens[state->have++] = (unsigned short)len;
				}
			}
#ifdef ZEBUG
	int _tmp;
	for(_tmp=0; _tmp < state->nlen; _tmp++)
		printf("LL=%2hd, ", state->lens[_tmp]);
	printf("\n\n");
	for(; _tmp < state->have; _tmp++)
		printf("DL=%2hd, ", state->lens[_tmp]);
	printf("\n\n");
#endif
			//handle error breaks in while
			if(state->mode == BAD) break;
			//check for end-of-block code (better have one)
			if(state->lens[256] == 0){
				printf("invalid code -- missing end-of-block\n");
				state->mode = BAD;
				break;
			}
			/*
			build code tables -- note: do not change the lenbits or distbits value 
			here (9 and 6), as the ENOUGH constants depends on those values.
			*/
			state->next = state->codes;
			state->lencode = (const code *)(state->next);
			state->lenbits = 9;
			ret = inflate_table(LENS, state->lens, state->nlen, &(state->next), \
					&(state->lenbits), state->work);
			if(ret){
				printf("invalid literal/lengths set[%d] \n", ret);
				state->mode = BAD;
				break;
			}
			state->distcode = (const code *)(state->next);
			state->distbits = 6;
			ret = inflate_table(DISTS, state->lens + state->nlen, state->ndist, \
					&(state->next), &(state->distbits), state->work);
			if(ret){
				printf("invalid distances set \n");
				state->mode = BAD;
				break;
			}
			//codes OK now
			state->mode = LEN_;
#ifdef ZEBUG
printf("line:%d, right now left should be zero: %d \n", left, __LINE__);
#endif
		case LEN_:
#ifdef ZEBUG
			MSG("LEN_->");
#endif
			state->mode = LEN;
		case LEN:	//不明白 back 的用途
#ifdef ZEBUG
			MSG("LEN->");
#endif
		//	if(have >= 6 && left >= 258){
#ifdef ZEBUG
printf("line:%d, have=%d, >=6?, left=%d, <=258?, out=%d, availout=%d, availin=%d\n", __LINE__, have, left, out, strm->avail_out, strm->avail_in);
#endif
			if(have >= 6 && left >= 0){	//[Zoo]left means the num of available output bytes
				RESTORE();
				inflate_fast_core(strm, state);
			//	inflate_fast_core(strm, out);
				LOAD();
				if(state->mode == TYPE){
					state->back = -1;
				}
#ifdef ZEBUG
printf("line:%d, have=%d, >=6?, left=%d, <=258?, out=%d, availout=%d, availin=%d\n", __LINE__, have, left, out, strm->avail_out, strm->avail_in);
#endif
				break;
			}
			state->back = 0;
			for( ; ; ){	//收集足够的弹药, for the rrot table
				here = state->lencode[BITS(state->lenbits)];
				if( (unsigned)(here.bits) <= bits) break;
				PULLBYTE();
			}
			if(here.op && (here.op & 0xF0)==0){//query the sub table link,
				last = here;
				for( ; ; ){	//对比这里和下面的sub-table索引，为何如此计算?
					here = state->lencode[last.val + \
						(BITS(last.bits+last.op) >> last.bits) ];
					if( (unsigned)(last.bits + here.bits) <= bits) break;
					PULLBYTE();
				}
				DROPBITS(last.bits);
				state->back += last.bits;
			}
			DROPBITS( here.bits );
			state->back += here.bits;
			state->length = (unsigned) here.val;
			if( (int)(here.op) == 0){ //op=0x00, literal
				state->mode = LIT;
				break;
			}
			if( here.op & 32 ){	//end of block
#ifdef ZEBUG
printf("END-OF block ---\n");
#endif
				state->back = -1;
				state->mode = CHECK;	//[ZOO]直接进入到CHECK模式就可以了；
			//	state->mode = TYPE;	//[ZOO]这里需要商榷，在并行版本中，当
			//	state->mode = DONE;	//still not end, left the adler to be proc.
				break;			//一个block完结，是否需要
			}
			if( here.op & 64 ){
				printf("invalid literal/length code \n");
				state->mode = BAD;
				break;
			}
			state->extra = (unsigned)(here.op) & 15;
			state->mode = LENEXT;
		case LENEXT:
#ifdef ZEBUG
		MSG("LENEXT->");
#endif
			if(state->extra){
				NEEDBITS(state->extra);
				state->length += BITS(state->extra);
				DROPBITS(state->extra);
				state->back += state->extra;
			}
			state->was = state->length;
			state->mode = DIST;
		case DIST:
#ifdef ZEBUG
		MSG("DIST->");
#endif
			for( ; ; ){	//读入dist值，收集足够bits
				here = state->distcode[BITS(state->distbits)];
				if( (unsigned)(here.bits) <= bits ) break;
				PULLBYTE();
			}
			if( (here.op & 0xF0) == 0 ){	//sub table index
				last = here;
				for( ; ; ){
					here = state->distcode[last.val + \
						(BITS(last.bits + last.op) >>last.bits)];
					if( (unsigned)(last.bits + here.bits) <= bits) break;
					PULLBYTE();
				}
				DROPBITS(last.bits);
				state->back += last.bits;
			}
			DROPBITS(here.bits);
			state->back += here.bits;
			if(here.op & 64){	//invalid code
				printf("invlalid distance code \n");
				state->mode = BAD;
				break;
			}
			state->offset = (unsigned)here.val;
			state->extra = (unsigned)(here.op) & 15;
			state->mode = DISTEXT;
		case DISTEXT:
#ifdef ZEBUG
		MSG("DISTEXT->");
#endif
			if(state->extra){
				NEEDBITS(state->extra);
				state->offset += BITS(state->extra);
				DROPBITS(state->extra);
				state->back += state->extra;
			}
			state->mode = MATCH;
		case MATCH:
#ifdef ZEBUG
		MSG("MATCH->");
#endif
			if(left == 0) goto inf_leave;
		#if 1
			//now get the match pair(offset, length)
#ifdef ZEBUG
printf("line:%d, (offset=%d, lenth=%d) \n", state->offset, state->length);
#endif
			from = put - state->offset;
			copy = state->length;
			state->length = 0;	//[ZOO] 如果这里不进行更新length，
			do{			//[ZOO] 那么后续状态机就死循环，导致3522-frymire.tif错误
				*put++ = *from++;
				left++;
			}while(--copy);
		#else
			//[TODO]可以先检查指针和lit是否相同；之后再考虑拷贝的事宜
			copy = out - left;	//out的维护在哪里?
			if(state->offset > copy){	//copy from window
				copy = state->offset - copy;
				if(copy > state->whave){
					if(state->sane){
						printf("invalid distance too far back \n");
						state->mode = BAD;
						break;
					}
				}
				if(copy > state->wnext){
					copy -= state->wnext;
					from = state->window + (state->wsize - copy);
				}
				else
					from = state->window + (state->wnext - copy);
				if(copy > state->length) copy = state->length;
			}
			else{
				from = put - state->offset;
				copy = state->length;
			}
			if(copy > left) copy = left;
			left -= copy;
			state->length -= copy;
			do{	//copy byte-by-byte
				*put++ = *from++;
			}while( --copy );
		#endif
			if(state->length == 0)  state->mode = LEN;
			break;
		case LIT:
			if(left == 0)  goto inf_leave;
			*put++ = (unsigned char)(state->length);
			left++;
			state->mode = LEN;
			break;
		case CHECK:	//[TODO] 不明白这里是做什么的；
#ifdef ZEBUG
		MSG("CHECK->");
#endif
			BYTEBITS();
//printf("line:%d, get the tables \n", __LINE__);
//ret = BAT_OK;
//goto inf_leave;
			if(state->wrap){
				NEEDBITS(32);
			#if 0
				out -= left;		//[ZOO] out is USELESS for me
				strm->total_out += out;
				state->total += out;
				if(out)
					strm->adler = state->check = \
					BAT_adler32(state->check, put - out, out);
				out = left;
				if( (SWAP32(hold)) != state->check ){
					printf("incorrect data check \n");
					state->mode = BAD;
					break;
				}
			#endif
				INITBITS();
				//inflate check matches trailer
			}
#ifdef ZEBUG
printf("line:%d, now have=%d, left = %d, avail_in=%d, avail_out=%d, \n",__LINE__, have, left, strm->avail_in, strm->avail_out);
printf("line:%d, now bits=%d, hold = 0x%02x, \n",__LINE__, bits, hold);
#endif
			RESTORE();
			state->mode = LENGTH;
		case LENGTH:
#ifdef ZEBUG
		MSG("LENGTH->");
#endif
			if(state->wrap && state->flags){
				NEEDBITS(32);
				if(hold != (state->total & 0xFFFFFFFFUL) ){
					printf("incorrect length check \n");
					state->mode = BAD;
					break;
				}
				INITBITS();
				//inflate: length matches trailer
			}
			state->mode = DONE;
		case DONE:
#ifdef ZEBUG
		MSG("DONE->");
#endif
			ret = BAT_STREAM_END;
			goto inf_leave;

		case BAD:
#ifdef ZEBUG
			MSG("BAD->");
#endif
			ret = BAT_DATA_ERROR;
			goto inf_leave;
		default:
			return BAT_STREAM_ERROR;
	}//end switch
	} //end for(;;)

inf_leave:
	RESTORE();
	//TODO update window
//	strm->avail_in = in;
//	in -= strm->avail_in;	//这里经常弄混一点，就是total_in其实和avail_in是相同的，
//	strm->total_in += in;	//只是前者为const，后者不停变动作为循环条件变量；
//	out += strm->avail_out;	//out is useless
	state->avail_out = left;
	strm->total_out += state->avail_out;
	state->total += state->avail_out;
#ifdef ZEBUG
printf("NOW avial-in=%d, avail-out=%d, total-in=\%d, total-out=%d,\n", strm->avail_in, strm->avail_out, strm->total_in, strm->total_out);
#endif
	//TODO
	// TODO 如果 ret不是BAT_OK而是 HT_STREAM_END, 如何设计呢?
	return ret;
}

local int  bat_uncompress(bat_streamp strm){
	//first, unlike deflate)_, here load in the comrpessed data
	assert(strm != BAT_NULL);
	uLong rd_len = read(strm->ifd, strm->all_in_buf, strm->total_in);
	if(rd_len != strm->total_in){
		printf("Expect %u bytes, read in %lu bytes \n",\
			strm->total_in, rd_len);
		exit(EXIT_FAILURE);
	}
	//conduct the actual inflate procession
	//这里应该是逐block的解析的，所以当遇到一个clock-end，就应该flush out；
	//另外，avail in和 avail out的数值也是异常的，需要进行修正；
	int ret;
	inflate_state *s = (inflate_state *)strm->inflate;
	do{
		unsigned char *head_out = s->output;
		ret = bat_inflate_core(strm, s, BAT_FINISH);
		//flush, avail_out means the decompressed in each round, total_out means in total
#ifdef ZEBUG
printf("Flush out the decompressed data now \n");
#endif
		if(s->avail_out > 0){
			s->output = s->output - s->avail_out;	//reset next_out
			write(strm->ofd, s->output, s->avail_out);
		}
		//clear the current iteration counter.
		s->avail_out = 0;
		s->output = head_out;
		//reset the common configurations
		bat_inflate_state_reset(s);
		//update the avail_in
		//avail in has been updated in inflate_core() func
	}while(ret==BAT_STREAM_END && s->avail_in > 0);
	//
	return ret;
}

//**************************************************************//


void BAT_uncompress_file(char *infile, char *outfile){
	int ret;
	uLong compr_len, plain_len;
	struct stat st_info;
	stat(infile, &st_info);
	compr_len = (uLong) st_info.st_size;
	if(compr_len <= 0){
		printf("input file size is <= 0, error ...\n");
		exit(EXIT_FAILURE);
	}
	int ifd = open(infile, O_RDONLY);
	int ofd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if(ifd == -1 || ofd == -1){
		perror("Error in file open");
		goto END;
	}
	//init the umcompression stream
	bat_stream stream;
	ret = bat_inflateInit_stream(&stream, ifd, ofd, compr_len);
	if(ret != BAT_OK){
		printf("Error occured in bat_inflateInit_stream() ...\n");
		goto END;
	}
	bat_inflateInit_state(&stream);
	//read in the input file form disk to memory
	ret = bat_uncompress(&stream);
	if(ret != BAT_OK && ret != BAT_STREAM_END){
		printf("Error occured in Uncompression() ...\n");
		if(ret == BAT_MEM_ERROR){
			printf("[Error] there was not enough memoy\n");
		}
		else if(ret == BAT_BUF_ERROR){
			printf("[Error] there was not enough room in output buffer\n");
		}
		else if(ret == BAT_DATA_ERROR){
			printf("[Error] the input data was corrupted\n");
		}
	}
	bat_inflateEnd_state(&stream);
END:
	bat_inflateEnd_stream(&stream);
	stat(outfile, &st_info);
	plain_len = (uLong)st_info.st_size;
	float ratio = 0.0f;
	ratio = compr_len * 100.0f / plain_len;
	printf("[HT2] input size = %lu, output size = %lu, ratio = %.2f%%\n", compr_len, plain_len, ratio);
}
