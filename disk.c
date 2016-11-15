/* relay.c 
 * Written by Bill Broadley bill@cse.ucdavis.edu
 * Version 0.2
 *
 * relay.c measures MPI bandwidth and latency for any size message for 2 to n nodes
 *         by sending a SINGLE message through a circularly linked list of nodes.
 *         Only a single message is in flight at a time.
 *
 * to test latency:
 * for i in 2 4 6 8; do mpirun -np $i -machinefile node_list ./relay 1; done
 * 
 * to test bandwidth:
 * for i in 2 4 6 8; do mpirun -np $i -machinefile node_list ./relay 1000000; done
 */

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>



#define MASTER_RANK 0

double gtod (void);

double
gtod ()
{
  struct timeval tv;
  gettimeofday (&tv, 0);
  return tv.tv_sec + 1e-6 * tv.tv_usec;
}

/* choose between l and h, inclusive of both. */
uint64_t
choose (uint64_t l, uint64_t h)
{
   uint64_t range, smallr, ret;

   range = h - l;
   assert (l <= h);
   smallr = range;
   ret = (l + (uint64_t) (drand48 () * smallr));
   assert (ret <= h);
   if (l < h)
      return ret;
   return h;
}


//# disk <size in GB> <size of read> <number of reads> 
int
main (int argc, char *argv[])
{
	int pool_size, my_rank, my_name_length;
	char my_name[MPI_MAX_PROCESSOR_NAME+1];
	char fname[256];
	MPI_Status status;
	double start=0, elapsed, rand_start, rand_stop, rand_diff;
	size_t rand;
	double rand_bandwidth, rand_IOPS;
	int ret,i,next,count;
	long long bytes;
	int MAX;
	int blockSize,num;
	size_t size,byteSize,byteCount;
	double *doubleBlock;
	FILE *fp;
	int numBlocks;
	int *mix;
	int maxMix;
	int maxSeek;
	size_t bytesRead;
	long int seed;
	
	// combine bime and getpid to ensure differences run to run
	// constant suggested by knuth or keep small PIDs changes from cancelling out
	// small time changes
	seed=(getpid()*2654435761U)^time(NULL);
	srand48(seed);
	printf ("Seed=%ld\n",seed);
	
	start = gtod ();
	MPI_Init (&argc, &argv);
	MPI_Comm_size (MPI_COMM_WORLD, &pool_size);
	MPI_Comm_rank (MPI_COMM_WORLD, &my_rank);
	MPI_Get_processor_name (my_name, &my_name_length);
	my_name_length++;
	my_name[my_name_length]=0;
 	size=(size_t)atoi(argv[1]);
 	blockSize=atoi(argv[2]);
 	num=atoi(argv[3]);
	if (my_rank == 0) {
		printf ("rank:%2d size=%zu blockSize=%d num=%d\n",my_rank,size,blockSize,num);
	}
		
	// share node names;
	if (my_rank == MASTER_RANK)
	{
		printf ("%s ", my_name);
		for (i = 1; i < pool_size; i++)
		{
  			MPI_Recv (my_name, MPI_MAX_PROCESSOR_NAME+1, MPI_CHAR, i, i, MPI_COMM_WORLD, &status);
			printf ("%s ", my_name);
		}
		printf ("\n");
	}
	else
	{
		MPI_Send (my_name, MPI_MAX_PROCESSOR_NAME+1, MPI_CHAR, MASTER_RANK, my_rank,
              MPI_COMM_WORLD);
	}
//	sprintf(fname,"bench-r%d",my_rank);
	sprintf(fname,"bench-r%d",my_rank);

//	if (my_rank<4) {
//	} else if (my_rank<8) {
//		sprintf(fname,"%sb/bench-r%d",argv[4],my_rank);
//	} else {
//		sprintf(fname,"%sc/bench-r%d",argv[4],my_rank);
//	}
	
	printf ("r%2d: fname=%s\n",my_rank,fname);	
	// all nodes stop here till all get here.
	MPI_Barrier(MPI_COMM_WORLD);
	byteSize=size*1024*1024*1024;
//	printf ("running for %f seconds, starting mix\n",gtod()-start);
	doubleBlock = (double *) malloc(blockSize*1024);
	numBlocks=byteSize/(blockSize*1024);
	mix = (int *) malloc(numBlocks*sizeof(int));
   /* initiallize each int to point to next int */
	for (i=0;i<(numBlocks-1);i++)
	{
		mix[i]=i+1;
	}	
	mix[numBlocks-1]=0;
	for (i=0;i<(numBlocks);i++)
	{
		int c,x;
		c=choose(i,numBlocks-1);
		x=mix[i];
		mix[i]=mix[c];
		mix[c]=x;	
//		if (my_rank==0) {
//			printf ("mix=%d\n",mix[i]);
//		}
	}
	if (my_rank == 0) {
		printf ("reading from file %s, others from their rank\n",fname);
	}
	fp=fopen(fname,"r");
	if (fp == NULL) {
		printf("can't open %s\n",fname);
		exit(-1);
	}
	// all nodes stop here till all get here.
	MPI_Barrier(MPI_COMM_WORLD);
	printf ("rank %2d: %f seconds, starting rand timer\n",my_rank, gtod()-start);
	rand_start=gtod();
   // reset for while loop
	count=0; ret=1;
 //  printf("before rand: while count=%d num=%d ret=%d\n",count,num,ret);
 //  printf("before rand: bytesize=%zu blocksize=%d\n",byteSize,blockSize*1024);
	maxMix=0; maxSeek=0; bytesRead=0;
	while ((ret=1)&&(count<num))
	{
		double percent;
		rand=mix[count];
		if (rand>maxMix) {
			maxMix=rand;
		}
		percent=((double)rand/(double)numBlocks)*100;
		ret=fseek(fp,rand*blockSize*1024,SEEK_SET);
		if (my_rank==0) {
//			printf("numBlocks=%d rand=%zd percent=%f ret=%d count=%d num=%d\n",numBlocks,rand,percent,ret,count,num);
		}
		if (ret!=0) {
			printf ("fseek ret=%d count=%d\n",ret,count);
		}
		ret=fread(doubleBlock,blockSize*1024,1,fp);
		bytesRead=bytesRead+blockSize*1024;
		if (ret!=1) {
			printf ("fread ret=%d count=%d\n",ret,count);
		}
//		printf ("rand=%zu numBlocks=%d\n",rand,numBlocks);
		for (i=0;i<(blockSize/sizeof(double));i++)
		{
			if (doubleBlock[i]==3.14159)
			{
				printf ("found pi at %d\n",i);
			}
		}
		count++;
	}
   /* this may vary */
	printf ("rank %2d: %f seconds, stopping rand timer\n",my_rank,gtod()-start);
	MPI_Barrier(MPI_COMM_WORLD);
	/* this should be VERY close across all nodes */
	rand_stop=gtod(); 
	close(fp);
	MPI_Finalize ();

	if (my_rank==0) { 
		printf ("rank %2d read %zd bytes, %d KB at a time\n",my_rank,bytesRead,blockSize);
		printf ("ran %d times through the rand loop, maxmix=%d\n",count,maxMix);
	}
	if (my_rank == MASTER_RANK)
	{
//    bytes = ((long long) MAX * (long long) PACKET_SIZE) /1024 * sizeof(int);
 //   printf
  //    ("size=%6d, %6d hops, %2d nodes in %6.2f sec (%6.2f us/hop) %6.0f KB/sec\n",
   //    PACKET_SIZE, MAX, pool_size, elapsed, (elapsed / MAX)*1e6, (long long) bytes / elapsed);
		rand_diff=rand_stop-rand_start;
      // MB/sec
      // MB/sec
		rand_bandwidth=(pool_size*((double)blockSize/1024)*num) / rand_diff;
		// IOPS
		rand_IOPS = (pool_size * num ) / rand_diff;
		printf ("pool_size=%d numBlocks=%d\n",pool_size,numBlocks);
		printf ("IOPS = %f \n",rand_IOPS);
	}
	return (0);
}

