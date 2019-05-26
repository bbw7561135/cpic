#include <utlist.h>
#include <string.h>
#include "sim.h"
#include "block.h"
#include "specie.h"
#include "particle.h"
#include "comm.h"
#include "plasma.h"

#define DEBUG 1
#include "log.h"

int
chunk_delta_to_index(int delta[], int dim)
{
	int j, d, n;

	/* Number of total blocks in each dimension considered in the
	 * neighbourhood */
	n = 3;
	j = 0;

	for(d = dim-1; d >= X; d--)
	{
		j *= n;
		j += 1 + delta[d];
	}

//	if(dim == 2)
//	{
//		dbg("Delta (%d,%d) translated to %d\n", delta[X], delta[Y], j);
//	}

	return j;
}

int
queue_particle(particle_set_t *set, particle_t *p, int j)
{
	DL_DELETE(set->particles, p);
	set->nparticles--;

	DL_APPEND(set->out[j], p);
	set->outsize[j]++;

	return 0;
}

void
particle_chunk_index(sim_t *sim, plasma_chunk_t *chunk, particle_t *p, int *dst)
{
	double dx, dy;

	/* Ensure the particle is already wrapped */
	assert(p->x[X] >= 0.0);
	assert(p->x[Y] >= 0.0);
	assert(p->x[X] < sim->L[X]);
	assert(p->x[Y] < sim->L[Y]);

	dx = chunk->L[X];
	dy = chunk->L[Y];

	dst[X] = p->x[X] / dx;
	dst[Y] = p->x[Y] / dy;
}

int
collect_specie(sim_t *sim, plasma_chunk_t *chunk, int is)
{
	particle_t *p, *tmp;
	particle_set_t *set;
	double x0, x1, y0, y1;
	int j, ix, iy;
	int dst, dst_ig[MAX_DIM];

	set = &chunk->species[is];

	/* TODO: Ensure the destination received the packet before
	 * erasing the queue */
	for(j=0; j<sim->nprocs; j++)
	{
		set->out[j] = NULL;
		set->outsize[j] = 0;
	}

	dbg("Moving particles for chunk (%d,%d)\n",
			chunk->ig[X], chunk->ig[Y]);

	ix = chunk->ig[X];
	iy = chunk->ig[Y];

	x0 = chunk->x0[X];
	y0 = chunk->x0[Y];

	x1 = chunk->x1[X];
	y1 = chunk->x1[Y];

	dbg("Chunk goes from (x=%e,y=%e) to (x=%e,y=%e)\n",
			x0, y0, x1, y1);

	DL_FOREACH_SAFE(set->particles, p, tmp)
	{
		/* Wrap particle position arount the whole simulation space, to
		 * determine the target chunk */
		wrap_particle_position(sim, p);

		particle_chunk_index(sim, chunk, p, dst_ig);

		/* If the particle is in our chunk, no movement is needed */
		if(dst_ig[X] == ix && dst_ig[Y] == iy) continue;

		/* Only one chunk per process in the Y direction */
		dst = dst_ig[Y];

		dbg("p%d at (%e,%e) exceeds chunk space "
			"(%e,%e) to (%e,%e), queueing in out dst=%d "
			"ig (%d,%d)\n",
			p->i,
			p->x[X], p->x[Y],
			x0, y0, x1, y1, dst, dst_ig[X], dst_ig[Y]);

		queue_particle(set, p, dst);
	}

	return 0;
}

