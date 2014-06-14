/*
 * construct the decoding trees.
 * Mar. 25th. 2014
*/
#include "bat_inflate.h"
#include <stdio.h>

/*
 * build a set of tables to decode the provided canonical Huffman code.
 * the code lengths are lens[0..codes-1]. the result starts at *table,
 * which indices are 0..2^bits-1. work is a writable array of at least
 * lens shorts, which is used as a work area. type is the type of code
 * to be generated, CODES, LENS, or DISTS. on return, zero is success, 
 * -1 is an invalid code, and +1 means that ENOUGH isn't enough. table
 * on return points to the next available entry's adress. bits is the 
 * requested root table index bits, and on return it is the actual root
 * table index bits. It will differ if the request is greater than the 
 * longest code or if it is less than the shorted code.
*/

//还没有搞懂bits为什么一开始选择 7 for lenlens-table
//因为尝试了其他的bits数值 (< 7)，会产生解码错误；
#define MAXBITS 15

#define DEBUG(s) printf("file:%s, line:%d ::%s\n", __FILE__, __LINE__, (s) )

int inflate_table(codetype type, unsigned short *lens, unsigned codes_max,\
		code * *table, unsigned *bits, unsigned short *work)
{
	unsigned len;		//a code's length in bits
	unsigned sym;		//index of code symbols
	unsigned min, max;	//minimum and maximum code lengths
	unsigned root;		//number of index bits for root table
	unsigned curr;		//number of index bits for current table
	int left;		//number of prefix codes available
	//
	code here;		//table entry for duplicaton
	code *next;		//next available space in table
	const unsigned short *base;	//base value table to use
	const unsigned short *extra;	//extra bits table to use
	int end;		//use base and extra for symbol > end
	unsigned short count[MAXBITS + 1]; //number of codes of each length
	unsigned short offs[MAXBITS + 1];  //offsets in table for each length
//	static const unsigned short lbase[31] = { /* Length codes 257..285 base */
//		3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
//		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
	static const unsigned short lbase[31] = { /* Length codes 257..285 base */
		4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32,
		36, 44, 52, 60, 68, 84, 100, 116, 132, 164, 196, 228, 259, 0, 0};
	static const unsigned short lext[31] = { /* Length codes 257..285 extra */
		16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18,
		19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 16, 72, 78};
	static const unsigned short dbase[32] = { /* Distance codes 0..29 base */
		1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
		8193, 12289, 16385, 24577, 0, 0};
	static const unsigned short dext[32] = { /* Distance codes 0..29 extra */
		16, 16, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
		23, 23, 24, 24, 25, 25, 26, 26, 27, 27,
		28, 28, 29, 29, 64, 64}; 
	/*
	Process a set of code lengths to create a canonical Huffman code.  The
	code lengths are lens[0..codes-1].  Each length corresponds to the
	symbols 0..codes-1.  The Huffman code is generated by first sorting the
	symbols by length from short to long, and retaining the symbol order
	for codes with equal lengths.  Then the code starts with all zero bits
	for the first code of the shortest length, and the codes are integer
	increments for the same length, and zeros are appended as the length
	increases.  For the deflate format, these bits are stored backwards
	from their more natural integer increment ordering, and so when the
	decoding tables are built in the large loop below, the integer codes
	are incremented backwards.
	
	This routine assumes, but does not check, that all of the entries in
	lens[] are in the range 0..MAXBITS.  The caller must assure this.
	1..MAXBITS is interpreted as that code length.  zero means that that
	symbol does not occur in this code.
	
	The codes are sorted by computing a count of codes for each length,
	creating from that a table of starting indices for each length in the
	sorted table, and then entering the symbols in order in the sorted
	table.  The sorted table is work[], with that space being provided by
	the caller.
	
	The length counts are used for other purposes as well, i.e. finding
	the minimum and maximum length codes, determining if there are any
	codes at all, checking for a valid set of lengths, and looking ahead
	at length counts to determine sub-table sizes when building the
	decoding tables.
	*/
	//accumulate lengths for codes (assumes lens[] all in 0..MAXBITS)
	for(len=0; len<=MAXBITS; len++)
		count[ len ] = 0;
	for(sym=0; sym<codes_max; sym++){//在这里误将"<"写为了"<="，谬之千里
		count[ lens[sym] ]++;
#ifdef ZEBUG
		if(lens[sym] == 6) printf("6[%3d], ", sym);
#endif
	}
#ifdef ZEBUG
printf("\n--------------intlate-table:  codes_max = %d\n", codes_max);
for(len=0; len<codes_max; len++)
	printf("TB=%2hd, ", lens[len]);	
printf("\n");
for(len=0; len<= MAXBITS; len++)
	printf("C=%2hd, ", count[len]);	
printf("\n");
#endif
	//bound code lengths, force root te be within code lengths
	root = *bits;
	for(max=MAXBITS; max>=1; max--)
		if( count[max] != 0) break;
#ifdef ZEBUG
printf("root=%d, max=%d\n", root, max);
#endif
	if(root > max) root = max;
	if(max == 0){	//no symbol to code at all
		here.op = (unsigned char)64;	//invalid code marker
		here.bits = (unsigned char) 1;
		here.val = (unsigned short)0;
		*(*table++) = here;	//make a table to force an error
		*(*table++) = here;
		*bits = 1;
		return 0;	//no symbols, but wait for decoding to report error
	}
	for(min=1; min<max; min++)
		if( count[min] != 0) break;
	if(root < min) root = min;
#ifdef ZEBUG
printf("min = %d, \n", min);
#endif
	//check for an over-subscribed or incomplete set of lengths
	left = 1;
	for(len=1; len<=MAXBITS; len++){
		left <<= 1;
#ifdef ZEBUG
printf("left = %d, coutn[%d]=%d, left-count=%d\n", left, len, count[len], left-count[len]);
#endif
		left -= count[len];
		if(left < 0) return -1;	//over-subscribed
	}
	if(left > 0 && (type==CODES || max != 1))
		return -1;		//incomplete set
	//generate offsets into symbol table for each length for sorting
	offs[1] = 0;
	for(len=1; len<MAXBITS; len++)
		offs[len+1] = offs[len] + count[len];
#ifdef ZEBUG
for(len=1; len<MAXBITS; len++)
	printf("O=%2hd, ", offs[len]);
printf("\n");
#endif
	//sort symbols by length, by symbol order within each length
	for(sym=0; sym<codes_max; sym++)
		if(lens[sym] != 0) work[offs[lens[sym]]++] = (unsigned short)sym;
#ifdef ZEBUG
for(sym=0; sym<codes_max; sym++)
	printf("W=%2hd, ", work[sym]);
printf("\n");
#endif

	/*
	Create and fill in decoding tables.  In this loop, the table being
	filled is at next and has curr index bits.  The code being used is huff
	with length len.  That code is converted to an index by dropping drop
	bits off of the bottom.  For codes where len is less than drop + curr,
	those top drop + curr - len bits are incremented through all values to
	fill the table with replicated entries.
	
	root is the number of index bits for the root table.  When len exceeds
	root, sub-tables are created pointed to by the root entry with an index
	of the low root bits of huff.  This is saved in low to check for when a
	new sub-table should be started.  drop is zero when the root table is
	being filled, and drop is root when sub-tables are being filled.
	
	When a new sub-table is needed, it is necessary to look ahead in the
	code lengths to determine what size sub-table is needed.  The length
	counts are used for this, and so count[] is decremented as codes are
	entered in the tables.
	
	used keeps track of how many table entries have been allocated from the
	provided *table space.  It is checked for LENS and DIST tables against
	the constants ENOUGH_LENS and ENOUGH_DISTS to guard against changes in
	the initial root table size constants.  See the comments in inftrees.h
	for more information.
	
	sym increments through all symbols, and the loop terminates when
	all codes of length max, i.e. all codes, have been processed.  This
	routine permits incomplete codes, so another loop after this one fills
	in the rest of the decoding tables with invalid code markers.
	*/
	//set up for the code type
	switch( type ){
	case CODES:
		base = extra = work;	//dummy value -- not used
		end = 19;
		break;
	case LENS:		//length/literals
		base = lbase;
		base -= 257;
		extra = lext;
		extra -= 257;
		end = 256;
		break;
	default:		//dists
		base = dbase;
		extra = dext;
		end = -1;
	}
	//initialize state for loop
	sym = 0;		//starting code symbol
	len = min;		//starting code length
	next = *table;		//current table to fill in
	curr = root;		//current table index bits
	unsigned huff = 0;		//starting code,按照约定，从0开始编码
	unsigned drop = 0;		//code bits to drop for sub-table
	unsigned low = (unsigned)(-1);	//low bits for current root entry
	unsigned used = 1U << root;	//use root table entries
	unsigned mask = used - 1;	//mask for comparing low root bits
	//check available table space
	if( (type==LENS && used > ENOUGH_LENS) || \
	    (type==DISTS && used > ENOUGH_DISTS) )
		return 1;
	//process all codes and make table entries
	unsigned incr;		//for increasmenting code, index
	unsigned fill;		//index for replicating entries
#ifdef ZEBUG
printf("--------build the tree---------\n");
#endif
	for( ; ; ){
		//create table entry
		here.bits = (unsigned char)(len - drop);
		if( (int)(work[sym]) < end){
			here.op = (unsigned char )0;
			here.val = work[sym];
		}
		else if( (int)(work[sym]) > end){
			here.op = (unsigned char)(extra[work[sym]] );
			here.val = base[ work[sym] ];
		}
		else{
			here.op = (unsigned char)(32 + 64);	//end of block
			here.val = 0;
		}
#ifdef ZEBUG
printf("here::sym=%3d, work[sym]=%3d, op=0x%02x, bits=%2d, val=%3d,huff=0x%02x (%3d)\n",sym, work[sym], here.op, here.bits, here.val, huff, huff);
#endif
		//replicate for those indices with low len bits equal to huff
		incr = 1U << (len - drop);
		fill = 1U << curr;
		min = fill;		//save offset to next table
		do{
			fill -= incr;
			next[(huff >> drop) + fill] = here;
		}while (fill != 0);
		//backwards increment the len-bit code huff
		incr = 1U << (len - 1);
		while(huff & incr)
			incr >>= 1;
		if(incr != 0){
			huff &= incr - 1;
			huff += incr;
		}
		else
			huff = 0;
		//go to next symbol, update count, len
		sym++;
		if( --(count[len]) == 0){
			if(len == max) break;
			len = lens[work[sym]];
		}
//printf("line:%d, len =%d, curr huff code = 0x%x or %d, incr=%d (0x%x) \n", __LINE__, len, huff, huff, incr, incr);
		//create new sub-table if needed
		if(len > root && (huff & mask) != low){
			//if first time, transition to sub-tables
			if(drop == 0)
				drop = root;
			//increment past last table
			next += min;	//here min records (1 << curr)
			//determine length of next table
			curr = len - drop;
			left = (int)(1 << curr);
			while(curr + drop < max){
				left -= count[curr + drop];
				if(left <= 0) break;
				curr ++;
				left <<= 1;	//在我看来，这里更想是进行over-subscribed检查；
			}
			//check for enough space
			used += 1U << curr;
			if ( (type==LENS && used > ENOUGH_LENS) || \
			     (type==DISTS && used > ENOUGH_DISTS) )
				return 1;
			//point entry in root table to sub-table
			low = huff & mask;
			(*table)[low].op = (unsigned char)curr;
			(*table)[low].bits = (unsigned char)root;
			(*table)[low].val = (unsigned short)(next - *table);
		}
	}
	/*
	fill in remaining table entry if code is incomplete (guranteed to have 
	at most one remaining entry, since if the code is incomplete, the maximum 
	code length that was allowed to get this far is one bit
	*/
	if(huff != 0){
		here.op = (unsigned char)64;	//invalid code marker
		here.bits = (unsigned char)(len - drop);
		here.val = (unsigned short)0;
		next[huff] = here;
	}
	//set return parameters
	*table += used;
	*bits = root;
	return 0;
}
