/*
 * Copyright © 2011  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Behdad Esfahbod
 */

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <pango/pangocairo.h>

#include "geometry.hh"

static void
bezier_calculate (double t,
		  point_t p0,
		  point_t p1,
		  point_t p2,
		  point_t p3,
		  point_t *p,
		  vector_t *dp,
		  vector_t *ddp
		  )
{
  double t_1_0, t_0_1;
  double t_2_0, t_0_2;
  double t_3_0, t_2_1, t_1_2, t_0_3;
  double _1__4t_1_0_3t_2_0, _2t_1_0_3t_2_0;

  t_1_0 = t;
  t_0_1 = 1 - t;

  t_2_0 = t_1_0 * t_1_0; /*      t  *      t  */
  t_0_2 = t_0_1 * t_0_1; /* (1 - t) * (1 - t) */

  t_3_0 = t_2_0 * t_1_0; /*      t  *      t  *      t  */
  t_2_1 = t_2_0 * t_0_1; /*      t  *      t  * (1 - t) */
  t_1_2 = t_1_0 * t_0_2; /*      t  * (1 - t) * (1 - t) */
  t_0_3 = t_0_1 * t_0_2; /* (1 - t) * (1 - t) * (1 - t) */

  _1__4t_1_0_3t_2_0 = 1 - 4 * t_1_0 + 3 * t_2_0;
  _2t_1_0_3t_2_0    =     2 * t_1_0 - 3 * t_2_0;

  if (p) {
    /* Bezier polynomial */
    p->x = p0.x * t_0_3
     + 3 * p1.x * t_1_2
     + 3 * p2.x * t_2_1
     +     p3.x * t_3_0;
    p->y = p0.y * t_0_3
     + 3 * p1.y * t_1_2
     + 3 * p2.y * t_2_1
     +     p3.y * t_3_0;
  }

  if (dp) {
    /* Bezier gradient */
    dp->x = -3 * p0.x * t_0_2
	   + 3 * p1.x * _1__4t_1_0_3t_2_0
	   + 3 * p2.x * _2t_1_0_3t_2_0
	   + 3 * p3.x * t_2_0;
    dp->y = -3 * p0.y * t_0_2
	   + 3 * p1.y * _1__4t_1_0_3t_2_0
	   + 3 * p2.y * _2t_1_0_3t_2_0
	   + 3 * p3.y * t_2_0;
  }

  if (ddp) {
    /* Bezier second derivatives */
    ddp->x = 6 * ( (-p0.x+3*p1.x-3*p2.x+p3.x) * t + (p0.x-2*p1.x+p2.x));
    ddp->y = 6 * ( (-p0.y+3*p1.y-3*p2.y+p3.y) * t + (p0.y-2*p1.y+p2.y));
  }
}

static point_t
point_add (point_t p, const vector_t v)
{
  p.x += v.x;
  p.y += v.y;
  return p;
}

static vector_t
vector_add (vector_t u, const vector_t v)
{
  u.x += v.x;
  u.y += v.y;
  return u;
}

static vector_t
points_difference (const point_t A, const point_t B)
{
  vector_t d;
  d.x = B.x - A.x;
  d.y = B.y - A.y;
  return d;
}

static point_t
mid_point (const point_t A, const point_t B)
{
  point_t C;
  C.x = .5 * (A.x + B.x);
  C.y = .5 * (A.y + B.y);
  return C;
}

static double
vector_length (const vector_t v)
{
  return hypot (v.x, v.y);
}

static double
points_distance (const point_t A, const point_t B)
{
  return vector_length (points_difference (A, B));
}

static vector_t
vector_normalize (vector_t v)
{
  double d = vector_length (v);
  if (!d)
    return v;

  v.x /= d;
  v.y /= d;
  return v;
}

static vector_t
vector_perpendicular (const vector_t v)
{
  vector_t n;
  n.x = -v.y;
  n.y =  v.x;
  return n;
}

static vector_t
vector_normal (const vector_t v)
{
  return vector_normalize (vector_perpendicular (v));
}

/* b should be normal */
static vector_t
vector_rebase (const vector_t v, const vector_t b)
{
  vector_t u;
  u.x = v.x * b.x + v.y * b.y;
  u.y = v.x * -b.y + v.y * b.x;
  return u;
}

static vector_t
vector_scale (vector_t v, double scale)
{
  v.x *= scale;
  v.y *= scale;
  return v;
}

static void
line_from_two_points (const point_t p0, const point_t p1, line_t *l)
{
  vector_t n = vector_perpendicular (points_difference (p0, p1));
  l->a = n.x;
  l->b = n.y;
  l->c = n.x * p0.x + n.y * p0.y;
}