int
send_packet_neigh(sim_t *sim, plasma_chunk_t *chunk, int dst)
{
	int is, ip, count, np, tag;
	size_t size;
	particle_set_t *set;
	comm_packet_t *pkt;
	specie_packet_t *sp;
	particle_t *p, *tmp;
	void *ptr;

	dbg("Sending out packet to process %d\n", dst);

	np = 0;
	count = 0;
	pkt = chunk->q[dst];
	size = sizeof(comm_packet_t);
	tag = sim->iter & COMM_TAG_ITER_MASK;

	/* Compute queue size */
	for(is=0; is<sim->nspecies; is++)
	{
		set = &chunk->species[is];

		/* Skip empty queues */
		if(set->outsize[dst] == 0)
		{
			dbg("No particles need communication for specie %d\n", is);
			continue;
		}

		count++;

		/* Header */
		size += sizeof(specie_packet_t);

		/* Particles */
		size += set->outsize[dst] * sizeof(particle_t);
	}

	/* Before erasing the previous buffer, we need to ensure that the
	 * receptor got the data. We may prefer to advance the neighbours that
	 * already finished first, in case that advanced nodes are not a
	 * problem. */

	if(pkt)
	{
		MPI_Wait(&chunk->req[dst], MPI_STATUS_IGNORE);
		free(pkt);
	}

	pkt = malloc(size);
	chunk->q[dst] = pkt;
	pkt->count = count;
	pkt->neigh = sim->rank;
	sp = pkt->s;

	/* Copy the particles into the queue */
	for(is=0; is<sim->nspecies; is++)
	{
		set = &chunk->species[is];

		/* Skip empty queues */
		if(set->outsize[dst] == 0) continue;

		sp->nparticles = set->outsize[dst];
		sp->specie_index = is;

		ip = 0;

		DL_FOREACH_SAFE(set->out[dst], p, tmp)
		{
			dbg("Packing particle %d at (%e,%e)\n", p->i, p->x[X], p->x[Y]);
			/* Copy the particle to the packet */
			memcpy(&sp->buf[ip], p, sizeof(*p));

			/* Free particles already copied */
			free(p);
			ip++;
		}

		/* We need to point carefully (byte by byte) to the next header,
		 * but we cannot advance sp properly, as it advaces multiples of
		 * the header. So we use a void pointer */
		ptr = sp;
		/* Header */
		ptr += sizeof(specie_packet_t);
		/* Particles */
		ptr += set->outsize[dst] * sizeof(particle_t);
		np += set->outsize[dst];
		sp = ptr;

		assert((((void *) sp) - ((void *) pkt)) == size);
	}

	dbg("Sending packet of size %lu (%d particles) rank=%d tag=%d\n",
			size, np, dst, tag);

	/* Now the packet is ready to be sent */
	MPI_Isend(pkt, size, MPI_BYTE, dst, tag, MPI_COMM_WORLD, &chunk->req[dst]);
	//MPI_Send(pkt, size, MPI_BYTE, dst, tag, MPI_COMM_WORLD);

	dbg("SENT size=%lu dst=%d tag=%d\n",
			size, dst, tag);
	//dbg("Sending to rank %d done\n", dst);

	return 0;
}


int
share_particles(sim_t *sim, plasma_chunk_t *chunk, int neigh)
{
	int is;
	particle_set_t *set;

	for(is=0; is<sim->nspecies; is++)
	{
		set = &chunk->species[is];

		/* TODO: Implement for multiple threads */

		if(set->outsize[neigh])
		{
			DL_APPEND(set->particles, set->out[neigh]);
			set->nparticles += set->outsize[neigh];
		}

		set->out[neigh] = NULL;
		set->outsize[neigh] = 0;
	}

	return 0;
}

/* Set the packets of particles to send to each neighbour */
int
send_particles(sim_t *sim, plasma_chunk_t *chunk, int global_exchange)
{

	int i, is, next, prev;

	prev = (sim->rank - 1 + sim->nprocs) % sim->nprocs;
	next = (sim->rank + 1) % sim->nprocs;

	if(global_exchange)
		dbg("Global exchange\n");
	else
		dbg("Local exchange\n");

	for(i=0; i<sim->nprocs; i++)
	{
		if(!global_exchange)
		{
			if(i != next && i != prev && i != sim->rank)
			{
				/* Ensure we don't have stored particles
				 * for global communication, when
				 * running in local */
				for(is=0; is<chunk->nspecies; is++)
					assert(chunk->species[is].outsize[i] == 0);

				continue;
			}
		}

		if(i == sim->rank)
		{
			dbg("Communication not needed for myself %d\n", i);
			share_particles(sim, chunk, i);
			continue;
		}

		dbg("Sending packets to proc %d\n", i);
		send_packet_neigh(sim, chunk, i);

	}

	return 0;
}

int
recv_comm_packet(sim_t *sim, plasma_chunk_t *chunk, comm_packet_t *pkt)
{
	particle_set_t *set;
	specie_packet_t *sp;
	particle_t *p, *p2;
	char *ptr;
	int i, is, ip;

	ptr = (char *) pkt->s;

	for(i=0; i<pkt->count; i++)
	{
		sp = (specie_packet_t *) ptr;
		is = sp->specie_index;
		set = &chunk->species[is];

		for(ip=0; ip<sp->nparticles; ip++)
		{
			p = &sp->buf[ip];
			dbg("Unpacking particle %d at (%e,%e)\n", p->i, p->x[X], p->x[Y]);

			p2 = malloc(sizeof(*p2));
			memcpy(p2, p, sizeof(*p));

			particle_set_add(set, p2);
		}

		ptr += sizeof(specie_packet_t);
		ptr += sizeof(particle_t) * sp->nparticles;
	}

	return 0;
}

