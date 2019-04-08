#include <libconfig.h>
#include "sim.h"

#define DEBUG 0
#include "log.h"
#include "specie.h"
#include "particle.h"
#include "field.h"
#include "config.h"
#include "plot.h"
#include "solver.h"

#include <math.h>
#include <assert.h>

#define ENERGY_CHECK 1


sim_t *
sim_init(config_t *conf, int quiet)
{
	sim_t *s;
	int i, mode;
	int seed;
	double wp, fp, n, q, e0, m, Tp;
	specie_t *sp;

	s = calloc(1, sizeof(sim_t));

	s->conf = conf;

	/* First set all direct configuration variables */
	config_lookup_int(conf, "simulation.dimensions", &s->dim);
	config_lookup_int(conf, "simulation.cycles", &s->cycles);
	config_lookup_float(conf, "simulation.time_step", &s->dt);
	config_lookup_int(conf, "simulation.random_seed", &seed);
	config_lookup_float(conf, "constants.light_speed", &s->C);
	config_lookup_float(conf, "constants.vacuum_permittivity", &s->e0);
	config_lookup_int(conf, "simulation.sampling_period.energy", &s->period_energy);
	config_lookup_int(conf, "simulation.sampling_period.field", &s->period_field);
	config_lookup_int(conf, "simulation.sampling_period.particle", &s->period_particle);
	config_lookup_int(conf, "simulation.realtime_plot", &mode);

	/* Load all dimension related vectors */
	config_lookup_array_float(conf, "simulation.space_length", s->L, s->dim);
	/* Note that we always need the 3 dimensions for the magnetic field, as
	 * for example in 2D, the Z is the one used */
	config_lookup_array_float(conf, "field.magnetic", s->B, MAX_DIM);
	config_lookup_array_int(conf, "grid.blocks", s->nblocks, s->dim);
	config_lookup_array_int(conf, "grid.blocksize", s->blocksize, s->dim);

	/* Then compute the rest */
	if(mode == 0 || quiet)
		s->mode = SIM_MODE_NORMAL;
	else
		s->mode = SIM_MODE_DEBUG;

	/* Begin the simulation rather than the plotting */
	s->run = 1;

	srand(seed);
	s->total_nodes = 0;
	for(i=0; i<s->dim; i++)
	{
		s->nnodes[i] = s->nblocks[i] * s->blocksize[i];
		s->dx[i] = s->L[i] / s->nnodes[i];
		s->total_nodes += s->nnodes[i];
		s->ghostsize[i] = s->blocksize[i] + 1;
	}
	for(i=s->dim; i<MAX_DIM; i++)
	{
		s->nnodes[i] = 1;
		s->dx[i] = 0.0;
		s->ghostsize[i] = 1;
	}
	s->t = 0.0;

	/* And finally, call all other initialization methods */
	s->field = field_init(s);

	if(species_init(s, conf))
		return NULL;

	if((s->solver = solver_init(s)) == NULL)
	{
		err("solver_init failed\n");
		return NULL;
	}

	/* We are set now, start the plotter if needed */
	if(s->mode == SIM_MODE_DEBUG)
	{
		pthread_cond_init(&s->signal, NULL);
		pthread_mutex_init(&s->lock, NULL);
		plot_thread_init(s);
	}


	/* Testing */
	sp = &s->species[0];

	//n = sp->nparticles / s->L;
	n = 1.0;
	q = sp->q;
	e0 = s->e0;
	m = sp->m;
	wp = sqrt(n * q * q / (e0 * m));
	fp = wp / (2*M_PI);
	Tp = 1/fp;

	//fprintf(stderr, "omega_p = %e rad/s, f_p = %e, tau_p = %e (%e iterations)\n",
	//		wp, fp, Tp, Tp / s->dt);

	//fprintf(stderr, "wp * dt = %e (should be between 0.1 and 0.2)\n", wp * s->dt);
	//assert(wp * s->dt <= 0.2);
	//assert(wp * s->dt >= 0.1);


	/* Initial computation of J */
	for(i = 0; i < s->nspecies; i++)
	{
		sp = &s->species[i];
		particle_J(s, sp);
		field_J(s, sp);
	}

	return s;
}

