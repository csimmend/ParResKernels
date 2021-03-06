/*
Copyright (c) 2013, Intel Corporation

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions 
are met:

* Redistributions of source code must retain the above copyright 
      notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above 
      copyright notice, this list of conditions and the following 
      disclaimer in the documentation and/or other materials provided 
      with the distribution.
* Neither the name of Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products 
      derived from this software without specific prior written 
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
POSSIBILITY OF SUCH DAMAGE.
*/

/*******************************************************************

NAME:    Pipeline

PURPOSE: This program tests the efficiency with which point-to-point
         synchronization can be carried out. It does so by executing 
         a pipelined algorithm on an m*n grid. The first array dimension
         is distributed among the threads (stripwise decomposition).
  
USAGE:   The program takes as input the number of threads, the 
         dimensions of the grid, and the number of iterations on the grid

               <progname> <# threads> <iterations> <m> <n>
  
         The output consists of diagnostics to make sure the 
         algorithm worked, and of timing statistics.

FUNCTIONS CALLED:

         Other than OpenMP or standard C functions, the following 
         functions are used in this program:

         wtime()
         bail_out()

HISTORY: - Written by Rob Van der Wijngaart, March 2006.
         - modified by Rob Van der Wijngaart, August 2006:
            * changed boundary conditions and stencil computation to avoid 
              overflow
            * introduced multiple iterations over grid and dependency between
              iterations
  
*******************************************************************/

#include <par-res-kern_general.h>
#include <par-res-kern_omp.h>

/* We need to be able to flush the contents of an array, so we must declare it
   statically. That means the total array size must be known at compile time     */
#ifndef MEMWORDS
#define MEMWORDS  1000000
#endif

/* define shorthand for indexing a multi-dimensional array                       */
#define ARRAY(i,j) vector[i+(j)*(m)]

