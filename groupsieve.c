/*
Copyright (c) 2014 Joseph B. Franks

https://github.com/JosephFranks/groupsieve.git

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>
#include "groupsieve.h"


//Global variables
int tabslot = 3;
u_int64_t slotCounter = 1;
int next = 3;
int primeCount = 3;
static int startIndex;
static u_int64_t lastPrimeIndex;
static u_int64_t maxSlots;
u_int64_t primes[PARRAY_SIZE]; //50847534 primes before 1x10^9
u_int64_t lastNum[PARRAY_SIZE];
u_int8_t lastCycle[PARRAY_SIZE];
u_int64_t cycleInfo[PARRAY_SIZE][4];
u_int64_t groupInfo[PARRAY_SIZE][4];

u_int8_t* table;
/*
table is the bit field that keeps track of the primes.  The way it works is that if you want
to find all primes up to 100000, table will be allocated 100000/10 = 10000 slots. Each slot in the table 
contains info for 10 numbers.  For instance, table[0] contains info for 0-9, table[1] contains
info for 10-19, table[2] contains info for 20-29 and so forth.
  
Since the only numbers other than 2 and 5 that can be prime have last digits 1,3,7, or 9, 
we use the first four bits of each slot in the table.  Here is the key for the bits:
1 bit corresponds to 1
2 bit corresponds to 3
4 bit corresponds to 7
8 bit corresponds to 9

If a bit is set to 1, it means prime, if the bit is set to 0, it means composite.
For instance, say we are looking at slot 2 of the table.  After sieving,
table[2] should contain 0b00001010 since 23 and 29 are primes, but 21 and 27 are composites.

At first, we assume all numbers are prime.  By the fundamental theorem of algebra, we know that
any number can be decomposed into a product of primes.  We determine that a number is composite if it is a non-unity multiple of some prime or primes.  

The way that a composite number is removed from the table is as follows:
Say we want to remove 11*11 from the table.  11*11=121 would be in table[12], so we want to
bitwise & table[12].  Since 121%10=1, we want to set the 1 bit of table[12] to 0 to mark it
as composite.  So, we would do table[12] &= 14 since 14 = 0b00001110.
The key for the bitwise &'s is:
1 corresponds to 0b00001110
3 corresponds to 0b00001101
7 corresponds to 0b00001011
9 corresponds to 0b00000111

Right now, the code allocates double the memory it really needs to.
I'm going to rewrite this code to use all 8 bits of each table slot so that memory usage is lower
and so that more relevant data can be fit into caches.  While this will make some things
a little more tricky, it should end up being faster. 

Additionally, I'm going to rewrite it so that instead of mallocing a potentially huge chunk 
of memory, it only mallocs either the wheel size or a fairly small multiple of BLOCK_SIZE 
for table.  Then a second array of the same size will be 
used that copies the data from table, sieves it, and then 
copies the data from table again.  That way we should be able to find primes up to 2^64-1
instead of being limited by how much memory the OS is willing to malloc.
*/

