#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include <mpi.h>
#include <assert.h>

#define min(x, y) (((x)<(y))?(x):(y))
#define max(x, y) (((x)>(y))?(x):(y))

#define op2(op, a, b)       (op(a,b))
#define op3(op, a, b, c)    (op(a,op(b,c)))
#define op4(op, a, b, c, d) (op(op(a,b),op(c,d)))

#define min2(a, b)       (op2(min,a,b))
#define min3(a, b, c)    (op3(min,a,b,c))
#define max2(a, b)       (op2(max,a,b))
#define max3(a, b, c)    (op3(max,a,b,c))


bool isunsignedinteger( const char* str ){
  for( int i = 0; i < strlen(str) && str[i] != '\0'; ++i ){
    if( ! isdigit( str[i] ) ) return false;
  }
  return true;
}

bool isinteger( const char* str ){
  ssize_t start = 0;
  // check for sign, advance start-of-unsigned-int position
  if( str[start] == '-' || str[start] == '+' ){
    start += 1;
  }
  // If remaining (possibly the whole if no sign) string is an unsigned integer
  // Then the whole string is an integer
  return isunsignedinteger( str + start );
}

// Distributed array
typedef struct {
  double* local_array;
  size_t local_elts, total_elts;
  size_t global_offset; // Index in global array that is locally index 0
} distributed_array;

typedef enum {
  distribute_fair,
  distribute_unfair,
} distribution_type_t;

typedef enum {
  iteration_order_regular_order,
  iteration_order_ascending_indirect_order,
  iteration_order_random_order,
} iteration_order_type_t;


typedef enum {
  verbosity_errors = 0,
  verbosity_less   = 5,
  verbosity_normal = 10,
  verbosity_more   = 20,
  verbosity_debug  = 30,
  verbosity_all    = 512,
} verbosity_t;

// Program arguments and such.
// A parallel context used to manage MPI and OpenMP information
typedef struct {
  const bool initialized;

  const int N;
  const bool synchronize_at_end_of_distributed_array_operations;
  const verbosity_t verbosity;
  const distribution_type_t distribution_type;

  const int rank, n_ranks, primary_rank, omp_num_threads;
  const MPI_Comm comm;
  const iteration_order_type_t iteration_order_type;
  const omp_sched_t omp_loop_schedule;
  const int omp_chunk_size;

  int omp_print_state;
  omp_lock_t lock;

  const int seed;
} program_context_t;


program_context_t global_program_context = {
  .initialized = false
};


// Finalize application
void program_finalize( ){
  MPI_Finalize();
}

