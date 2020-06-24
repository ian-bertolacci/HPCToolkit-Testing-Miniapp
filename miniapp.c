#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <omp.h>
#include <mpi.h>
#include <assert.h>

#define min(x,y) (((x)<(y))?(x):(y))
#define max(x,y) (((x)>(y))?(x):(y))

int factorial( int n ){
  if( n <= 0 ) return 1;
  if( n == 1 || n == 2 ) return n;

  int product = 1;
  for( int i = 1; i <= n; ++i ){
    product *= i;
  }
  return product;
}

// A parallel context used to manage MPI and OpenMP information
typedef struct {
  const int rank, n_ranks, primary_rank, openmp_threads;
  const MPI_Comm comm;
} parallel_context;


// Initialize app context
// Include MPI_init
parallel_context app_init( int argc, char** argv, parallel_context* context ){
  MPI_Init( NULL, NULL );
  const MPI_Comm comm = MPI_COMM_WORLD;

  const int primary = 0;
  int rank;
  int n_ranks, threads;

  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &n_ranks );

  #pragma omp parallel
  {
    #pragma omp single
    threads = omp_get_num_threads();
  }

  // Create mock return object
  parallel_context ret_obj = {
    .rank           = rank,
    .n_ranks        = n_ranks,
    .primary_rank   = primary,
    .comm           = comm,
    .openmp_threads = threads
  };

  // I really want all members of parallel_context to be const,
  // so this is how I initialize a parallel_context which I only have a reference to.
  memcpy( context, &ret_obj, sizeof(parallel_context) );
}

// Finalize app context
void app_finalize( parallel_context* context ){
  MPI_Finalize();
}

// Rank specific printf using valist. All ranks wait until print completed.
void rank_vprintf( parallel_context* context, int rank, const char *format, va_list args ){
  #pragma omp single
  {
    // if printing rank is the primary rank, and this is the primary rank simply print to stdout
    if( rank == context->primary_rank && context->rank == context->primary_rank){
      vfprintf( stdout,  format, args );

      // If this rank is either the printing or primary rank, perform send/recieve from printing to primary
    } else if( context->rank == context->primary_rank || context->rank == rank ) {
      size_t buffer_size = max(1024, 4*strlen(format));
      char* buffer = (char*) calloc( buffer_size, sizeof(char) );

      if( context->rank == rank ){
        vsprintf( buffer, format, args );
        // printf( "rank %d sending \"%s\"\n", context->rank, buffer );
        MPI_Send( buffer, buffer_size, MPI_CHAR, context->primary_rank, 0, context->comm );
      }

      if( context->rank == context->primary_rank ){
        // printf( "rank %d recieving from %d\n", context->rank, rank );
        MPI_Recv( buffer, buffer_size, MPI_CHAR, rank, 0, context->comm, NULL );
        // printf( "rank %d recieved from %d: \"%s\"\n", context->rank, rank, buffer );
        fprintf( stdout, "%s", buffer );
      }

      free( buffer );
    }
    fflush( stdout );
    MPI_Barrier( context->comm );
  }
}

// Rank specific printf. All ranks wait until print completed.
void rank_printf( parallel_context* context, int rank, const char *format, ... ){
  va_list args;
  va_start( args, format );
  rank_vprintf( context, rank, format, args );
  va_end( args );
}

// Primary only printf. All ranks wait until print completed.
void primary_printf( parallel_context* context, const char *format, ... ){
  va_list args;
  va_start( args, format );
  rank_vprintf( context, context->primary_rank, format, args );
  va_end( args );
}

// Distributed printf
// All ranks print sequentially, in order.
void distributed_printf( parallel_context* context, const char *format, ... ){
  for( int printing_rank = 0; printing_rank < context->n_ranks; ++printing_rank ){
    va_list args;
    va_start( args, format );
    rank_vprintf( context, printing_rank, format, args );
    va_end( args );
  }
}

// Distributed array
typedef struct {
  double* local_array;
  size_t local_elts, total_elts;
  size_t global_offset; // Index in global array that is locally index 0
} distributed_array;


