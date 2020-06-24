.PHONY: build run analyze analyse analysis viewprofile viewtrace viewall clean clean-bin clean-exe clean-objs clean-profile clean-hpcstruct clean-hpcmeasurement clean-hpcdatabase

.EXPORT_ALL_VARIABLES:

CC_FLAGS = -O3 -gdwarf-2 -g3 -lm -fopenmp
CC=mpicc

EXE=miniapp

MPISPWAN?=mpirun
HPCSTRUCT?=hpcstruct
HPCRUN?=hpcrun
HPCPROF?=hpcprof-mpi

TEST_MPI_PROCESSES?=4
OMP_NUM_THREADS?=2
TEST_NUM_ELEMENTS?=1000

HPC_TRACE?=yes
HPC_DATABASE_METRIC_DB?=yes

HPC_RUN_EVENTS ?= CPUTIME@1
hpc_run_events_name != echo $(HPC_RUN_EVENTS) | sed 's|[[:space:]]\+|_|g'
HPC_STRUCT=$(EXE).hpcstruct
HPC_MEASUREMENTS=$(EXE)_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS)_trace-$(HPC_TRACE)_$(hpc_run_events_name).hpcmeasurements
HPC_DATABASE=$(EXE)_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS)_trace-$(HPC_TRACE)_$(hpc_run_events_name)_metric-db-$(HPC_DATABASE_METRIC_DB).hpcdatabase

ifeq ($(HPC_TRACE),yes)
	hpcrun_trace_arg=-t
else
	ifeq ($(HPC_TRACE),no)
		hpcrun_trace_arg=
	else
		$(error Bad HPC_TRACE value "$(HPC_TRACE)" must be "yes" or "no")
	endif
endif

ifeq ($(HPC_RUN_EVENTS), )
	hpc_run_events_arg =
else
	hpc_run_events_arg = -e $(shell echo $(HPC_RUN_EVENTS) | sed 's|[[:space:]]\+| -e |g')
endif

build: $(EXE)

run: $(HPC_MEASUREMENTS)

analyze: $(HPC_STRUCT) $(HPC_MEASUREMENTS) $(HPC_DATABASE)
analyse: analyze
analysis: analyze

viewprofile: $(HPC_DATABASE)
	hpcviewer $(HPC_DATABASE) &

viewtrace: $(HPC_DATABASE)
	hpctraceviewer $(HPC_DATABASE) &

viewall: $(HPC_DATABASE)
	hpcviewer $(HPC_DATABASE) &
	hpctraceviewer $(HPC_DATABASE) &

# Build executable
$(EXE):%:%.c
	$(CC) $^ -o $@ $(CC_FLAGS)

# Run app with hpcrun to create measurements file (HPC_MEASUREMENTS)
$(HPC_MEASUREMENTS): $(EXE)
	$(MPISPWAN) --oversubscribe -np $(TEST_MPI_PROCESSES) $(HPCRUN) $(hpcrun_trace_arg) $(hpc_run_events_arg) -o $@ ./$(EXE) $(TEST_NUM_ELEMENTS)

# Inspect executable
$(HPC_STRUCT): $(EXE)
	$(HPCSTRUCT) $(EXE) -o $@

# Create database from struct file (analysis of executable) and source analysis
$(HPC_DATABASE): $(HPC_STRUCT) $(HPC_MEASUREMENTS)
	$(HPCPROF) -S $(HPC_STRUCT) -I ./+ $(HPC_MEASUREMENTS) --metric-db $(HPC_DATABASE_METRIC_DB) -o $@


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