static void
segment_axis_line (const point_t p0, const point_t p1, line_t *l)
{
  vector_t d = points_difference (p0, p1);

  l->a = d.x;
  l->b = d.y;
  l->c = ((d.x*p0.x + d.y*p0.y) + (d.x*p1.x + d.y*p1.y)) / 2.;
}

static bool
line_normalize (line_t *l)
{
  double d = hypot (l->a, l->b);

  if (!d) return false;

  l->a /= d;
  l->b /= d;
  l->c /= d;
  return true;
}

static bool
lines_intersection (const line_t l0, const line_t l1, point_t *p)
{
  double det = l0.a * l1.b - l1.a * l0.b;

  if (!det) {
    p->x = p->y = INFINITY;
    return false;
  }

  p->x = (l0.c * l1.b - l1.c * l0.b) / det;
  p->y = (l0.a * l1.c - l1.a * l0.c) / det;
  return true;
}

static bool
segments_intersection (const point_t p0,
		       const point_t p1,
		       const point_t p2,
		       const point_t p3,
		       point_t *p)
{
  line_t l0, l1;

  line_from_two_points (p0, p1, &l0);
  line_from_two_points (p3, p2, &l1);

  return lines_intersection (l0, l1, p);
}

static double
point_distance_to_line (const point_t p, line_t l)
{
  return (l.a * p.x + l.b * p.y - l.c) / hypot (l.a, l.b);
}

static double
point_distance_to_normal_line (point_t p, line_t l)
{
  return l.a * p.x + l.b * p.y - l.c;
}

/* https://secure.wikimedia.org/wikipedia/en/wiki/Incentre#Coordinates_of_the_incenter */
static void
incenter_point (const point_t A,
		const point_t B,
		const point_t C,
		point_t *g)
{
  double a = points_distance (B, C);
  double b = points_distance (C, A);
  double c = points_distance (A, B);
  double P = a + b + c;

  if (P == 0) {
    /* Degenerate case: A = B = C */
    *g = A;
    return;
  }

  g->x = (a * A.x + b * B.x + c * C.x) / P;
  g->y = (a * A.y + b * B.y + c * C.y) / P;
}

static double
atan_two_points (const point_t p0, const point_t p1)
{
  return atan2 (p1.y - p0.y, p1.x - p0.x);
}

static void
circle_by_two_points_and_tangent (const point_t p0, const point_t p1, const point_t p3, circle_t *c)
{
  double d, x, r;
  line_t tangent;

  line_from_two_points (p0, p1, &tangent);
  line_normalize (&tangent);

  x = point_distance_to_normal_line (p3, tangent);
  d = points_distance (p0, p3);
  r = d * d / (2 * x);

  c->r = r;
  c->c.x = p0.x + r * tangent.a;
  c->c.y = p0.y + r * tangent.b;
}

static void
circle_by_three_points (const point_t p0, const point_t p1, const point_t p2, circle_t *c)
{
  line_t l0, l1;

  segment_axis_line (p0, p1, &l0);
  segment_axis_line (p1, p2, &l1);
  lines_intersection (l0, l1, &c->c);
  c->r = points_distance (p0, c->c);
}



void fancy_cairo_stroke (cairo_t *cr);
void fancy_cairo_stroke_preserve (cairo_t *cr);


#define MY_CAIRO_PATH_ARC_TO (CAIRO_PATH_CLOSE_PATH+1)


/* A fancy cairo_stroke[_preserve]() that draws points and control
 * points, and connects them together.
 */
