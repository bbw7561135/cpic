#include "specie.h"
#include "mat.h"
#include "block.h"
#include "particle.h"

#define DEBUG 1
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <utlist.h>

int
specie_init(sim_t *sim, config_setting_t *cs, specie_t *s)
{
	config_setting_lookup_string(cs, "name", &s->name);
	config_setting_lookup_float(cs, "charge", &s->q);
	config_setting_lookup_float(cs, "mass", &s->m);
	config_setting_lookup_int(cs, "particles", &s->nparticles);

	s->conf = cs;

	if(s->nparticles <= 0)
	{
		err("The number of particles must be greater than 0\n");
		return 1;
	}

	dbg("New specie \"%s\" added\n", s->name);

	return 0;
}

int
species_init(sim_t *sim)
{
	config_setting_t *cs, *specie;
	int i, ns;

	cs = config_lookup(sim->conf, "species");
	ns = config_setting_length(cs);

	if(ns <= 0)
	{
		err("The number of species must be at least 1.\n");
		return 1;
	}

	sim->nspecies = ns;
	sim->species = malloc(ns * sizeof(specie_t));

	for(i=0; i < ns; i++)
	{
		specie = config_setting_get_elem(cs, i);
		if(specie_init(sim, specie, &sim->species[i]))
		{
			err("specie_init failed\n");
			return 1;
		}
	}
	return 0;
}

void
specie_block_add_particle(specie_block_t *sb, particle_t *p)
{
	dbg("Adding particle p_%d to specie block %p\n", p->i, sb);
	DL_APPEND(sb->particles, p);
}

int
specie_block_init(sim_t *sim, block_t *b, specie_block_t *sb, specie_t *s)
{
	int ib, i, j, n;
	particle_t *p;

	sb->info = s;
	sb->particles = NULL;

	n = sim->ntblocks[X] * sim->ntblocks[Y];
	ib = b->i[X] * sim->nblocks[Y] + b->i[Y];

	dbg("n=%d, ib=%d\n", n, ib);

	for(i = ib; i < s->nparticles; i+=n)
	{
		dbg("i=%d\n", i);
		/* Ensure correct process rank */
		if((i % sim->ntblocks[Y]) != b->i[Y])
		{
			dbg("(i %% sim->ntblocks[Y])=%d, expecting b->i[Y]=%d\n",
				(i % sim->ntblocks[Y]), b->i[Y]);
			abort();
		}

		j = i / sim->ntblocks[Y];

		/* Ensure correct local block */
		if((j % sim->ntblocks[X]) != b->i[X])
		{
			dbg("(j %% sim->ntblocks[X])=%d, expecting b->i[X]=%d\n",
				(j % sim->ntblocks[X]), b->i[X]);
			abort();
		}

		/* We assign the particle i to the block b, at the current
		 * process */

		p = particle_init();

		p->i = i;
		specie_block_add_particle(sb, p);
	}
	return 0;
}
