#include "../../src/internal.hpp"
#include "../../src/testing.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

using namespace CaDiCaL;

static void check (int offset, int sign) {
  Solver solver;
  solver.set ("quiet", 1);
  solver.set ("preprocesslight", 0);
  solver.resize (37);

  // Give an empty solver a nonidentity variable map. Compaction can also
  // make internal and external numbers differ, but this does not depend on
  // when preprocessing chooses to compact a particular formula.
  Testing access (solver);
  for (int external = 1; external <= 37; ++external) {
    const int internal = (external - 1 + offset) % 37 + 1;
    access.external ()->e2i[external] = internal;
    access.internal ()->i2e[internal] = external;
  }

  // Six pigeons and five holes: lookahead must emit cubes rather than
  // immediately decide the formula. Variable 37 is an independent assumption.
  const int holes = 5;
  for (int pigeon = 0; pigeon <= holes; ++pigeon) {
    for (int hole = 0; hole < holes; ++hole)
      solver.add (1 + pigeon * holes + hole);
    solver.add (0);
  }
  for (int hole = 0; hole < holes; ++hole)
    for (int first = 0; first <= holes; ++first)
      for (int second = first + 1; second <= holes; ++second) {
        solver.add (-(1 + first * holes + hole));
        solver.add (-(1 + second * holes + hole));
        solver.add (0);
      }
  solver.assume (sign * 37);
  const auto result = solver.generate_cubes (1);
  assert (!result.status);
  assert (!result.cubes.empty ());
  for (const auto &cube : result.cubes) {
    assert (!cube.empty ());
    assert (cube.front () == sign * 37);
  }
}

int main () {
  for (int offset : {1, 7})
    for (int sign : {-1, 1})
      check (offset, sign);
}
