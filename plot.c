#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <GL/glut.h>

#include "specie.h"

GLenum doubleBuffer = GL_FALSE;
GLint windW = 1400, windH = 400;
int win1, win2;

#define MAX_HIST 10
#define MAX_LINE 256
#define MAX_V 6e8

particle_t *particles[MAX_HIST];
int hist = 0;

int nparticles;
int shape;
double dx, dt;
int play = 1;
int clear = 0;
int plotting = 1;
int grid = 1;

double maxEE = 0, maxTE = 0, maxKE = 0;
double minEE = 0, minTE = 0, minKE = 0;
int cursor_x;
double TE0, EE0, KE0;
double TE=0, EE=0, KE=0;

#define ENERGY_HIST 1024

double EV[ENERGY_HIST];

static int
get_line(char *buf, size_t n, int prefix)
{
	while(getline(&buf, &n, stdin))
	{
		if(buf[0] == prefix)
			return 0;

		printf("%s", buf);
		fflush(stdout);
	}
	return -1;
}

static void
init_particles(void)
{
	int i;
	particle_t *p;
	char buf[MAX_LINE];

	if(get_line(buf, MAX_LINE, 'p'))
	{
		fprintf(stderr, "EOF before read config\n");
		exit(1);
	}

	sscanf(buf, "p %d %d %le %le", &nparticles, &shape, &dx, &dt);
	printf("Number of particles=%d, shape=%d\n", nparticles, shape);

	for(i = 0; i < MAX_HIST; i++)
		particles[i] = calloc(sizeof(particle_t), nparticles);

	glClearColor(0.0, 0.0, 0.0, 0.0);

	glDisable(GL_DITHER);
}

void
Reshape(int width, int height)
{
	windW = width;
	windH = height;

	glViewport(0, 0, windW, windH);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(-0.5, windW + 0.5, -0.5, windH + 0.5);
	glMatrixMode(GL_MODELVIEW);
}

/* ARGSUSED1 */
static void
Key(unsigned char key, int x, int y)
{
	switch (key) {
	case 'p':
		play = !play;
		break;
	case 'c':
		clear = !clear;
		grid = 1;
		break;
	case 'n':
		plotting = !plotting;
		fprintf(stderr, "plotting = %d\n", plotting);
		break;
	case 'q':
	case 27:
		exit(0);
	}
}

void
idle_particles(char *line)
{
	float t, x, u;
	int i, j, ret;
	particle_t *p;
	char buf[MAX_LINE];
	size_t n = MAX_LINE;

	glutSetWindow(win1);

	if(!play)
	{
		glutPostRedisplay();
		return;
	}

	hist = (hist + 1) % MAX_HIST;

	for(i = 0; i < nparticles; i++)
	{
		/* Copy old particle into particles1 */
		//memcpy(&particles1[prev_hist][i], &particles[hist][i], sizeof(particle_t));

		if(!fgets(buf, n, stdin))
		{
			fprintf(stderr, "EOF reached\n");
			exit(0);
		}

		ret = sscanf(buf, "%d %f %f",
				&j, &x, &u);

		if(ret != 3)
		{
			printf("ret=%d i=%d j=%d, exitting\n", ret, i, j);
			exit(1);
		}

		p = &particles[hist][j];
		p->x = x;
		p->u = u;
	}

	glutPostRedisplay();
}

static int
get_curve(float *xx, float *yy, int *segment, int pi)
{
	particle_t *p;
	int from_hist = (hist + 1) % MAX_HIST;
	float x0 = 0, y0 = 0, x1, y1;
	int i, si = 0, sl = 0;

	for(i = 0; i < MAX_HIST; i++, from_hist = (from_hist + 1) % MAX_HIST, sl++)
	{
		p = &particles[from_hist][pi];

		x1 = (p->x / (dx * shape)) * windW;
		y1 = (p->u / (MAX_V)) * windH;

		/* Center y, as u goes from about -c to +c */
		y1 += windH / 2.0;

		if(i > 0 && fabs(x1 - x0) > windW/4) // Wrap around the screen
		{
			/* Create another segment */
			segment[si++] = sl;
			sl = 0;
		}

//		/* For plotting purposes, we need at least one pixel drawn */
//		if(i > 0 && fabs(x0 - x1) < 1.0)
//		{
//			x1 = x0 + 1.0;
//		}

		xx[i] = x1;
		yy[i] = y1;

		x0 = x1;
		y0 = y1;
	}

	segment[si++] = sl;

	return si;
}

