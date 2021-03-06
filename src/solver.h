#pragma once

struct solver;
typedef struct solver solver_t;

/* Wheter we implement LU or not (requires GSL) */
//#define WITH_LU

#include "sim.h"
#include "mat.h"
#include <gsl/gsl_linalg.h>
#include <fftw3.h>

enum solver_method {
	METHOD_LU=1,
	METHOD_MFT,
	METHOD_MFT_TAP,
};

struct solver
{
	int method;

	int dim;
	int nx, ny;
#ifdef WITH_LU
	gsl_permutation *P;
	gsl_matrix *LU;
#endif

	/* For MFT */
	mat_t *G;
	fftw_complex *g;
	fftw_plan plan;

	/* Custom data for the specific solver */
	void *data;
};

typedef struct solver solver_t;

solver_t *
solver_init(sim_t *sim);

//int
//solve(mat_t *phi, mat_t *rho);

int
solve_xy(sim_t *sim, solver_t *s, mat_t *phi, mat_t *rho);

int
solver_rho_size(sim_t *sim, int *cnx, int *cny);

int
solver_end(sim_t *sim, solver_t *solver);
