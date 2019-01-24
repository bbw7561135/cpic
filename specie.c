#include "specie.h"
#include "mat.h"

#include <stdlib.h>
#include <stdio.h>

specie_t *
specie_alloc(int dim, int *shape, int nparticles)
{
	specie_t *s;

	s = malloc(sizeof(specie_t));
	s->dim = dim;
	s->nparticles = nparticles;

	s->particles = malloc(nparticles * sizeof(particle_t));

	s->E = mat_init(dim, shape, 0.0);
	s->B = mat_init(dim, shape, 0.0);
	s->J = mat_init(dim, shape, 0.0);
	s->rho = mat_init(dim, shape, 0.0);

	return s;
}

int
field_init(mat_t *f)
{
	int i;
	particle_t *p;

	for(i = 0; i < f->size; i++)
	{
		((float *) f->data)[i] = 0.0;
	}

	return 0;
}

int
particles_init(specie_t *s)
{
	int i;
	particle_t *p;

	for(i = 0; i < s->nparticles; i++)
	{
		p = &s->particles[i];
		//p->x = ((float) i / (float) s->nparticles) * s->E->size * s->dx;
		p->x = ((float) rand() / RAND_MAX) * s->E->size * s->dx;
		p->u = ((i % 2) - 0.5) * s->C; /* m/s */
		p->E = 0.0;
		p->J = 0.0;
	}
}

specie_t *
specie_init()
{
	specie_t *s;
	int dim = 1;
	int shape[] = {10};
	int nfields = 1;
	int nparticles = 25;

	s = specie_alloc(dim, shape, nparticles);

	s->C = 2.99792458e+8;
	s->dt = 1.0e-7;
	s->dx = 1.0e+5;
	s->q = -1.60217662e-19; /* The charge of an electron in coulombs */
	s->m = 9.10938356e-31; /* The electron mass */
	s->e0 = 8.85e-12; /* Vacuum permittivity */

	particles_init(s);

	return s;
}

int
specie_print(specie_t *s)
{
	int i;
	particle_t *p;

	printf("The specie %p has %d dimensions with %d particles\n",
		s, s->dim, s->nparticles);

	for(i = 0; i < s->nparticles; i++)
	{
		p = &s->particles[i];
		printf("Particle %3d: x=%10.3e  u=%10.3e  E=%10.3e  J=%10.3e\n",
			i, p->x, p->u, p->E, p->J);
	}

	return 0;
}