//This is the main function.  It takes arguments from the command line to determine
//the wheel size to use and whether or not to print out the primes.
int main(int argc, char *argv[])
{	
	int wheelSize;
	u_int64_t maxNum;
	
	//Checks the program was passed the proper number of arguments
	if (( argc < 3 ) || ( argc > 4))
	{
        printInstructions(argv[0]);
		return 1;
    }
    
    //Get command line arguments
    maxNum = strtol(argv[1], NULL, 0);;
    wheelSize = atoi(argv[2]);
    
    //Checks the passed arguments are all integers and within bounds
    if ((maxNum == 0) || (maxNum > MAX_NUMBER) || (wheelSize <= 0) || (wheelSize > 6))
    {
		printInstructions(argv[0]);
		return 1;
	}
	
	int i;
	int j;
	
	for (j = 0; j < PARRAY_SIZE; j++)
	{
		lastNum[j] = 0;
		lastCycle[j] = 1;
	}
    
    //This conditional makes sure we get all the primes desired
    if (maxNum < 210)
    {
		//If we're looking for a small number of primes, just sieve up to 210 
		maxSlots = 21;
	}
	else
    {
		if (maxNum%10 == 0)
		{
			maxSlots = maxNum/10;
		}
		else
		{
			maxSlots = maxNum/10+1;
		}
	}
	
	//Checks that the wheel size doesn't exceed the number of slots we're using
    u_int64_t wheelCheck = getWheelSize(wheelSize);
    if (wheelCheck > maxSlots)
    {
		printf("Error: Wheel size specified is larger than the Max slots specified.\n");
		printf("Please try smaller wheel size\n");
		return 1;
	}
    
    //malloc memory for the table, which is a bit field of 8-bit integers, that keeps
    //track of all the primes
    if ((table = (u_int8_t *) malloc(maxSlots*sizeof(u_int8_t))) == NULL)
    {
		printf("Error: problem allocating memory for the table\n");
		return 1;
	}

	//The first four primes are hardcoded.
	primes[0] = 2;
	primes[1] = 3;
	primes[2] = 5;
	primes[3] = 7;
	
	//Set the values in the table for 3's cycle.  3 generates (Z/10,+) (The group of
	//integers mod 10 under addition) every 30 numbers.
	table[0] = 5;
	table[1] = 15;
	table[2] = 10;	

	//Set tabslot to 21 since 3's cycle takes 30 numbers, 7's cycle takes 70 numbers.
	//Their cycles both start with no overlap at 210, which is 3*7*10.
	tabslot = 21;
	
	//Copy the values for 3's cycle until it completes through 210
	for (i = 3; i<tabslot; i+=3)
	{
		table[i] = table[0];
		table[i+1] = table[1];
		table[i+2] = table[2];
	}
	
	//Remove 3 as a potential prime for future cycles
	table[0] = 1;
	
	//
	switch (maxSlots%3)
	{
		case 0:
		{
			//We're already done
			break;
		}
		case 1:
		{
			table[i] = table[0];
			break;
		}
		case 2:
		{	
			table[i] = table[0];
			table[i+1] = table[1];
			break;
		}
		default:
		{
			printf("Error: hit the default case while filling the table with 3's wheel\n");
			return 1;
		}
	}
	
	//Get primes up to 49
	getPrimes(3);
	
	//Mark off wheels up to wheelSize and then roll the wheel to maxSlots	
	int nextPrime = rollWheel(wheelSize, 3);
	
	//Get primes up to the square of the next prime number
	getPrimes(nextPrime);
	nextPrime++;
	
	//Determine if single or multithreaded and mark off remaining composites
	//If BLOCK_SIZE>maxSlots, just ignore NUM_THREADS and use single thread
	if ((NUM_THREADS == 1) || (BLOCK_SIZE > maxSlots))
	{
		finishPrimes(nextPrime);
	}
	else if (NUM_THREADS > 1)
	{
		multiFinishPrimes(nextPrime);
	}
	else
	{
		printf("Error: NUM_THREADS must be greater than or equal to 1\n");
		printf("Please change the value of NUM_THREADS and recompile\n");
		exit(-1);
	}
	
	//If a fourth argument is supplied at runtime, print the list of primes. 
	if (argv[3])
	{
		singlePrintPrimes(maxNum);
	}
	
	//Cleanup
	//free(table);
	
}

//Print the instructions if input was not supplied properly
void printInstructions(char* progName)
{
	printf("Proper usage is: \n");
	printf("\n");
    printf("%s maxInt wheelSize -print\n", progName );
    printf("\n");
	printf("maxInt: The positive integer you want to find primes up to.\n");
	printf("wheelSize: Can be any integer from 1-7.  See readme for more info.\n");
	printf("-print is optional.  If it is included, it will print out the primes.\n");
	printf("\n");
	printf("If you're having trouble, the readme has a comprehensive explanation of the program and the inputs.\n");
}

//Returns the wheel size based on wheelNum supplied at run time
inline int getWheelSize(int wheelNum)
{
	switch (wheelNum)
	{
		case 1:
			return 21; //3*7
		case 2:
			return 231; //3*7*11
		case 3:
			return 3003; //3*7*11*13
		case 4:
			return 51051; //3*7*11*13*17
		case 5:
			return 969969; //3*7*11*13*17*19
		case 6:
			return 22309287; //3*7*11*13*17*19*23
		case 7:
			return 646969323; //3*7*11*13*17*19*23*29
		case 8:
			return maxSlots; //20056049013;
		default:
		{
			printf("Error while getting the wheel size.\n");				
			exit(-1);
		}
	}
}

//This function takes the input wheel size and marks off composites by determining
//each prime's cycle and "rolling" that wheel over the table to remove all potentially
//prime multiples of the prime.
int rollWheel(int wheelNum, int currPrime)
{
	unsigned int i;
	u_int64_t j;
	u_int64_t count = 0;
	int currWheel;
	int nextWheel=21;

	for (i=1; i<wheelNum; i++)
	{
		currWheel = getWheelSize(i);
		nextWheel = getWheelSize(i+1);
		
		//Determine the cycle for the current prime and remove all potentially prime multiples of it
		wheelRemove((u_int8_t)primes[currPrime], currWheel);
		
		//Copy the values in the table up to the next wheel
		for (j = currWheel; j < nextWheel; j++)
		{
				table[j] = table[count];
				count++;
		}
		
		count = 0;
		currPrime++;
	}
	
	//Remove all multiples of the last prime we're rolling
	wheelRemove((u_int8_t)primes[currPrime], nextWheel);
	
	//Copy the values in the table up to max slots
	for (j = nextWheel; j < maxSlots; j++)
	{
			table[j] = table[count];
			count++;
	}
	
	return currPrime;
}

