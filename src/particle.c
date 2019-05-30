#include "particle.h"
#include "sim.h"
#include "config.h"
#include "interpolate.h"
#include "comm.h"

#define DEBUG 1
#include "log.h"
#include "utils.h"
#include <math.h>
#include <assert.h>
#include <utlist.h>
#include <string.h>

int
init_default(sim_t *sim, plasma_chunk_t *chunk, particle_set_t *set);

int
init_randpos(sim_t *sim, plasma_chunk_t *chunk, particle_set_t *set);

int
init_position_delta(sim_t *sim, plasma_chunk_t *chunk, particle_set_t *set);

particle_config_t pc[] =
{
	{"random position",		init_randpos},
	{"position delta",		init_position_delta},
	{"default",			init_default},
	{NULL, NULL}
};

particle_t *
particle_init()
{
	particle_t *p;

	p = safe_malloc(sizeof(*p));

	/* As we send the particle via MPI_Send directly, some wholes don't get
	 * initialized, thus we use memset meanwhile */

	/* TODO: Use a packed version of particle_t for MPI */
	memset(p, 0, sizeof(*p));

	p->next = NULL;
	p->prev = NULL;

	return p;
}

int
particles_init(sim_t *sim, plasma_chunk_t *chunk, particle_set_t *set)
{
	int i;
	const char *method;
	config_setting_t *cs;
	specie_t *s;

	s = set->info;
	cs = s->conf;

	if(config_setting_lookup_string(cs, "init_method", &method) != CONFIG_TRUE)
	{
		err("WARNING: Particle init method for specie \"%s\" not specified. Using \"default\".\n",
				s->name);
		method = "default";
	}

	for(i = 0; pc[i].name; i++)
	{
		if(strcmp(pc[i].name, method) != 0)
			continue;

		if(!pc[i].init)
		{
			err("The init method is NULL, aborting.\n");
			exit(1);
		}

		return pc[i].init(sim, chunk, set);
	}

	err("Unknown init method \"%s\", aborting.\n", method);
	exit(1);

	return 0;
}

int
init_default(sim_t *sim, plasma_chunk_t *chunk, particle_set_t *set)
{
	return init_randpos(sim, chunk, set);
}

double
uniform(double a, double b)
{
	return rand() / (RAND_MAX + 1.0) * (b - a) + a;
}


int
init_randpos(sim_t *sim, plasma_chunk_t *chunk, particle_set_t *set)
{
	particle_t *p;
	double v[MAX_DIM];
	config_setting_t *cs_v;

	/* FIXME: Use specific random velocity inerval name */
	cs_v = config_setting_get_member(set->info->conf, "drift_velocity");
	if(config_array_float(cs_v, v, sim->dim))
		return 1;

	for(p = set->particles; p; p = p->next)
	{
		p->x[X] = uniform(0.0, sim->L[X]);
		p->x[Y] = uniform(0.0, sim->L[Y]);
		p->x[Z] = 0.0;

		/* FIXME: Separate position and velocity init metods */
		p->u[X] = uniform(-v[X], v[X]);
		p->u[Y] = uniform(-v[Y], v[Y]);
		p->u[Z] = 0.0;

		p->E[X] = 0.0;
		p->E[Y] = 0.0;
		p->E[Z] = 0.0;

		if(p->i < 100)
			dbg("Particle %d randpos init at (%e, %e) in chunk (%d, %d)\n",
				p->i, p->x[X], p->x[Y], chunk->ig[X], chunk->ig[Y]);

		//if(p->x[Y] > b->x1[Y] || p->x[Y] < b->x0[Y])
		//	err("WARN: Particle %d exceeds block boundary in Y\n", p->i);
	}

	return 0;
}

int
init_position_delta(sim_t *sim, plasma_chunk_t *chunk, particle_set_t *set)
{
	int d;
	particle_t *p;
	double r[MAX_DIM] = {0};
	double dr[MAX_DIM] = {0};
	double *L;
	double v[MAX_DIM] = {0};
	config_setting_t *cs, *cs_v, *cs_r, *cs_dr;

	cs = set->info->conf;
	L = sim->L;

	cs_v = config_setting_get_member(cs, "drift_velocity");
	if(config_array_float(cs_v, v, sim->dim))
		return 1;

	cs_dr = config_setting_get_member(cs, "position_delta");
	if(config_array_float(cs_dr, dr, sim->dim))
		return 1;

	cs_r = config_setting_get_member(cs, "position_init");
	if(config_array_float(cs_r, r, sim->dim))
		return 1;

	dbg("Init position and speed in %d particles\n", set->nparticles);

	for(p = set->particles; p; p = p->next)
	{
		for(d=0; d<sim->dim; d++)
		{
			p->u[d] = v[d];

			WRAP(p->x[d], r[d] + dr[d] * p->i, L[d]);
			p->E[d] = 0.0;
		}

		if(p->i < 100)
			dbg("Particle %d offset init at (%e, %e) in chunk (%d, %d)\n",
				p->i, p->x[X], p->x[Y], chunk->ig[X], chunk->ig[Y]);
	}

	return 0;
}


