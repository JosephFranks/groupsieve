/*
Copyright (c) 2014 Joseph B. Franks

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
#include <sys/types.h>

#ifndef GROUPSIEVE_H

#define BLOCK_SIZE 32000  //Should probably be set to L1 cache size for fastest speed
#define PARRAY_SIZE 100000 //Sets the size of the primes array. More than enough given MAX_NUMBER.
#define NUM_THREADS 4 //Sets the number of threads to use.  Should probably equal number of cores
#define MAX_NUMBER 10000000000 
//MAX_NUMBER is the limit on memory, in bytes, that will be malloc'd. Not this
//much memory will be malloc'd unless you want to find all primes up to 10 times this number.
//This will be removed in the future.

//Function declarations
void printInstructions(char*);
inline int getWheelSize(int);
int rollWheel(int, int);
void finishPrimes(int);
void multiFinishPrimes(int);
void* primeThread(void*);
void getPrimes(u_int64_t);
void wheelRemove(u_int8_t, unsigned int);
inline void getCycleInfo(u_int64_t);
inline void determineGroup(u_int64_t);
inline void singleRemoveComposites(u_int64_t, u_int64_t);
inline void multiRemoveComposites(u_int64_t, u_int64_t);
void singlePrintPrimes(u_int64_t);

#endif