static int
conservation_energy(sim_t *sim, specie_t *s)
{
	double EE = 0.0; /* Electrostatic energy */
	double KE = 0.0; /* Kinetic energy */
#if 0
	int i, nn, np;
	particle_t *p;
	block_t *b;

	double E,L;
	//double L = sim->L;
	double H = sim->dx[0];

	double *phi, *rho;

	rho = sim->field->rho->data;
	phi = sim->field->phi->data;
	nn = sim->nnodes[0];
	np = s->nparticles;
	L = sim->L[0];


	/* We need all previous tasks to finish before computing the energy, but
	 * the check is only needed for validation */
//	#pragma oss taskwait
//	for(i=0; i<s->nblocks; i++)
//	{
//		for(j=0; j < s->blocksize; j++)
//		{
//			b = &s->blocks[i];
//			//EE += b->field.rho->data[j] * b->field.J->data[j];
//			//EE += b->field.E->data[j] * b->field.E->data[j];
//			E = b->field.E->data[j];
//
//			//EE += E * E * H / (8.0 * M_PI);
//			EE += E * E;
//		}
//	}

	/* From Hockney book */
	for(i=0; i<nn; i++)
	{
		EE += phi[i] * rho[i];
	}

	EE *= -16 * (2 / L) / (0.25*0.25);

	for(i=0; i<s->nparticles; i++)
	{
		p = &s->particles[i];
		KE += s->m * p->u[0] * p->u[0];
	}

	//EE *= H/(8 * M_PI);
//	KE *= H/8;
	KE *= 8;

	/* Change units to eV */
	//EE /= 1.6021766208e-19;
	//KE /= 1.6021766208e-19; /* ??? */

#endif


	/* Factor correction */
	sim->energy_kinetic *= s->m / 2.0;
	sim->total_momentum[X] *= s->m * 5.0;
	sim->total_momentum[Y] *= s->m * 5.0;



	EE = sim->energy_electrostatic;
	KE = sim->energy_kinetic;

	printf("e %10.3e %10.3e %10.3e %10.3e %10.3e\n", EE+KE, EE, KE,
			sim->total_momentum[X], sim->total_momentum[Y]);

	/* As we add the kinetic energy of the particles in each block, we erase
	 * here the previous energy */
	sim->energy_kinetic = 0.0;

	sim->total_momentum[X] = 0.0;
	sim->total_momentum[Y] = 0.0;

	return 0;
}

void
test_radius(sim_t *sim)
{
	/* FIXME: This is bad */
	static double minx=0.0, maxx=0.0;
	static int dir = 0, old_dir = 0;;

	double r, rexp, qabs, m, v;
	particle_t *p;

	if(minx == 0.0) minx = sim->L[X];

	p = &sim->species[0].particles[0];

	if (p->x[X] < minx) minx = p->x[X];
	if (p->x[X] > maxx) maxx = p->x[X];

	if (p->u[X] < 0.0) dir = -1;
	else dir = +1;

	if(dir != old_dir)
	{
		old_dir = dir;

		qabs = fabs(sim->species[0].q);
		m = fabs(sim->species[0].m);
		v = sqrt(p->u[X]*p->u[X] + p->u[Y]*p->u[Y]);

		r = (maxx - minx) / 2.0;
		rexp = m * v / (qabs * fabs(sim->B[Z]));

		if(dir == +1)
		{
			printf("Pos = (%10.3e, %10.3e), Velocity %10.3e\n", p->x[X], p->x[Y], v);
			printf("Larmor radius %10.3e, expected %10.3e\n", r, rexp);
		}

		minx = p->x[X];
		maxx = p->x[X];

	}

}

int
sim_header(sim_t *sim)
{
	/* FIXME: By now we only use the first configuration (only one specie)*/
	int trackp, nparticles;

	config_lookup_int(sim->conf, "plot.track_particles", &trackp);

	nparticles = sim->species[0].nparticles;

	if(trackp == 0 || nparticles < trackp)
		trackp = nparticles;

	printf("p %d %d %e %e\n",
		trackp, sim->nnodes[0], sim->dx[0], sim->dt);

	return 0;
}

int
sim_plot(sim_t *sim)
{
	pthread_mutex_lock(&sim->lock);
	sim->run = 0;

	pthread_cond_signal(&sim->signal);

	while(sim->run == 0)
		pthread_cond_wait(&sim->signal, &sim->lock);

	pthread_mutex_unlock(&sim->lock);
	return 0;
}

int
sim_step(sim_t *sim)
{
	int j;
	specie_t *s;

	if(sim->iter >= sim->cycles)
		return -1;

	/* Phase CP:FS. Field solver, calculation of the electric field
	 * from the current */

	/* Line 6: Update E on the grid, eq 5 */
	field_E(sim);


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

		/* Interpolate the current and density of charge of each
		 * specie to the field */
		field_J(sim, s);
	}


	/* Print the status */
	//if(sim->period_particle && ((sim->iter % sim->period_particle) == 0))
	//	specie_print(sim, s);

	if(sim->mode == SIM_MODE_DEBUG)
		sim_plot(sim);

#if ENERGY_CHECK
	if(sim->period_energy && ((sim->iter % sim->period_energy) == 0))
		conservation_energy(sim, s);
#endif
	//test_radius(sim);

	sim->iter += 1;
	sim->t = sim->iter * sim->dt;

	return 0;
}

int
sim_run(sim_t *sim)
{
	while(sim->iter < sim->cycles)
		sim_step(sim);

	return 0;
}