static void
_fancy_cairo_stroke (cairo_t *cr, cairo_bool_t preserve)
{
  int i;
  double line_width;
  cairo_path_t *path;
  cairo_path_data_t *data;

  cairo_save (cr);
  cairo_set_source_rgb (cr, 1.0, 0.0, 0.0);

  line_width = cairo_get_line_width (cr);
  path = cairo_copy_path (cr);
  cairo_new_path (cr);

  cairo_save (cr);
  cairo_set_line_width (cr, line_width / 3);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
    case CAIRO_PATH_LINE_TO:
	cairo_move_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_CURVE_TO:
	cairo_line_to (cr, data[1].point.x, data[1].point.y);
	cairo_move_to (cr, data[2].point.x, data[2].point.y);
	cairo_line_to (cr, data[3].point.x, data[3].point.y);
	break;
    case CAIRO_PATH_CLOSE_PATH:
	break;
    default:
	g_assert_not_reached ();
    }
  }
  cairo_stroke (cr);
  cairo_restore (cr);

  cairo_save (cr);
  cairo_set_line_width (cr, line_width * 4);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
	cairo_move_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_LINE_TO:
	cairo_rel_line_to (cr, 0, 0);
	cairo_move_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_CURVE_TO:
	cairo_rel_line_to (cr, 0, 0);
	cairo_move_to (cr, data[1].point.x, data[1].point.y);
	cairo_rel_line_to (cr, 0, 0);
	cairo_move_to (cr, data[2].point.x, data[2].point.y);
	cairo_rel_line_to (cr, 0, 0);
	cairo_move_to (cr, data[3].point.x, data[3].point.y);
	break;
    case CAIRO_PATH_CLOSE_PATH:
	cairo_rel_line_to (cr, 0, 0);
	break;
    default:
	g_assert_not_reached ();
    }
  }
  cairo_rel_line_to (cr, 0, 0);
  cairo_stroke (cr);
  cairo_restore (cr);

  cairo_append_path (cr, path);

  if (preserve)
    cairo_stroke_preserve (cr);
  else
    cairo_stroke (cr);

  cairo_path_destroy (path);

  cairo_restore (cr);
}

/* A fancy cairo_stroke() that draws points and control points, and
 * connects them together.
 */
void
fancy_cairo_stroke (cairo_t *cr)
{
  _fancy_cairo_stroke (cr, FALSE);
}

/* A fancy cairo_stroke_preserve() that draws points and control
 * points, and connects them together.
 */
void
fancy_cairo_stroke_preserve (cairo_t *cr)
{
  _fancy_cairo_stroke (cr, TRUE);
}

static void
print_path_stats (const cairo_path_t *path)
{
  int i;
  cairo_path_data_t *data;
  int lines = 0, curves = 0, arcs = 0;

  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch ((int) data->header.type) {
    case CAIRO_PATH_MOVE_TO:
        break;
    case CAIRO_PATH_LINE_TO:
	lines++;
	break;
    case CAIRO_PATH_CURVE_TO:
	curves++;
	break;
    case MY_CAIRO_PATH_ARC_TO:
	arcs++;
	break;
    case CAIRO_PATH_CLOSE_PATH:
	break;
    default:
	g_assert_not_reached ();
    }
  }
  printf ("%d lines, %d arcs, %d curves\n", lines, arcs, curves);
}

/* This is a fast approximation of max_dev(). */
static double
max_dev_approx (double d0, double d1)
{
  double e0, e1;
  d0 = fabs (d0);
  d1 = fabs (d1);
  e0 = 3./4. * MAX (d0, d1);
  e1 = 4./9. * (d0 + d1);
  return MIN (e0, e1);
}

/* Returns max(abs(d₀ t (1-t)² + d₁ t² (1-t)) for 0≤t≤1. */
static double
max_dev (double d0, double d1)
{
  double e;
  unsigned int i;
  double candidates[4] = {0,1};
  unsigned int num_candidates = 2;
  double delta = d0*d0 - d0*d1 + d1*d1;
  /* This code can be optimized to avoid the sqrt if the solution
   * is not feasible (ie. lies outside (0,1)).  I have implemented
   * that in cairo-spline.c:_cairo_spline_bound().  Can be reused
   * here.
   */
  if (d0 == d1)
    candidates[num_candidates++] = .5;
  else if (delta == 0)
    candidates[num_candidates++] = (2 * d0 - d1) / (3 * (d0 - d1));
  else if (delta > 0) {
    double t0 = 2 * d0 - d1, t1 = sqrt (delta), t2 = 1. / (3 * (d0 - d1));
    candidates[num_candidates++] = (t0 - t1) * t2;
    candidates[num_candidates++] = (t0 + t1) * t2;
  }

  for (i = 0; i < num_candidates; i++) {
    double t = candidates[i];
    double ee;
    if (t < 0. || t > 1.)
      continue;
    ee = 3 * t * (1-t) * (d0 * (1 - t) + d1 * t);
    e = MAX (e, fabs (ee));
  }


  return e;
}

