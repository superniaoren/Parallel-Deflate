#OS Name (Linux)
OSUPPER = $(shell uname -s 2>/dev/null | tr [:lower:] [:upper:])
OSLOWER = $(shell uname -s 2>/dev/null | tr [:upper:] [:lower:])

#flags to detect 32-bit or 64-bit OS platform
OS_SIZE = $(shell uname -m | sed -e "s/i.86/32/" -e "s/x86_64/64/")
OS_ARCH = $(shell uname -m | sed -e "s/i386/i686/")

#these flags will override any settings
ifeq ($(i386), 1)
	OS_SIZE = 32
	OS_ARCH = i686
endif

ifeq ($(x86_64), 1)
	OS_SIZE = 64
	OS_ARCH = x86_64
endif


#################################################################################
#	CUDA paths, libraries, runtime APIs, 					#
#################################################################################
CUDA_SAMPLE_PATH ?= /home/$(USER)/NVIDIA_CUDA-5.5_Samples
CUDA_PATH ?= /usr/local/cuda-5.5
CUDA_INC_PATH ?= $(CUDA_PATH)/include
CUDA_BIN_PATH ?= $(CUDA_PATH)/bin

ifeq ($(OS_SIZE), 32)
 CUDA_LIB_PATH ?= $(CUDA_PATH)/lib
else
 CUDA_LIB_PATH ?= $(CUDA_PATH)/lib64
endif

NVCC ?= $(CUDA_BIN_PATH)/nvcc
GENCODE_SM20 := -gencode arch=compute_20,code=sm_20
GENCODE_FLAGS := $(GENCODE_SM20)

ifeq ($(OS_SIZE),32)
  NVCCFLAGS := -m32  -DDEV_LZSS
else
  NVCCFLAGS := -m64  -DDEV_LZSS
endif


ifeq ($(OS_SIZE), 32)
 LDFLAGS := -L$(CUDA_LIB_PATH) -lcudart
 CCFLAGS := -m32 -Wall
else
 LDFLAGS := -L$(CUDA_LIB_PATH) -lcudart
 CCFLAGS := -m64 -Wall
endif
 

INCLUDES := -I$(CUDA_INC_PATH) -I. -I.. -I$(CUDA_SAMPLE_PATH)/common/inc

EXTRA_NVCCFLAGS ?=  -DUSE_TRAD_WAY=1  -DHAVE_HIDDEN -DUSE_FAST_HUFFMAN_GEN=0
#EXTRA_NVCCFLAGS ?= -DUSE_TRAD_WAY=1 -DUSE_PARA_4B_MATCH=1 -DUSE_PINNED_MEMORY=1

#################################################################################
#	Common Usage of flags 							#
#################################################################################
#CC=gcc
CC=g++ 
AR=ar
CCFLAGS += -p
EXTRA_CFLAGS= -DUSE_TRAD_WAY=1 -DHAVE_HIDDEN -DUSE_FAST_HUFFMAN_GEN=0
LDFLAGS += -lpthread
ARFLAGS= rcs

STATIC_LIB = lib_lz77.a


SRC= bat_zlz.c	\
	bat_inflate.c bat_inftrees.c bat_inffast.c	\
	bat_trees.c bat_deflate.c			\
	bat_test.cu 					\
	two_stages_host.c two_stages_dev.cu

#SRC= zutil.c crc32.c adler32.c trees.c deflate.c 	\
	inftrees.c inffast.c inflate.c			\
	compress.c uncompress.c				\
	zok_zlz.c zok_zlz_deflate.c zok_zlz_inflate.c	\
	ht_zlz.c ht_deflate.c ht_trees.c		\

OBJ=  bat_zlz.o	 \
	bat_inflate.o bat_inftrees.o bat_inffast.o	\
	bat_trees.o bat_deflate.o			\
	bat_test.o 					\
	two_stages_host.o two_stages_dev.o
	
#OBJ= zutil.o crc32.o adler32.o trees.o deflate.o 	\
	inftrees.o inffast.o inflate.o			\
	compress.o uncompress.o				\
	zok_zlz.o zok_zlz_deflate.o zok_zlz_inflate.o	\
	ht_zlz.o ht_deflate.o ht_trees.o		\

all: zoo_test 

bat_zlz.o: bat_zlz.c
	$(CC) -c bat_zlz.c $(CCFLAGS) $(EXTRA_CFLAGS)
#inftrees.o: inftrees.c
#	$(CC) -c inftrees.c $(CCFLAGS) $(EXTRA_CFLAGS)
#inffast.o: inffast.c
#	$(CC) -c inffast.c $(CCFLAGS) $(EXTRA_CFLAGS)
#inflate.o: inflate.c
#	$(CC) -c inflate.c $(CCFLAGS) $(EXTRA_CFLAGS)
#uncompress.o: uncompress.c
#	$(CC) -c uncompress.c $(CCFLAGS) $(EXTRA_CFLAGS)

bat_inflate.o: bat_inflate.c
	$(CC) -c bat_inflate.c $(CCFLAGS) $(EXTRA_CFLAGS)
bat_inftrees.o: bat_inftrees.c
	$(CC) -c bat_inftrees.c $(CCFLAGS) $(EXTRA_CFLAGS)
bat_inffast.o: bat_inffast.c
	$(CC) -c bat_inffast.c $(CCFLAGS) $(EXTRA_CFLAGS)
#
bat_trees.o:  bat_trees.c bat_trees.h
	$(CC) -c bat_trees.c $(CCFLAGS) $(EXTRA_CFLAGS)
bat_deflate.o: bat_deflate.c bat_deflate.h
	$(CC) -c bat_deflate.c $(CCFLAGS) $(EXTRA_CFLAGS)
bat_test.o: bat_test.cu 
	$(NVCC) $(NVCCFLAGS) $(EXTRA_NVCCFLAGS) $(GENCODE_FLAGS) $(INCLUDES) -o $@ -c $<

two_stages_host.o: two_stages_host.c ts_deflate.h
	$(CC) -c two_stages_host.c $(CCFLAGS) $(EXTRA_CFLAGS)
two_stages_dev.o: two_stages_dev.cu ts_deflate.h
	$(NVCC) $(NVCCFLAGS) $(EXTRA_NVCCFLAGS) $(GENCODE_FLAGS) $(INCLUDES) -o $@ -c $<


$(STATIC_LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $(OBJ)

zoo_test: $(OBJ) $(SRC) main.c $(STATIC_LIB)
	$(CC) $(CCFLAGS) $(INCLUDES) -o zoo_test  main.c $(STATIC_LIB) $(LDFLAGS) $(EXTRA_CFLAGS)

clean:
	rm -f *.o zoo_test $(STATIC_LIB)
