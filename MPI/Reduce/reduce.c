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

NAME:    Reduce

PURPOSE: This program tests the efficiency with which a collection of 
         vectors that are distributed among the processes can be added in
         elementwise fashion. The number of vectors per process is two, 
         so that a reduction will take place even if the code runs on
         just a single process.
  
USAGE:   The program takes as input the length of the vectors, plus the
         number of times the reduction is repeated.

               <progname> <# iterations> <vector length>
  
         The output consists of diagnostics to make sure the 
         algorithm worked, and of timing statistics.

FUNCTIONS CALLED:

         Other than MPI or standard C functions, the following external 
         functions are used in this program:

         wtime();
         bail_out();

HISTORY: Written by Rob Van der Wijngaart, March 2006.
  
*******************************************************************/

#include <par-res-kern_general.h>
#include <par-res-kern_mpi.h>


int main(int argc, char ** argv)
{
  int Num_procs;        /* Number of processors                              */
  int my_ID;            /* Process ID (i.e. MPI rank)                        */
  int root=0;
  int iterations;       /* number of times the reduction is carried out      */
  int i, iter;          /* dummies                                           */
  int vector_length;    /* length of the vectors to be aggregated            */
  double * RESTRICT vector; /* vector to be reduced                          */
  double reduce_time,   /* timing parameters                                 */
         avgtime = 0.0, 
         maxtime = 0.0, 
         mintime = 366.0*8760.0*3600.0; /* set the minimum time to a large 
                             value; one leap year should be enough           */
  double epsilon=1.e-8; /* error tolerance                                   */
  double element_value; /* verification value                                */
  int    error = 0;     /* error flag                                        */

  /***************************************************************************
  ** Initialize the MPI environment
  ****************************************************************************/
  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_ID);
  MPI_Comm_size(MPI_COMM_WORLD, &Num_procs);

  /***************************************************************************
  ** process, test and broadcast input parameters
  ****************************************************************************/

  if (my_ID == root){
    if (argc != 3){
      printf("Usage: %s <# iterations> <vector_length>\n", *argv);
      error = 1;
      goto ENDOFTESTS;
    }

    iterations    = atoi(*++argv);
    if (iterations < 1) {
      printf("ERROR: Iterations must be positive: %d\n", iterations);
      error = 1;
      goto ENDOFTESTS;
    }

    vector_length = atoi(*++argv);
    if (vector_length < 1) {
      printf("ERROR: Vector length should be positive: %d\n", vector_length);
      error = 1;
      goto ENDOFTESTS;
    }

    ENDOFTESTS:;
  }
  bail_out(error);


  if (my_ID == root) {
    printf("MPI Vector Reduction\n");
    printf("Number of processes  = %d\n", Num_procs);
    printf("Vector length        = %d\n", vector_length);
    printf("Number of iterations = %d\n", iterations);     
  }

  /* Broadcast benchmark data to all processes */
  MPI_Bcast(&iterations,    1, MPI_INT, root, MPI_COMM_WORLD);
  MPI_Bcast(&vector_length, 1, MPI_INT, root, MPI_COMM_WORLD);
  vector= (double *) malloc(2*vector_length*sizeof(double)); 
  if (vector==NULL) {
    printf("ERROR: Could not allocate space for vector in process %d\n", my_ID);
    error = 1;
  }
  bail_out(error);

  for (iter=0; iter<iterations; iter++) { 

    /* initialize the arrays                                                    */
    for (i=0; i<vector_length; i++) {
      vector[i] = (double)(my_ID+1);
      vector[vector_length+i] = (double)(my_ID+1);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    reduce_time = wtime();

    /* first do the "local" part                                                */
    for (i=0; i<vector_length; i++) {
      vector[vector_length+i] += vector[i];
    }

    /* now do the "non-local" part                                              */
    MPI_Reduce(vector+vector_length, vector, vector_length, MPI_DOUBLE, MPI_SUM, 
               root, MPI_COMM_WORLD);

    if (my_ID == root) {
      if (iter>0 || iterations==1) { /* skip the first iteration */
        reduce_time = wtime() - reduce_time;
        avgtime = avgtime + reduce_time;
        mintime = MIN(mintime, reduce_time);
        maxtime = MAX(maxtime, reduce_time);
      }
    }
  }

  /* verify correctness */
  if (my_ID == root) {
    element_value = Num_procs*(Num_procs+1.0);

    for (i=0; i<vector_length; i++) {
      if (ABS(vector[i] - element_value) >= epsilon) {
        error = 1;
#ifdef VERBOSE
        printf("ERROR at i=%d; value: %lf; reference value: %lf\n",
               i, vector[i], element_value);
#else
        printf("First error at i=%d; value: %lf; reference value: %lf\n",
               i, vector[i], element_value);
        break;
#endif
      }
    }
  }
  bail_out(error);

  if (my_ID == root) {
    printf("Solution validates\n");
#ifdef VERBOSE
    printf("Element verification value: %lf\n", element_value);
#endif
    avgtime = avgtime/(double)(MAX(iterations-1,1));
    printf("Rate (MFlops/s): %lf,  Avg time (s): %lf,  Min time (s): %lf",
           1.0E-06 * (2.0*Num_procs-1.0)*vector_length/mintime, avgtime, mintime);
    printf(", Max time (s): %lf\n", maxtime);
  }

  MPI_Finalize();
  exit(EXIT_SUCCESS);

}  /* end of main */

