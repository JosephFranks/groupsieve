groupsieve
==========

groupsieve 1.0

Author: Joseph B. Franks

E-mail: jos.b.franks@gmail.com

groupsieve is a software program written in C that generates prime numbers
in order using a segmented sieve of Eratosthenes with optimizations 
based on the properties of additive groups of integers.  It's a work in
progress and will be updated frequently.  More information 
can be found here:
http://en.wikipedia.org/wiki/Sieve_of_Eratosthenes
http://en.wikipedia.org/wiki/Group_theory
http://en.wikipedia.org/wiki/Cyclic_group



###Table of Contents###
1. Compilation/Execution Instructions
2. Speed Comparisons
3. Explanation of Theory
4. Algorithm/Implementation Details



###1.Compilation/Execution Instructions###

First, change the values of BLOCK_SIZE and NUM_THREADS in groupsieve.h.
BLOCK_SIZE should be changed to the size of your L1 cache, in kB.
NUM_THREADS should be changed to the number of threads you wish to use.
It seems to be generally accepted that the optimal number of threads is
the number of cores in your machine.

Next, open up a terminal.  From terminal, cd into the groupsieve directory.
Then simply type "make" (without quotes) into the terminal, i.e.,

$ make

That should build an executable named groupsieve.

To delete the groupsieve executable file, type:
$ make clean

To enable debugging, type:
$make debug

If you want to experiment with different BLOCK_SIZE or NUM_THREADS values,
just change the values in groupsieve.h, save, and then type "make" into
the terminal again.

To run groupsieve to generate all primes up to 10000000, type:

$ ./groupsieve 10000000 WHEEL_SIZE 

WHEEL_SIZE can be any value from 1-6.  See the explanation in groupsieve.c
for further explanation.  Generally, the larger the wheel, the better.
10000000 can be changed to any value from 1-10000000000 for now, although
future versions will provide the ability to generate and print primes
up to 2^64-1.

To print out all the primes up to 10000000000 using a WHEEL_SIZE of 6, type:

$ ./groupsieve 10000000000 6 --print
or
$ ./groupsieve 10000000000 6 --p

If you want to see help from the console, type: 
$ ./groupsieve



###2. Speed Comparisons###

To compare groupsieve, I used two prime sieves that seem to be frequently
cited as the two fastest prime number generators on the internet, 
D.J. Bernstein's primegen and  Kim Walisch's primesieve.

primegen can be found here:
http://cr.yp.to/primegen.html

primegen uses a sieve of Atkin implementation.  The sieve of Atkin was 
actually created by D.J. Bernstein and A.O.L. Atkin.  More information 
on the sieve of Atkin can be found here:
http://en.wikipedia.org/wiki/Sieve_of_Atkin

primesieve can be found here:
http://primesieve.org/

primesieve uses a highly optimized sieve of Eratosthenes implementation,
for which more information can be found at the link above.

All speed comparisons performed on a Lenovo T510 laptop from 2010 with
Intel Core I5 CPU with 4 cores, 32kB L1 cache size, 256kB L2 cache size, 
3072kB L3 cache size, 2GB of RAM, and a 500GB SSD (from 2013).  
OS: Crunchbang Waldorf.  All output piped to /dev/null.  Each test timed 
by running with time function, each test performed 3 times.  Each time 
listed is the median time of the 3 runs.

primgen was compiled with conf-word size of 8000.  The primegen console 
application doesn't offer a way to just generate primes without printing
them out, although I believe there's a library included that allows you 
to do this.  I haven't tested that yet, so I don't have times for just 
generating the primes using primegen.  Also, primegen only supports 1 thread.

primesieve offers it's own timer that it displays when run, however, to 
keep everything consistent, I just used the time function when I 
ran it.  In a future version of groupsieve, I'll include an internal 
timer for greater accuracy and so I can test it against primesieve's 
internal timer.  

You may want to download and view this file in a text editor so the formatting isn't crazy.

Generating primes without printing them: 
					groupsieve						primesieve
