#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

#include "bat_zlz.h"

#define VERSION "zoo-zlib-simplified version 0.2\n"

#if 0
typedef enum{
	DEFAULT_MODE = 1, 
	GZIP_MODE = 2, 
	HT_MODE = 3, 
} procession_mode;
#else
#define DEFAULT_MODE 1
#define GZIP_MODE 2
#define HT_MODE  3
#define PARALLEL_MODE  4
#define BATCH_MODE 5
#define TEST_MODE  6
#endif

static void print_helper_info(void){
	printf("--------------- Hi, Mongoo, Help info is here: \n");
	printf("-h:		print help info \n");
	printf("-c:		compression mode\n");
	printf("-d:		decompression mode\n");
	printf("-b:		use batch procession mode\n");
	printf("-t:		use the test mode\n");
	printf("-i infilename:	input filename \n");
	printf("-o oufilename:	output filename \n");
	printf("--------------- DO Remember: DO NOT DO STUPID --- Gump\n");
	return ;
}

int main(int argc, char *argv[]){
	assert(argc > 1);
	printf("%s", VERSION);
	int  opt;
	int decomp = -1;
	int mode = DEFAULT_MODE;
	char infile[512];
	char outfile[512];
	while( (opt = getopt(argc, argv, "hcdbti:o:")) != -1){
		switch( opt ){
		case 'h':
			print_helper_info();
			exit(EXIT_SUCCESS);
		case 'c':
			decomp = 0;
			break;
		case 'd':
			decomp = 1;
			break;
		case 'b':
			mode = BATCH_MODE;
			break;
		case 't':
			mode = TEST_MODE;
			break;
		case 'i':
			strncpy(infile, optarg, 511);
			break;
		case 'o':
			strncpy(outfile, optarg, 511);
			break;
		default:
			print_helper_info();
			exit(EXIT_SUCCESS);
		}//end switch
	}
	if(mode == TEST_MODE){	//the highest priority 
		printf("[Enter the TEST mode now] \n");
		BAT_test_const_tables();
	}
	else if(decomp == 0){	//compression mode
		printf("[Compress file (%s) to file (%s)\n", infile, outfile);
		switch(mode){
		case BATCH_MODE:
		//	BAT_compress_file(infile, outfile);
		//	TS_compress_file_host(infile, outfile);
			TS_compress_file_dev(infile, outfile);
			break;
		default:
			fprintf(stderr, "Undefined compression mode\n");
			break;
		}
	}
	else if(decomp == 1){	//decompression mode
		printf("[DeCompress file (%s) to file (%s)\n", infile, outfile);
		switch(mode){
		case BATCH_MODE:
			BAT_uncompress_file(infile, outfile);
			break;
		default:
			fprintf(stderr, "Undefined decompression mode\n");
			break;
		}
	}
	else{			//undefined mode
		printf("Error mode, exit ...\n");
		//free allocated space
		//TODO
		exit(EXIT_FAILURE);
	}
	return 0;
}
