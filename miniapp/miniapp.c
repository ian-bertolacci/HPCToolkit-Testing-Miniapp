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

// Test if argument string would parse into an unsigned integer
bool isunsignedinteger( const char* str ){
  for( int i = 0; i < strlen(str) && str[i] != '\0'; ++i ){
    if( ! isdigit( str[i] ) ) return false;
  }
  return true;
}

// Test if argument string would parse into an integer
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
  double* local_array;           // Pointer to start of local portion of array
  size_t total_elts;             // Total number of elements in distributed array
  size_t local_elts;             // Number of elements maintained by this rank in local_array
  size_t global_offset;          // Index in global array that is locally index 0

  // To mimic/exacerbate caching issues, may use one or more inderection arrays
  // if necessary (when using -o "indirect" or "random")
  size_t** indirection_arrays;   // array of indirection arrays
  // Each element in this array, is another array with local_elts number of elements.
  // Each element is an index into local_array.
  // Order is unspecified here.
  // Can be ascending (if using -o "indirect")
  // Can be random (if using -o "random")
  // For -o "random", multiple arrays are created so that a loop sequence
  // is unlikely to benifit from caching the previous values
  size_t n_indirection_arrays;   // total number of local indirection arrays
  size_t indirection_array_next; // which indirection array to use next


} distributed_array;

// Macro for iterating over distributed array
// ptr_distributed_array: (distributed_array_t*)
// iterator: symbol
// body: statement list
// Note: the for loop is the first 'line', so that openmp can be used on it.
#define distributed_array_local_for( ptr_distributed_array, iterator, body )  \
  for(                                                                        \
    size_t iterator ## idx = 0;                                               \
    iterator ## idx < (ptr_distributed_array)->local_elts;                    \
    ++ iterator ## idx                                                        \
  ){                                                                          \
    size_t iterator;                                                          \
    /* if using indirection arrays, use i'th indirect index iterator */       \
    if( ptr_distributed_array->indirection_arrays != NULL ){                  \
      /* note: would love to have this be outside the loop, but need for      \
         statement exposed for openmp pragma. The expression getting the      \
         indirection array is likely to be common-subexpression-eliminated */ \
      iterator = ptr_distributed_array->indirection_arrays[ptr_distributed_array->indirection_array_next][iterator ## idx]; \
    }                                                                         \
    /* otherwise use literal index */                                         \
    else {                                                                    \
      iterator = iterator ## idx;                                             \
    }                                                                         \
    /* do loop body */                                                        \
    {                                                                         \
      body                                                                    \
    }                                                                         \
  }                                                                           \
  /* set next indirection_array */                                            \
  if( ptr_distributed_array->indirection_arrays != NULL ){                    \
    (ptr_distributed_array)->indirection_array_next =                         \
    /* if this current inridection array is the last array, use the first */  \
    ((ptr_distributed_array)->indirection_array_next == (ptr_distributed_array)->n_indirection_arrays - 1 ) \
      ? 0                                                                     \
      /* Otherwise use the next array */                                      \
      : ((ptr_distributed_array)->indirection_array_next + 1);                \
  }


// Work distribution enum
typedef enum {
  distribute_fair,
  distribute_unfair,
} distribution_type_t;

// Loop iteration order enum
typedef enum {
  iteration_order_regular_order             = 0,
  iteration_order_ascending_indirect_order  = (1 << 0) | 1,
  iteration_order_random_order              = (2 << 1) | 1,
} iteration_order_type_t;

// iteration_order_type_t will have bit 0 set if using some indirection
inline bool is_iteration_order_indirect( const iteration_order_type_t iteration_order_type ){
  return ( iteration_order_type & 1 ) != 0;
}

// Verbosity enum
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
  const int iterations;
  const bool synchronize_at_end_of_distributed_array_operations;
  const verbosity_t verbosity;
  const distribution_type_t distribution_type;

  const int rank, n_ranks, primary_rank, omp_num_threads;
  const MPI_Comm comm;
  const iteration_order_type_t iteration_order_type;
  const omp_sched_t omp_loop_schedule;
  const int omp_chunk_size;

  const int seed;
} program_context_t;


program_context_t global_program_context = {
  .initialized = false
};


// Finalize application
void program_finalize( ){
  MPI_Finalize();
}

