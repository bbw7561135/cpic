
/* Copyright (c) Mark J. Kilgard, 1994. */

/**
 * (c) Copyright 1993, Silicon Graphics, Inc.
 * ALL RIGHTS RESERVED 
 * Permission to use, copy, modify, and distribute this software for 
 * any purpose and without fee is hereby granted, provided that the above
 * copyright notice appear in all copies and that both the copyright notice
 * and this permission notice appear in supporting documentation, and that 
 * the name of Silicon Graphics, Inc. not be used in advertising
 * or publicity pertaining to distribution of the software without specific,
 * written prior permission. 
 *
 * THE MATERIAL EMBODIED ON THIS SOFTWARE IS PROVIDED TO YOU "AS-IS"
 * AND WITHOUT WARRANTY OF ANY KIND, EXPRESS, IMPLIED OR OTHERWISE,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY OR
 * FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL SILICON
 * GRAPHICS, INC.  BE LIABLE TO YOU OR ANYONE ELSE FOR ANY DIRECT,
 * SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY
 * KIND, OR ANY DAMAGES WHATSOEVER, INCLUDING WITHOUT LIMITATION,
 * LOSS OF PROFIT, LOSS OF USE, SAVINGS OR REVENUE, OR THE CLAIMS OF
 * THIRD PARTIES, WHETHER OR NOT SILICON GRAPHICS, INC.  HAS BEEN
 * ADVISED OF THE POSSIBILITY OF SUCH LOSS, HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE
 * POSSESSION, USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 * US Government Users Restricted Rights 
 * Use, duplication, or disclosure by the Government is subject to
 * restrictions set forth in FAR 52.227.19(c)(2) or subparagraph
 * (c)(1)(ii) of the Rights in Technical Data and Computer Software
 * clause at DFARS 252.227-7013 and/or in similar or successor
 * clauses in the FAR or the DOD or NASA FAR Supplement.
 * Unpublished-- rights reserved under the copyright laws of the
 * United States.  Contractor/manufacturer is Silicon Graphics,
 * Inc., 2011 N.  Shoreline Blvd., Mountain View, CA 94039-7311.
 *
 * OpenGL(TM) is a trademark of Silicon Graphics, Inc.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <GL/glut.h>

#include "specie.h"

enum {
	NORMAL = 0,
	WEIRD = 1
};

enum {
	STREAK = 0,
	CIRCLE = 1
};

#define MAXSTARS 4000
#define MAXPOS 10000
#define MAXWARP 10
#define MAXANGLES 6000

typedef struct _starRec {
	GLint type;
	float x[2], y[2], z[2];
	float offsetX, offsetY, offsetR, rotation;
} starRec;

GLenum doubleBuffer;
GLint windW = 300, windH = 300;

GLenum flag = NORMAL;
GLint starCount = MAXSTARS / 2;
float speed = 1.0;
GLint nitro = 0;
starRec stars[MAXSTARS];
float sinTable[MAXANGLES];

particle_t *particles;
int nparticles;
int shape;
float dx, dt;

float
Sin(float angle)
{
	return (sinTable[(GLint) angle]);
}

float
Cos(float angle)
{
	return (sinTable[((GLint) angle + (MAXANGLES / 4)) % MAXANGLES]);
}

void
NewStar(GLint n, GLint d)
{
	if (rand() % 4 == 0) {
		stars[n].type = CIRCLE;
	} else {
		stars[n].type = STREAK;
	}
	stars[n].x[0] = (float) (rand() % MAXPOS - MAXPOS / 2);
	stars[n].y[0] = (float) (rand() % MAXPOS - MAXPOS / 2);
	stars[n].z[0] = (float) (rand() % MAXPOS + d);
	stars[n].x[1] = stars[n].x[0];
	stars[n].y[1] = stars[n].y[0];
	stars[n].z[1] = stars[n].z[0];
	if (rand() % 4 == 0 && flag == WEIRD) {
		stars[n].offsetX = (float) (rand() % 100 - 100 / 2);
		stars[n].offsetY = (float) (rand() % 100 - 100 / 2);
		stars[n].offsetR = (float) (rand() % 25 - 25 / 2);
	} else {
		stars[n].offsetX = 0.0;
		stars[n].offsetY = 0.0;
		stars[n].offsetR = 0.0;
	}
}

void
RotatePoint(float *x, float *y, float rotation)
{
	float tmpX, tmpY;

	tmpX = *x * Cos(rotation) - *y * Sin(rotation);
	tmpY = *y * Cos(rotation) + *x * Sin(rotation);
	*x = tmpX;
	*y = tmpY;
}

void
MoveStars(void)
{
	float offset;
	GLint n;

	offset = speed * 60.0;

	for (n = 0; n < starCount; n++) {
		stars[n].x[1] = stars[n].x[0];
		stars[n].y[1] = stars[n].y[0];
		stars[n].z[1] = stars[n].z[0];
		stars[n].x[0] += stars[n].offsetX;
		stars[n].y[0] += stars[n].offsetY;
		stars[n].z[0] -= offset;
		stars[n].rotation += stars[n].offsetR;
		if (stars[n].rotation > MAXANGLES) {
			stars[n].rotation = 0.0;
		}
	}
}

GLenum
StarPoint(GLint n)
{
	float x0, y0;

	x0 = stars[n].x[0] * windW / stars[n].z[0];
	y0 = stars[n].y[0] * windH / stars[n].z[0];
	RotatePoint(&x0, &y0, stars[n].rotation);
	x0 += windW / 2.0;
	y0 += windH / 2.0;

	if (x0 >= 0.0 && x0 < windW && y0 >= 0.0 && y0 < windH) {
		return GL_TRUE;
	} else {
		return GL_FALSE;
	}
}

void
ShowStar(GLint n)
{
	float x0, y0, x1, y1, width;
	GLint i;

	x0 = stars[n].x[0] * windW / stars[n].z[0];
	y0 = stars[n].y[0] * windH / stars[n].z[0];
	RotatePoint(&x0, &y0, stars[n].rotation);
	x0 += windW / 2.0;
	y0 += windH / 2.0;

	if (x0 >= 0.0 && x0 < windW && y0 >= 0.0 && y0 < windH) {
		if (stars[n].type == STREAK) {
			x1 = stars[n].x[1] * windW / stars[n].z[1];
			y1 = stars[n].y[1] * windH / stars[n].z[1];
			RotatePoint(&x1, &y1, stars[n].rotation);
			x1 += windW / 2.0;
			y1 += windH / 2.0;

			glLineWidth(MAXPOS / 100.0 / stars[n].z[0] + 1.0);
			glColor3f(1.0, (MAXWARP - speed) / MAXWARP, (MAXWARP - speed) / MAXWARP);
			if (fabs(x0 - x1) < 1.0 && fabs(y0 - y1) < 1.0) {
				glBegin(GL_POINTS);
				glVertex2f(x0, y0);
				glEnd();
			} else {
				glBegin(GL_LINES);
				glVertex2f(x0, y0);
				glVertex2f(x1, y1);
				glEnd();
			}
		} else {
			width = MAXPOS / 10.0 / stars[n].z[0] + 1.0;
			glColor3f(1.0, 0.0, 0.0);
			glBegin(GL_POLYGON);
			for (i = 0; i < 8; i++) {
				float x = x0 + width * Cos((float) i * MAXANGLES / 8.0);
				float y = y0 + width * Sin((float) i * MAXANGLES / 8.0);
				glVertex2f(x, y);
			};
			glEnd();
		}
	}
}

void
UpdateStars(void)
{
	GLint n;

	glClear(GL_COLOR_BUFFER_BIT);

	for (n = 0; n < starCount; n++) {
		if (stars[n].z[0] > speed || (stars[n].z[0] > 0.0 && speed < MAXWARP)) {
			if (StarPoint(n) == GL_FALSE) {
				NewStar(n, MAXPOS);
			}
		} else {
			NewStar(n, MAXPOS);
		}
	}
}

void
ShowStars(void)
{
	GLint n;

	glClear(GL_COLOR_BUFFER_BIT);

	for (n = 0; n < starCount; n++) {
		if (stars[n].z[0] > speed || (stars[n].z[0] > 0.0 && speed < MAXWARP)) {
			ShowStar(n);
		}
	}
}

static void
Init(void)
{
	int i;
	particle_t *p;

	scanf("%d %d %f %f", &nparticles, &shape, &dx, &dt);
	printf("Number of particles=%d, shape=%d\n", nparticles, shape);

	particles = calloc(sizeof(particle_t), nparticles);

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
	case ' ':
		flag = (flag == NORMAL) ? WEIRD : NORMAL;
		break;
	case 't':
		nitro = 1;
		break;
	case 'q':
	case 27:
		exit(0);
	}
}

void
Idle(void)
{
	float t;
	int i, j, ret;
	particle_t *p;

	for(i = 0; i < nparticles; i++)
	{
		p = &particles[i];
		ret = scanf(" %f %d %f %f %f %f",
				&t, &j, &p->x, &p->u, &p->E, &p->J);
		if(ret == EOF)
		{
			printf("EOF reached\n");
			exit(0);
		}

		if((ret != 6) || (j != i))
		{
			printf("ret=%d i=%d j=%d, exitting\n", ret, i, j);
			exit(1);
		}
	}

	glutPostRedisplay();
}

static void
plot_particle(particle_t *p)
{

	float x, y;
	float x1, y1, width;
	GLint i;

	x = (p->x / (dx * shape)) * windW;
	y = (p->u / (4.0 * 3e8)) * windH;
	//x += windW / 2.0;
	y += windH / 2.0;

	if (!(x >= 0.0 && x < windW && y >= 0.0 && y < windH))
	{
		printf("Particle out of bounds x=%f(%d) y=%f(%d)\n",
				x, windW, y, windH);
	}

	glLineWidth(1.0);
	glColor3f(1.0, 1.0, 1.0);
	glBegin(GL_POINTS);
	glVertex2f(x, y);
	glEnd();
}


static void
plot_particles()
{
	int i;

	for(i=0; i<nparticles; i++)
	{
		plot_particle(&particles[i]);
	}
}

void
Display(void)
{
	ShowStars();
	plot_particles();
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
	glutCreateWindow("Stars");

	Init();

	glutReshapeFunc(Reshape);
	glutKeyboardFunc(Key);
	glutVisibilityFunc(Visible);
	glutDisplayFunc(Display);
	glutMainLoop();
	return 0;             /* ANSI C requires main to return int. */
}