int
recv_particles(sim_t *sim, plasma_chunk_t *chunk, int global_exchange)
{
	int i;
	int source, tag, size, neigh;
	MPI_Status status;
	comm_packet_t *pkt;
	int *recv_from;
	int max_procs;

	dbg(" --- RECV PHASE REACHED ---\n");

	if(global_exchange)
		max_procs = sim->nprocs;
	else
		max_procs = 2;

	recv_from = calloc(sim->nprocs, sizeof(int));

	tag = sim->iter & COMM_TAG_ITER_MASK;

	/* FIXME: We shouldn't need to receive from more than 2 processes */
	for(i=0; i<max_procs; i++)
	{
		/* FIXME: We trust that only one message with the iteration tag
		 * is sent from each neighbour */

		if(i == sim->rank) continue;

		source = MPI_ANY_SOURCE;

		//dbg("Receiving packets from rank=any tag=%d iter=%d\n",
		//		tag, sim->iter);

		//MPI_Recv(buf, 1024, MPI_BYTE, chunk->neigh_rank[i],
		//	MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		MPI_Probe(MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, &status);

		source = status.MPI_SOURCE;

		dbg("PROB rank=%d tag=%d\n", source, tag);

		assert(status.MPI_TAG == tag);

		MPI_Get_count(&status, MPI_BYTE, &size);

		pkt = malloc(size);

		/* Can this receive another packet? */
		MPI_Recv(pkt, size, MPI_BYTE, source, tag,
				MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		neigh = pkt->neigh;

		dbg("RECV size=%d rank=%d neigh=%d tag=%d rf[%d]=%d\n",
				size, source, neigh, tag, neigh, recv_from[neigh]);

		assert(recv_from[neigh] == 0);
		recv_from[neigh]++;

		/* Do some stuff with pkt */
		recv_comm_packet(sim, chunk, pkt);

		free(pkt);

	}

	free(recv_from);

	return 0;
}


/* Move particles to the correct chunk */
int
comm_plasma_chunk(sim_t *sim, int i, int global_exchange)
{
	int is;
	plasma_chunk_t *chunk;
	plasma_t *plasma;

	plasma = &sim->plasma;
	chunk = &plasma->chunks[i];

	/* Collect particles in a queue that need to change chunk */
	for(is = 0; is < sim->nspecies; is++)
	{
		collect_specie(sim, chunk, is);
	}

	/* Then fill the packets and send each to the corresponding neighbour */
	send_particles(sim, chunk, global_exchange);

	/* Finally receive particles from the neighbours */
	recv_particles(sim, chunk, global_exchange);


	return 0;
}

int
comm_mat_send(sim_t *sim, double *data, int size, int dst, int op, int dir, MPI_Request *req)
{
	int tag;

	tag = op << COMM_TAG_ITER_SIZE;
	tag |= sim->iter & COMM_TAG_ITER_MASK;
	tag <<= COMM_TAG_DIR_SIZE;
	tag |= dir;

	if(*req)
		MPI_Wait(req, MPI_STATUS_IGNORE);

	dbg("SEND mat size=%d rank=%d tag=%d op=%d\n", size, dst, tag, op);
	//MPI_Send(data, size, MPI_DOUBLE, dst, tag, MPI_COMM_WORLD);
	MPI_Isend(data, size, MPI_DOUBLE, dst, tag, MPI_COMM_WORLD, req);

	return 0;
}

int
comm_mat_recv(sim_t *sim, double *data, int size, int dst, int op, int dir)
{
	int tag;

	tag = op << COMM_TAG_ITER_SIZE;
	tag |= sim->iter & COMM_TAG_ITER_MASK;
	tag <<= COMM_TAG_DIR_SIZE;
	tag |= dir;

	dbg("RECV mat size=%d rank=%d tag=%d op=%d\n", size, dst, tag, op);
	MPI_Recv(data, size, MPI_DOUBLE, dst, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	return 0;
}

int
comm_send_ghost_rho(sim_t *sim)
{
	int op, neigh, size;
	double *ptr;
	mat_t *rho;
	field_t *f;

	f = &sim->field;

	/* We only consider the 2D space by now and only 1 plasma chunk*/
	if(sim->dim != 2)
		die("Communication of fields only implemented for 2D\n");

	assert(sim->plasma_chunks == 1);

	rho = f->rho;

	neigh = (sim->rank + 1) % sim->nprocs;

	op = COMM_TAG_OP_RHO;

	/* We already have the ghost row of the lower part in contiguous memory
	 * */

	ptr = &MAT_XY(rho, 0, sim->blocksize[Y] - sim->ghostpoints);

	/* We only send 1 row of ghost elements, so we truncate rho to avoid
	 * sending the VARIABLE padding added by the FFTW */
	size = sim->blocksize[X] * sim->ghostpoints;

	/* Otherwise we will need to remove the padding in X */
	assert(sim->ghostpoints == 1);

	dbg("SEND rho size=%d rank=%d op=%d\n", size, neigh, op);
	//MPI_Send(ptr, size, MPI_DOUBLE, neigh, tag, MPI_COMM_WORLD);
	comm_mat_send(sim, ptr, size, neigh, op, SOUTH, &f->req_rho[SOUTH]);

	return 0;
}

int
comm_recv_ghost_rho(sim_t *sim)
{
	int op, neigh, size, ix, iy;
	double *ptr;
	mat_t *rho;

	/* We only consider the 2D space by now and plasma chunks = 1 */
	if(sim->dim != 2)
		die("Communication of fields only implemented for 2D\n");

	assert(sim->plasma_chunks == 1);

	/* Otherwise we will need to remove the padding in X */
	assert(sim->ghostpoints == 1);

	rho = sim->field.rho;

	neigh = (sim->rank + sim->nprocs - 1) % sim->nprocs;

	op = COMM_TAG_OP_RHO;

	/* We need to add the data to our rho matrix at the first row, can MPI
	 * add the buffer as it cames, without a temporal buffer? TODO: Find out */

	ptr = &MAT_XY(rho, 0, sim->blocksize[Y]);

	/* We only recv 1 row of ghost elements, so we truncate rho to avoid
	 * sending the VARIABLE padding added by the FFTW */
	size = sim->blocksize[X] * sim->ghostpoints;
	ptr = malloc(sizeof(double) * size);

	dbg("RECV rho size=%d rank=%d op=%d\n", size, neigh, op);
	comm_mat_recv(sim, ptr, size, neigh, op, SOUTH);

	/* Finally add the received frontier */
	iy = 0;
	for(ix=0; ix<sim->blocksize[X]; ix++)
	{
		MAT_XY(rho, ix, iy) += ptr[ix];
	}

	free(ptr);

	mat_print(rho, "rho after add the ghost");

	return 0;
}


int
comm_phi_send(sim_t *sim)
{
	int size, south, north, op;
	double *data;
	mat_t *phi;
	mat_t *gn, *gs;
	field_t *f;

	f = &sim->field;
	phi = f->phi;
	gn = f->ghostphi[NORTH];
	gs = f->ghostphi[SOUTH];

	north = (sim->rank + sim->nprocs - 1) % sim->nprocs;
	south = (sim->rank + sim->nprocs + 1) % sim->nprocs;

	op = COMM_TAG_OP_PHI;

	/* We also send the FFT padding, as otherwise we need to pack the
	 * frontier ghosts. Notice that we swap the destination rank and the
	 * tag used to indicate the reception direction.*/

	data = &MAT_XY(phi, 0, 0);
	size = gs->real_shape[X] * gs->shape[Y];
	comm_mat_send(sim, data, size, north, op, SOUTH, &f->req_phi[SOUTH]);

	data = &MAT_XY(phi, 0, phi->shape[Y] - gn->shape[Y]);
	size = gn->real_shape[X] * gn->shape[Y];
	comm_mat_send(sim, data, size, south, op, NORTH, &f->req_phi[NORTH]);

	return 0;
}

int
comm_phi_recv(sim_t *sim)
{
	int size, south, north, op;
	mat_t *gn, *gs;

	gn = sim->field.ghostphi[NORTH];
	gs = sim->field.ghostphi[SOUTH];

	north = (sim->rank + sim->nprocs - 1) % sim->nprocs;
	south = (sim->rank + sim->nprocs + 1) % sim->nprocs;

	op = COMM_TAG_OP_PHI;

	size = gs->real_shape[X] * gs->shape[Y];
	comm_mat_recv(sim, gs->data, size, south, op, SOUTH);

	size = gn->real_shape[X] * gn->shape[Y];
	comm_mat_recv(sim, gn->data, size, north, op, NORTH);

	return 0;
}
