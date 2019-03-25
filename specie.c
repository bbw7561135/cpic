#include "specie.h"
#include "mat.h"
#include "block.h"
#include "particle.h"

#define DEBUG 0
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

specie_t *
specie_alloc(int dim, int *shape, int nparticles)
{
	specie_t *s;

	s = malloc(sizeof(specie_t));
	s->nparticles = nparticles;

	s->particles = malloc(nparticles * sizeof(particle_t));

	return s;
}

int
specie_init(sim_t *sim, config_setting_t *cs, specie_t *s)
{
	config_setting_lookup_float(cs, "charge", &s->q);
	config_setting_lookup_float(cs, "mass", &s->m);
	config_setting_lookup_int(cs, "particles", &s->nparticles);

	if(s->nparticles <= 0)
	{
		err("The number of particles must be greater than 0\n");
		return 1;
	}

	s->particles = malloc(s->nparticles * sizeof(particle_t));

	if(particles_init(sim, cs, s))
		return 1;

	if(blocks_init(sim, s))
		return 1;

	return 0;
}

int
species_init(sim_t *sim, config_t *conf)
{
	config_setting_t *cs, *specie;
	int i, ns;

	cs = config_lookup(conf, "species");

	ns = config_setting_length(cs);

	if(ns <= 0)
	{
		err("The number of species must be at least 1.\n");
		return 1;
	}

	sim->nspecies = ns;
	sim->species = calloc(ns, sizeof(specie_t));

	for(i=0; i < ns; i++)
	{
		specie = config_setting_get_elem(cs, i);
		dbg("specie_init for sim %p, specie conf %p, specie %p\n",
				sim, specie, &sim->species[i]);
		if(specie_init(sim, specie, &sim->species[i]))
		{
			err("specie_init failed\n");
			return 1;
		}
	}
	return 0;
}

void
specie_step(sim_t *sim)
{
	sim->t += sim->dt;
}

int
specie_print(sim_t *sim, specie_t *s)
{
	int i,j;
	particle_t *p;
	double x, u, max_x;
	int np;

	config_lookup_int(sim->conf, "plot.track_particles", &np);

	if((np == 0) || (np > s->nparticles))
		np = s->nparticles;

	//printf("The specie %p has %d dimensions with %d particles\n",
	//	s, s->dim, s->nparticles);

	//for(i = 0; i < s->nparticles; i++)
	printf("p\n");
	for(i = 0; i < np; i++)
	{
		p = &s->particles[i];
		//printf("%10.3e %d %10.3e %10.3e %10.3e %10.3e\n",
		//	s->t, i, p->x, p->u, p->E, p->J);
		for(j=0; j<sim->dim; j++)
			printf("%d %d %10.3e %10.3e\n", p->i, j, p->x[j], p->u[j]);
	}

//	max_x = 1 * sim->dx;
//
//	x = ((double) rand() / RAND_MAX) * max_x;
//	//x = s->nnodes * sim->dx;
//	//x = 2.0 * sim->dx;
//	u = (((double) rand() - RAND_MAX/2.0) / RAND_MAX) * 0.99 * 8 * 3e8;
//
//	/* The last particle is a test one, just to ensure proper rendering */
//	printf("%d %10.3e %10.3e\n", s->nparticles - 1, x, u);

	return 0;
}