static inline void
cross_product(double *r, double *a, double *b)
{
	assert(r != a && r != b);
	r[X] = a[Y]*b[Z] - a[Z]*b[Y];
	r[Y] = a[Z]*b[X] - a[X]*b[Z];
	r[Z] = a[X]*b[Y] - a[Y]*b[X];
}

static inline void
boris_rotation(double q, double m, double *u, double *v, double *E, double *B, double dt)
{
	/* Straight forward from Birdsall section 4-3 and 4-4 */
	int d;
	double v_prime[MAX_DIM];
	double v_minus[MAX_DIM];
	double v_plus[MAX_DIM];
	double t[MAX_DIM];
	double s[MAX_DIM], s_denom;
	double dtqm2;

	dtqm2 = 0.5 * dt * q / m;
	s_denom = 1.0;

	for(d=X; d<MAX_DIM; d++)
	{
		t[d] = B[d] * dtqm2;
		s_denom += t[d] * t[d];

		/* Advance the velocity half an electric impulse */
		v_minus[d] = u[d] + dtqm2 * E[d];

		s[d] = 2.0 * t[d] / s_denom;
	}

	/* Compute half the rotation in v' */
	cross_product(v_prime, v_minus, t);

	for(d=X; d<MAX_DIM; d++)
		v_prime[d] += v_minus[d];

	cross_product(v_plus, v_prime, s);

	for(d=X; d<MAX_DIM; d++)
	{
		/* Then finish the rotation by symmetry */
		v_plus[d] += v_minus[d];

		/* Advance the velocity the other half electric impulse */
		v[d] = v_plus[d] + dtqm2 * E[d];
	}

}

void
wrap_particle_position(sim_t *sim, particle_t *p)
{
	if(p->i < 100)
		dbg("Particle %d is at (%.10e, %.10e) before wrap\n",
				p->i, p->x[X], p->x[Y]);

	if(sim->dim >= 1)
	{
		while(p->x[X] >= sim->L[X])
			p->x[X] -= sim->L[X];

		while(p->x[X] < 0.0)
			p->x[X] += sim->L[X];
	}

	if(sim->dim >= 2)
	{
		while(p->x[Y] >= sim->L[Y])
			p->x[Y] -= sim->L[Y];

		while(p->x[Y] < 0.0)
			p->x[Y] += sim->L[Y];
	}

	if(p->i < 100)
		dbg("Particle %d is now at (%.10e, %.10e)\n",
				p->i, p->x[X], p->x[Y]);

	/* Notice that we allow p->x to be equal to L, as when the position is
	 * wrapped from x<0 but -1e-17 < x, the wrap sets x equal to L, as with
	 * bigger numbers the error increases, and the round off may set x to
	 * exactly L */
	if(sim->dim >= 1)
	{
		assert(p->x[X] <= sim->L[X]);
		assert(p->x[X] >= 0.0);
	}
	if(sim->dim >= 2)
	{
		assert(p->x[Y] <= sim->L[Y]);
		assert(p->x[Y] >= 0.0);
	}
}

int
particle_set_E(sim_t *sim, plasma_chunk_t *chunk, int i)
{
	field_t *f;
	particle_set_t *set;
	particle_t *p;

	f = &sim->field;
	set = &chunk->species[i];

	for(p=set->particles; p; p=p->next)
	{
		p->E[X] = 0.0;
		p->E[Y] = 0.0;
		if(p->i < 100)
			dbg("p-%d old E=(%f %f)\n", p->i, p->E[X], p->E[Y]);
		interpolate_field_to_particle_xy(sim, p, &p->E[X], f->E[X]);
		interpolate_field_to_particle_xy(sim, p, &p->E[Y], f->E[Y]);
		if(p->i < 100)
			dbg("p-%d new E=(%f %f)\n", p->i, p->E[X], p->E[Y]);
	}
	return 0;
}

int
chunk_E(sim_t *sim, int i)
{
	plasma_chunk_t *chunk;

	chunk = &sim->plasma.chunks[i];

	for(i=0; i<chunk->nspecies; i++)
	{
		particle_set_E(sim, chunk, i);
	}

	return 0;
}

int
particle_E(sim_t *sim)
{
	int i;

	perf_start(sim->perf, TIMER_PARTICLE_E);

	/* Computation */
	for(i=0; i<sim->plasma.nchunks; i++)
	{
		chunk_E(sim, i);
	}

	/* No communication required, as only p->E is updated */

	perf_stop(sim->perf, TIMER_PARTICLE_E);

	return 0;
}


/* Communicate particles out of their block to the correct one */
int
particle_comm(sim_t *sim)
{
	int i;
	plasma_t *plasma;

	plasma = &sim->plasma;

	/* Communication */
	for (i = 0; i < plasma->nchunks; i++)
	{
		comm_plasma_chunk(sim, i, 0);
	}

	return 0;
}

