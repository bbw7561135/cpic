#include "tap.h"

#define DEBUG 1
#include "log.h"
#include "utils.h"
#include <assert.h>
#include <stdlib.h>

#define INDEX_LOCAL	0
#define INDEX_GLOBAL	1


/* Allocates a buffer of size "size" to be seen by all MPI process of comm
 * "comm". Must be caled by *one* process in the communicator, the one whic will
 * allocate memory, the others must call tap_shared_query */

void *
tap_shared_alloc(size_t size, MPI_Comm comm)
{
	MPI_Win win;
	void *buf;
	MPI_Aint asize;

	asize = (MPI_Aint) size;

	MPI_Win_allocate_shared(size, 1, MPI_INFO_NULL, comm, &buf, &win);

	return buf;
}

/* Obtains the shared buffer. Must be called by all the other processes which
 * didn't allocate */

void *
tap_shared_query(size_t *size, MPI_Comm comm)
{
	MPI_Win win;
	void *buf;
	int disp;
	MPI_Aint asize;

	disp = 1;

	MPI_Win_allocate_shared(0, disp, MPI_INFO_NULL, comm, &buf, &win);
	MPI_Win_shared_query(win, MPI_PROC_NULL, &asize, &disp, &buf);

	*size = (size_t) asize;

	return buf;
}

/* The master is placed at the end */
int
reorder_node_comm(MPI_Comm comm, int master_rank, int size, MPI_Comm *new_comm)
{
	int i, ret;
	int *order;
	MPI_Group group, new_group;

	order = safe_malloc(sizeof(int) * size);

	MPI_Comm_group(comm, &group);

	/* All ranks are kept the same */
	for(i=0; i<size; i++)
	{
		order[i] = i;
	}

	/* But we swap the master with the last rank */
	order[master_rank] = size - 1;
	order[size - 1] = master_rank;

	MPI_Group_incl(group, size, order, &new_group);
	free(order);

	return MPI_Comm_create(comm, new_group, new_comm);
}

/* This should be the good version, but ENOTIME, let's go with the quick and
 * dirty one */
//int
//tap_sort_ranks_worker(MPI_Comm node_comm, int master_rank)
//{
//	MPI_Group node_group;
//	int node_rank, rank;
//	int tag, buf[2];
//
//	tag = 959;
//
//	MPI_Comm_group(node_comm, &node_group);
//	MPI_Comm_rank(node_comm, &node_rank);
//
//	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
//
//	if(master_rank >= 0)
//	{
//		buf[INDEX_LOCAL] = node_rank;
//		buf[INDEX_GLOBAL] = rank;
//	}
//	else
//	{
//		buf[INDEX_LOCAL] = node_rank;
//		buf[INDEX_GLOBAL] = rank;
//	}
//
//	MPI_Send(buf, 2, MPI_INT, master_rank, tag, node_comm);
//
//	return 0;
//}
//
//int
//tap_sort_ranks_master(MPI_Comm node_comm)
//{
//	MPI_Group node_group;
//	int node_rank, node_size, rank;
//	int i, tag;
//	int *new_order;
//	int buf[2];
//
//	tag = 959;
//
//	MPI_Comm_group(node_comm, &node_group);
//	MPI_Comm_rank(node_comm, &node_rank);
//	MPI_Comm_size(node_comm, &node_size);
//
//	order = safe_malloc(sizeof(int) * node_size);
//
//	/* First the workers must know which process is the master */
//
//	/* We move the master to the end */
//	for(i=0; i<node_size; i++)
//	{
//		order[i] = i;
//	}
//	order[rank] = node_size - 1;
//	order[node_size - 1] = rank;
//
//	/* Now we propagate the new order to the workers */
//	MPI_Bcast(order, node_size, MPI_INT, rank, );
//
//	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
//
//	for(i=0; i<node_size; i++)
//	{
//		if(i == node_rank)
//			continue;
//
//		MPI_Recv(buf, 2, MPI_INT, i, tag, node_comm, MPI_STATUS_IGNORE);
//		new_order[buf[INDEX_LOCAL]] = buf[INDEX_GLOBAL];
//	}
//
//	if(master_rank >= 0)
//	{
//		buf[INDEX_LOCAL] = node_rank;
//		buf[INDEX_GLOBAL] = rank;
//	}
//	else
//	{
//		buf[INDEX_LOCAL] = node_rank;
//		buf[INDEX_GLOBAL] = rank;
//	}
//
//	MPI_Send(buf, 2, MPI_INT, master_rank, tag, node_comm);
//
//	return 0;
//}

/* Launches "n" workers in *each process* of MPI_COMM_WORLD, using "cmd" and
 * sets a communicator for all workers in the node. Each process should run in
 * one node. */