//Remove all potentially prime multiples of all primes until prime*prime is greater
//than the maximum number of slots in table.  This is the single threaded version.
void finishPrimes(int currPrime)
{
	int i;
	int blockNum;
	int pindex = currPrime;
	int startIndex = currPrime;
	u_int64_t stop =  sqrt(maxSlots*10); //stops when prime*prime>maxSlots
	u_int64_t minSize = BLOCK_SIZE;
	
	//Checks that we only find primes up to maxSlots
	if (BLOCK_SIZE > maxSlots)
	{
		minSize = maxSlots;
	}
	
	int blockCounter = BLOCK_SIZE;
	
	//Keep removing multiples of primes from the first block of the table.  
	//It also keeps getting primes from finished entries of the table.  
	//It does this until it finds a prime such that prime > (maxSlot)^(1/2).
	//If we need more primes than are in the first block, it continues sieving,
	//block by block, until the condition above is met.
	while (primes[currPrime] <= stop)
	{
		//If we need primes larger than BLOCK_SIZE, sieve the next block and keep getting
		//primes.  
		if (primes[currPrime]>blockCounter)
		{
			currPrime = startIndex;
			blockCounter += BLOCK_SIZE;
			minSize = blockCounter;  
		}
		
		//Remove multiples of the current prime
		singleRemoveComposites(currPrime, minSize);
		currPrime++;
		
		//If we've sieved this block using all the primes we've gotten, get more primes
		//and continue sieving
		if (primeCount-currPrime == 0)
		{
			getPrimes(currPrime-1);
		}
	}
	
	//Increment blockCounter so we start sieving the next block.
	blockCounter += BLOCK_SIZE;
	
	//lastPrimeIndex is the index of the greatest prime such that prime*prime <= maxSlots 
	lastPrimeIndex = currPrime-1;
	u_int64_t thisBlock;
	
	//Continue sieving, one block at a time.  thisBlock keeps track of what block we're
	//currently sieving
	for (blockNum = blockCounter/BLOCK_SIZE; (thisBlock = blockNum*BLOCK_SIZE) < maxSlots; blockNum++)
	{	
		//This loop sieves a block.
		for (i=startIndex; i<= lastPrimeIndex; i++)
		{
			singleRemoveComposites(i, thisBlock);
		}
	}
	
	//Sieve the last block if it hasn't already been done
	if (minSize == BLOCK_SIZE)
	{
		for (i=startIndex; i<= lastPrimeIndex; i++)
		{
			singleRemoveComposites(i, maxSlots);
		}
	}
	//At this point, we're removed all composite numbers from the table.
}

//Remove all potentially prime multiples of all primes until prime*prime is greater
//than the maximum number of slots in table.  This is the multi-threaded version.
void multiFinishPrimes(int currPrime)
{
	pthread_t tid[NUM_THREADS];
	int threadCount = 0;
	int i;
	int blockNum;
	int pindex = currPrime;
	startIndex = currPrime;
	u_int64_t stop =  sqrt(maxSlots*10); //stops when prime*prime>maxSlots
	u_int64_t minSize = BLOCK_SIZE;
	
	//Checks that we only find primes up to maxSlots
	//if (BLOCK_SIZE > maxSlots)
	//{
		//minSize = maxSlots;
	//}
	
	int blockCounter = BLOCK_SIZE;
	//Keep removing multiples of primes from the first block of the table.  
	//It also keeps getting primes from finished entries of the table.  
	//It does this until it finds a prime such that prime > (maxSlot)^(1/2).
	//If we need more primes than are in the first block, it continues sieving,
	//block by block, until the condition above is met.  This is still single threaded,
	//so there could be a slight performance gain by multi-threading this code, especially
	//for large maxSlots.
	while (primes[currPrime] <= stop)
	{
		//If we need primes larger than BLOCK_SIZE, sieve the next block and keep getting
		//primes.
		if (primes[currPrime]>blockCounter)
		{
			currPrime = startIndex;
			blockCounter += BLOCK_SIZE;
			minSize = blockCounter;  
		}
		
		//Remove multiples of the current prime
		singleRemoveComposites(currPrime, minSize);
		currPrime++;
		
		//If we've sieved this block using all the primes we've gotten, get more primes
		//and continue sieving
		if (primeCount-currPrime == 0)
		{
			getPrimes(currPrime-1);
		}
	}
	
	//lastPrimeIndex is the index of the greatest prime such that prime*prime <= maxSlots	
	lastPrimeIndex = currPrime-1;
	u_int64_t thisBlock;
	
	//Determine remaining blocks to be sieved
	int totalBlocks = maxSlots/BLOCK_SIZE-blockCounter/BLOCK_SIZE+1;
	
	//Increment blockCounter so we start sieving the next block.
	blockCounter += BLOCK_SIZE;
	
	if (totalBlocks > NUM_THREADS) //Use all threads
	{
		//Spawn all threads
		for (i=0; i < NUM_THREADS; i++)
		{
			pthread_create(&tid[i], NULL, primeThread, (void*) ((u_int64_t) blockCounter+(i*BLOCK_SIZE)));
		}
		
		//Wait for threads to complete
		for (i=0; i<NUM_THREADS; i++)
		{
			pthread_join(tid[i], NULL);
		}
	}
	else //We only need a few threads given the blocksize and limit
	{
		//Spawn necessary threads
		for (i=0; i < totalBlocks-1; i++)
		{
			pthread_create(&tid[i], NULL, primeThread, (void*)((u_int64_t) blockCounter+(i*BLOCK_SIZE)));
		}
		
		pthread_create(&tid[totalBlocks-1], NULL, primeThread, (void*) maxSlots);
		
		//Wait for threads to complete
		for (i=0; i < totalBlocks; i++)
		{
			pthread_join(tid[i], NULL);
		}
	}
	
	//At this point, we've removed all composite numbers from the table.
}

