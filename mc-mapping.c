#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/time.h>
#include <time.h>

#include <inttypes.h>

#include <sys/mman.h>
#include <fcntl.h>

#include <sys/types.h>
#include <errno.h>
#include <sys/resource.h>
#include <assert.h>

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define L3_NUM_WAYS   16                    // cat /sys/devices/system/cpu/cpu0/cache/index3/ways..
#define NUM_ENTRIES   (uint64_t)(L3_NUM_WAYS * 2)       // # of list entries to iterate
#define CACHE_LINE_SIZE 64

#define MAX(a,b) ((a>b)?(a):(b))
#define CEIL(val,unit) (((val + unit - 1)/unit)*unit)

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
   __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)


#define ENTRY_SHIFT_CNT (5)
static uint64_t entry_shift[ENTRY_SHIFT_CNT];

#define NUM_DIST ((uint64_t)1 << ENTRY_SHIFT_CNT)
static uint64_t entry_dist[NUM_DIST] = {0};
static uint64_t g_mem_size = 0;

//#define TST 1

/* bank data */
static int* list;
/* index array for accessing banks */
static uint64_t indices[NUM_ENTRIES];
static uint64_t next;

/**************************************************************************
 * Public Function Prototypes
 **************************************************************************/
uint64_t get_elapsed(struct timespec *start, struct timespec *end)
{
	uint64_t dur;
	if (start->tv_nsec > end->tv_nsec)
		dur = (uint64_t)(end->tv_sec - 1 - start->tv_sec) * 1000000000 +
			(1000000000 + end->tv_nsec - start->tv_nsec);
	else
		dur = (uint64_t)(end->tv_sec - start->tv_sec) * 1000000000 +
			(end->tv_nsec - start->tv_nsec);

	return dur;

}
#define SEED (uint64_t)0x49720718

void run(uint64_t iter)
{
	uint64_t i, j = 0, tmp;
	int data, global_id, base, in_id, random_id = 0;

	for (i = 0; i < iter; i++){
		//printf("%luth: access list[0x%lx], next time: indices[j=%lu] = 0x%lx\n", i, next, j, indices[j]);
		data = list[next];	
		
		next = indices[j];
		//printf("next : %luth:indices[%lu] = 0x%lx\n", i + 1, j, next);
		
		/* shuffle every 16 item */
		if(j % L3_NUM_WAYS == 15){
			global_id = j;
			base = (global_id / L3_NUM_WAYS) * L3_NUM_WAYS;
			in_id = 15;
			while(in_id >= 1){
				random_id = (SEED * random_id + 1013904223 + data * 3) % in_id + base;
				
				tmp = indices[global_id];
				indices[global_id] = indices[random_id];
				indices[random_id] = tmp;

				in_id --;
				global_id --;
			}
		}

		j = (j + 1) % NUM_ENTRIES;
	}
}


int main(int argc, char* argv[])
{
	cpu_set_t cmask;
	int num_processors;
	int cpuid = 0;

	int *memchunk = NULL;
	int opt;
	int i, j;

	uint64_t naccess = 0;

	int bank_bit = -1;
	int xor_bank_bit = -1;

	//init entry_shift: increase
	entry_shift[0] = 23;
	entry_shift[1] = 24;
	entry_shift[2] = 26;
	entry_shift[3] = 28;
	entry_shift[4] = 29;
	//entry_shift[5] = 30;
	uint64_t min_interval = ((uint64_t)1 << entry_shift[1]) - ((uint64_t)1 << entry_shift[0]);
	
	//printf("min_interval = 0x%lx\n", min_interval);
	
	uint64_t farest_dist = 0;

	//printf("****************init entry_dist:\n");
	for(i = 0; i < NUM_DIST; i ++){
		j = 0;
		int index = i;
		while(index > 0){
			if(index & 1) entry_dist[i] += ((uint64_t)1 << entry_shift[j]);
			j ++;
			index = index >> 1;
		}
		if(farest_dist < entry_dist[i]) farest_dist = entry_dist[i];

		//printf("entry_dist[%d] = 0x%lx\n", i, entry_dist[i]);
	}

	printf("farest_dist = 0x%lx\n", farest_dist);
	/*
	 * get command line options 
	 */
	while ((opt = getopt(argc, argv, "a:xb:s:o:m:c:i:l:h")) != -1) {
		switch (opt) {
			case 'b': /* bank bit */
				bank_bit = strtol(optarg, NULL, 0);
				break;
			case 's': /* xor-bank bit */
				xor_bank_bit = strtol(optarg, NULL, 0);
				break;
			case 'c': /* set CPU affinity */
				cpuid = strtol(optarg, NULL, 0);
				num_processors = sysconf(_SC_NPROCESSORS_CONF);
				CPU_ZERO(&cmask);
				CPU_SET(cpuid % num_processors, &cmask);
				if (sched_setaffinity(0, num_processors, &cmask) < 0)
					perror("error");
				break;
			case 'i': /* iterations */
				naccess = (uint64_t)strtol(optarg, NULL, 0);
				break;
		}

	}


	if(xor_bank_bit >= 0)
		g_mem_size += (uint64_t)1 << xor_bank_bit;

	if(bank_bit >= 0)
		g_mem_size += (uint64_t)1 << bank_bit;
	else{
		printf("test bank bit < 0\n");
		exit(1);
	}

	struct timespec seed;
	int mask[NUM_DIST] = {0};
	int ibit = 0;

	
	if(NUM_DIST < NUM_ENTRIES){
		printf("need %lu different items!\n", NUM_ENTRIES);
		exit(1);
	}

	//printf("***************init indices:\n");
	for(i = 0; i < NUM_ENTRIES; i ++){
		while(1){
			clock_gettime(CLOCK_REALTIME, &seed);
			ibit = seed.tv_nsec % NUM_DIST;
			if(mask[ibit] == 0){
				mask[ibit] ++;
				break;
			}
		}
		indices[i] = entry_dist[ibit] / 4;
		//printf("indices[%d] = 0x%lx\n", i, indices[i]);
	}

	g_mem_size +=  farest_dist;
#ifdef TST
	g_mem_size += (uint64_t)1 << 20;
#endif
	
	g_mem_size = CEIL(g_mem_size, min_interval);

	/* alloc memory. align to a page boundary */
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	void *addr = (void *) 0x1000000100000000;

	if (fd < 0){
		perror("Open failed");
		exit(1);
	}

	memchunk = mmap(0, g_mem_size,
			PROT_READ | PROT_WRITE, 
			MAP_SHARED, 
			fd, (off_t)addr);

	if (memchunk == MAP_FAILED) {
		perror("failed to alloc");
		exit(1);
	}

	int off_idx;

	if(xor_bank_bit >= 0){
		off_idx = (((uint64_t)1 << bank_bit) + ((uint64_t)1 << xor_bank_bit)) / 4;
	}
	else
		off_idx = ((uint64_t)1 << bank_bit) / 4;

#ifdef TST
	off_idx += ((uint64_t)1 << 20) / 4;
#endif

	list = &memchunk[off_idx];

	next = 0; 
	struct timespec start, end;
	clock_gettime(CLOCK_REALTIME, &start);

	/* access banks */
	run(naccess);

	clock_gettime(CLOCK_REALTIME, &end);

	uint64_t nsdiff = get_elapsed(&start, &end);
	
	printf("time %.2f s\n", (double)nsdiff / 1000000000.0);
	printf("bandwidth %.2f MB/s\n", 64.0*1000.0*(double)naccess/(double)nsdiff);

	return 0;
}