int
tap_spawn(int n, char *cmd, MPI_Comm *comm)
{
	MPI_Comm intercomm, universe, node_comm;
	int i, node_size, node_rank, size;
	int master_rank, master_size;
	int total_workers;
	MPI_Group node_group;

	MPI_Comm_size(MPI_COMM_WORLD, &size);

	total_workers = n * size;
	MPI_Comm_spawn(cmd, MPI_ARGV_NULL, total_workers,
		MPI_INFO_NULL, 0, MPI_COMM_WORLD,
		&intercomm, MPI_ERRCODES_IGNORE);

	MPI_Intercomm_merge(intercomm, 0, &universe);

	MPI_Comm_split_type(universe, MPI_COMM_TYPE_SHARED, 0,
			MPI_INFO_NULL, &node_comm);

	MPI_Comm_size(node_comm, &node_size);
	MPI_Comm_rank(node_comm, &node_rank);
	assert(node_size == n + 1);

	for(i=0; i<node_size; i++)
	{
		if(i==node_rank) continue;
		MPI_Send(&node_rank, 1, MPI_INT, i, 987, node_comm);
	}

	dbg("Communicator with workers has %d processes\n", node_size);

	reorder_node_comm(node_comm, node_rank, node_size, comm);

	MPI_Comm_rank(*comm, &node_rank);
	dbg("After reordering, master is at %d\n", node_rank);
	assert(node_rank == node_size-1);

	/* The workers need to know which rank is the master in master_comm */
	MPI_Comm_rank(MPI_COMM_WORLD, &master_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &master_size);

	for(i=0; i<node_size; i++)
	{
		if(i==node_rank) continue;
		MPI_Send(&master_rank, 1, MPI_INT, i, 988, *comm);
		MPI_Send(&n, 1, MPI_INT, i, 989, *comm);
	}

	return 0;
}

struct worker_info
{
	int master_rank;
	int node_rank;
	int worker_rank;
};

int
reorder_workers(MPI_Comm old, struct worker_info *info, int nworkers, MPI_Comm *new_comm)
{
	int i, n;
	int *order;
	MPI_Group old_group, new_group;

	MPI_Comm_size(old, &n);

	order = safe_malloc(sizeof(int) * n);

	for(i=0; i<n; i++)
	{
		order[i] = info[i].master_rank * nworkers + info[i].node_rank;
	}

	MPI_Comm_group(old, &old_group);
	MPI_Group_incl(old_group, n, order, &new_group);

	free(order);

	return MPI_Comm_create(old, new_group, new_comm);
}

int
tap_child(MPI_Comm *node_comm, MPI_Comm *worker_comm, int *master_rank)
{
	int i;
	int size, rank, worker_rank, total_workers;
	int nworkers, master_node_rank;
	struct worker_info *info;
	struct worker_info myinfo;
	MPI_Comm parent, universe, old_node_comm;

	MPI_Comm_get_parent(&parent);

	assert(parent != MPI_COMM_NULL);


	MPI_Intercomm_merge(parent, 0, &universe);

	MPI_Comm_split_type(universe, MPI_COMM_TYPE_SHARED, 0,
			MPI_INFO_NULL, &old_node_comm);

	MPI_Comm_rank(old_node_comm, &rank);
	MPI_Comm_size(old_node_comm, &size);

	MPI_Recv(&master_node_rank, 1, MPI_INT, MPI_ANY_SOURCE, 987, old_node_comm, MPI_STATUS_IGNORE);

	dbg("Worker is at %d/%d master at %d\n", rank, size, master_node_rank);
	reorder_node_comm(old_node_comm, master_node_rank, size, node_comm);

	MPI_Comm_rank(*node_comm, &rank);
	dbg("After reordering, worker is at %d\n", rank);

	master_node_rank = size - 1;

	MPI_Recv(master_rank, 1, MPI_INT, master_node_rank, 988, *node_comm, MPI_STATUS_IGNORE);
	MPI_Recv(&nworkers, 1, MPI_INT, master_node_rank, 989, *node_comm, MPI_STATUS_IGNORE);
	dbg("Worker %d master is %d, computed rank is %d\n", rank, *master_rank, ((*master_rank)*nworkers)+rank);

	MPI_Comm_size(MPI_COMM_WORLD, &total_workers);
	MPI_Comm_rank(MPI_COMM_WORLD, &worker_rank);

	dbg("Worker %d has total_workers = %d\n", rank, total_workers);

	info = safe_malloc(sizeof(*info) * total_workers);

	myinfo.master_rank = *master_rank;
	myinfo.node_rank = rank;
	myinfo.worker_rank = worker_rank;

	MPI_Allgather(&myinfo, sizeof(myinfo), MPI_BYTE,
			info, sizeof(*info)*total_workers, MPI_BYTE,
			MPI_COMM_WORLD);

	for(i=0; i<total_workers; i++)
	{
		struct worker_info *infop;

		infop = &info[i];
		dbg("Worker %d sees worker %d at node_rank=%d and master_rank=%d\n",
				worker_rank, i, infop->node_rank, infop->master_rank);
	}

	reorder_workers(MPI_COMM_WORLD, info, nworkers, worker_comm);

	return 0;
}


int
tap_sort_ranks(int nmasters, int nworkers, MPI_Comm *new_comm)
{
	/* FIXME: We assume the order in MPI_COMM_WORLD of a worker i of n
	 * workers per master, of the master j of m masters, to be (i*m + j).
	 *
	 * Unfortunately, we want (j*n + i) */

	int i, j, k, size, rank;
	int *ranks;
	MPI_Group old, new;

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	dbg("Reordering the %d workers by %d\n", size, rank);

	ranks = safe_malloc(size * sizeof(int));

	for(k=0; k<size; k++)
	{
		j = k % nmasters;
		i = k / nmasters;
		ranks[k] = j * nworkers + i;
		if(rank == 0)
			err("ranks[k=%d] = %d, i=%d, j=%d\n", k, ranks[k], i, j);
	}

	MPI_Comm_group(MPI_COMM_WORLD, &old);

	MPI_Group_incl(old, size, ranks, &new);
	MPI_Comm_create(MPI_COMM_WORLD, new, new_comm);
	dbg("Reorder complete for %d\n", rank);

	free(ranks);

	return 0;
}