Limit			1 thread	4 threads		1 thread	4 threads
1000000			0m0.004s	0m0.004s		0m0.023s	0m0.023s
10000000		0m0.014s	0m0.010s		0m0.025s	0m0.026s
100000000		0m0.067s	0m0.050s		0m0.047s	0m0.045s
1000000000		0m0.739s	0m0.511s		0m0.229s	0m0.170s
10000000000		0m10.635s	0m6.570s		0m2.837s	0m1.725s

Printing primes:
					groupsieve						primesieve			primegen
Limit			1 thread	4 threads		1 thread	4 threads		1 thread
1000000			0m0.017s	0m0.023s		0m0.040s	0m0.042s		0m0.022s
10000000		0m0.103s	0m0.102s		0m0.112s	0m0.111s		0m0.070s
100000000		0m0.871s	0m0.853s		0m0.768s	0m1.289s		0m0.597s
1000000000		0m8.148s	0m8.018s		0m6.854s	0m12.261s		0m5.704s
10000000000		1m20.470s	1m19.005s		1m4.350s	2m0.088s		0m56.517s


Here is the data again showing only the fastest times in each category:

Generating primes without printing them: 
					groupsieve						primesieve
Limit			1 thread	4 threads		1 thread	4 threads
1000000			0m0.004s	0m0.004s			
10000000		0m0.014s	0m0.010s		
100000000									0m0.047s	0m0.045s
1000000000									0m0.229s	0m0.170s
10000000000									0m2.837s	0m1.725s

Printing primes:
					groupsieve						primesieve			primegen
Limit			1 thread	4 threads		1 thread	4 threads		1 thread
1000000			0m0.017s	0m0.023s									
10000000					0m0.102s									0m0.070s
100000000					0m0.853s									0m0.597s
1000000000					0m8.018s									0m5.704s
10000000000					1m19.005s									0m56.517s


One strange inconsistency in the data is that primesieve takes a significantly
longer time to print out primes using 4 threads.  I'm not sure why
this is the case, but I imagine it has to do with the way the threads are
locking somehow.

I'm pretty happy with groupsieve's performance given that the code isn't
nearly as optimized as primegen or primesieve.  Hopefully with further
optimizations, groupsieve will perform even better.  One thing I definitely 
think will help is using the whole bitfield used to store primes instead 
of only half of it.  This will essentially double the segment size that 
can fit in the L1 caches.  

Additionally, there was a friendly contest to see who could create the 
fastest prime number generator here:
http://forums.whirlpool.net.au/archive/1872681
One user had massive speed improvements when printing primes by writing
his own print function.  Right now, I'm just using printf.



###3. Explanation of Theory###

When I was studying abstract algebra a couple summers ago, I started to
play around with mapping the primes to vertices of various dihedral groups.
While this in and of itself didn't yield much, I did start to notice patterns.
Eventually, instead of mapping primes to vertices, I decided to look at
prime numbers modulus 10.  From there, I decided to concentrate on the
group of integers modulus 10 under addition, henceforth referred to as
(Z/10,+).

Clearly, all primes greater than 2 must be odd.  In addition, all numbers
congruent to 5 mod 10 must be composite, other than 5 itself.  Thus, the
only numbers we're concerened with testing the primality of must be
congruent to either 1, 3, 7, or 9 mod 10.  When we apply
the Euler-Phi function to 10, phi(10) = 4.  This implies that there are
4 elements of (Z/10,+) that generate the group.  The generators of the 
group also happen to be 1, 3, 7, and 9 since gcd(10, (1,3,7, or 9)) = 1,
where gcd is greatest common denominator.  
This means that any prime number other than 2 and 5 will generate (Z/10,+).

From here, I started looking at the cycles prime numbers generate in
(Z/10,+).  I started with 3 and 7.

3 generates (Z/10,+) as:
3, 6, 9, 12=2, 15=5, 18=8, 21=1, 24=4, 27=7, 30=0

7 generates (Z/10,+) as:
7, 14=4, 21=1, 28=8, 35=5, 42=2, 49=9, 56=6, 63=3, 70=0

