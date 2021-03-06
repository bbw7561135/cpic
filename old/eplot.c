#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <GL/glut.h>

#include "specie.h"

GLenum doubleBuffer;
GLint windW = 500, windH = 500;

#define MAX_HIST 30
#define TAG 'e'
#define MAX_LINE 256

particle_t *particles[MAX_HIST];
int hist = 0;

int nparticles;
int shape;
float dx, dt;
int play = 1;
int clear = 0;

double maxEE = 0, maxTE = 0, maxKE = 0;
int cursor_x;
double TE0, EE0, KE0;
double TE=0, EE=0, KE=0;

static int
get_line(char *buf, size_t n)
{
	while(getline(&buf, &n, stdin))
	{
		if(buf[0] == TAG)
			return 0;

		printf("%s", buf);
	}
	return -1;
}

static void
Init(void)
{
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
		break;
	case 'q':
	case 27:
		exit(0);
	}
}

void
Idle(void)
{
	int ret;
	char buf[MAX_LINE];

//	if(!play)
//	{
//		glutPostRedisplay();
//		return;
//	}

	if(get_line(buf, MAX_LINE))
	{
		fprintf(stderr, "EOF reached\n");
		exit(0);
	}

	TE0 = TE;
	EE0 = EE;
	KE0 = KE;

	ret = sscanf(buf, "e %le %le %le",
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
	double x, yTE, yTE0, yEE, yEE0;
	double lTE = (TE);
	double lEE = (EE);
	double lKE = (KE);
	double lTE0 = (TE0);
	double lEE0 = (EE0);
	double lKE0 = (KE0);

	if(clear)
		glClear(GL_COLOR_BUFFER_BIT);

	//fprintf(stderr, "TE=%e\n", TE);
	//fprintf(stderr, "EE=%e\n", EE);

	if(lTE > maxEE) maxTE = lTE;
	if(lEE > maxEE) maxEE = lEE;
	if(lKE > maxEE) maxKE = lKE;

	if(maxTE == 0.0) maxTE = 1e-10;
	if(maxEE == 0.0) maxEE = 1e-10;
	if(maxKE == 0.0) maxKE = 1e-10;

	yTE  = lTE  / maxTE * windH;
	yTE0 = lTE0 / maxTE * windH;
	yEE  = lEE  / maxEE / 2 * windH + windH/2;
	yEE0 = lEE0 / maxEE / 2 * windH + windH/2;

	x = (double) cursor_x;

	cursor_x = (cursor_x + 1) % windW;

	//fprintf(stderr, "x=%e yTE0=%e\n", x, yTE0);
	//fprintf(stderr, "x=%e yTE=%e\n", x-1, yTE);

	glLineWidth(1.0);
	glColor3f(0.0, 0.0, 0.0);
	glBegin(GL_LINES);
	glVertex2f(x, 0);
	glVertex2f(x, windH);
	glEnd();

	glColor3f(1.0, 0.0, 0.0);
	glBegin(GL_LINES);
	glVertex2f(x-1, yTE0);
	glVertex2f(x, yTE);
	glEnd();

	glColor3f(1.0, 1.0, 0.5);
	glBegin(GL_LINES);
	glVertex2f(x-1, yEE0);
	glVertex2f(x, yEE);
	glEnd();


}

void
Display(void)
{
	plot();

	if (doubleBuffer) {
		glutSwapBuffers();
	} else {
		glFlush();
	}
}

void
Visible(int state)
{
	if (state == GLUT_VISIBLE) {
		glutIdleFunc(Idle);
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
	glutCreateWindow("plot");
	//glutCreateWindow("two streams");
	//glutFullScreen();

	Init();

	glutReshapeFunc(Reshape);
	glutKeyboardFunc(Key);
	glutVisibilityFunc(Visible);
	glutDisplayFunc(Display);
	glutMainLoop();
	return 0;             /* ANSI C requires main to return int. */
}
