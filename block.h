#pragma once

struct block;
typedef struct block block_t;

#include "mat.h"
#include "specie.h"

#include <stdlib.h>

/* A block is designed to run in a node, where the memory can be shared */
struct block
{
	/* Block number */
	int i;

	/* Position of the first node of the block */
	float x;

	/* The fields */
	mat_t *E;
	mat_t *J;
	mat_t *rho;
	/*mat_t *B;*/

	float rE, rJ, rrho;

	/* Particles of the block */
	particle_t *particles;

	/* Lists for particles leaving the block boundary */
	particle_t *left, *right;

};

int
blocks_init(specie_t *s);

void
blocks_print(block_t *blocks, size_t n);

int
block_particle_J(specie_t *s, block_t *b);

int
block_field_J(specie_t *s, block_t *b);

int
block_comm_field_J(block_t *from, block_t *to);

int
block_field_E(specie_t *s, block_t *b);

int
block_particle_E(specie_t *s, block_t *b);

int
block_particle_x(specie_t *s, block_t *b);

int
block_comm_particles(specie_t *s, block_t *left, block_t *b, block_t *right);

int
block_print_particles(specie_t *s, block_t *b);

int
block_comm_field_E(block_t *from, block_t *to);
