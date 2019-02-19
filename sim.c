#include <libconfig.h>
#include "sim.h"

#define DEBUG 1
#include "log.h"
#include "specie.h"
#include "particle.h"
#include "field.h"

#include <math.h>
#include <assert.h>

#define ENERGY_CHECK 1

sim_t *
sim_init(config_t *conf)
{
	sim_t *s;
	int ns, nblocks, blocksize;
	int seed;
	double wp;
	specie_t *sp;

	s = calloc(1, sizeof(sim_t));

	s->conf = conf;

	config_lookup_int(conf, "simulation.cycles", &s->cycles);
	config_lookup_float(conf, "simulation.time_step", &s->dt);
	config_lookup_float(conf, "simulation.space_length", &s->L);
	config_lookup_int(conf, "simulation.random_seed", &seed);
	config_lookup_int(conf, "plot.energy_cycles", &s->energy_cycles);

	srand(seed);

	config_lookup_int(conf, "grid.blocks", &nblocks);
	config_lookup_int(conf, "grid.blocksize", &blocksize);

	s->dx = s->L / (nblocks * blocksize);

	s->t = 0.0;

	config_lookup_float(conf, "constants.light_speed", &s->C);
	config_lookup_float(conf, "constants.vacuum_permittivity", &s->e0);


	species_init(s, conf);

	sp = &s->species[0];

	wp = sqrt(sp->nparticles * sp->q*sp->q / s->e0 / sp->m);

	fprintf(stderr, "plasma_frequency = %e Hz (period %e iterations)\n", wp, 1/(wp*s->dt));

	fprintf(stderr, "wp * dt = %e\n", wp * s->dt);
	assert(wp * s->dt <= 0.2);
	assert(wp * s->dt >= 0.1);

	return s;
}

static int
conservation_energy(sim_t *sim, specie_t *s)
{
	int i,j,k;
	particle_t *p;
	block_t *b;

	double EE = 0.0; /* Electrostatic energy */
	double KE = 0.0; /* Kinetic energy */
	double L = sim->L;



	/* We need all previous tasks to finish before computing the energy, but
	 * the check is only needed for validation */
	#pragma oss taskwait
	for(i=0; i<s->nblocks; i++)
	{
		for(j=0; j < s->blocksize; j++)
		{
			b = &s->blocks[i];
			EE += b->field.rho->data[j] * b->field.J->data[j];
		}
	}

	for(i=0; i<s->nparticles; i++)
	{
		p = &s->particles[i];
		KE += 0.5 * s->m * p->u * p->u;
//		EE += p->E * p->E;
	}

	EE /= L*2.0;

	/* Change units to eV */
	EE /= 1.6021766208e-19;
	KE /= 1.6021766208e-19;
	printf("e %10.3e %10.3e %10.3e\n", EE+KE, EE, KE);

	return 0;
}

int
sim_header(sim_t *sim)
{
	/* FIXME: By now we only use the first configuration (only one specie)*/
	int nparticles, nblocks, blocksize, nnodes;

	config_lookup_int(sim->conf, "grid.blocks", &nblocks);
	config_lookup_int(sim->conf, "grid.blocksize", &blocksize);

	nnodes = nblocks * blocksize;
	nparticles = sim->species[0].nparticles;

	printf("p %d %d %e %e\n",
		nparticles, nnodes, sim->dx, sim->dt);

	return 0;
}

int
sim_run(sim_t *sim)
{
	int i, j;
	specie_t *s;

	/* Tell the plotting program some information about the simulation */
	sim_header(sim);

	/* Initial computation of J */
	for(j = 0; j < sim->nspecies; j++)
	{
		s = &sim->species[j];
		particle_J(sim, s);
		field_J(sim, s);
	}

	for(i = 0; i < sim->cycles; i++)
	{
		//dbg("------ Begin iteration i=%d ------\n", i);


		/* Phase CP:FS. Field solver, calculation of the electric field
		 * from the current */

		/* Line 6: Update E on the grid, eq 5 */
		field_E(sim, s);

		for(j = 0; j < sim->nspecies; j++)
		{
			s = &sim->species[j];
			/* Phase IP:FI. Field interpolation, projection of the electric
			 * field from the grid nodes to the particle positions. */

			/* Line 7: Interpolate E on each particle, eq 8 */
			particle_E(sim, s);

			/* Phase CP:PM. Particle mover, updating of the velocity and the
			 * position of the particles from the values of the projected
			 * electric field. */

			/* Line 8: Update the speed on each particle, eq 6 */
			/* Line 9: Update the position on each particle, eq 7 */
			particle_x(sim, s);

			/* Phase IP:MG. Moment gathering, assembling of the electric
			 * current from the values of the particle positions and
			 * velocities. */

			/* Line 10: Update the current field on grid, algorithm 3 */
			particle_J(sim, s);
		}

		field_J(sim, s);

		/* Print the status */
		specie_print(sim, s);

#if ENERGY_CHECK
		if((i % sim->energy_cycles) == 0)
			conservation_energy(sim, s);
#endif

		//#pragma oss taskwait
		specie_step(sim);
	}

	//printf("Loop finished\n");

	/* sync before leaving the program */
	#pragma oss taskwait

	return 0;
}