//This is the thread that sieves blocks.  
void* primeThread(void* thisBlockVS)
{
	u_int64_t thisBlock = (u_int64_t) thisBlockVS;
	u_int64_t i;
	u_int64_t j;
	
	//Start by sieving the block that was passed to the thread.
	//Then, sieve the block that is thisBlock + BLOCK_SIZE*NUM_THREADS.
	//Continue doing this until we would sieve a block with numbers
	//larger than max slots.
	//This ensures that at any given time, all threads are modifying
	//completely independent parts of the table, which removes the need
	//for locking.
	for (j = thisBlock; j <= maxSlots; j += BLOCK_SIZE*NUM_THREADS)
	{
		//Sieve this block with all necessary primes
		for (i=startIndex; i<= lastPrimeIndex; i++)
		{
			multiRemoveComposites(i, j);
		}
	}
	
	u_int64_t lastBlock = j - BLOCK_SIZE*NUM_THREADS;
	//If this thread finished the block closest to maxSlots,
	//then sieve the final block up to maxSlots if it hasn't already been sieved.
	//This makes sure we sieve up to the end of the table while only using one thread
	//to sieve the last piece of the table.
	if ((maxSlots-lastBlock <= BLOCK_SIZE) && (maxSlots-lastBlock > 0))
	{
		//Sieve this piece of the table with all necessary primes
		for (i=startIndex; i<= lastPrimeIndex; i++)
		{
			multiRemoveComposites(i, maxSlots);
		}
	}
}

