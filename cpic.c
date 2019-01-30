#include "specie.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define DEBUG 0
#define BLOCK_SIZE 10240

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#if DEBUG
#define dbg(...) fprintf(stderr, __VA_ARGS__);
#else
#define dbg(...)
#endif

#define err(...) fprintf(stderr, __VA_ARGS__);

int
field_E(specie_t *s)
{
	int i,iend,b;
	float *E = s->E->data;
	float *J = s->J->data;
	int size = s->E->size;
	float dt = s->dt;
	float e0 = s->e0;
	float coef = -dt/e0;


	//#pragma oss task in(J[0:size-1]) out(E[0:size-1]) label(field_E)
	for(b = 0; b < size; b+=BLOCK_SIZE)
	{
		iend = MIN(b+BLOCK_SIZE, size);
		i = b;
		#pragma oss task in(J[i:iend-1]) out(E[i:iend-1]) label(field_E)
		for(; i < iend; i++)
		{
			E[i] += coef * J[i];
			dbg("Current updates E[%d]=%10.3e\n", i, E[i]);
		}
	}
	//#pragma oss taskwait
}

/* The field J is updated based on the electric current computed on each
 * particle p, by using an interpolation function */
int
field_J(specie_t *s)
{
	particle_t *p;
	int i, iend, b, j0, j1;
	float *J = s->J->data;
	float w0, w1;
	int size = s->J->size;

	/* Erase previous current */
	#pragma oss task out(J[0:size-1])
	memset(J, 0, sizeof(float) * size);

	for(b = 0; b < s->nparticles; b+=BLOCK_SIZE)
	{
		iend = MIN(b+BLOCK_SIZE, s->nparticles);
		i = b;
		#pragma oss task in(s->particles[i:iend-1]) inout(J[i:iend-1])
		for(i = 0; i < iend; i++)
		{
			p = &(s->particles[i]);

			j0 = (int) floor(p->x / s->dx);
			j0 = j0 % size;
			j1 = (j0 + 1) % size;

			#pragma oss task in(p->x, p->J) inout(J[j0], J[j1])
			{
				/* As p->x approaches to j0, the weight w1 must be close to 1 */
				w1 = (p->x/s->dx) - j0;
				w0 = 1.0 - w1;

				J[j0] += w0 * p->J;
				J[j1] += w1 * p->J;
				dbg("Particle %d updates J[%d]=%10.3e\n", i, j0, J[j0]);
			}
		}
	}
}
int
particle_E(specie_t *s)
{
	int i, j0, j1, size;
	float *E = s->E->data;
	float w0, w1;
	particle_t *p;

	size = s->E->size;

	#pragma oss task inout(s->particles[0:s->nparticles-1]) in(E[0:size-1])
	for (i = 0; i < s->nparticles; i++)
	{
		p = &(s->particles[i]);

		j0 = (int) floor(p->x / s->dx);
		j0 = j0 % size;
		j1 = (j0 + 1) % size;

		//#pragma oss task in(p->x, E[j0], E[j1]) out(p->E)
		{
			/* As p->x approaches to j0, the weight w1 must be close to 1 */
			w1 = (p->x/s->dx) - j0;
			w0 = 1.0 - w1;

			p->E = w0 * E[j0] + w1 * E[j1];
			dbg("Field %d updates particle %d E=%10.3e\n", j0, i, E[j0]);
		}
	}
	return 0;
}

int
particle_u(specie_t *s)
{
	int i;
	particle_t *p;

	float dt = s->dt;
	float incr;

	#pragma oss task inout(s->particles[0:s->nparticles-1])
	for (i = 0; i < s->nparticles; i++)
	{
		p = &(s->particles[i]);
		//#pragma oss task in(p->E) out(p->u)
		{
			incr = dt * s->q * p->E / s->m;
			dbg("Particle %d increases speed by %10.3e\n", i, incr);
			p->u += incr;
		}
	}
	return 0;
}

int
particle_x(specie_t *s)
{
	int i;
	particle_t *p;

	float dt = s->dt;
	float incr;
	float max_x = s->dx * s->E->size;

	#pragma oss task inout(s->particles[0:s->nparticles-1])
	for (i = 0; i < s->nparticles; i++)
	{
		p = &(s->particles[i]);
		//#pragma oss task in(p->u) out(p->x)
		{
			incr = dt * p->u;
			if(fabs(incr) > s->dx)
			{
				err("Particle %d has exceeded dx with x+=%10.3e\n", i, incr);
				err("Please, reduce dt=%10.3e or increase dx=%10.3e\n",
						s->dt, s->dx);
				exit(1);
			}
			p->x += incr;
			if(p->x >= max_x)
				p->x -= max_x;
			else if(p->x < 0.0)
				p->x += max_x;
			if((p->x < 0.0) || (p->x >= max_x + 0.01*s->dx))
			{
				err("Particle %d is at x=%10.3e with max_x=%10.e\n",
						i, p->x, max_x);
				exit(1);
			}
		}
	}
	return 0;
}

/* At each particle p, the current J_p is computed based on the charge and speed
 */
int
particle_J(specie_t *s)
{
	int i;
	particle_t *p;

	float dt = s->dt;

	#pragma oss task inout(s->particles[0:s->nparticles-1])
	for (i = 0; i < s->nparticles; i++)
	{
		p = &(s->particles[i]);

		p->J = s->q * p->u / dt;
	}
	return 0;
}

int
main()
{
	int i, max_it = 3;
	specie_t *s;

	s = specie_init();

	//specie_print(s);

	particle_J(s);
	field_J(s);

	for(i = 0; i < max_it; i++)
	//while(1)
	{
		dbg("------ Begin iteration i=%d ------\n", i);


		/* Phase CP:FS. Field solver, calculation of the electric field
		 * from the current */

		/* Line 6: Update E on the grid, eq 5 */
		field_E(s);

		/* Phase IP:FI. Field interpolation, projection of the electric
		 * field from the grid nodes to the particle positions. */

		/* Line 7: Interpolate E on each particle, eq 8 */
		particle_E(s);

		/* Phase CP:PM. Particle mover, updating of the velocity and the
		 * position of the particles from the values of the projected
		 * electric field. */

		/* Line 8: Update the speed on each particle, eq 6 */
		particle_u(s);
		/* Line 9: Update the position on each particle, eq 7 */
		particle_x(s);

		/* Phase IP:MG. Moment gathering, assembling of the electric
		 * current from the values of the particle positions and
		 * velocities. */

		/* Line 10: Update the current field on grid, algorithm 3 */
		particle_J(s);
		field_J(s);

		/* Print the status */
		//specie_print(s);

		specie_step(s);
	}

	/* sync before leaving the program */
	#pragma oss taskwait

}
