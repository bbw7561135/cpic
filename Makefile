MODULES:=src
#MODULES+=test user

CC=gcc
#CC=clang
#OCC=mcc
LDLIBS:=
CFLAGS:=-g -pthread -Wall
LDFLAGS:=-L. -Wl,-rpath,.

CFLAGS+=-DGLOBAL_DEBUG

# Instrument functions so Extrae can get some information
CFLAGS+=-finstrument-functions

# Stack protector
#CFLAGS+=-fstack-protector-all

# Optimization?
#CFLAGS+=-O2

#Include all modules for headers
CFLAGS+=$(patsubst %,-I%,$(MODULES))

BIN:=
SRC:=

all:


# include the description for each module
include $(patsubst %,%/build.mk,$(MODULES))


# include the C include dependencies
include $(OBJ:.o=.d)

# determine the object files
OBJ := \
$(patsubst %.c,%.o, $(filter %.c,$(SRC))) \
$(patsubst %.y,%.o, $(filter %.y,$(SRC)))
#OBJ=$(SRC:.c=.o)
DEP=$(SRC:.c=.d)

include $(DEP)

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

all: $(BIN)

#test/cyclotron: $(CPIC_OBJ)

#nm -g cpic | sed -e 's/ . / /g' -e '/GLIBC/d' -e '/ _/d' -e '/^ /d' -e 's/ /#/g' -e '/interpol/d' > function.list

function.all: cpic
	nm -g cpic > $@

function.list: function.all
	grep -wf filter.list $< | sed -e 's/ . /#/g' > $@

clean:
	rm -f $(OBJ) $(BIN) $(DEP)

#%.mcc.c: %.c
#	$(OCC) $(CFLAGS) $(OCFLAGS) -y -o $@ $<
#
#load:
#	module load gcc/7.2.0 extrae ompss-2
#
cpic.prv: cpic extrae2.xml trace.sh function.list
	rm -rf set-0/ TRACE.sym TRACE.mpits
	taskset -c 0-8 mpirun --oversubscribe -n 8 bash -c './trace.sh ./cpic conf/mpi.conf 2> $$PMIX_RANK.log'
	mpi2prv -f TRACE.mpits -o cpic.prv

run: cpic.prv

.PHONY: cpic.prv

#runmn:
#	LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/apps/PM/ompss-2/2018.11/lib taskset -c 0-20 ./cpic
#	#NANOS6=extrae taskset -c 0-25 ./cpic
#	#NANOS6=extrae taskset -c 0-20 ./cpic
#	${EXTRAE_HOME}/bin/mpi2prv -f TRACE.mpits -o output.prv
#
#vg: cpic
#	valgrind --fair-sched=yes ./cpic
#
#.PRECIOUS: %.mcc.c