//This function gets prime numbers from the table and places them in the
//primes array.
void getPrimes(u_int64_t currPrime)
{	
	int pri = primes[currPrime];
	int stopCheck = sqrt(maxSlots*10);
	
	//This switch removes the square of the next prime from the table so we can
	//get primes up to prime*prime (since prime*prime is the first composite that
	//this prime will remove that hasn't been removed already).
	//There's probably a better place for this code, so it needs to be changed. 
	int remove = pri*pri;
	switch (remove%10)
	{
		case 1:
		{
			table[remove/10] &= 14;				
			break;
		}
		case 3:
		{
			table[remove/10] &= 13;				
			break;
		}
		case 7:
		{
			table[remove/10] &= 11;				
			break;
		}
		case 9:
		{
			table[remove/10] &= 7;				
			break;
		}
		default:
		{
			printf("We hit default case while getting rid of square.\n");				
			break;
		}
	}//end of switch
	
	int i;
	int thisSquare = remove/10;	
	int mult;
	
	if (primes[primeCount] >= stopCheck)
	{
		return;
	}
	//Get primes from the table up to prime*prime and place them in the primes array.
	//For each prime found, we determine the group and jumps of this prime, since
	//each prime found in this function will be used for sieving.
	for (i=slotCounter; i<=thisSquare;i++)
	{		
		mult = i*10;
		switch (table[i])
		{
			case 0:
			{
				//This means there are no primes in table[i]
				break;
			}
			case 1:
			{
				//This means table[i] contains prime i*10+1
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
				
			}
			case 2:
			{
				//This means table[i] contains primes i*10+3
				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 3:
			{
				//This means table[i] contains primes i*10+1 and i*10+3
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
		
				break;
			}
			case 4:
			{
				//This means table[i] contains prime i*10+7
				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 5:
			{
				//This means table[i] contains primes i*10+1 and i*10+7
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 6:
			{
				//This means table[i] contains primes i*10+3 and i*10+7
				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 7:
			{	
				//This means table[i] contains primes i*10+1, i*10+3, and i*10+7
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);				
				
				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 8:
			{
				//This means table[i] contains prime i*10+9
				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;			
			}
			case 9:
			{
				//This means table[i] contains primes i*10+1 and i*10+9
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 10:
			{
				//This means table[i] contains primes i*10+3 and i*10+9
				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;

			}
			case 11:
			{
				//This means table[i] contains primes i*10+1, i*10+3, and i*10+9
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;

			}
			case 12:
			{
				//This means table[i] contains primes i*10+7 and i*10+9
				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 13:
			{
				//This means table[i] contains primes i*10+1, i*10+7, and i*10+9 
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			case 14:
			{
				//This means table[i] contains prime i*10+3, i*10+7, and i*10+9
				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;

			}
			case 15:
			{
				//This means table[i] contains primes i*10+1, i*10+3, i*10+7, and i*10+9
				primeCount++;
				primes[primeCount] = mult+1;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+3;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				primeCount++;
				primes[primeCount] = mult+7;
				getCycleInfo(primeCount);
				determineGroup(primeCount);

				primeCount++;
				primes[primeCount] = mult+9;
				getCycleInfo(primeCount);
				determineGroup(primeCount);
				
				break;
			}
			default:
			{
				printf("We hit the default case in getPrimes.\n");
			}

		}//end of switch
		
		//NEED TO CHECK AND FIX
		//This checks if prime*prime>maxSlots.  If so, we've found all the primes
		//we need for sieving, so break the loop.
		if (primes[primeCount] >= stopCheck)
		{
			thisSquare=i;
			//printf("i= %d prime = %d\n", i, primes[primeCount]);
			break;
		}
	}
	
	//Keeps track of the slots in the table that we've already gotten primes from
	slotCounter = thisSquare+1;
	//printf("sc = %d\n", slotCounter);
}

//This function takes a prime and removes potentially prime multiples of the
//given prime from the table until wheelSize is reached.  wheelSize is the point
//at which this prime's cycle and all previously removed prime's cycles start 
//over again with no overlap.
//For instance, 13's cycle generates (Z/10,+) at 130. wheelSize for 13 is 
//3*7*9*11*13*10
void wheelRemove(u_int8_t prime, unsigned int wheelSize)
{
	//Determine the jumps in the table in between multiples of the prime
	u_int8_t addindex = prime/10;
	u_int8_t jumpOne = (prime*3)/10;
	u_int8_t jumpTwo = (prime*7)/10;
	u_int8_t jumpThree = (prime*9)/10;
	
	u_int8_t first;
	u_int8_t second;
	u_int8_t third;
	u_int8_t fourth;
	
	u_int8_t test = prime%10;
	
	//This switch determines what group we're working with, which must be
	//either 1,3,7, or 9 mod 10.  
	switch (test)
	{
		case 1: 
		{
			first = 14;
			second = 13;
			third = 11;
			fourth = 7;				
			break;
		}
		case 3:
		{
			first = 13;
			second = 7;
			third = 14;
			fourth = 11;					
			break;
		}
		case 7:
		{
			first = 11;
			second = 14;
			third = 7;
			fourth = 13;						
			break;
		}
		case 9:
		{
			first = 7;
			second = 11;
			third = 13;
			fourth = 14;						
			break;
		}
		default:
		{
			printf("We hit default case while figuring out what group we're working with in wheelRemove.\n");				
			break;
		}
	}//end of switch
	
	//This code takes care of the first cycle
	table[addindex] &= first;
	table[jumpOne] &= second;
	table[jumpTwo] &= third;
	table[jumpThree] &= fourth;
	
	unsigned int i;
	//This loop iterates over the group cycle until we reach the desired wheel size for
	//this prime.
	for (i = prime; i < wheelSize; i += prime)
	{
		table[i + addindex] &= first;
		table[i + jumpOne] &= second;
		table[i + jumpTwo] &= third;
		table[i + jumpThree] &= fourth;
	} 
}

//This function determines the jumps in the table in between potentially prime multiples
//of the given prime
inline void getCycleInfo(u_int64_t pindex)
{
	u_int64_t prime = primes[pindex];
	
	cycleInfo[pindex][0] = prime/10;
	cycleInfo[pindex][1] = (prime*3)/10;
	cycleInfo[pindex][2] = (prime*7)/10;
	cycleInfo[pindex][3] = (prime*9)/10;
}

//This function determines what element of the group (Z/10,+) we're working with, 
//which must be either 1,3,7, or 9 mod 10.  It stores the order in which (Z/10,+)
//is generated in the groupInfo array. 
inline void determineGroup(u_int64_t pindex)
{	
	switch (primes[pindex]%10)
	{
		case 1: 
		{
			//This prime is congruent to 1mod10.
			//Hence, this prime generates (Z/10,+) as 1,2,3,4,5,6,7,8,9,0
			//However, potential primes only have last digit 1,3,7, or 9
			groupInfo[pindex][0] = 14; //1
			groupInfo[pindex][1] = 13; //3
			groupInfo[pindex][2] = 11; //7
			groupInfo[pindex][3] = 7;  //9			
			break;
		}
		case 3:
		{
			//This prime is congruent to 3mod10.
			//3mod10 generates the group (Z/10,+) as 3,6,9,2,5,8,1,4,7,0
			groupInfo[pindex][0] = 13; //3
			groupInfo[pindex][1] = 7;  //9
			groupInfo[pindex][2] = 14; //1
			groupInfo[pindex][3] = 11; //7			
			break;
		}
		case 7:
		{
			//This prime is congruent to 7mod10.
			groupInfo[pindex][0] = 11; //7
			groupInfo[pindex][1] = 14; //1
			groupInfo[pindex][2] = 7;  //9
			groupInfo[pindex][3] = 13; //3				
			break;
		}
		case 9:
		{
			//This prime is congruent to 9mod10.
			groupInfo[pindex][0] = 7;  //9
			groupInfo[pindex][1] = 11; //7
			groupInfo[pindex][2] = 13; //3
			groupInfo[pindex][3] = 14; //1				
			break;
		}
		default:
		{
			printf("We hit default case while figuring out what group we're working with in determineGroup.\n");				
			return;
		}
	}//end of switch
}

//This function takes a prime and removes all potentially prime multiples
//of that prime from the table up to stopIndex, where stopIndex is the 
//block we're currently sieving.  This is the single threaded version.
inline void singleRemoveComposites(u_int64_t pindex, u_int64_t stopIndex)
{
	u_int64_t prime = primes[pindex];
	
	//Get the jumps in between potentially prime multiples of this prime
	u_int64_t addindex = cycleInfo[pindex][0];
	u_int64_t jumpOne = cycleInfo[pindex][1];
	u_int64_t jumpTwo = cycleInfo[pindex][2];
	u_int64_t jumpThree = cycleInfo[pindex][3];
	
	//Get the order that (Z/10,+) is generated by this prime
	u_int8_t first = groupInfo[pindex][0];
	u_int8_t second = groupInfo[pindex][1];
	u_int8_t third = groupInfo[pindex][2];
	u_int8_t fourth = groupInfo[pindex][3];
	
	u_int64_t start = lastNum[pindex];
	int stop = stopIndex-prime;
	int i = start;
	
	//This conditional determines if we can fit at least one cycle of this prime
	//in this block
	if (start <= stop)
	{
		//This switch takes care of the first cycle in this block
		//based on where in the cycle we stopped in the last block
		switch (lastCycle[pindex])
		{
			case 0: 
			{
				table[start + addindex] &= first;
				table[start + jumpOne] &= second;
				table[start + jumpTwo] &= third;
				table[start + jumpThree] &= fourth;				
				break;
			}
			case 1:
			{
				table[start + jumpOne] &= second;
				table[start + jumpTwo] &= third;
				table[start + jumpThree] &= fourth;
				break;
			}
			case 2:
			{
				table[start + jumpTwo] &= third;
				table[start + jumpThree] &= fourth;						
				break;
			}
			case 3:
			{
				table[start + jumpThree] &= fourth;						
				break;
			}
			default:
			{
				printf("We hit default case while figuring out where to start in genRem.\n");
				return;
			}
		}//end of switch
	
		start += prime;
		
		//Continue removing multiples of this prime from this block
		for (i = start; i <= stop; i += prime)
		{
			table[i + addindex] &= first;
			table[i + jumpOne] &= second;
			table[i + jumpTwo] &= third;
			table[i + jumpThree] &= fourth;
		}
		
		lastNum[pindex] = i;
		lastCycle[pindex] = 0;
	}
	
	//These conditionals remove the last multiples of this prime from this block
	//and also keeps track of where in the cycle of the prime we're stopping.
	if (i+addindex <= stopIndex)
	{
		table[i + addindex] &= first;
		lastCycle[pindex] = 1;
		
		if (i + jumpOne <= stopIndex)
		{
			table[i + jumpOne] &= second;
			lastCycle[pindex] = 2;

			if (i + jumpTwo <= stopIndex)
			{
				table[i + jumpTwo] &= third;
				lastCycle[pindex] = 3;
				
				if (i + jumpThree <= stopIndex)
				{
					table[i + jumpThree] &= fourth;
					lastCycle[pindex] = 0;
				}
			}
		}
	}
	
}

//This function takes a prime and removes all potentially prime multiples
//of that prime from the table up to stopIndex, where stopIndex is the 
//block we're currently sieving.  This is the multi-threaded version.
inline void multiRemoveComposites(u_int64_t pindex, u_int64_t stopIndex)
{
	u_int64_t prime = primes[pindex];
	
	//Get the jumps in between potentially prime multiples of this prime
	u_int64_t addIndex = cycleInfo[pindex][0];
	u_int64_t jumpOne = cycleInfo[pindex][1];
	u_int64_t jumpTwo = cycleInfo[pindex][2];
	u_int64_t jumpThree = cycleInfo[pindex][3];
	
	//Get the order that (Z/10,+) is generated by this prime
	u_int8_t first = groupInfo[pindex][0];
	u_int8_t second = groupInfo[pindex][1];
	u_int8_t third = groupInfo[pindex][2];
	u_int8_t fourth = groupInfo[pindex][3];
	
	//This code takes care of the first cycle in this block
	//based on where we stopped in the last block
	u_int64_t start=stopIndex-BLOCK_SIZE;
	u_int64_t prevStop = (start + (prime - (start%prime))) - prime;
	
	if (prime < BLOCK_SIZE)
	//We can fit at least one full cycle for this prime in this block
	{
		if (prevStop+jumpThree > start)
		{
			table[prevStop + jumpThree] &= fourth;
			if (prevStop+jumpTwo > start)
			{
				table[prevStop + jumpTwo] &= third;
				if (prevStop+jumpOne > start)
				{
					table[prevStop + jumpOne] &= second;
					if (prevStop+addIndex > start)
					{
						table[prevStop + addIndex] &= first;
					}
				}
			}
		}
		
		start = prevStop + prime;
			
		int stop = stopIndex-prime;
		int i;
		
		//Continue removing multiples of this prime in this block
		for (i = start; i <= stop; i += prime)
		{
			table[i + addIndex] &= first;
			table[i + jumpOne] &= second;
			table[i + jumpTwo] &= third;
			table[i + jumpThree] &= fourth;
		}
		
		//These conditionals remove the last multiples of this prime from this block.
		//They're necessary since a cycle of this prime may not fully complete in this
		//block.
		if (i+addIndex <= stopIndex)
		{
			table[i + addIndex] &= first;
			if (i + jumpOne <= stopIndex)
			{
				table[i + jumpOne] &= second;
				if (i + jumpTwo <= stopIndex)
				{
					table[i + jumpTwo] &= third;
					if (i + jumpThree <= stopIndex)
					{
						table[i + jumpThree] &= fourth;
					}
				}
			}
		}
	}
	else
	//The prime is larger than the block size
	{
		//This can probably be sped up.  These conditionals determine if we can
		//remove any multiples of this prime from this block.
		if ((prevStop+addIndex > start) && (prevStop+addIndex <= stopIndex))
		{
			table[prevStop + addIndex] &= first;
		}
		if ((prevStop+jumpOne > start) && (prevStop+jumpOne <= stopIndex))
		{
			table[prevStop + jumpOne] &= second;
		}
		if ((prevStop+jumpTwo > start) && (prevStop+jumpTwo <= stopIndex))
		{
			table[prevStop + jumpTwo] &= third;
		}
		if ((prevStop+jumpThree > start) && (prevStop+jumpThree <= stopIndex))
		{
			table[prevStop + jumpThree] &= fourth;
		}
		
		//Since the prime is larger than the block size, we've taken care of all
		//potential multiples of this prime in this block, so we can return safely.
	}
	
}

//Print out all the primes.  This is single threaded; I still need 
//to write a multi-threaded version.  It would probably be helpful to
//just write my own print method at some point, since I've seen other 
//sieves online that got massive performance gains from doing so.
void singlePrintPrimes(u_int64_t max)
{
	u_int64_t i = 0;
	u_int64_t mult;
	
	//This conditional takes care of the case when we're looking for small primes
	if (max <= primes[primeCount])
	{
		while ((primes[i] <= max) && (primes[i] != 0))
		{
			printf("%llu\n", primes[i]);
			i++;
		}
		
		//We're done!
		return;
	}
	
	//First, print out all the primes we have stored in the prime array.
	//These primes are placed here in the getPrimes function.
	for (i = 0; i <= primeCount; i++)
	{
		printf("%llu\n", primes[i]);
	}
	
	u_int64_t stop = maxSlots;
	if (max/10 < maxSlots)
	{
		stop = max/10+1;
	}
	
	//Print out the remaining primes by starting in the next slot of the table
	//not already checked by the getPrimes function.
	for (i=slotCounter; i<stop-1; i++)
	{
		mult = i*10;
		switch (table[i])
		{
			case 0:
			{
				//This means table[i] contains no primes
				break;
			}
			case 1:
			{
				//This means table[i] contains prime i*10+1
				printf("%llu\n", mult+1);
				break;	
			}
			case 2:
			{
				//This means table[i] contains prime i*10+3
				printf("%llu\n", mult+3);
				break;
			}
			case 3:
			{
				//This means table[i] contains primes i*10+1 and i*10+3
				printf("%llu\n%llu\n", mult+1, mult+3);
				break;
			}
			case 4:
			{
				//This means table[i] contains prime i*10+7
				printf("%llu\n", mult+7);
				break;
			}
			case 5:
			{
				//This means table[i] contains primes i*10+1 and i*10+7
				printf("%llu\n%llu\n", mult+1, mult+7);
				break;
			}
			case 6:
			{
				//This means table[i] contains primes i*10+3 and i*10+7
				printf("%llu\n%llu\n", mult+3, mult+7);
				break;
			}
			case 7:
			{	
				//This means table[i] contains primes i*10+1, i*10+3, and i*10+7
				printf("%llu\n%llu\n%llu\n", mult+1, mult+3, mult+7);	
				break;
			}
			case 8:
			{
				//This means table[i] contains prime i*10+9
				printf("%llu\n", mult+9);
				break;			
			}
			case 9:
			{
				//This means table[i] contains primes i*10+1 and i*10+9
				printf("%llu\n%llu\n", mult+1, mult+9);
				break;
			}
			case 10:
			{
				//This means table[i] contains primes i*10+3 and i*10+9
				printf("%llu\n%llu\n", mult+3, mult+9);
				break;
			}
			case 11:
			{
				//This means table[i] contains primes i*10+1, i*10+3, and i*10+9
				printf("%llu\n%llu\n%llu\n", mult+1, mult+3, mult+9);
				break;
			}
			case 12:
			{
				//This means table[i] contains primes i*10+7 and i*10+9
				printf("%llu\n%llu\n", mult+7, mult+9);
				break;
			}
			case 13:
			{
				//This means table[i] contains primes i*10+1, i*10+7, and i*10+9
				printf("%llu\n%llu\n%llu\n", mult+1, mult+7, mult+9);
				break;
			}
			case 14:
			{
				//This means table[i] contains primes i*10+3, i*10+7, and i*10+9
				printf("%llu\n%llu\n%llu\n", mult+3, mult+7, mult+9);
				break;

			}
			case 15:
			{
				//This means table[i] contains primes i*10+1, i*10+3, i*10+7, and i*10+9
				printf("%llu\n%llu\n%llu\n%llu\n", mult+1, mult+3, mult+7, mult+9);
				break;
			}
			default:
			{
				printf("We hit the default case in singlePrintPrimes.\n");
			}

		}//end of switch
	}
	
	//Do the last slot separately so we don't go over the bounds
	mult = (stop-1)*10;
	switch (table[stop-1])
	{
		case 0:
		{
			//This means table[i] contains no primes
			break;
		}
		case 1:
		{
			//This means table[i] contains prime i*10+1
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
			}
			break;	
		}
		case 2:
		{
			//This means table[i] contains prime i*10+3
			if (mult+3 <= max)
			{
				printf("%llu\n", mult+3);
			}
			break;
		}
		case 3:
		{
			//This means table[i] contains primes i*10+1 and i*10+3
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
				if (mult+3 <= max)
				{
					printf("%llu\n", mult+3);
				}
			}
			break;
		}
		case 4:
		{
			//This means table[i] contains prime i*10+7
			if (mult+7 <= max)
			{
				printf("%llu\n", mult+7);
			}
			break;
		}
		case 5:
		{
			//This means table[i] contains primes i*10+1 and i*10+7
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
				if (mult+7 <= max)
				{
					printf("%llu\n", mult+7);
				}
			}
			break;
		}
		case 6:
		{
			//This means table[i] contains primes i*10+3 and i*10+7
			if (mult+3 <= max)
			{
				printf("%llu\n", mult+3);
				if (mult+7 <= max)
				{
					printf("%llu\n", mult+7);
				}
			}
			break;
		}
		case 7:
		{	
			//This means table[i] contains primes i*10+1, i*10+3, and i*10+7
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
				if (mult+3 <= max)
				{
					printf("%llu\n", mult+3);
					if (mult+7 <= max)
					{
						printf("%llu\n", mult+7);
					}
				}
			}
			break;
		}
		case 8:
		{
			//This means table[i] contains prime i*10+9
			if (mult+9 <= max)
			{
				printf("%llu\n", mult+9);
			}
			break;			
		}
		case 9:
		{
			//This means table[i] contains primes i*10+1 and i*10+9
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
				if (mult+9 <= max)
				{
					printf("%llu\n", mult+9);
				}
			}
			break;
		}
		case 10:
		{
			//This means table[i] contains primes i*10+3 and i*10+9
			if (mult+3 <= max)
			{
				printf("%llu\n", mult+3);
				if (mult+9 <= max)
				{
					printf("%llu\n", mult+9);
				}
			}
			break;
		}
		case 11:
		{
			//This means table[i] contains primes i*10+1, i*10+3, and i*10+9
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
				if (mult+3 <= max)
				{
					printf("%llu\n", mult+3);
					if (mult+9 <= max)
					{
						printf("%llu\n", mult+9);
					}
				}
			}
			break;
		}
		case 12:
		{
			//This means table[i] contains primes i*10+7 and i*10+9
			if (mult+7 <= max)
			{
				printf("%llu\n", mult+7);
				if (mult+9 <= max)
				{
					printf("%llu\n", mult+9);
				}
			}
			break;
		}
		case 13:
		{
			//This means table[i] contains primes i*10+1, i*10+7, and i*10+9
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
				if (mult+7 <= max)
				{
					printf("%llu\n", mult+7);
					if (mult+9 <= max)
					{
						printf("%llu\n", mult+9);
					}
				}
			}
			break;
		}
		case 14:
		{
			//This means table[i] contains primes i*10+3, i*10+7, and i*10+9
			if (mult+3 <= max)
			{
				printf("%llu\n", mult+3);
				if (mult+7 <= max)
				{
					printf("%llu\n", mult+7);
					if (mult+9 <= max)
					{
						printf("%llu\n", mult+9);
					}
				}
			}
			break;
		}
		case 15:
		{
			//This means table[i] contains primes i*10+1, i*10+3, i*10+7, and i*10+9
			if (mult+1 <= max)
			{
				printf("%llu\n", mult+1);
				if (mult+3 <= max)
				{
					printf("%llu\n", mult+3);
					if (mult+7 <= max)
					{
						printf("%llu\n", mult+7);
						if (mult+9 <= max)
						{
							printf("%llu\n", mult+9);
						}
					}
				}
			}
			break;
		}
		default:
		{
			printf("We hit the default case in singlePrintPrimes.\n");
		}

	}//end of switch
	
}
