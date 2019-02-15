#include "field.h"
#include "log.h"

#include <string.h>
#include <assert.h>
#include <math.h>


/* The field J is updated based on the electric current computed on each
 * particle p, by using an interpolation function */
static int
block_J_update(sim_t *sim, specie_t *s, block_t *b)
{
	particle_t *p;
	int i, j0, j1;
	double *J = b->field.J->data;
	double *rho = b->field.rho->data;
	double w0, w1, px, deltax, deltaxj;
	int size = b->field.J->size;
	double dx = sim->dx;
	double x0 = b->x;
	double x1 = b->x + dx*size;
	double xhalf = (x0 + x1) / 2.0;

	/* Erase previous current */
	memset(J, 0, sizeof(double) * size);
	b->field.rJ = 0.0;

	memset(rho, 0, sizeof(double) * size);
	b->field.rrho = 0.0;

	dbg("Block %d boundary [%e, %e]\n", b->i, x0, x1);

	for(p = b->particles; p; p = p->next)
	{
		/* The particle position */
		px = p->x;
		deltax = px - x0;
		assert(deltax >= 0.0);

		/* Ensure the particle is in the block boundary */
		if(!(x0 <= px && px <= x1))
		{
			dbg("Particle %d at x=%e (deltax=%e) is outside block boundary [%e, %e]\n",
				p->i, px, deltax, x0, x1);
			exit(1);
		}

		j0 = (int) floor(deltax / dx);
		deltaxj = deltax - j0 * dx;

		assert(j0 >= 0);
		assert(j0 < size);

		/* As p->x approaches to j0, the weight w0 must be close to 1 */
		w1 = deltaxj / dx;
		w0 = 1.0 - w1;

		/* Last node updates the ghost */
		if(px >= x1 - dx)
		{
			J[j0] += w0 * p->J;
			b->field.rJ += w1 * p->J;

			/* Approximate the charge by a triangle */
			rho[j0] += w0 * s->q;
			b->field.rrho += w1 * s->q;

			dbg("Particle %d at x=%e (deltax=%e) updates J[%d] and rJ\n",
				p->i, px, deltax, j0);
		}
		else
		{
			j1 = (j0 + 1) % size;
			assert(j1 < size);
			J[j0] += w0 * p->J;
			J[j1] += w1 * p->J;

			/* Approximate the charge by a triangle */
			rho[j0] += w0 * s->q;
			rho[j1] += w1 * s->q;

			dbg("Particle %d at x=%e (deltax=%e) updates J[%d] and J[%d]\n",
				p->i, px, deltax, j0, j1);
		}
	}
	return 0;
}

/* The ghost node of J (from->rB) is added in to->J[0] */
static int
block_J_comm(block_t *dst, block_t *left)
{
	dst->field.J->data[0] += left->field.rJ;
	dst->field.rho->data[0] += left->field.rrho;
	/* from->rJ cannot be used */
	/*left->rJ = 0.0;*/
	return 0;
}

/* The field J is updated based on the electric current computed on each
 * particle p, by using an interpolation function */
int
field_J(sim_t *sim, specie_t *s)
{
	int i, li;
	block_t *b, *lb;

	/* Computation */
	for (i = 0; i < s->nblocks; i++)
	{
		b = &(s->blocks[i]);

		block_J_update(sim, s, b);
	}

	#pragma oss task inout(sim->blocks[0, sim->nblocks-1]) label(field_blocks_J_update)
	/* Communication */
	for (i = 0; i < s->nblocks; i++)
	{
		li = (s->nblocks + i - 1) % s->nblocks;

		b = &(s->blocks[i]);
		lb = &(s->blocks[li]);

		block_J_comm(b, lb);
	}
	return 0;
}

#pragma oss task inout(*b) label(field_block_E_update)
static int
block_E_update(sim_t *sim, specie_t *s, block_t *b)
{
	particle_t *p;
	int i, size = b->field.E->size;
	double *E = b->field.E->data;
	double *J = b->field.J->data;
	double coef = - sim->dt / sim->e0;

	for(i=0; i < size; i++)
	{
		E[i] += coef * J[i];
		dbg("Block %d current updates E[%d]=%10.3e\n", b->i, i, E[i]);
	}

	return 0;
}

#pragma oss task inout(*b) in(*rb) label(field_block_E_comm)
/* We need to get the field from the neighbour at E[0] */
static int
block_E_comm(block_t *dst, block_t *right)
{
	dst->field.rE = right->field.E->data[0];
	return 0;
}


int
field_E(sim_t *sim, specie_t *s)
{
	int i, ri;
	block_t *b, *rb;

	for (i = 0; i < s->nblocks; i++)
	{
		b = &(s->blocks[i]);

		{
			//usleep(100);
			block_E_update(sim, s, b);
		}
	}

	/* Communication */
	for (i = 0; i < s->nblocks; i++)
	{
		ri = (i + 1) % s->nblocks;

		b = &(s->blocks[i]);
		rb = &(s->blocks[ri]);

		{
			//usleep(100);
			block_E_comm(b, rb);
		}
	}
	return 0;
}
