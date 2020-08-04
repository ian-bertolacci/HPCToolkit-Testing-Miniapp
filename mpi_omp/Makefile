.PHONY: all build run analyze analyse analysis viewprofile viewtrace viewall clean clean-bin clean-exe clean-objs clean-profile clean-hpcstruct clean-hpcmeasurement clean-hpcdatabase

.EXPORT_ALL_VARIABLES:

MPICC?=mpicc
MPICXX?=mpicxx
MPISPWAN?=mpirun

HPCSTRUCT?=hpcstruct
HPCRUN?=hpcrun
HPCPROF?=hpcprof

TEST_MPI_PROCESSES?=4
OMP_NUM_THREADS?=2
TEST_NUM_ELEMENTS?=1000

HPC_TRACE?=yes
HPC_DATABASE_METRIC_DB?=no

HPC_RUN_EVENTS ?= CPUTIME@1

CC_FLAGS ?= -O3 -gdwarf-2 -g3 -lm -fopenmp
CC=$(MPICC) # I dont like this, but CC is set by default in make, I think, so ?= does not overwrite the CC variable.

EXE=miniapp

# Setup hpcrun trace arguments
ifeq ($(HPC_TRACE),yes)
	hpcrun_trace_arg=-t
else
	ifeq ($(HPC_TRACE),no)
		hpcrun_trace_arg=
	else
		$(error Bad HPC_TRACE value "$(HPC_TRACE)" must be "yes" or "no")
	endif
endif

# Setup hpcrun event arguments and events name substring
ifeq ($(HPC_RUN_EVENTS), )
	hpc_run_events_name=DEFAULT
	hpc_run_events_arg=
else
	hpc_run_events_name = $(shell echo $(HPC_RUN_EVENTS) | sed 's|[[:space:]]\+|_|g' )
	hpc_run_events_arg = -e $(shell echo $(HPC_RUN_EVENTS) | sed 's|[[:space:]]\+| -e |g')
endif

HPC_STRUCT=$(EXE).hpcstruct

HPC_FAIR_MEASUREMENTS = $(EXE)_fair_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS)_trace-$(HPC_TRACE)_$(hpc_run_events_name).hpcmeasurements
HPC_FAIR_DATABASE     = $(EXE)_fair_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS)_trace-$(HPC_TRACE)_$(hpc_run_events_name)_metric-db-$(HPC_DATABASE_METRIC_DB).hpcdatabase

HPC_UNFAIR_MEASUREMENTS = $(EXE)_unfair_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS)_trace-$(HPC_TRACE)_$(hpc_run_events_name).hpcmeasurements
HPC_UNFAIR_DATABASE     = $(EXE)_unfair_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS)_trace-$(HPC_TRACE)_$(hpc_run_events_name)_metric-db-$(HPC_DATABASE_METRIC_DB).hpcdatabase

HPC_MEASUREMENTS = $(HPC_FAIR_MEASUREMENTS) $(HPC_UNFAIR_MEASUREMENTS)
HPC_DATABASES = $(HPC_FAIR_DATABASE) $(HPC_UNFAIR_DATABASE)


# Phoney dependencies
# Build miniapp executable
build: $(EXE)

# Run miniapp executable in profiling mode, creating HPCToolkit measurements output
run: $(HPC_MEASUREMENTS)

# Create HPCToolkit profile database
analyze: $(HPC_STRUCT) $(HPC_MEASUREMENTS) $(HPC_DATABASES)
analyse: analyze
analysis: analyze

all: analyze

# Real dependencies
# Build executable
$(EXE):%:%.c
	$(CC) $^ -o $@ $(CC_FLAGS)

# Run app with hpcrun to create measurements file
# The fair run
$(HPC_FAIR_MEASUREMENTS): $(EXE)
	$(MPISPWAN) --oversubscribe -np $(TEST_MPI_PROCESSES) $(HPCRUN) $(hpcrun_trace_arg) $(hpc_run_events_arg) -o $@ ./$(EXE) -n $(TEST_NUM_ELEMENTS) -q -d fair

# The unfair run
$(HPC_UNFAIR_MEASUREMENTS): $(EXE)
	$(MPISPWAN) --oversubscribe -np $(TEST_MPI_PROCESSES) $(HPCRUN) $(hpcrun_trace_arg) $(hpc_run_events_arg) -o $@ ./$(EXE) -n $(TEST_NUM_ELEMENTS) -q -d unfair

# Inspect executable
$(HPC_STRUCT): $(EXE)
	$(HPCSTRUCT) $(EXE) -o $@

# Create database from struct file (analysis of executable) and source analysis
%_metric-db-$(HPC_DATABASE_METRIC_DB).hpcdatabase: %.hpcmeasurements $(HPC_STRUCT)
	$(HPCPROF) -S $(HPC_STRUCT) -I ./+ $< --metric-db $(HPC_DATABASE_METRIC_DB) -o $@

# Cleaning
clean: clean-profile clean-bin

clean-bin: clean-exe clean-objs

clean-exe:
	- rm $(EXE)

clean-objs:
	-rm *.o

clean-profile: clean-hpcmeasurement clean-hpcdatabase clean-hpcstruct

clean-hpcstruct:
	- rm *.hpcstruct

clean-hpcmeasurement:
	- rm -r *.hpcmeasurements

clean-hpcdatabase:
	- rm -r *.hpcdatabase *.hpcdatabase-*