// \brief Parse CLI arguments and create a program context, and assign it to global_program_context
// \param argc number of argument strings (length of argv)
// \param argv array of null-terminated argument strings (length is argc )
// \return Fully initialized program_context_t which was assigned to global_program_context
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
  const int default_iterations = 1000;
  const verbosity_t default_verbosity = verbosity_normal;
  const int default_iteration_order = iteration_order_regular_order;
  const int default_omp_num_threads = initial_system_omp_threads;
  const omp_sched_t default_omp_schedule = initial_system_omp_schedule;
  const int default_omp_chunk_size = initial_system_omp_schedule_modifier;
  const bool default_wait_on_non_collective_distiributed_array_operations = false;


  // Arguments set with default values, possibly overwritten by flags
  int N = default_N;
  int iterations = default_iterations;
  verbosity_t verbosity = default_verbosity;
  distribution_type_t distribution_type = distribute_fair;
  iteration_order_type_t iteration_order_type = default_iteration_order;
  int omp_num_threads = default_omp_num_threads;
  omp_sched_t omp_schedule = default_omp_schedule;
  int omp_chunk_size = default_omp_chunk_size;
  bool synchronize_at_end_of_distributed_array_operations = default_wait_on_non_collective_distiributed_array_operations;

  char* usage_fmt_string = \
    "    -h\n"
    "        Print this message and exit.\n\n"
    "    -N <unsigned int>\n"
    "        Set number of elements in distributed array.\n"
    "        Default: %d\n\n"
    "    -i <unsigned int>\n"
    "        Set number of iterations to perform.\n"
    "        Default: %d\n\n"
    "    -n <unsigned int>\n"
    "        Equivalent to -N <unsigned int>\n\n"
    "    -d <distribution type>\n"
    "        Set type of distribution for elements of distributed array.\n"
    "        Values:\n"
    "          \"fair\" : Distribute values fairly.\n"
    "          \"unfair\" : Distribute values unfairly.\n\n"
    "    -w \n"
    "        Force ranks to synchronize at each distributed array opperation.\n\n"
    "    -t <unsigned int>\n"
    "        Set number of OpenMP threads.\n"
    "        Default: %d (system default)\n\n"
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
    "        Default : \"default\"\n\n"
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
    "        Set verbosity to silent (equivalent to -v \"errors\").\n\n";

  #define print_help_error(flag,argument) { \
    fprintf( stderr, "Error: invalid value for -%c: %s\n", flag_char, optarg ); \
    fprintf( stderr, usage_fmt_string, default_N, default_iterations, default_omp_num_threads, default_omp_chunk_size ); \
    exit(-1); \
  }

  char* options = "hN:n:i:d:wt:l:c:o:v:qs";
  char flag_char;
  opterr = 0;
  while( ( flag_char = getopt( argc, argv, options ) ) != -1 ){
    switch( flag_char ) {
      case 'N':
      case 'n': {
        if( isunsignedinteger( optarg ) ){
          N = atoi( optarg );
        } else {
          print_help_error( flag_char, optarg );
        }
      }
      break;

      case 'i': {
        if( isunsignedinteger( optarg ) ){
          iterations = atoi( optarg );
        } else {
          print_help_error( flag_char, optarg );
        }
      }
      break;


      case 'd': {
        // Do all string comparisons
        if(      strcmp( "fair",   optarg ) == 0 ) distribution_type = distribute_fair;
        else if( strcmp( "unfair", optarg ) == 0 ) distribution_type = distribute_unfair;
        else {
          print_help_error( flag_char, optarg );
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
          print_help_error( flag_char, optarg );
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
          print_help_error( flag_char, optarg );
        }
      }
      break;

      case 'c': {
        // Check that all characters in string are a integer digits
        if( isunsignedinteger( optarg ) ){
          omp_chunk_size = atoi( optarg );
        } else {
          print_help_error( flag_char, optarg );
        }
      }
      break;

      case 'o': {
        // Do all string comparisons
        if(      strcmp( "default",  optarg ) == 0 ) iteration_order_type = iteration_order_regular_order;
        else if( strcmp( "indirect", optarg ) == 0 ) iteration_order_type = iteration_order_ascending_indirect_order;
        else if( strcmp( "random",   optarg ) == 0 ) iteration_order_type = iteration_order_random_order;
        else {
          print_help_error( flag_char, optarg );
        }
      }
      break;

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
            print_help_error( flag_char, optarg );
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

  #undef print_help_error

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
    .iterations            = iterations,
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

    .seed                  = rank_seed
  };

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

  return ret_obj;
}


