/* 
 * csim.c - A cache simulator that can replay traces from Valgrind
 *     and output statistics such as number of hits, misses, and
 *     evictions.  The replacement policy is LRU.
 *
 * Implementation and assumptions:
 *  1. Each load/store can cause at most one cache miss. (I examined the trace,
 *  the largest request I saw was for 8 bytes).
 *  2. Instruction loads (I) are ignored, since we are interested in evaluating
 *  trans.c in terms of its data cache performance.
 *  3. data modify (M) is treated as a load followed by a store to the same
 *  address. Hence, an M operation can result in two cache hits, or a miss and a
 *  hit plus an possible eviction.
 *
 * The function printSummary() is given to print output.
 * Please use this function to print the number of hits, misses and evictions.
 * This is crucial for the driver to evaluate your work. 
 * 
 * TODO: Replace with your information
 * Author: Konstantin Petrov
 * NYU netID: kp1381
 */
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <strings.h>
#include <math.h>

#include "cachelab.h"

/* Always use a 64-bit variable to hold memory addresses*/
typedef unsigned long long int mem_addr_t;

/* a struct that groups cache parameters together */
typedef struct {
	int s; /* 2**s cache sets */
	int b; /* cacheline block size 2**b bytes */
	int E; /* number of cachelines per set */
	int S; /* number of sets, derived from S = 2**s */
	int B; /* cacheline block size (bytes), derived from B = 2**b */
	int hits;//number of hits
	int misses; //number of misses
	int evictions;//number of evictions
} cache_param_t;

typedef struct{
	int lastUsed;
	int valid;
	mem_addr_t tag;
	char *block;
}setLine;

typedef struct{
	setLine *lines;
}cacheSet;

typedef struct{
	cacheSet *Sets;
}cache;
int verbosity;

long long bitPow(int exp){
	long long res=1;
	res = res << exp;
	return res;
}
/*
 * printUsage - Print usage info
 */
void printUsage(char* argv[])
{
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\nExamples:\n");
    printf("  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
    printf("  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]);
    exit(0);
}

cache initCache(long long sets, int lines, long long blockSize){
	cache res;
	cacheSet set;
	setLine line;
	int si;//set index
	int li;//line index
	res.Sets = (cacheSet*) malloc(sizeof(cacheSet) * sets);
	for(si=0;si<sets;si++)
	{
		set.lines=(setLine*) malloc(sizeof(setLine) * lines);
		res.Sets[si]=set;
		for(li=0;li<lines;li++){
			line.lastUsed=0;
			line.valid=0;
			line.tag=0;
			set.lines[li]=line;
		}
	}
	return res;
}
void clrCache(cache toDel, long long sets, int lines, long long blockSize){
	int si;//set index
	for(si=0;si<sets;si++)
	{
		cacheSet set=toDel.Sets[si];
		if(set.lines!=NULL){
			free(set.lines);
		}
	}
	if(toDel.Sets != NULL){
		free(toDel.Sets);
	}
}
int nextEmptyLine(cacheSet Cache, cache_param_t par){
	int lines=par.E;
	int i;
	setLine line;
	for(i=0;i<lines;i++){
		line=Cache.lines[i];
		if(line.valid==0){
			return i;
		}
	}//shouldn't come here. only called when the cache is not full
	return -1;
}
int nextEvictLine(cacheSet Cache, cache_param_t par, int *usedLines){
	//returns the index of the least used line
	//usedLines[0] is leat used usedLines[1] is most used
	int lines=par.E;
	int maxUsed=Cache.lines[0].lastUsed;
	int minUsed=Cache.lines[0].lastUsed;
	int minIndex=0;
	setLine line;
	int li;//line index
	for(li=1;li<lines;li++){
		line=Cache.lines[li];
		if(minUsed>line.lastUsed){
			minIndex=li;
			minUsed=line.lastUsed;
		}
		if(maxUsed<line.lastUsed){
			maxUsed=line.lastUsed;
		}
	}
	usedLines[0]=minUsed;
	usedLines[1]=maxUsed;
	return minIndex;
}

cache_param_t run(cache Cache, cache_param_t par, mem_addr_t address){
	int li;//line index
	int cacheFull=1;
	int lines=par.E;
	int prevHits=par.hits;
	int tagSize=(64-(par.s+par.b));
	mem_addr_t inputTag = address >> (par.s+par.b);
	unsigned long long temp = address << (tagSize);
	unsigned long long si = temp >> (tagSize+par.b);
	cacheSet querySet = Cache.Sets[si];
	for(li=0;li<lines;li++){
		setLine line = querySet.lines[li];
		if(line.valid){
			if(line.tag==inputTag){
				line.lastUsed++;
				par.hits++;
				querySet.lines[li]=line;
			}
		}else if(!(line.valid) && (cacheFull)){
			cacheFull=0;
		}
	}
	if(prevHits==par.hits){//that's a miss
		par.misses++;
	}else{//data is in cache
		return par;
	}
	//we missed so we need to check if evection is necessary
	int *usedLines = (int*) malloc(sizeof(int)*2);
	int minIndex=nextEvictLine(querySet,par,usedLines);
	if(cacheFull){
		par.evictions++;
		querySet.lines[minIndex].tag=inputTag;//overwrite the least-recently-used line
		querySet.lines[minIndex].lastUsed=usedLines[1]+1;
	}
	else{//find an empty line and write on it
		int emptyIndex=nextEmptyLine(querySet,par);
		querySet.lines[emptyIndex].tag=inputTag;
		querySet.lines[emptyIndex].valid=1;
		querySet.lines[emptyIndex].lastUsed=usedLines[1]+1;
	}
	free(usedLines);
	return par;
}
int main(int argc, char **argv)
{
	cache simCache;
	cache_param_t par;
	bzero(&par, sizeof(par));
	long long sets;
	long long blockSize;
	FILE *readTrace;
	char traceCmd;
	mem_addr_t address;
	int size;

	char *trace_file;
	char c;
    while( (c=getopt(argc,argv,"s:E:b:t:vh")) != -1){
        switch(c){
        case 's':
            par.s = atoi(optarg);
            break;
        case 'E':
            par.E = atoi(optarg);
            break;
        case 'b':
            par.b = atoi(optarg);
            break;
        case 't':
            trace_file = optarg;
            break;
        case 'v':
            verbosity = 1;
            break;
        case 'h':
            printUsage(argv);
            exit(0);
        default:
            printUsage(argv);
            exit(1);
        }
    }

    if (par.s == 0 || par.E == 0 || par.b == 0 || trace_file == NULL) {
        printf("%s: Missing required command line argument\n", argv[0]);
        printUsage(argv);
        exit(1);
    }
	sets = pow(2.0,par.s); //compute S and B
	blockSize = bitPow(par.b);
	par.hits=0;
	par.misses=0;
	par.evictions=0;
	simCache= initCache(sets,par.E,blockSize);
	readTrace = fopen(trace_file, "r");
	//read trough the trace and simulate
	if(readTrace != NULL){
		while(fscanf(readTrace, " %c %llx,%d",&traceCmd, &address, &size) ==3){
			switch(traceCmd){
				case 'I': break;
				case 'L': par=run(simCache,par,address);
					  break;
				case 'S': par=run(simCache,par,address);
					  break;
				case 'M': par=run(simCache,par,address);
					  par=run(simCache,par,address);
					  break;
				default : break;
			}
		} 
	}
	printSummary(par.hits,par.misses,par.evictions);
	clrCache(simCache,sets,par.E,blockSize);
	fclose(readTrace);
	return 0;
}