int main(int argc, char ** argv) {

  int    my_ID;           /* Thread ID                                           */
  int    m, n;            /* grid dimensions                                     */
  int    i, j, iter, ID;  /* dummies                                             */
  int    iterations;      /* number of times to run the pipeline algorithm       */
  int    flag[MAX_THREADS]; /* vector used for pairwise synchronizations         */
  int    *start, *end;    /* starts and ends of grid slices                      */
  int    segment_size;
  double pipeline_time,   /* timing parameters                                   */
         avgtime = 0.0, 
         maxtime = 0.0, 
         mintime = 366.0*24.0*3600.0; /* set the minimum time to a large 
                             value; one leap year should be enough               */
  double epsilon = 1.e-8; /* error tolerance                                     */
  double corner_val;      /* verification value at top right corner of grid      */
  int    nthread_input,   /* thread parameters                                   */
         nthread; 
  static                  /* use "static to put the thing on the heap            */
  double vector[MEMWORDS];/* array holding grid values; we would like to 
                             allocate it dynamically, but need to be able to 
                             flush the thing                                     */
  int    total_length;    /* total required length to store grid values          */
  int    num_error=0;     /* flag that signals that requested and obtained
                             numbers of threads are the same                     */

  /*******************************************************************************
  ** process and test input parameters    
  ********************************************************************************/

  if (argc != 5){
    printf("Usage: %s <# threads> <# iterations> <first array dimension> ", *argv);
    printf("<second array dimension>\n");
    return(EXIT_FAILURE);
  }

  /* Take number of threads to request from command line */
  nthread_input = atoi(*++argv); 

  if ((nthread_input < 1) || (nthread_input > MAX_THREADS)) {
    printf("ERROR: Invalid number of threads: %d\n", nthread_input);
    exit(EXIT_FAILURE);
  }

  omp_set_num_threads(nthread_input);

  iterations  = atoi(*++argv); 
  if (iterations < 1){
    printf("ERROR: iterations must be >= 1 : %d \n",iterations);
    exit(EXIT_FAILURE);
  }

  m  = atoi(*++argv);
  n  = atoi(*++argv);

  if (m < 1 || n < 1){
    printf("ERROR: grid dimensions must be positive: %d, %d \n", m, n);
    exit(EXIT_FAILURE);
  }

  /*  make sure we stay within the memory allocated for vector                   */
  total_length = m*n;
  if (total_length/n != m || total_length > MEMWORDS) {
    printf("Grid of %d by %d points too large; ", m, n);
    printf("increase MEMWORDS in Makefile or  reduce grid size\n");
    exit(EXIT_FAILURE);
  }

  if (m<nthread_input) {
    printf("First grid dimension %d smaller than number of threads requested: %d\n", 
           m, nthread_input);
    exit(EXIT_FAILURE);
  }

  start = (int *) malloc(2*nthread_input*sizeof(int));
  if (!start) {
    printf("ERROR: Could not allocate space for array of slice boundaries\n");
    exit(EXIT_FAILURE);
  }
  end = start + nthread_input;
  start[0] = 0;
  for (ID=0; ID<nthread_input; ID++) {
    segment_size = m/nthread_input;
    if (ID < (m%nthread_input)) segment_size++;
    if (ID>0) start[ID] = end[ID-1]+1;
    end[ID] = start[ID]+segment_size-1;
  }

  #pragma omp parallel private(i, j, my_ID, iter) 
  {

  #pragma omp master
  {
  nthread = omp_get_num_threads();

  printf("OpenMP pipeline execution on 2D grid\n");
  if (nthread != nthread_input) {
    num_error = 1;
    printf("ERROR: number of requested threads %d does not equal ",
           nthread_input);
    printf("number of spawned threads %d\n", nthread);
  } 
  else {
    printf("Number of threads         = %d\n",nthread_input);
    printf("Grid sizes                = %d, %d\n", m, n);
#ifdef VERBOSE
    printf("Number of pairwise synchs = %d\n", (nthread-1)*(n-1));
#endif
    printf("Number of iterations      = %d\n", iterations);
  }
  }
  bail_out(num_error);

  my_ID = omp_get_thread_num();

  /* clear the array, assuming first-touch memory placement                      */
  for (j=0; j<n; j++) for (i=start[my_ID]; i<=end[my_ID]; i++) ARRAY(i,j) = 0.0;
  /* set boundary values (bottom and left side of grid                           */
  if (my_ID==0) for (j=0; j<n; j++) ARRAY(start[my_ID],j) = (double) j;
  for (i=start[my_ID]; i<=end[my_ID]; i++) ARRAY(i,0) = (double) i;

  for (iter = 0; iter<iterations; iter++){

    /* set flags to zero to indicate no data is available yet                    */
    flag[my_ID] = 0;
    /* we need a barrier after setting the flags, to make sure each is
       visible to all threads, and to synchronize before the timer starts        */
    #pragma omp barrier

    #pragma omp master
    {   
    pipeline_time = wtime();
    }

    for (j=1; j<n; j++) {

      /* if not on left boundary,  wait for left neighbor to produce data         */
      if (my_ID > 0) {
       while (flag[my_ID-1] == 0) {
           #pragma omp flush(flag)
        }
        flag[my_ID-1] = 0;
        #pragma omp flush(flag,vector)
      }

      for (i=MAX(start[my_ID],1); i<= end[my_ID]; i++) {
        ARRAY(i,j) = ARRAY(i-1,j) + ARRAY(i,j-1) - ARRAY(i-1,j-1);
      }

      /* if not on right boundary, wait until right neighbor has received my data */
      if (my_ID < nthread-1) {
        while (flag[my_ID] == 1) {
          #pragma omp flush(flag)
        }
        flag[my_ID] = 1;
        #pragma omp flush(flag,vector)
      }
    }

    #pragma omp master
    {
    pipeline_time = wtime() - pipeline_time;
    if (iter>0 || iterations==1) { /* skip the first iteration                   */
      avgtime = avgtime + pipeline_time;
      mintime = MIN(mintime, pipeline_time);
      maxtime = MAX(maxtime, pipeline_time);
    }
    }

    /* copy top right corner value to bottom left corner to create dependency; we
       need a barrier to make sure the latest value is used. This also guarantees
       that the flags for the next iteration (if any) are not getting clobbered  */
    #pragma omp barrier
    #pragma omp master
    {
    ARRAY(0,0) = -ARRAY(m-1,n-1);
    }
  }

  } /* end of OPENMP parallel region                                             */

  /*******************************************************************************
  ** Analyze and output results.
  ********************************************************************************/

  /* verify correctness, using top right value;                                  */
  corner_val = (double)(iterations*(n+m-2));
  if (abs(ARRAY(m-1,n-1)-corner_val)/corner_val > epsilon) {
    printf("ERROR: checksum %lf does not match verification value %lf\n",
           ARRAY(m-1,n-1), corner_val);
    exit(EXIT_FAILURE);
  }

#ifdef VERBOSE   
  printf("Solution validates; verification value = %lf\n", corner_val);
  printf("Point-to-point synchronizations/s: %lf\n",
         ((float)((n-1)*(nthread-1)))/(mintime));
#else
  printf("Solution validates\n");
#endif
  avgtime = avgtime/(double)(MAX(iterations-1,1));
  printf("Rate (MFlops/s): %lf, Avg time (s): %lf, Min time (s): %lf",
         1.0E-06 * 2 * ((double)((m-1)*(n-1)))/mintime, avgtime, mintime);
  printf(", Max time (s): %lf\n", maxtime);

  exit(EXIT_SUCCESS);
}
