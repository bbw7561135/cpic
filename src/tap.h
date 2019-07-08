#include <mpi.h>
#include <stddef.h>

enum tap_process {
	TAP_PARENT,
	TAP_CHILD
};

void *
tap_shared_alloc(size_t size, MPI_Comm comm);

void *
tap_shared_query(size_t *size, MPI_Comm comm);

int
tap_spawn(int n, char *cmd, MPI_Comm *comm);

int
tap_child(MPI_Comm *node_comm, MPI_Comm *worker_comm, int *master_rank);