double
arc_bezier_error (const point_t p0,
		  const point_t p1,
		  const point_t p2,
		  const point_t p3,
		  const circle_t cm,
		  cairo_t *cr)
{
  double a0, a1, a4, _4_3_tan_a4;
  point_t p1s, p2s;
  double ea, eb, e;

  a0 = atan_two_points (cm.c, p0);
  a1 = atan_two_points (cm.c, p3);
  a4 = (a1 - a0) / 4.;
  _4_3_tan_a4 = 4./3.*tan (a4);
  p1s = point_add (p0,
	  vector_scale (
	    vector_perpendicular (
	      points_difference (cm.c, p0)
	    ), _4_3_tan_a4));
  p2s = point_add (p3,
	  vector_scale (
	    vector_perpendicular (
	      points_difference (p3, cm.c)
	    ), _4_3_tan_a4));

  ea = 2./27.*cm.r*pow(sin(a4),6)/pow(cos(a4)/4.,2);
  //eb = max_dev (points_distance (p1, p1s), points_distance (p2, p2s));
  {
    vector_t v0, v1, v, u;
    v0 = points_difference (p1, p1s);
    v1 = points_difference (p2, p2s);

    vector_t b = vector_normalize (vector_add (points_difference (cm.c, p0), points_difference (cm.c, p3)));
    v0 = vector_rebase (v0, b);
    v1 = vector_rebase (v1, b);

    v.x = max_dev (v0.x, v1.x);
    v.y = max_dev (v0.y, v1.y);

    vector_t b2 = vector_normalize (vector_rebase (points_difference (cm.c, p3), b));
    u = vector_rebase (v, b2);
    eb = sqrt ((cm.r+u.x)*(cm.r+u.x) + u.y*u.y) - cm.r;
  }
  e = ea + eb;

  cairo_save (cr);
  double line_width = cairo_get_line_width (cr);
  cairo_set_source_rgba (cr, 0.0, 1.0, 0.0, 1.0);

  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to (cr, p1s.x, p1s.y);
  cairo_rel_line_to (cr, 0, 0);
  cairo_move_to (cr, p2s.x, p2s.y);
  cairo_rel_line_to (cr, 0, 0);
  cairo_set_line_width (cr, line_width * 4);
  cairo_stroke (cr);

  cairo_set_line_width (cr, line_width * 1);
  cairo_arc (cr, cm.c.x, cm.c.y, cm.r, a0, a1);
  cairo_stroke (cr);

  cairo_set_line_width (cr, line_width * .25);
  cairo_move_to (cr, p0.x, p0.y);
  cairo_line_to (cr, p1s.x, p1s.y);
  cairo_move_to (cr, p3.x, p3.y);
  cairo_line_to (cr, p2s.x, p2s.y);
  cairo_stroke (cr);
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_move_to (cr, p0.x, p0.y);
  cairo_curve_to (cr, p1s.x, p1s.y, p2s.x, p2s.y, p3.x, p3.y);
  cairo_stroke (cr);

  cairo_restore (cr);

  return e;
}

static void
demo_curve (cairo_t *cr)
{
  int i;
  double line_width;
  cairo_path_t *path;
  cairo_path_data_t *data, current_point;

  fancy_cairo_stroke_preserve (cr);
  path = cairo_copy_path (cr);
  cairo_new_path (cr);

  print_path_stats (path);

  cairo_save (cr);
  line_width = cairo_get_line_width (cr);
  cairo_set_line_width (cr, line_width / 16);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
    case CAIRO_PATH_LINE_TO:
	current_point = data[1];
	break;
    case CAIRO_PATH_CURVE_TO:
	{
#define P(d) (*(point_t *)&(d).point)

	  point_t p0 = P(current_point), p1 = P(data[1]), p2 = P(data[2]), p3 = P(data[3]);

	  if (1)
	  {
	    circle_t cm;
	    point_t m;

	    /* divide the curve into two */
	    point_t p01, p12, p23, p012, p123, p0123;
	    p01 = mid_point (p0, p1);
	    p12 = mid_point (p1, p2);
	    p23 = mid_point (p2, p3);
	    p012 = mid_point (p01, p12);
	    p123 = mid_point (p12, p23);
	    p0123 = mid_point (p012, p123);
	    m = p0123;

	    circle_by_three_points (p0, m, p3, &cm);

	    double e, e0, e1;
	    e0 = arc_bezier_error (p0, p01, p012, p0123, cm, cr);
	    e1 = arc_bezier_error (p0123, p123, p23, p3, cm, cr);
	    e = MAX (e0, e1);

	    printf ("%g %g = %g\n", e0, e1, e);

	    //e1 = arc_bezier_error (p0, p1, p2, m, cm, cr);

	    {
	      double t;
	      double e = 0;
	      for (t = 0; t <= 1; t += .001) {
		point_t p;
		bezier_calculate (t, p0, p1, p2, p3, &p, NULL, NULL);
		e = MAX (e, fabs (points_distance (p, cm.c) - cm.r));
	      }
	      printf ("Actual arc max error %g\n", e);
	    }

	    cairo_save (cr);
	    cairo_set_source_rgba (cr, 0.0, 1.0, 0.0, 1.0);

	    cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	    cairo_move_to (cr, m.x, m.y);
	    cairo_rel_line_to (cr, 0, 0);
	    cairo_set_line_width (cr, line_width * 4);
	    cairo_stroke (cr);

	    cairo_set_line_width (cr, line_width * 1);
	    {
	      double a0, a1;
	      a0 = atan_two_points (cm.c, p0);
	      a1 = atan_two_points (cm.c, p3);
	      cairo_arc (cr, cm.c.x, cm.c.y, cm.r, a0, a1);
	    }
	    cairo_stroke (cr);

	    cairo_restore (cr);
	  }

	  {
	    double t;
	    for (t = 0; t <= 1; t += .05) {
	      point_t p;
	      vector_t dp, ddp, nv, cv;
	      double len, curvature;
	      bezier_calculate (t, p0, p1, p2, p3, &p, &dp, &ddp);

	      /* normal vector len squared */
	      len = vector_length (dp);
	      nv = vector_normal (dp);

	      curvature = (ddp.y * dp.x - ddp.x * dp.y) / (len*len*len);
	      cv.x = nv.x / curvature;
	      cv.y = nv.y / curvature;

	      cairo_move_to (cr, p.x, p.y);
	      cairo_rel_line_to (cr, cv.x, cv.y);
	    }
	  }
	}
	current_point = data[3];
	break;
    case CAIRO_PATH_CLOSE_PATH:
	break;
    default:
	g_assert_not_reached ();
    }
  }
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_stroke (cr);
  cairo_restore (cr);

