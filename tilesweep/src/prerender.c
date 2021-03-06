#include "prerender.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include "stretchy_buffer.h"

static double tile_poly_area_approx(const vec2d* poly, int32_t len) {
  switch (len) {
    case 0:
      return 0;
    case 1:
      return 1;
    case 2: {
      vec2d p = poly[0];
      vec2d n = poly[1];

      if (p.y == n.y) {
        return fabs(n.x - p.x);
      }
      if (p.x == n.x) {
        return fabs(n.y - p.y);
      }

      double x = n.x - p.x;
      double y = n.y - p.y;
      return 1.5 * sqrtf(x * x + y * y);
    }
    default:
      return poly_area(poly, len);
  }
}

static int32_t exists(const vec2d* points, int32_t len, vec2d p) {
  for (int32_t i = 0; i < len; i++) {
    vec2d e = points[i];
    if (p.x == e.x && p.y == e.y) {
      return 1;
    }
  }

  return 0;
}

tile_calc_job** make_tile_calc_jobs(const vec2d* coordinates,
                                    int32_t num_coordinates, int32_t min_zoom,
                                    int32_t max_zoom, int32_t tile_size,
                                    int32_t tiles_per_job) {
  if (num_coordinates < 3) {
    return NULL;
  }

  tile_calc_job** jobs = NULL;
  for (int32_t z = min_zoom; z <= max_zoom; z++) {
    vec2d* tile_polygon = NULL;

    for (int32_t i = 0; i < num_coordinates; i++) {
      vec2d mercator_pt = coordinates[i];
      vec2d tile = mercator_to_tile(mercator_pt.x, mercator_pt.y, z, tile_size);
      sb_push(tile_polygon, tile);
    }

    tile_calc_job* job = (tile_calc_job*)calloc(1, sizeof(tile_calc_job));
    job->tile_size = tile_size;
    job->zoom = z;
    job->fill_limit = tiles_per_job;
    job->estimated_tiles =
        (uint64_t)tile_poly_area_approx(tile_polygon, sb_count(tile_polygon));

    fill_poly_state_init(&job->fill_state, tile_polygon,
                         sb_count(tile_polygon));

    sb_push(jobs, job);
    sb_free(tile_polygon);
  }

  return jobs;
}

vec2i* calc_tiles(tile_calc_job* job, int32_t* count) {
  vec2d* tiles = fill_poly_advance(&job->fill_state, job->fill_limit);

  const int32_t num_tiles = sb_count(tiles);
  *count = num_tiles;

  if (num_tiles <= 0) {
    sb_free(tiles);
    return NULL;
  }

  vec2i* tile_coords = (vec2i*)calloc(num_tiles, sizeof(vec2i));

  for (int32_t i = 0; i < num_tiles; i++) {
    tile_coords[i].x = (int32_t)tiles[i].x;
    tile_coords[i].y = (int32_t)tiles[i].y;
  }

  sb_free(tiles);

  return tile_coords;
}