// Construct distributed array
distributed_array allocate_distributed_array( size_t n_elts, parallel_context* context ){
  // Fair partitioning
  // // Calculate local size for local array
  // // Low portion is the floored average elements per rank.
  // int low_portion = n_elts / context->n_ranks;
  // // High portion is low portion + remainder of the elements.
  // int high_portion = low_portion + (n_elts % context->n_ranks);
  // // Total number of elements is (n_ranks - 1)*low_portion + high_portion
  //
  // // Determine which portion this rank owns (Primary takes larger portion, others take smaller portion)
  // size_t portion = (context->rank == context->primary_rank)?
  //                    high_portion   // Primary takes larger portion
  //                  : low_portion;   // Others take smaller portion
  //
  // // Determin this ranks offset (global index corresponding to local index 0) (Primary starts a 0)
  // size_t offset = (context->rank == context->primary_rank)?
  //                   0
  //                 : high_portion + ((context->rank-1)*(low_portion));

  // Unfair partitioning (for load imballance demonstration)
  int denomenator = (int) ( .5 * context->n_ranks * (context->n_ranks + 1) );
  int portions[context->n_ranks];
  int offsets[context->n_ranks];
  int portion_sum = 0;
  for( int sim_rank = 0; sim_rank < context->n_ranks; ++sim_rank ){
    portions[sim_rank] = (sim_rank+1) * (n_elts / denomenator);

    if( sim_rank + 1 == context->n_ranks ){
      portions[sim_rank] += n_elts % denomenator;
    }

    portion_sum += portions[sim_rank];

    if( sim_rank == 0 ){
      offsets[sim_rank] = 0;
    } else {
      offsets[sim_rank] = offsets[sim_rank-1] + portions[sim_rank-1];
    }
  }

  // if( context->rank == context->primary_rank ){
  //   printf( "Denomenator: %d\nPortion Sum: %d\n", denomenator, portion_sum );
  //   for( int sim_rank = 0; sim_rank < context->n_ranks; ++sim_rank ){
  //     printf( "Rank %d: %d (+%d)\n", sim_rank, portions[sim_rank], offsets[sim_rank] );
  //   }
  //   if( portion_sum != n_elts ){
  //     printf("SUM NOT EQUAL!!!\n\n\n");
  //   }
  // }
  // MPI_Barrier( context->comm );

  assert( portion_sum == n_elts && "Invalid portioning in init_distributed_array" );


  int portion = portions[context->rank];
  int offset  = offsets [context->rank];

  // distributed_printf( context, "Rank %d\nPortion: %d\nOffset %d\n", context->rank, portion, offset );

  double* array = (double*) malloc( portion*sizeof(double) );

  // Construct and return distributed_array structure
  distributed_array ret_obj = {
    .local_array   = array,
    .local_elts    = portion,
    .total_elts    = n_elts,
    .global_offset = offset
  };

  return ret_obj;
}

// Initialize distributed array with arbitrary values
void init_distributed_array( distributed_array* distributed_array, parallel_context* context ){
  #pragma omp parallel for
  for( int i = 0; i < distributed_array->local_elts; ++i ){
    // Use global offset to create value for this local index
    double j =  (i+1) + distributed_array->global_offset;
    distributed_array->local_array[i] = cos( sqrt( abs( exp( sin( j ) ) ) ) );
  }
}

void free_distributed_array( distributed_array* distributed_array ){
  free(distributed_array->local_array);
}

// Parallel reduce a double array
double reduce_local_array( double* array, size_t n_elts ){
  double local_sum = 0;
  #pragma omp parallel for reduction(+: local_sum)
  for( int i = 0 ; i < n_elts; ++i ){
    local_sum += array[i];
  }
  return local_sum;
}

// Distributed-Parallel reduce a distributed array
// Note: returns zero if not calling on the primary rank
double reduce_distributed_array( distributed_array* distributed_array, parallel_context* context ){
  // Array where (on primary) sums will be gathered into
  double* all_reductions = NULL ;

  // Only allocate/free on primary
  if( context->rank == context->primary_rank ){
    all_reductions = (double*) malloc( context->n_ranks*sizeof(double) );
  }

  // Perform reduction on local portion of array
  double local_reduction = reduce_local_array( distributed_array->local_array, distributed_array->local_elts );

  // Gather all local reduction
  MPI_Gather( &local_reduction, 1, MPI_DOUBLE, all_reductions, 1, MPI_DOUBLE, context->primary_rank, context->comm );

  // Primary computes final part of reduction locally
  double sum = 0.0;
  if( context->rank == context->primary_rank ){
    // Compute parallel reduction (potential source of bad performance; small number of ranks)
    sum = reduce_local_array( all_reductions, context->n_ranks );

    // Only allocate/free on primary
    free( all_reductions );
  }

  // Note: returns zero if not calling on the primary rank
  return  sum;
}

int main( int argc, char** argv ){

  // Initialize app and context
  parallel_context context;
  app_init( argc, argv, &context );

  // Whoami?
  distributed_printf( &context, "Rank %d/%d with %d OpenMP threads\n", context.rank, context.n_ranks, context.openmp_threads );

  // Parse argument
  int N;
  if( argc > 1){
    N = atoi( argv[1] );
  } else {
    N = 10;
  }

  // Print number of elements
  primary_printf( &context, "N: %d\n", N );

  // Allocate distributed array
  distributed_array array = allocate_distributed_array( N, &context );
  distributed_printf( &context, "Rank %d/%d owns %d of %d elements\n", context.rank, context.n_ranks, array.local_elts, array.total_elts );

  // Initialize distributed array with arbitrary values
  init_distributed_array( &array, &context );

  // Reduce distributed array
  double sum = reduce_distributed_array( &array, &context );

  // Print reduction value
  primary_printf( &context, "Sum: %f\n", sum );

  // Free distributed array
  free_distributed_array( &array );

  // Finalize app
  app_finalize( &context );

  return 0;
}
