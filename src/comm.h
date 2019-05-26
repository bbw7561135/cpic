#pragma once

#include "block.h"
#include "sim.h"

#define COMM_TAG_NEIGH_SIZE 5
#define COMM_TAG_ITER_SIZE 10
#define COMM_TAG_ITER_MASK (~((~0)<<COMM_TAG_ITER_SIZE))
#define COMM_TAG_NEIGH_MASK (~((~0)<<COMM_TAG_NEIGH_SIZE))

#define COMM_TAG_OP_PARTICLES 1
#define COMM_TAG_OP_RHO 2
#define COMM_TAG_OP_PHI 3

#define COMM_TAG_OP_SIZE 4
#define COMM_TAG_OP_MASK (~((~0)<<COMM_TAG_OP_SIZE))

#define COMM_TAG_DIR_SIZE 4
#define COMM_TAG_DIR_MASK (~((~0)<<COMM_TAG_DIR_SIZE))

int
comm_block(sim_t *sim, block_t *b);

int
block_delta_to_index(int delta[], int dim);

/* Move particles to the correct chunk */
int
comm_plasma_chunk(sim_t *sim, int i, int global_exchange);

int
comm_send_ghost_rho(sim_t *sim);

int
comm_recv_ghost_rho(sim_t *sim);

int
comm_phi_send(sim_t *sim);

int
comm_phi_recv(sim_t *sim);
