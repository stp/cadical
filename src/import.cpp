#include "internal.hpp"

namespace CaDiCaL {

/*------------------------------------------------------------------------*/

// Clause import and search diversification, an STP extension for solvers
// forked from one loaded solver which exchange learned clauses.
//
// Every imported clause is promised to be implied by the clauses added to
// this solver (the 'ClauseImporter' contract). Such a clause can be added as
// a redundant clause whatever inprocessing has done, provided every one of
// its variables is still active here: removing clauses or eliminating
// variables (all that the enabled techniques do to the irredundant clauses
// apart from adding implied ones) keeps every model of the added clauses
// a model of what remains, so an implied clause excludes no model the
// solver could still return, and a reconstructed model satisfies it anyway.
// A clause over an eliminated, substituted or unused variable is dropped
// instead of restoring that variable. 'block', 'cover', 'condition' and
// 'instantiate' (all off by default) are not analysed for import; with any
// of them enabled nothing is imported, conservatively.
//
// Clauses are imported between propagation and the next decision, so the
// watch invariants hold. A clause with at least two unassigned literals is
// watched on two of them at once; any other clause, and every unit, makes
// the solver backtrack first: units to the root, other clauses to the
// highest level at which two of their literals are unassigned. One poll
// backtracks at most once, to the lowest level any of its clauses needs.

bool Internal::importing () {
  return external->clause_importer && stats.conflicts >= import_limit;
}

void Internal::import_clauses () {
  ClauseImporter *importer = external->clause_importer;
  assert (importer);
  const int interval = importer->import_interval ();
  import_limit = stats.conflicts + (interval > 0 ? interval : 1);
  import_stats.polls++;
  if (unsat || proof || opts.block || opts.cover || opts.condition ||
      opts.instantiate)
    return;

  // The literals of every accepted clause, each clause followed by a zero,
  // in internal numbering with root-level falsified literals removed.
  vector<int> accepted;
  int target = level;
  const int budget = importer->import_budget ();
  int64_t longer = 0; // clauses of two or more literals handed over
  for (;;) {
    import_external.clear ();
    if (!importer->import_clause (import_external))
      break;
    if (import_external.size () > 1 && longer++ >= budget) {
      import_stats.dropped++; // beyond the budget: the importer's fault
      break;
    }
    assert (clause.empty ());
    bool drop = false, satisfied = false;
    for (const int elit : import_external) {
      const int eidx = abs (elit);
      if (!elit || elit == INT_MIN || eidx > external->max_var ||
          (eidx < (int) external->ervars.size () &&
           external->ervars[eidx])) {
        drop = true;
        break;
      }
      const int iidx = external->e2i[eidx];
      if (!iidx) {
        drop = true;
        break;
      }
      const int ilit = elit < 0 ? -iidx : iidx;
      const Flags &f = flags (ilit);
      if (f.status == Flags::FIXED) {
        const int value = fixed (ilit);
        if (value > 0) {
          satisfied = true;
          break;
        }
        continue; // falsified at the root: not part of the clause
      }
      if (!f.active ()) {
        drop = true;
        break;
      }
      const signed char tmp = marked (ilit);
      if (tmp > 0)
        continue; // duplicate
      if (tmp < 0) {
        satisfied = true; // tautology
        break;
      }
      mark (ilit);
      clause.push_back (ilit);
    }
    for (const int ilit : clause)
      unmark (ilit);
    if (drop || satisfied) {
      if (drop)
        import_stats.dropped++;
      else
        import_stats.satisfied++;
      clause.clear ();
      continue;
    }
    if (clause.empty ()) {
      // Every literal is false at the root: the formula is unsatisfiable.
      clause.clear ();
      if (level)
        backtrack (0);
      import_stats.clauses++;
      learn_empty_clause ();
      return;
    }
    // The level this clause needs: the root for a unit; otherwise the
    // highest level leaving two of its literals unassigned.
    int needed = level;
    if (clause.size () == 1)
      needed = 0;
    else {
      int open = 0, first = -1, second = -1;
      for (const int ilit : clause) {
        if (!val (ilit)) {
          open++;
          continue;
        }
        const int l = var (ilit).level;
        if (l > first)
          second = first, first = l;
        else if (l > second)
          second = l;
      }
      if (open == 1)
        needed = first - 1;
      else if (!open)
        needed = second - 1;
      if (needed < 0)
        needed = 0;
    }
    if (needed < target)
      target = needed;
    for (const int ilit : clause)
      accepted.push_back (ilit);
    accepted.push_back (0);
    clause.clear ();
  }
  if (accepted.empty ())
    return;
  if (target < level) {
    import_stats.backtracks++;
    backtrack (target);
  }

  // Add what was accepted. Units first, at the root; a unit falsified by an
  // earlier unit of this poll refutes the formula.
  for (size_t i = 0; i < accepted.size (); i++) {
    const size_t begin = i;
    while (accepted[i])
      i++;
    if (i - begin != 1)
      continue;
    assert (!level);
    const int ilit = accepted[begin];
    const signed char tmp = val (ilit);
    if (tmp > 0)
      continue;
    import_stats.clauses++;
    import_stats.units++;
    if (tmp < 0) {
      learn_empty_clause ();
      return;
    }
    assign_unit (ilit);
  }
  for (size_t i = 0; i < accepted.size (); i++) {
    const size_t begin = i;
    while (accepted[i])
      i++;
    if (i - begin < 2)
      continue;
    assert (clause.empty ());
    // Two unassigned literals go first and are watched; a literal fixed at
    // the root by a unit of this poll satisfies or shortens the clause.
    bool satisfied = false;
    int open = 0;
    for (size_t j = begin; j < i; j++) {
      const int ilit = accepted[j];
      const int value = fixed (ilit);
      if (value > 0) {
        satisfied = true;
        break;
      }
      if (value < 0)
        continue;
      clause.push_back (ilit);
      if (!val (ilit) && open < 2) {
        swap (clause[open], clause.back ());
        open++;
      }
    }
    if (satisfied || open < 2) {
      // Only units of this poll can have made this happen: satisfied by one,
      // or shortened by them below two open literals (dropped, best effort).
      if (satisfied)
        import_stats.satisfied++;
      else
        import_stats.dropped++;
      clause.clear ();
      continue;
    }
    Clause *c = new_clause (true, (int) clause.size ());
    watch_clause (c);
    clause.clear ();
    import_stats.clauses++;
  }
}

/*------------------------------------------------------------------------*/

void Internal::diversify (int seed, int phase, bool shuffle) {
  opts.seed = seed;
  Random random (seed);
  if (-1 <= phase && phase <= 1)
    for (auto idx : vars)
      phases.saved[idx] = phase > 0   ? 1
                          : phase < 0 ? -1
                          : (random.generate_bool () ? -1 : 1);
  if (!shuffle)
    return;
  if (level)
    backtrack (0);
  const int saved_shuffle = opts.shuffle, saved_queue = opts.shufflequeue,
            saved_scores = opts.shufflescores,
            saved_random = opts.shufflerandom;
  opts.shuffle = opts.shufflequeue = opts.shufflescores =
      opts.shufflerandom = 1;
  shuffle_scores ();
  shuffle_queue ();
  opts.shuffle = saved_shuffle;
  opts.shufflequeue = saved_queue;
  opts.shufflescores = saved_scores;
  opts.shufflerandom = saved_random;
}

} // namespace CaDiCaL