static void
plot_particle(int pi)
{
	float x[MAX_HIST], y[MAX_HIST];
	int segments[MAX_HIST];
	int i, j, k=0, n;
	float cm, ch, cl;
	float h = 1.0, l = 0.1;

	n = get_curve(x, y, segments, pi);


	/* Erase a bit of the tail */
	glLineWidth(5.0);
	glColor3f(0,0,0);

	for(i = 0; i < n; i++)
	{
		glBegin(GL_LINE_STRIP);
		for(j = 0; j<segments[i]; j++)
		{
			glVertex2f(x[k], y[k]);
			k++;
			if(k >= 2) break;
		}
		glEnd();
		if(k >= 2) break;
	}

	k = 0;
	glLineWidth(2.0);

	for(i = 0; i < n; i++)
	{

		glBegin(GL_LINE_STRIP);
		for(j = 0; j<segments[i]; j++)
		{
			//cm = (float) k / MAX_HIST;
			//if(cm < 0.5) cm = 0.5;
			if(k < 2)
				cm = 0.3;
			else
				cm = 1.0;

			//fprintf(stderr, "k=%d, MAX_HIST=%d\n", k, MAX_HIST);

			ch = cm * h;
			cl = cm * l;
			if(pi % 2)
				glColor3f(ch, cl, cl);
			else
				glColor3f(cl, ch, cl);
			glVertex2f(x[k], y[k]);
			glVertex2f(x[k]-1, y[k]);
			k++;
		}
		//exit(0);
		glEnd();
	}
}

static void
plot_grid()
{
	int i;
	double x;
	double gray = 0.1;

	glColor3f(gray, gray, gray);

	for(i=0; i<shape; i++)
	{
		x = ((double)i) / shape * windW;

		glBegin(GL_LINES);
		glVertex2f(x, -1);
		glVertex2f(x, windH);
		glEnd();
	}

}


static void
plot_particles()
{
	int i;

	if(clear)
		glClear(GL_COLOR_BUFFER_BIT);

	for(i=0; i<nparticles; i++)
	{
		plot_particle(i);
	}
}

void
display_particles(void)
{
	if(!plotting)
		return;

	//fprintf(stderr, "Plotting particles\n");

	if(grid)
	{
		plot_grid();
		grid = 0;
	}
	plot_particles();

	if (doubleBuffer) {
		glutSwapBuffers();
	} else {
		glFlush();
	}
}


void
idle_energy(char *line)
{
	int ret;

	glutSetWindow(win2);

	if(!play)
	{
		glutPostRedisplay();
		return;
	}

	TE0 = TE;
	EE0 = EE;
	KE0 = KE;

	ret = sscanf(line, "e %le %le %le",
			&TE, &EE, &KE);

	if(ret != 3)
	{
		printf("ret=%d, exitting\n", ret);
		exit(1);
	}

	glutPostRedisplay();
}

