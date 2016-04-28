#pragma once

#include <stdint.h>
#include "common.h"

enum tl_request_type : uint64_t {
  rq_invalid = 0,
  rq_tile = 1,
  rq_prerender = 2,

  RQ_COUNT = 3
};

struct tl_tile {
  int z;
  int x;
  int y;
  int w;
  int h;

  uint64_t hash() {
    return (uint64_t(z) << 40) | (uint64_t(x) << 20) | uint64_t(y);
  }
};

struct tl_point {
  double x;
  double y;
};

struct tl_prerender {
  tl_point points[MAX_PRERENDER_COORDS];
  int w;
  int h;
};

union tl_request_union {
  tl_tile tile;
  tl_prerender prerender; 
};

struct tl_request {
  int64_t request_time;
  int client_fd;
  tl_request_type type;
  tl_request_union as;
};