int
particle_comm_initial(sim_t *sim)
{
	int i;
	plasma_t *plasma;
	plasma = &sim->plasma;

	/* Communication */
	for (i = 0; i < plasma->nchunks; i++)
	{
		comm_plasma_chunk(sim, i, 1);
	}

	return 0;
}

/* The speed u and position x of the particles are computed in a single phase */
static int
particle_x_update(sim_t *sim, plasma_chunk_t *chunk, int i)
{
	particle_set_t *set;
	particle_t *p;
	specie_t *s;
	double *E, *B, u[MAX_DIM], dx[MAX_DIM];
#if 0
	double uu, vv;
#endif
	double v[MAX_DIM] = {0};
	double dt = sim->dt;
	double q, m;

	set = &chunk->species[i];
	s = set->info;

	q = s->q;
	m = s->m;
	B = sim->B;

	for (p = set->particles; p; p = p->next)
	{
		u[X] = p->u[X];
		u[Y] = p->u[Y];
		u[Z] = p->u[Z];
		E = p->E;

		E[X] = 0.0;
		E[Y] = 0.0;
		E[Z] = 0.0;

		if(sim->iter == 0)
		{

			/* TODO: Improve the rotation to avoid the if. Also set
			 * the time sim->t properly. */

			boris_rotation(q, m, u, v, E, B, -dt/2.0);
			if(p->i < 100)
				dbg("Backward move: At t=%e u=(%.3e,%.3e) to t=%e u=(%.3e,%.3e)\n",
						sim->t, u[X], u[Y],
						sim->t-dt/2.0, v[X], v[Y]);
			p->u[X] = v[X];
			p->u[Y] = v[Y];
			continue;
		}

		boris_rotation(q, m, u, v, E, B, dt);

		if(p->i < 100)
			dbg("Particle %d at x=(%.3e,%.3e) increases speed by (%.3e,%.3e)\n",
					p->i, p->x[X], p->x[Y], v[X] - u[X], v[Y] - u[Y]);

		/* We advance the kinetic energy here, as we know the old
		 * velocity at t - dt/2 and the new one at t + dt/2. So we take
		 * the average, to estimate v(t) */


#if 0
		uu = sqrt(v[X]*v[X] + v[Y]*v[Y]);
		vv = sqrt(u[X]*u[X] + u[Y]*u[Y]);

		sim->energy_kinetic += 0.5 * (uu+vv) * (uu+vv);
		sim->total_momentum[X] += v[X];
		sim->total_momentum[Y] += v[Y];
#endif

		p->u[X] = v[X];
		p->u[Y] = v[Y];

		/* Notice we advance the position x by the new velocity just
		 * computed, following the leapfrog integrator */

		dx[X] = dt * v[X];
		dx[Y] = dt * v[Y];

		if(fabs(dx[X]) > sim->L[X])
		{
			err("Particle %d at x=(%.3e,%.3e) has exceeded L[X]=%.3e with dx[X]=%.3e\n",
					p->i, p->x[X], p->x[Y], sim->L[X], dx[X]);
			err("Please, reduce dt=%.3e or increase L\n",
					sim->dt);
			exit(1);
		}

		if(fabs(dx[Y]) > chunk->L[Y])
		{
			err("Particle %d at x=(%.3e,%.3e) has exceeded chunk L[Y]=%.3e with dx[Y]=%.3e\n",
					p->i, p->x[X], p->x[Y], chunk->L[Y], dx[Y]);
			err("Please, reduce dt=%.3e or increase L\n",
					sim->dt);
			exit(1);
		}

		if(fabs(dx[X]) > sim->dx[X] || fabs(dx[Y]) > sim->dx[Y])
		{
			err("Particle %d at x=(%.3e,%.3e)+(%.3e,%.3e) has exceeded sim dx=(%.3e,%.3e)\n",
					p->i, p->x[X], p->x[Y],
					dx[X], dx[Y],
					sim->dx[X], sim->dx[Y]);
			err("Please, reduce dt=%.3e or increase L\n",
					sim->dt);
			exit(1);
		}


		p->x[X] += dx[X];
		p->x[Y] += dx[Y];

		if(p->i < 100)
			dbg("Particle %d moved to x=(%.3e,%.3e)\n",
					p->i, p->x[X], p->x[Y]);

		/* Wrapping is done after the particle is moved to the right
		 * block */

	}

	return 0;
}

int
chunk_x_update(sim_t *sim, int i)
{
	plasma_chunk_t *chunk;

	chunk = &sim->plasma.chunks[i];

	for(i=0; i<chunk->nspecies; i++)
	{
		particle_x_update(sim, chunk, i);
	}

	return 0;
}

int
plasma_x(sim_t *sim)
{
	int i;

	perf_start(sim->perf, TIMER_PARTICLE_X);

	/* Computation */
	for(i=0; i<sim->plasma.nchunks; i++)
	{
		chunk_x_update(sim, i);
	}

	particle_comm(sim);

	perf_stop(sim->perf, TIMER_PARTICLE_X);

	return 0;
}
