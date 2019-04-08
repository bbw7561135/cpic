#pragma once

struct sim;
typedef struct sim sim_t;

#include "def.h"
#include "specie.h"
#include "field.h"
#include "solver.h"
#include <libconfig.h>
#include <pthread.h>

enum sim_mode {
	SIM_MODE_NORMAL,
	SIM_MODE_DEBUG,
};

struct sim
{
	/* Current iteration */
	int iter;

	/* Number of dimensions used */
	int dim;

	/** Time step in seconds*/
	double dt;

	/** Spacial step in meters */
	double dx[MAX_DIM];

	/** Length of the simulation in meters */
	double L[MAX_DIM];

	/** The current simulation time in seconds */
	double t;

	/** Speed of light in meters/second */
	double C;

	/** Vacuum permittivity in Farad/meter (F/m) */
	double e0;

	/** Number of simulation steps */
	int cycles;

	int period_particle;
	int period_field;
	int period_energy;

	double energy_electrostatic;
	double energy_kinetic;
	double total_momentum[MAX_DIM];


	/* For now we assume a background fixed magnetic field */
	double B[MAX_DIM];

	/* TODO: The global list of species should dissapear, as we can only
	 * hold the species inside each block */

	/** Species of particles */
	int nspecies;
	specie_t *species;

	/* Number of blocks */
	int nblocks[MAX_DIM];

	/* Shape of the block, without ghosts cells */
	int blocksize[MAX_DIM];

	/* Shape of the block, including the ghosts cells */
	int ghostsize[MAX_DIM];

	/* The number of nodes with all blocks of the specified dimension */
	int nnodes[MAX_DIM];

	/* The total number of nodes in all dimensions */
	int total_nodes;

	/** Global field: TODO: May be reused? Sync? */
	field_t *field;

	/** Simulation configuration */
	config_t *conf;

	/* The configuration file */
	char *conf_path;

	/* Simulation mode */
	enum sim_mode mode;

	/* The plotter */
	pthread_t plot_thread;

	/* Syncronization part between simulator and plotter */
	pthread_mutex_t lock;
	pthread_cond_t signal;
	int run;

	/* The solver needs some information during the simulation */
	solver_t *solver;

	/* A pointer to let the user save a reference to an external structure
	 * or any other data */
	//void *user; // Not used, yet.
};


sim_t *
sim_init(config_t *conf, int quiet);

int
sim_run(sim_t *sim);

int
sim_step(sim_t *sim);