#if 0
  cairo_save (cr);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
	cairo_move_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_LINE_TO:
	cairo_line_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_CURVE_TO:
	cairo_curve_to (cr, data[1].point.x, data[1].point.y,
			    data[2].point.x, data[2].point.y,
			    data[3].point.x, data[3].point.y);
	break;
    case CAIRO_PATH_CLOSE_PATH:
	cairo_close_path (cr);
	break;
    default:
	g_assert_not_reached ();
    }
  }
  cairo_stroke (cr);
  cairo_restore (cr);
#endif
  cairo_path_destroy (path);
}

static void
draw_dream (cairo_t *cr)
{
  printf ("SAMPLE: dream line\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_move_to (cr, 50, 650);

  cairo_rel_line_to (cr, 250, 50);
  cairo_rel_curve_to (cr, 250, 50, 600, -50, 600, -250);
  cairo_rel_curve_to (cr, 0, -400, -300, -100, -800, -300);

  cairo_set_line_width (cr, 5);
  cairo_set_source_rgba (cr, 0.3, 1.0, 0.3, 0.3);

  demo_curve (cr);

  cairo_restore (cr);
}

static void
draw_raskus_simple (cairo_t *cr)
{
  printf ("SAMPLE: raskus simple\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_save (cr);
  cairo_translate (cr, -1300, 500);
  cairo_scale (cr, 200, -200);
  cairo_translate (cr, -10, -1);
  cairo_move_to (cr, 16.9753, .7421);
  cairo_curve_to (cr, 18.2203, 2.2238, 21.0939, 2.4017, 23.1643, 1.6148);
  cairo_restore (cr);

  cairo_set_line_width (cr, 2.0);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);

  demo_curve (cr);

  cairo_restore (cr);
}

static void
draw_skewed (cairo_t *cr)
{
  printf ("SAMPLE: skewed\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_move_to (cr, 50, 380);
  cairo_scale (cr, 2, 2);
  cairo_rel_curve_to (cr, 0, -100, 250, -50, 330, 10);

  cairo_set_line_width (cr, 2.0);
  cairo_set_source_rgba (cr, 0.3, 1.0, 0.3, 1.0);

  demo_curve (cr);

  cairo_restore (cr);
}

int main (int argc, char **argv)
{
  cairo_t *cr;
  char *filename;
  cairo_status_t status;
  cairo_surface_t *surface;

  if (argc != 2)
    {
      g_printerr ("Usage: cairotwisted OUTPUT_FILENAME\n");
      return 1;
    }

  filename = argv[1];

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					1400, 1000);
  cr = cairo_create (surface);

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

//  draw_skewed (cr);
  draw_raskus_simple (cr);
//  draw_dream (cr);

  cairo_destroy (cr);

  status = cairo_surface_write_to_png (surface, filename);
  cairo_surface_destroy (surface);

  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_printerr ("Could not save png to '%s'\n", filename);
      return 1;
    }

  return 0;
}