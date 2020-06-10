.PHONY: all run profile viewprofile viewtrace viewall clean clean-bin clean-profile

.EXPORT_ALL_VARIABLES:

CC_FLAGS = -O0 -g3 -fopenmp
CC=mpicc

EXE=miniapp


all: $(EXE)

$(EXE):%:%.c
	$(CC) $(CC_FLAGS) $^ -o $@ -lm

TEST_MPI_PROCESSES?=4
OMP_NUM_THREADS?=2
TEST_NUM_ELEMENTS?=1000

HPC_STRUCT=$(EXE)_threads-$(OMP_NUM_THREADS).hpcstruct
HPC_MEASUREMENTS=$(EXE)_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS).hpcmeasurements
HPC_DATABASE=$(EXE)_procs-$(TEST_MPI_PROCESSES)_threads-$(OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS).hpcdatabase


run: $(HPC_MEASUREMENTS)

profile: $(HPC_STRUCT) $(HPC_MEASUREMENTS) $(HPC_DATABASE)

viewprofile: $(HPC_DATABASE)
	hpcviewer $(HPC_DATABASE)

viewtrace: $(HPC_DATABASE)
	hpctraceviewer $(HPC_DATABASE)

viewall: $(HPC_DATABASE)
	hpcviewer $(HPC_DATABASE) &
	hpctraceviewer $(HPC_DATABASE)

# Run app with hpcrun to create measurements file (HPC_MEASUREMENTS)
$(HPC_MEASUREMENTS): $(EXE)
	mpirun -np $(TEST_MPI_PROCESSES) hpcrun -o $@ -t  ./$< $(TEST_NUM_ELEMENTS)

# Inpsect executable
$(HPC_STRUCT): $(EXE)
	hpcstruct -j $(OMP_NUM_THREADS) $(EXE) -o $@

$(HPC_DATABASE): $(HPC_STRUCT) $(HPC_MEASUREMENTS)
	hpcprof -S $(HPC_STRUCT) -I ./+ $(HPC_MEASUREMENTS) -o $@


clean: clean-profile clean-bin

clean-bin:
	- rm $(EXE) *.o

clean-profile:
	- rm -r *.hpcstruct *.hpcmeasurements *.hpcdatabase