// Parse arguments and create global program context
// Note: Cannot use any of the distributed printf functions here
program_context_t program_init( int argc, char** argv ){
  if( global_program_context.initialized ){
    fprintf( stderr, "Error: Program already initialized\n" );
    exit(-1);
  }

  // Initialize MPI runtime
  MPI_Init( NULL, NULL );
  const MPI_Comm comm = MPI_COMM_WORLD;

  // Get MPI information
  int rank, n_ranks;
  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &n_ranks );

  // Get OpenMP information
  int initial_system_omp_threads, initial_system_omp_schedule_modifier;
  omp_sched_t initial_system_omp_schedule;

  #pragma omp parallel
  {
    #pragma omp single
    {
      // Note: initial number of threads can only be read from inside parallel section
      initial_system_omp_threads = omp_get_num_threads();
      omp_get_schedule( &initial_system_omp_schedule, &initial_system_omp_schedule_modifier );
    }
  }

  // Default argument values
  const int default_N = 10;
  const verbosity_t default_verbosity = verbosity_normal;
  const int default_iteration_order = iteration_order_regular_order;
  const int default_omp_num_threads = initial_system_omp_threads;
  const omp_sched_t default_omp_schedule = initial_system_omp_schedule;
  const int default_omp_chunk_size = initial_system_omp_schedule_modifier;
  const bool default_wait_on_non_collective_distiributed_array_operations = false;


  // Arguments set with default values, possibly overwritten by flags
  int N = default_N;
  verbosity_t verbosity = default_verbosity;
  distribution_type_t distribution_type = distribute_fair;
  iteration_order_type_t iteration_order_type = default_iteration_order;
  int omp_num_threads = default_omp_num_threads;
  omp_sched_t omp_schedule = default_omp_schedule;
  int omp_chunk_size = default_omp_chunk_size;
  bool synchronize_at_end_of_distributed_array_operations = default_wait_on_non_collective_distiributed_array_operations;

  char* usage_fmt_string = \
    "    -h"
    "        Print this message and exit.\n\n"
    "    -v <string or int>\n"
    "        Set verbosity level.\n"
    "        Values:\n"
    "          \"normal\" : Show standard messages.\n"
    "          \"more\"   : Show standard messages and some debugging info.\n"
    "          \"less\"   : Only show very important messages.\n"
    "          \"errors\" : Only show error messages.\n"
    "          \"debug\"  : Show standard messages and standard debugging info.\n"
    "          \"all\"    : Show all messages.\n"
    "          <int>      : Set to some priority integer value.\n"
    "        Default: \"normal\"\n\n"
    "    -q\n"
    "        Set verbosity to quiet (equivalent to -v \"less\").\n\n"
    "    -s\n"
    "        Set verbosity to silent (equivalent to -v \"errors\").\n\n"
    "    -N <unsigned int>\n"
    "        Set number of elements in distributed array.\n"
    "        Default: %d\n\n"
    "    -n <unsigned int>\n"
    "        Equivalent to -N <unsigned int>\n\n"
    "    -t <unsigned int>\n"
    "        Set number of OpenMP threads.\n"
    "        Default: %d (system default)\n\n"
    "    -d <distribution type>\n"
    "        Set type of distribution for elements of distributed array.\n"
    "        Values:\n"
    "          \"fair\" : Distribute values fairly.\n"
    "          \"unfair\" : Distribute values unfairly.\n\n"
    "    -w \n"
    "        Force ranks to synchronize at each distributed array opperation.\n\n"
    "    -l <OpenMP schedule name>\n"
    "        Set OpenMP loop schedule.\n"
    "        Values:\n"
    "          \"default\" : Use the default schedule, or schedule defined by\n"
    "                      the environment variable OMP_SCHEDULE.\n" // Note: there are meta-characters in the above so this line does not line up here, but does in the output
    "          \"static\"  : Use OpenMP static schedule.\n"
    "          \"dynamic\" : Use OpenMP dynamic schedule.\n"
    "          \"guided\"  : Use OpenMP quided schedule.\n"
    "          \"auto\"    : Use OpenMP auto schedule.\n"
    "        Default: \"default\"\n\n"
    "    -c <unsigned int>\n"
    "        Set OpenMP chunk size.\n"
    "        Default: %d (system default)\n\n"
    "    -o <loop order>\n"
    "        Set loop ordering method.\n"
    "        Values:\n"
    "          \"default\"  : Iterate over indices in the default implementation order.\n"
    "          \"indirect\" : Iterate over indices in ascending order (ignoring parallelism order) though an indirection array.\n"
    "          \"random\"   : Iterate over indices in random order through an indirection array.\n"
    "        Default : \"default\"\n\n";

  char* options = "hv:qsN:n:d:wt:l:c:o:";
  char flag_char;
  opterr = 0;
  while( ( flag_char = getopt( argc, argv, options ) ) != -1 ){
    switch( flag_char ) {
      case 'h': {
        printf( usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
        exit(-1);
        program_finalize();
        exit(0);
      }
      break;

      case 'v': {
        // Do all string comparisons
        if(      strcmp( "normal", optarg ) == 0 ) verbosity = verbosity_normal;
        else if( strcmp( "more",   optarg ) == 0 ) verbosity = verbosity_more;
        else if( strcmp( "less",   optarg ) == 0 ) verbosity = verbosity_less;
        else if( strcmp( "errors", optarg ) == 0 ) verbosity = verbosity_errors;
        else if( strcmp( "debug",  optarg ) == 0 ) verbosity = verbosity_debug;
        else if( strcmp( "all",    optarg ) == 0 ) verbosity = verbosity_all;
        // Integer fallback if necessary
        else {
          if( isinteger( optarg ) ){
            verbosity = atoi( optarg );
          } else {
            fprintf( stderr, "Error: invalid value for -%c: %s\n", flag_char, optarg );
            fprintf( stderr, usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
            exit(-1);
          }
        }
      }
      break;

      case 'q': {
        verbosity = verbosity_less;
      }
      break;

      case 's': {
        verbosity = verbosity_errors;
      }
      break;

      case 'N':
      case 'n': {
        if( isunsignedinteger( optarg ) ){
          N = atoi( optarg );
        } else {
          fprintf( stderr, "Error: invalid value for -%c: %s\n", flag_char, optarg );
          fprintf( stderr, usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
          exit(-1);
        }
      }
      break;

      case 'd': {
        // Do all string comparisons
        if(      strcmp( "fair",   optarg ) == 0 ) distribution_type = distribute_fair;
        else if( strcmp( "unfair", optarg ) == 0 ) distribution_type = distribute_unfair;
        else {
          fprintf( stderr, "Error: invalid value for -%c: %s\n", flag_char, optarg );
          fprintf( stderr, usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
          exit(-1);
        }
      }
      break;

      case 'w': {
        synchronize_at_end_of_distributed_array_operations = true;
      }
      break;

      case 't': {
        if( isunsignedinteger( optarg ) ){
          omp_num_threads = atoi( optarg );
        } else {
          fprintf( stderr, "Error: invalid value for -%c: %s\n", flag_char, optarg );
          fprintf( stderr, usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
          exit(-1);
        }
      }
      break;

      case 'l': {
        // Do all string comparisons
        if(      strcmp( "default", optarg ) == 0 ) omp_schedule = default_omp_schedule;
        else if( strcmp( "static",  optarg ) == 0 ) omp_schedule = omp_sched_static;
        else if( strcmp( "dynamic", optarg ) == 0 ) omp_schedule = omp_sched_dynamic;
        else if( strcmp( "guided",  optarg ) == 0 ) omp_schedule = omp_sched_guided;
        else if( strcmp( "auto",    optarg ) == 0 ) omp_schedule = omp_sched_auto;
        else {
          fprintf( stderr, "Error: invalid value for -%c: %s\n", flag_char, optarg );
          fprintf( stderr, usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
          exit(-1);
        }
      }
      break;

      case 'c': {
        // Check that all characters in string are a integer digits
        if( isunsignedinteger( optarg ) ){
          omp_chunk_size = atoi( optarg );
        } else {
          printf( "Error: invalid value for -%c: %s\n", flag_char, optarg );
          printf( usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
          exit(-1);
        }
      }
      break;

      case 'o': {
        // Do all string comparisons
        if(      strcmp( "default",  optarg ) == 0 ) iteration_order_type = iteration_order_regular_order;
        else if( strcmp( "indirect", optarg ) == 0 ) iteration_order_type = iteration_order_ascending_indirect_order;
        else if( strcmp( "random",   optarg ) == 0 ) iteration_order_type = iteration_order_random_order;
        else {
          fprintf( stderr, "Error: invalid value for -%c: %s\n", flag_char, optarg );
          fprintf( stderr, usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
          exit(-1);
        }
      }
      break;


      case '?': {
        char* option_ptr = strchr( options, optopt );
        // option is NOT in option string
        if( option_ptr == NULL ){
          if( isprint( optopt ) ){
            fprintf( stderr, "Unknown option `-%c'.\n", optopt );
          } else {
            fprintf( stderr, "Unknown option character `\\x%x'.\n", optopt );
          }
        }
        // Option is in option string
        else {
          // Get position in option string to test if it's followed by a colon.
          int opt_pos = option_ptr - options;
          if( 0 <= opt_pos && opt_pos < strlen( options ) ){
            // Safely check next character to see if is colon
            if( opt_pos + 1 < strlen( options ) && options[opt_pos+1] == ':' ){
              fprintf( stderr, "Option -%c requires an argument.\n", optopt );
            } else {
              fprintf( stderr, "Internal Error: Unknown issue using flag '%1$c' (''\\x%1$x').\n", optopt );
            }
          }
          // Note: This should never happen
          else {
            if( isprint( optopt ) ){
              fprintf( stderr, "Internal Error: Flag character '%1$c' (''\\x%1$x') somehow in options str, but outside the string's bounds ( %2$d not in [0,%3$lu) ).\n", optopt, opt_pos, strlen( options ) );
            } else {
              fprintf( stderr, "Internal Error: Unprintable flag character '\\x%1$x' somehow in options str, but outside the string's bounds ( %2$d not in [0,%3$lu) ).\n", optopt, opt_pos, strlen( options ) );
            }
          }
        }
      } // case '?'
      // Note: Fallthrough

      default: {
        fprintf( stderr, usage_fmt_string, default_N, default_omp_num_threads, default_omp_chunk_size );
        exit( -1 );
      }
    } // switch
  } // while getopt

  // Create get a new random number seed for this rank
  // initialize srand to something all ranks may have
  srand( time(NULL) );
  // generate rank+1 number of numbers from this seed, and use
  // the last number as the seed for this rank.
  // It is unlikely that two ranks have the same seed.
  int rank_seed;
  for( int i = 0; i <= rank; ++i ) rank_seed = rand();
  // Re-seed srand with the seed
  srand( rank_seed );


  // Create return/memcopy object
  program_context_t ret_obj = {
    .initialized           = true,

    .N                     = N,
    .synchronize_at_end_of_distributed_array_operations = synchronize_at_end_of_distributed_array_operations,
    .verbosity             = verbosity,
    .distribution_type     = distribution_type,
    .iteration_order_type  = iteration_order_type,

    .rank                  = rank,
    .n_ranks               = n_ranks,
    .primary_rank          = 0, // rank 0 is always the primary rank
    .comm                  = comm,
    .omp_num_threads       = omp_num_threads,

    .omp_loop_schedule     = omp_schedule,
    .omp_chunk_size        = omp_chunk_size,

    .omp_print_state       = 0,

    .seed                  = rank_seed
  };

  omp_init_lock( &(ret_obj.lock) );

  // I really want all members of program_context_t to be const,
  // So we memcpy it into place to force the overwrite.
  memcpy( &global_program_context, &ret_obj, sizeof(program_context_t) );

  int compare = memcmp( &ret_obj, &global_program_context, sizeof(program_context_t) );
  if( compare != 0 ){
    fprintf( stderr, "Internal Error: mock program context (@%p) and global_program_context (@%p) are unequal (%d)\n", &ret_obj, &global_program_context, compare );
    exit(-1);
  }

  // Should be able to set the schedule here, and never worry about how to set
  // set the for loop clause
  omp_set_schedule( global_program_context.omp_loop_schedule, global_program_context.omp_chunk_size );

  omp_set_num_threads( global_program_context.omp_num_threads );

  #pragma omp single
  {
    if( ! omp_test_lock( &(global_program_context.lock) ) ){
      fprintf( stderr, "Unable to acquire newly initialized OpenMP lock.\n" );
      exit(-1);
    }
    omp_unset_lock( &(global_program_context.lock) );
  }

  return ret_obj;
}

// Construct distributed array
distributed_array allocate_distributed_array( size_t n_elts, distribution_type_t distribution_type ){
  size_t portion, offset;

  // Fair partitioning
  if( distribution_type == distribute_fair ){
    // Calculate size for local array
    // Low portion is the floored average elements per rank.
    int low_portion = n_elts / global_program_context.n_ranks;
    // High portion is low portion + remainder of the elements.
    // Easiest way to calculate this is to subtract the total amount of work
    // owned by the *other* ranks (n_ranks-1 of them) , and subtract it from the total
    int high_portion = n_elts - (low_portion * (global_program_context.n_ranks - 1));

    // Determine which portion this rank owns
    // Primary takes larger portion
    if( global_program_context.rank == global_program_context.primary_rank ){
      portion = high_portion;
    }
    // Others take smaller portion)
    else {
      portion = low_portion;
    }

    // Determin this ranks offset, or global index corresponding to local index 0
    // Primary starts a 0
    if( global_program_context.rank == global_program_context.primary_rank ){
      offset = 0;
    }
    // Everyone else starts
    else {
      offset = high_portion + ((global_program_context.rank-1)*(low_portion));
    }

  }
  // Unfair partitioning (for load imballance demonstration)
  // Each partition takes rank * (N / ranks) amount of data
  else if( distribution_type == distribute_unfair ){
    int denomenator = (int) ( .5 * global_program_context.n_ranks * (global_program_context.n_ranks + 1) );
    int portions[global_program_context.n_ranks];
    int offsets[global_program_context.n_ranks];
    int portion_sum = 0;
    for( int sim_rank = 0; sim_rank < global_program_context.n_ranks; ++sim_rank ){
      portions[sim_rank] = (sim_rank+1) * (n_elts / denomenator);

      if( sim_rank + 1 == global_program_context.n_ranks ){
        portions[sim_rank] += n_elts % denomenator;
      }

      portion_sum += portions[sim_rank];

      if( sim_rank == 0 ){
        offsets[sim_rank] = 0;
      } else {
        offsets[sim_rank] = offsets[sim_rank-1] + portions[sim_rank-1];
      }
    }

    if( portion_sum != n_elts ){
      fprintf( stderr, "Invalid portioning in init_distributed_array:\n" );

      // Print rank's offset and portion
      for( int sim_rank = 0; sim_rank < global_program_context.n_ranks; ++sim_rank ){
        fprintf( stderr, "\t%d:\t+%d\t%d\n", sim_rank, portions[sim_rank], offsets[sim_rank] );
      }
      fprintf( stderr, "\t------------------------\n\ttotal:\t\t\t%d\n", portion_sum );

      exit(-1);
    }

    portion = portions[global_program_context.rank];
    offset  = offsets [global_program_context.rank];

  } else {
    fprintf( stderr, "Unknown distribution type (%d)\n", distribution_type );
    exit(-1);
  }

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

size_t* create_local_indirection_array( size_t size ){
  size_t* indirection_array = NULL;

  // One of the indirection orderings
  if(  global_program_context.iteration_order_type == iteration_order_ascending_indirect_order
    || global_program_context.iteration_order_type == iteration_order_random_order ) {
    indirection_array = (size_t*) malloc( size * sizeof(size_t) );
    // Fill indirection array with
    // Note: this does not *need* to be parallel but hey whatever.
    #pragma omp parallel for
    for( size_t i = 0; i < size; ++i ){
      indirection_array[i] = i;
    }

    // Random indirection order
    // Note: Going through the motions of the randomization, but only do the
    // value swap if order is random_order. This includes forcing *both* memory
    // reads and writes to locations that where the source and destination are
    // easily analysed to be the same, which requires the volatiles in order to
    // force the compiler to always read and write the values, even if it's
    // writing to the same place it read the value from originally and would
    // have no effect.
    // Todo: is swap passes enough? Too much?
    const int swap_passes = 2;
    for( int swap_pass = 0; swap_pass < swap_passes; swap_pass += 1 ){
      for( int from = 0; from < size; ++from){
        // choose another index that is different
        int to;
        while( (to = rand() % size) == from ){ }

        // swap indexes
        // Note: again, going through the motions. Need volatile pointer casting
        // to force the reads *and* writes
        size_t from_value =  ((size_t volatile*)indirection_array)[from];
        size_t to_value   =  ((size_t volatile*)indirection_array)[to];

        if( global_program_context.iteration_order_type == iteration_order_random_order ){
          ((size_t volatile*)indirection_array)[from] = to_value;
          ((size_t volatile*)indirection_array)[to]   = from_value;
        } else {
          ((size_t volatile*)indirection_array)[from] = from_value;
          ((size_t volatile*)indirection_array)[to]   = to_value;
        }
      } // for from ...
    } // for swap_pass
  } // if global_program_context.iteration_order_type ...

  return indirection_array;
}

// Initialize distributed array with arbitrary values
void init_distributed_array( distributed_array* distributed_array ){
  // Regular ordering
  if( global_program_context.iteration_order_type == iteration_order_regular_order ){
    // Note: Schedule and chunk-size were set at program init.
    //       There should be no reason to need any scheduling causes here.
    #pragma omp parallel for
    for( size_t i = 0; i < distributed_array->local_elts; ++i ){
      // Use global offset to create value for this local index
      double j =  (i+1) + distributed_array->global_offset;
      distributed_array->local_array[i] = sin( (j/distributed_array->total_elts) * 3.14159265358979323846 );
    }
  }
  // One of the indirection orderings
  else {
     size_t* indirection_array = create_local_indirection_array( distributed_array->local_elts );
     #pragma omp parallel for
     for( size_t indirect_i = 0; indirect_i < distributed_array->local_elts; ++indirect_i ){
       // Get index via indirection array
       size_t i = indirection_array[indirect_i];
       // Use global offset to create value for this local index
       double j =  (i+1) + distributed_array->global_offset;
       distributed_array->local_array[i] = sin( (j/distributed_array->total_elts) * 3.14159265358979323846 );
     }
     free( indirection_array );
  }
}

// "Stencilize" a local array in parallel
void in_place_stencilize_local_array( double* array, size_t n_elts ){
  double* update_array = (double*) malloc( n_elts * sizeof(double) );

  if( global_program_context.iteration_order_type == iteration_order_regular_order ){
    // Note: Schedule and chunk-size were set at program init.
    //       There should be no reason to need any scheduling causes here.
    #pragma omp parallel for
    for( size_t i = 0; i < n_elts; ++i ){
      double min_val, max_val;
      if( i == 0 ){
        max_val = max2( array[0], array[1] );
        min_val = min2( array[0], array[1] );
      } else if ( i == n_elts - 1 ){
        max_val = max2( array[n_elts-2], array[n_elts-1] );
        min_val = min2( array[n_elts-2], array[n_elts-1] );
      } else {
        max_val = max3( array[i-1], array[i], array[i+1] );
        min_val = min3( array[i-1], array[i], array[i+1] );
      }

      update_array[i] =  max_val / (1 + min_val );
    }
  }
  // One of the indirection orderings
  else {
     size_t* indirection_array = create_local_indirection_array( n_elts );
     // Note: Schedule and chunk-size were set at program init.
     //       There should be no reason to need any scheduling causes here.
     #pragma omp parallel for
     for( size_t indirect_i = 0 ; indirect_i < n_elts; ++indirect_i ){
       size_t i = indirection_array[indirect_i];

       double min_val, max_val;
       if( i == 0 ){
         max_val = max2( array[0], array[1] );
         min_val = min2( array[0], array[1] );
       } else if ( i == n_elts - 1 ){
         max_val = max2( array[n_elts-2], array[n_elts-1] );
         min_val = min2( array[n_elts-2], array[n_elts-1] );
       } else {
         max_val = max3( array[i-1], array[i], array[i+1] );
         min_val = min3( array[i-1], array[i], array[i+1] );
       }

       update_array[i] =  max_val / (1 + min_val );
     }

     free( indirection_array );
  }

  // Overwrite array with updates
  memcpy( array, update_array, n_elts*sizeof(double) );

  free( update_array );
}

// Distributed-Parallel "Stencilize" a distributed array
void in_place_stencilize_distributed_array( distributed_array* distributed_array ){
  // First, need to make copies of the end_values of our local
  double end_0_neighborhood[3] = {
    0.0, // Will recieve later
    distributed_array->local_array[0], // Will send later
    distributed_array->local_array[1]
  };
  double end_n_neighborhood[3] = {
    distributed_array->local_array[distributed_array->local_elts - 2],
    distributed_array->local_array[distributed_array->local_elts - 1], // Will send later
    0.0 // Will recieve later
  };

  // Second, setup async communicate with neighboring ranks (no rotating, ends of array do not send/recieve in on the high/low end)
  // TODO: should there be an option to synchronize before computing?

  // Send/Recieve request handles
  MPI_Request send_requests[2];
  MPI_Request recv_requests[2];

  // How many sends/recieves this rank will need to wait on (min=0, if serial, max=2)
  size_t n_recvs = 0;
  size_t n_sends = 0;

  // Send/recieve low side
  if( global_program_context.rank != 0 ){
    int send_err = MPI_Isend( &end_0_neighborhood[1], 1, MPI_DOUBLE, global_program_context.rank - 1, 0, global_program_context.comm, &send_requests[n_sends] );
    if( send_err != MPI_SUCCESS ){
      fprintf( stderr, "Error during end 0 MPI_Isend call: %d", send_err );
      exit(-1);
    }
    n_sends += 1;

    int recv_err = MPI_Irecv( &end_0_neighborhood[0], 1, MPI_DOUBLE, global_program_context.rank - 1, 0, global_program_context.comm, &recv_requests[n_recvs] );
    if( recv_err != MPI_SUCCESS ){
      fprintf( stderr, "Error during end 0 MPI_Irecv call: %d", recv_err );
      exit(-1);
    }

    n_recvs += 1;
  }

  // Send/recieve high side
  if( global_program_context.rank != global_program_context.n_ranks - 1 ){
    int send_err = MPI_Isend( &end_n_neighborhood[1], 1, MPI_DOUBLE, global_program_context.rank + 1, 0, global_program_context.comm, &send_requests[n_sends] );
    if( send_err != MPI_SUCCESS ){
      fprintf( stderr, "Error during end N MPI_Isend call: %d", send_err );
      exit(-1);
    }
    n_sends += 1;

    int recv_err = MPI_Irecv( &end_n_neighborhood[2], 1, MPI_DOUBLE, global_program_context.rank + 1, 0, global_program_context.comm, &recv_requests[n_recvs] );
    if( recv_err != MPI_SUCCESS ){
       fprintf( stderr, "Error during end N MPI_Irecv call: %d", recv_err );
      exit(-1);
    }

    n_recvs += 1;
  }

  // Third, perform local stencilization
  // Note: This happens in parallel with the send.
  // TODO: should there be an option to synchronize before computing?
  in_place_stencilize_local_array( distributed_array->local_array, distributed_array->local_elts );

  // Fourth, complete recieves
  // Note: do not need to wait on sends to complete because not writing to
  //       index the send is copying from
  // Note: *could* do computation while waiting for other recieve to come it,
  //       but not worth the effort right now.
  for( size_t recv_i = 0; recv_i < n_recvs; ++recv_i ){
    MPI_Wait( &recv_requests[recv_i], NULL );
  }

  // Fifth, compute ends
  // Compute low side
  if( global_program_context.rank != 0 ){
    double min_val = min3( end_0_neighborhood[0], end_0_neighborhood[1], end_0_neighborhood[2] );
    double max_val = max3( end_0_neighborhood[0], end_0_neighborhood[1], end_0_neighborhood[2] );

    distributed_array->local_array[0] = max_val / (1 + abs(min_val));
  }
  // Compute high side
  if( global_program_context.rank != global_program_context.n_ranks - 1 ){
    double min_val = min3( end_n_neighborhood[0], end_n_neighborhood[1], end_n_neighborhood[2] );
    double max_val = max3( end_n_neighborhood[0], end_n_neighborhood[1], end_n_neighborhood[2] );

    distributed_array->local_array[distributed_array->local_elts - 1] = max_val / (1 + abs(min_val));
  }

  // Sixth, wait on sends just because
  // I'm 99% sure this is unnecessary, especially since there is no error
  // handling here.
  for( size_t send_i = 0; send_i < n_sends; ++send_i ){
    MPI_Wait( &send_requests[send_i], NULL );
  }

  // Done
  if( global_program_context.synchronize_at_end_of_distributed_array_operations ){
    MPI_Barrier( global_program_context.comm );
  }
}

// Parallel sum a double array
double sum_local_array( double* array, size_t n_elts ){
  double rank_local_sum = 0.0;

  if( global_program_context.iteration_order_type == iteration_order_regular_order ){
    // Note: Schedule and chunk-size were set at program init.
    //       There should be no reason to need any scheduling causes here.
    #pragma omp parallel for reduction(+: rank_local_sum)
    for( size_t i = 0 ; i < n_elts; ++i ){
      rank_local_sum += array[i];
    }
  }
  // One of the indirection orderings
  else {
     size_t* indirection_array = create_local_indirection_array( n_elts );
     // TODO: is this kosher?
     #pragma omp parallel for reduction(+: rank_local_sum)
     for( size_t indirect_i = 0; indirect_i < n_elts; ++indirect_i ){
       // Get index via indirection array
       size_t i = indirection_array[indirect_i];
       rank_local_sum += array[i];
     }
     free( indirection_array );
  }

  return rank_local_sum;
}

// Distributed-Parallel sum a distributed array
// Note: returns zero if not calling on the primary rank
double sum_distributed_array( distributed_array* distributed_array ){
  // Array where (on primary) sums will be gathered into
  double* all_sums = NULL ;

  // Only allocate/free on primary
  if( global_program_context.rank == global_program_context.primary_rank ){
    all_sums = (double*) malloc( global_program_context.n_ranks * sizeof(double) );
  }

  // Perform reduction on local portion of array
  double rank_local_sum = sum_local_array( distributed_array->local_array, distributed_array->local_elts );

  // Gather all local reduction
  MPI_Gather( &rank_local_sum, 1, MPI_DOUBLE, all_sums, 1, MPI_DOUBLE, global_program_context.primary_rank, global_program_context.comm );

  // Primary computes final part of reduction locally
  double sum = 0.0;
  if( global_program_context.rank == global_program_context.primary_rank ){
    // Compute parallel reduction (potential source of bad performance; small number of ranks)
    sum = sum_local_array( all_sums, global_program_context.n_ranks );

    // Only allocate/free on primary
    free( all_sums );
  }

  if( global_program_context.synchronize_at_end_of_distributed_array_operations ){
    // Thought about doing a Bcast of sum here, but I imagine that this is not
    // as "ineffecient" as the spirit of this synchronization option would want.
    MPI_Barrier( global_program_context.comm );
  }

  // Note: returns zero if not calling on the primary rank
  return  sum;
}

void free_distributed_array( distributed_array* distributed_array ){
  free(distributed_array->local_array);
}

int main( int argc, char** argv ){
  // Initialize program
  program_init( argc, argv );

  // Allocate distributed array
  distributed_array array = allocate_distributed_array( global_program_context.N, global_program_context.distribution_type );
  // Print information about ranks and array
  if( global_program_context.verbosity >= verbosity_normal )
    printf( "Rank %d/%d with %d OpenMP threads owns %lu of %lu elements\n", global_program_context.rank, global_program_context.n_ranks, global_program_context.omp_num_threads, array.local_elts, array.total_elts  );

  // Initialize distributed array with arbitrary values
  init_distributed_array( &array );

  // "Stencilize" distributed array
  in_place_stencilize_distributed_array( &array );

  // sum distributed array
  double sum = sum_distributed_array( &array );

  // Print reduction value
  // Note: the expression (true | (int)sum) is a trick to force the not optimize
  // the reduce_distributed_array call to be under this conditional, thus
  // allowing us to run performance tests without any of the overhead of the
  // synchronized print-statements
  if( global_program_context.verbosity >= verbosity_less && (true | (int) sum) ){
    if( global_program_context.rank == global_program_context.primary_rank ){
      printf( "Sum: %f\n", sum );
    }
  }

  // Free distributed array
  free_distributed_array( &array );

  // Finalize program
  program_finalize( );

  return 0;
}