// \brief Create proper indirection array of size for distributed_array_t given the indirection order type
// \param size number indices of indirection array
// \param iteration_order_type order type of indirection array
// \returns The allocated and populated indirection array
size_t* create_local_indirection_array( size_t size, iteration_order_type_t iteration_order_type  ){
  size_t* indirection_array = NULL;

  // One of the indirection orderings
  if( is_iteration_order_indirect( iteration_order_type ) ) {
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

// \brief Construct distributed array
// \param n_elts total number of elements across all processes for this distributed array.
// \param distribution_type type of distribution of elements across process
// \return allocated and populated distributed array object
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

  // Create all the indirection arrays
  size_t** indirection_arrays = NULL;
  int n_indirection_arrays = 0;
  if( is_iteration_order_indirect( global_program_context.iteration_order_type ) ){
    n_indirection_arrays = 3 ;
    indirection_arrays = (size_t**) malloc( n_indirection_arrays*sizeof(size_t*) );
    for( size_t i = 0; i < n_indirection_arrays; ++i ){
      indirection_arrays[i] = create_local_indirection_array( portion, global_program_context.iteration_order_type );
    }
  }

  // Construct and return distributed_array structure
  distributed_array ret_obj = {
    .local_array            = array,
    .local_elts             = portion,
    .total_elts             = n_elts,
    .global_offset          = offset,
    .n_indirection_arrays   = n_indirection_arrays,
    .indirection_array_next = 0,
    .indirection_arrays     = indirection_arrays
  };

  return ret_obj;
}

// \brief deallocated distributed array object
// \param distributed_array distributed array object to be deallocated
void free_distributed_array( distributed_array* distributed_array ){
  free(distributed_array->local_array);
  if( distributed_array->n_indirection_arrays > 0 ){
    for( size_t i = 0; i < distributed_array->n_indirection_arrays; ++i ){
      free( distributed_array->indirection_arrays[i] );
    }
    free( distributed_array->indirection_arrays );
  }
}

// \brief Initialize distributed array with arbitrary values.
// \param distributed_array distributed array object to populate with data
void init_distributed_array( distributed_array* distributed_array ){
    // Note: Schedule and chunk-size were set at program init.
    //       There should be no reason to need any scheduling causes here.
    #pragma omp parallel for
    distributed_array_local_for(
      distributed_array,
      i,
      {
        // Use global offset to create value for this local index
        double j = (i+1) + distributed_array->global_offset;
        distributed_array->local_array[i] = sin( (j/distributed_array->total_elts) * 3.14159265358979323846 );
      }
    );
}

// \brief "Stencilize" a local array in parallel
// The stencil function is: A'[i] = max( A[i-1], A[i], A[i+1] ) / (1 + abs( min( A[i-1], A[i], A[i+1]  ) ) )
// Operation happens 'in-place' in that the distribued array object is modified,
// but because there can be a random order of modifications, this still requires
// the allocation of a new array to write updates into. When all updates are
// written, the array object's previous local array is freed, and the update
// array is assigned as the array object's local array. This is still legal for
// future deallocation of the distributed array object. Bondaries are handled
// by only using the valid cells in the neighborhood in the stencil fuction.
// \param distributed_array distributed array object whose local array will have stencil operation applied to it.
void in_place_stencilize_local_array( distributed_array* distributed_array ){
  // Array where updates are written to.
  // At the end, this will become the new local array.
  double* update_array = (double*) malloc( distributed_array->local_elts * sizeof(double) );

  // Use these constants for less typing.
  const double* const array = distributed_array->local_array;
  const size_t n_elts = distributed_array->local_elts;

  // Note: Schedule and chunk-size were set at program init.
  //       There should be no reason to need any scheduling causes here.
  #pragma omp parallel for
  distributed_array_local_for(
    distributed_array,
    i,
    {
      double max_val;
      double min_val;
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
      update_array[i] =  max_val / (1 + abs(min_val) );
    }
  );

  // Swap out old array with update array, free old array
  double* previous_local_array = distributed_array->local_array;
  distributed_array->local_array = update_array;
  free( previous_local_array );
}

// \brief Distributed-Parallel "Stencilize" whole distributed array
// The stencil function is: A'[i] = max( A[i-1], A[i], A[i+1] ) / (1 + abs( min( A[i-1], A[i], A[i+1]  ) ) )
// Bondaries are handled by only using the valid cells in the neighborhood in
// the stencil fuction. Communication occurs between ranks which have adjacent
// and the function is applied to those neighborhood, and thus the stencilize
// operation always results in the same array regardless of if and how it is
// distributed (both in terms of number of processes, and in work distribution).
// \param distributed_array distributed array object to perform stencil operation on
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
  in_place_stencilize_local_array( distributed_array );


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
    double max_val = max3( end_0_neighborhood[0], end_0_neighborhood[1], end_0_neighborhood[2] );
    double min_val = min3( end_0_neighborhood[0], end_0_neighborhood[1], end_0_neighborhood[2] );

    distributed_array->local_array[0] =  max_val / (1 + abs(min_val) );
  }

  // Compute high side
  if( global_program_context.rank != global_program_context.n_ranks - 1 ){
    double max_val = max3( end_n_neighborhood[0], end_n_neighborhood[1], end_n_neighborhood[2] );
    double min_val = min3( end_n_neighborhood[0], end_n_neighborhood[1], end_n_neighborhood[2] );

    distributed_array->local_array[distributed_array->local_elts - 1] =  max_val / (1 + abs(min_val) );
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

// \brief Parallel sum local portion of distributed array
// \param distributed_array distributed array object whose local array elements will be summed.
// \return the value of the sum of the distributed array object's local array
double sum_local_array( distributed_array* distributed_array ){
  double rank_local_sum = 0.0;

  // Note: Schedule and chunk-size were set at program init.
  //       There should be no reason to need any scheduling causes here.
  #pragma omp parallel for reduction(+: rank_local_sum)
  distributed_array_local_for(
    distributed_array,
    i,
    {
      rank_local_sum += distributed_array->local_array[i];
    }
  );

  return rank_local_sum;
}

// \brief Distributed-Parallel sum a distributed array
// All ranks communicate their local sums to the primary, who computes the
// final value.
// \return value of sum (only if called on primary rank or if
//   global_program_context.synchronize_at_end_of_distributed_array_operations
//   set).
double sum_distributed_array( distributed_array* distributed_array ){
  // Array where (on primary) sums will be gathered into
  double* all_sums = NULL ;

  // Only allocate/free on primary
  if( global_program_context.rank == global_program_context.primary_rank ){
    all_sums = (double*) malloc( global_program_context.n_ranks * sizeof(double) );
  }

  // Perform reduction on local portion of array
  double rank_local_sum = sum_local_array( distributed_array );

  // Gather all local reduction
  MPI_Gather( &rank_local_sum, 1, MPI_DOUBLE, all_sums, 1, MPI_DOUBLE, global_program_context.primary_rank, global_program_context.comm );

  // Primary computes final part of reduction locally
  double sum = 0.0;
  if( global_program_context.rank == global_program_context.primary_rank ){
    // Local Sum
    for( size_t i = 0; i < global_program_context.n_ranks; ++i ){
      sum += all_sums[i];
    }

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


// \brief Main
// \param argc number of argument strings (length of argv)
// \param argv array of null-terminated argument strings (length is argc )
// \return exit status
int main( int argc, char** argv ){
  // Initialize program
  program_init( argc, argv );

  // Print information about this execution
  if( global_program_context.verbosity >= verbosity_normal && global_program_context.rank == global_program_context.primary_rank ){
    printf( "Performing %d iterations of stencilize and sum over a distributed array with %d elements.\n", global_program_context.iterations, global_program_context.N );
  }

  // Allocate distributed array
  distributed_array array = allocate_distributed_array( global_program_context.N, global_program_context.distribution_type );

  // Print information about ranks and array
  if( global_program_context.verbosity >= verbosity_normal ){
    printf( "Rank %d/%d with %d OpenMP threads owns %lu of %lu elements\n", global_program_context.rank, global_program_context.n_ranks, global_program_context.omp_num_threads, array.local_elts, array.total_elts  );
  }

  // Initialize distributed array with arbitrary values
  init_distributed_array( &array );

  double mean_sum = 0.0;
  for( int iteration = 0; iteration < global_program_context.iterations; ++iteration ){
    // "Stencilize" distributed array
    in_place_stencilize_distributed_array( &array );

    // sum distributed array
    double iteration_sum = sum_distributed_array( &array );
    mean_sum += iteration_sum / global_program_context.iterations;

    // Print reduction value
    // Note: the expression (true | (int)sum) is a trick to force the not optimize
    // the reduce_distributed_array call to be under this conditional.
    if( global_program_context.verbosity >= verbosity_more && (true | (int) iteration_sum) && global_program_context.rank == global_program_context.primary_rank ){
      printf( "Iteration %d sum: %f\n", iteration, iteration_sum );
    }
  }

  // Print mean sum
  if( global_program_context.verbosity >= verbosity_less && global_program_context.rank == global_program_context.primary_rank ){
    printf( "Mean sum: %f\n", mean_sum );
  }

  // Free distributed array
  free_distributed_array( &array );

  // Finalize program
  program_finalize( );

  return 0;
}