static void
plot()
{
	char buf[MAX_LINE];
	int i;
	double x, yTE, yTE0, yEE, yEE0, yKE, yKE0;
	double lTE = (TE);
	double lEE = (EE);
	double lKE = (KE);
	double lTE0 = (TE0);
	double lEE0 = (EE0);
	double lKE0 = (KE0);

	//fprintf(stderr, "TE=%e\n", TE);
	//fprintf(stderr, "EE=%e\n", EE);

	if(lTE > maxTE) maxTE = 1.5 * lTE;
	if(lEE > maxEE) maxEE = 1.5 * lEE;
	if(lKE > maxKE) maxKE = 1.5 * lKE;

	if(lTE < minTE) minTE = lTE;
	//if(lEE < minEE) minEE = lEE;
	//if(lKE < minKE) minKE = lKE;

	if(maxTE == 0.0) maxTE = 1e-10;
	if(maxEE == 0.0) maxEE = 1e-10;
	if(maxKE == 0.0) maxKE = 1e-10;

	maxEE = maxKE;

	yTE  = (lTE  - minTE) / (maxTE - minTE) * windH/2;
	yTE0 = (lTE0 - minTE) / (maxTE - minTE) * windH/2;
	yEE  = (lEE  - minEE) / (maxEE - minEE) * windH/2 + 1*windH/2;
	yEE0 = (lEE0 - minEE) / (maxEE - minEE) * windH/2 + 1*windH/2;
	yKE  = (lKE  - minKE) / (maxKE - minKE) * windH/2 + 1*windH/2;
	yKE0 = (lKE0 - minKE) / (maxKE - minKE) * windH/2 + 1*windH/2;

	x = (double) cursor_x;

	cursor_x = (cursor_x + 1) % windW;

	//fprintf(stderr, "x=%e yTE0=%e\n", x, yTE0);
	//fprintf(stderr, "x=%e yTE=%e\n", x-1, yTE);

	glLineWidth(1.0);

	/* Clear previous segment */
	glColor3f(0.0, 0.0, 0.0);
	glBegin(GL_LINES);
	glVertex2f(x, 0);
	glVertex2f(x, windH);
	glEnd();

	/* Draw total energy */
	glColor3f(1.0, 0.0, 0.0);
	glBegin(GL_LINES);
	glVertex2f(x-1, yTE0);
	glVertex2f(x, yTE);
	glEnd();

	/* Draw electrostatic energy */
	glColor3f(1.0, 1.0, 0.0);
	glBegin(GL_LINES);
	glVertex2f(x-1, yEE0);
	glVertex2f(x, yEE);
	glEnd();

	/* Draw kinetic energy */
	glColor3f(0.0, 1.0, 0.0);
	glBegin(GL_LINES);
	glVertex2f(x-1, yKE0);
	glVertex2f(x, yKE);
	glEnd();


}

void
display_energy(void)
{
	if(!play)
		return;
	//fprintf(stderr, "Plotting energy\n");
	plot();

	if (doubleBuffer) {
		glutSwapBuffers();
	} else {
		glFlush();
	}
}

void
idle()
{
	if(!play)
		return;

	char buf[MAX_LINE];
	size_t n = MAX_LINE;

	if(!fgets(buf, n, stdin))
		return;

	//fprintf(stderr, "Loading info\n");

	if(buf[0] == 'p')
		idle_particles(buf);
	else if(buf[0] == 'e')
		idle_energy(buf);

	if(play)
	{
		glutPostRedisplay();
	}
}

void
idle_redisplay()
{
	if(plotting)
	{
		//fprintf(stderr, "Calling redisplay for particles\n");
		glutPostRedisplay();
	}
}


void
visible_energy(int state)
{
	if (state == GLUT_VISIBLE) {
		glutIdleFunc(idle);
	} else {
		glutIdleFunc(NULL);
	}
}

void
visible_particles(int state)
{
	if (state == GLUT_VISIBLE) {
		glutIdleFunc(idle);
	} else {
		glutIdleFunc(NULL);
	}
}
int
main(int argc, char **argv)
{
	GLenum type;

	glutInitWindowSize(windW, windH);
	glutInit(&argc, argv);

	type = GLUT_RGB;
	type |= (doubleBuffer) ? GLUT_DOUBLE : GLUT_SINGLE;
	glutInitDisplayMode(type);
	win1 = glutCreateWindow("plot");
	glutPositionWindow(50, 50);

	init_particles();

	glutReshapeFunc(Reshape);
	glutKeyboardFunc(Key);
	//glutVisibilityFunc(visible_particles);
	glutIdleFunc(idle_redisplay);
	glutDisplayFunc(display_particles);

	win2 = glutCreateWindow("plot energy");
	glutPositionWindow(50, 100 + windH);
	glutReshapeFunc(Reshape);
	glutKeyboardFunc(Key);
	//glutVisibilityFunc(visible_energy);
	glutIdleFunc(idle);
	glutDisplayFunc(display_energy);


	glutMainLoop();
	return 0;             /* ANSI C requires main to return int. */
}