Since lcm(30,70) = 210, where lcm is lowest common multiple, the cycles
of 3 and 7, when overlayed, create a larger cycle of 210.  Thus, I noticed
that it could be determined if any number was a multiple of 3 or 7 based
on what that number equals modulus 210.  I did the same with 11 and 13,
creating cycles of size 210*11=2310 and 210*11*13=30030, respectively.

Unfortunately, these cycles obviously grow in size very quickly.  However,
it seemed like a fast way to quickly rule out multiples of small primes.
After doing some research, I found out that this is called wheel factorization.
More information on wheel factorization can be found here:
http://en.wikipedia.org/wiki/Wheel_factorization

So, from here, I still needed to find a way to sieve multiples of primes
larger than the ones used to construct the wheel.  In the classic sieve
of Eratosthenes, this is accomplished by taking a prime, say 61, and
removing all multiples of that proceeding as follows:
61, 61+61=122, 61+61+61=183, and so on.

One optimazation that can be made to the sieve of Eratosthenes is realizing
that you only need to start sieving at the square of a prime.  Another 
optimization is to note that, by the fundamental theorem of algebra, you 
only need to rule out prime multiples of any given number.  For instance,
starting with 61, we would first mark off 61*61, then 61*67, 61*71, and
so forth.

This, however, requires you to first determine some primes, sieve their 
multiples, find more primes, sieve their multiples in addition to multiples
of the first set of primes with the second set of primes and so on.  I 
wanted to be able to take a prime, such as 61, and mark off all multiples 
of that prime, regardless of other prime numbers. 

Since the only composites we need to rule out are congruent to 1, 3, 7, 
or 9 mod 10, and since any prime number will generate (Z/10,+), I decided
to sieve by taking a prime, finding where in the prime's cycle it generates
numbers congruent to 1, 3, 7, and 9 mod 10, ruling out those elements, and
then repeatedly overlaying that cycle until the desired limit is reached.

For instance, 37 generates (Z/10,+) as:
37=7, 74=4, 111=1, 148=8, 185=5, 222=2, 259=9, 296=6, 333=3, 370=0
Since 37 is congruent to 7 mod 10, we see that 37 generates the numbers 
of interest in the same order that 7 does, i.e., 7,1,9,3.  Next, we just
need to figure out how much we need to add each time to get to the next
number of interest.  We do this as follows:
37=7  which is 37*1
111=1 which is 37*3
259=9 which is 37*7
333=3 which is 37*9

So to sieve 37 by cycles, we rule out:
Cycle 1: 37, 37*3=111, 37*7=259, 37*9=333
Cycle 2: 370+37 = 407, 370+(37*3)=481, 370+(37*7)=629, 370+(37*9)=703
For the next cycle, 370*2=740
Cycle 3: 740+37 = 777, 740+(37*3)=851, 740+(37*7)=999, 740+(37*9)=1073

We continue in this fashion until we reach the desired limit.  The nice 
thing about this is we don't need any information about other primes to 
sieve all multiples of a given prime.  Using this method, we do rule out 
some numbers multiple times, for instance, 111 is ruled out by both 37 
and 11.  However, especially for small primes, we do far fewer multiplications 
this way.

This seems to be a fairly unique method of sieving.  I haven't come across
it in my research of sieves, although admittedly I didn't do a ton of 
research since I wanted to come up with all algorithmic and implementation
details on my own as a fun exercise.

I have the number of operations worked out in a notebook for sieving some 
primes up to 10^9 using this method vs. the optimized sieve of Eratosthenes 
discussed above that I will include in a future update.  I'll also do an
overall complexity analysis in a future update.

In a future update of the code, I want to make it so that primes can be 
found between ranges of numbers, instead of just all primes up to a specified
input, as it is now.  The nice thing about this method is that if we want 
to look at numbers in the range of, say 10^16 and 15^16, we don't need
to do a bunch of large multiplications of numbers, we just keep adding.

There are probably some other means of using group theory to further 
optimize this algorithm, but I just wanted to get the code up and running.
Hopefully, after taking some time to read up on group theory again and 
talking to some of my old professors, I can figure some things out to 
improve this algorithm.



###4.Algorithm/Implementation Details###

Coming soon in a future update.

