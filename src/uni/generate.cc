
extern "C" {
#include "cx/syscx.h"
}

#include "adjlist.hh"
#include "livelock.hh"
#include "uniact.hh"
#include "unifile.hh"

#include "../pfmla.hh"

#include "cx/bittable.hh"
#include "cx/fileb.hh"
#include "cx/table.hh"

#include <algorithm>

#include "../namespace.hh"

#include "canonical.cc"

struct SearchOpt
{
  uint domsz;
  uint max_depth;
  uint max_period;
  uint max_propagations;
  bool check_ppg_overapprox;
  bool nw_disabling;
  bool use_bdds;
  bool line_flush;
  bool trust_given;

  // Ehh... mutable is okay because this is a single-use
  // class for recursion parameters.
  mutable OFile id_ofile;
  mutable OFile bfs_ofile;

  OFileB id_ofileb;
  OFileB bfs_ofileb;

  uint dfs_threshold;

  PFmlaCtx pfmla_ctx;
  Table<PFmlaVbl> pfmla_vbls;
  uint allbut2_pfmla_list_id;

  Table<UniAct> given_acts;

  SearchOpt()
    : domsz( 0 )
    , max_depth( 0 )
    , max_period( 0 )
    , max_propagations( 0 )
    , check_ppg_overapprox( true )
    , nw_disabling( false )
    , use_bdds( false )
    , line_flush( true )
    , trust_given( false )
    , id_ofile( stdout_OFile() )
    , dfs_threshold( 0 )
  {}

  void commit_domsz() {
    if (max_depth == 0)
      max_depth = domsz*domsz;
    if (max_period == 0)
      max_period = 2*domsz+2;
    if (max_propagations == 0)
      max_propagations = domsz*domsz;

    if (use_bdds) {
      pfmla_vbls.affysz(1+max_period);
      for (uint i = 0; i < 1+max_period; ++i) {
        uint vbl_id = pfmla_ctx.add_vbl((String("x") << i), domsz);
        pfmla_vbls[i] = pfmla_ctx.vbl(vbl_id);
      }
      allbut2_pfmla_list_id = pfmla_ctx.add_vbl_list();
      for (uint i = 0; i+2 < 1+max_period; ++i) {
        pfmla_ctx.add_to_vbl_list(allbut2_pfmla_list_id,
                                  id_of(pfmla_vbls[i]));
      }
    }
  }
};


  void
del_node(AdjList<uint>& digraph, AdjList<uint>& rdigraph, uint node_id)
{
  {
    const uint* dsts = digraph.arcs_from(node_id);
    const uint ndsts = digraph.degree(node_id);
    for (uint i = 0; i < ndsts; ++i)
      rdigraph.del_arc(dsts[i], node_id);
  }
  {
    const uint* srcs = rdigraph.arcs_from(node_id);
    const uint nsrcs = rdigraph.degree(node_id);
    for (uint i = 0; i < nsrcs; ++i)
      digraph.del_arc(srcs[i], node_id);
  }
  digraph.del_arcs_from(node_id);
}

static
  void
init_ppgfun(Table<PcState>& ppgfun, const BitTable& delegates, const uint domsz)
{
  ppgfun.affysz(domsz*domsz, domsz);
  for each_in_BitTable(actid , delegates) {
    UniAct act = UniAct::of_id(actid, domsz);
    ppgfun[id_of2(act[0], act[1], domsz)] = act[2];
  }
}

  void
fill_livelock_actions_fn(void** data, const UniAct& act, uint rowidx, uint colidx) {
  (void) rowidx;
  (void) colidx;
  ((BitTable*) data[0])->set1(id_of(act, *(PcState*) data[1]));
}

/** Given a list of actions and variables x[j-1] and x[j],
 * compute the tiling constraints for column j
 * in the form of a transition formula.
 **/
static
  X::Fmla
column_xfmla(const BitTable& actset, const PFmlaVbl& x_p, const PFmlaVbl& x_j, uint domsz)
{
  X::Fmla xn( false );
  for each_in_BitTable( actid, actset ) {
    const UniAct act = UniAct::of_id(actid, domsz);
    //    x'[j-1]==a         && x[j]==b     && x'[j]==c
    xn |= x_p.img_eq(act[0]) && x_j==act[1] && x_j.img_eq(act[2]);
  }
  return xn;
}

  Trit
periodic_leads_semick(const BitTable& delegates,
                      const Table<PcState>& ppgfun,
                      BitTable& mask,
                      Table<BitTable>& candidates_stack,
                      const uint depth,
                      const SearchOpt& opt)
{
#define PruneLivelocks
  const uint domsz = opt.domsz;
  BitTable& candidates = candidates_stack[depth];

  if (opt.use_bdds) {
    bool livelock_found = false;
    X::Fmla lo_xn(true);
    X::Fmla hi_xn(true);
    X::Fmla lo_col_xn;
    X::Fmla hi_col_xn;
    const Table<PFmlaVbl>& vbls = opt.pfmla_vbls;
    Claim( vbls.sz() == 1+opt.max_period );
    for (uint j = 1; j < 1+opt.max_period; ++j) {
      lo_col_xn =
        column_xfmla(delegates, vbls[j-1], vbls[j], domsz);
      lo_xn &= lo_col_xn;

      if (opt.check_ppg_overapprox) {
        hi_col_xn = lo_col_xn |
          column_xfmla(candidates, vbls[j-1], vbls[j], domsz);
        hi_xn &= hi_col_xn;
      }

      const X::Fmla periodic_xn = lo_xn &
        (vbls[0]==vbls[j]) & vbls[0].img_eq_img(vbls[j]);

      if (periodic_xn.cycle_ck(0)) {
        livelock_found = true;
        break;
      }
    }
    if (livelock_found)
      return Nil;

    P::Fmla scc(false);
    if (lo_xn.cycle_ck(&scc)) {
      X::Fmla last_tile = (scc & lo_xn);
      last_tile = last_tile.smooth(opt.allbut2_pfmla_list_id);
      last_tile = last_tile.smooth_pre(opt.pfmla_vbls[opt.max_period-1]);
      if (lo_col_xn.equiv_ck(last_tile)) {
        return Yes;
      }
    }

    if (!opt.check_ppg_overapprox) {
      return May;
    }

    if (!hi_xn.cycle_ck(&scc)) {
      return Nil;
    }
    else {
      X::Fmla last_tile = (scc & hi_xn);
      last_tile = last_tile.smooth(opt.allbut2_pfmla_list_id);
      last_tile = last_tile.smooth_pre(opt.pfmla_vbls[opt.max_period-1]);
      if (!lo_col_xn.subseteq_ck(last_tile)) {
        return Nil;
      }
    }
    return May;
  }

  const bool detect_livelocks_well = true;

  bool maybe_livelock = true;
  if (detect_livelocks_well) {
    Table<PcState> live_row, live_col;
    Trit livelock_found =
      livelock_semick(opt.max_period, ppgfun, domsz, &live_row, &live_col);
    if (livelock_found == Nil) {
      maybe_livelock = false;
    }
    else if (livelock_found == Yes) {
#ifdef PruneLivelocks
      {
        PcState tmp_domsz = domsz;
        mask.wipe(0);
        void* data[2] = { &mask, &tmp_domsz };
        map_livelock_ppgs(fill_livelock_actions_fn, data, live_row, live_col,
                          ppgfun, domsz);
      }
      uint max_livelock_actids[2] = { 0, 0 };
      for each_in_BitTable( livelock_actid , mask ) {
        max_livelock_actids[0] = max_livelock_actids[1];
        max_livelock_actids[1] = livelock_actid;
      }
      // We should never have a self-loop livelock.
      Claim( max_livelock_actids[0] > 0 );
      for (uint i = 0; i < depth; ++i) {
        if (!candidates_stack[i].ck(max_livelock_actids[1])) {
          candidates_stack[i].wipe(0);
        }
        else if (!candidates_stack[i].ck(max_livelock_actids[0])) {
          // If the penultimate livelock action is picked,
          // forbid the maximal livelock action.
          candidates_stack[i].set0(max_livelock_actids[1]);
        }
      }
#endif
      return Nil;
    }
  }

  AdjList<uint> digraph( domsz*domsz*domsz );
  make_tile_cont_digraph(digraph, delegates, ppgfun, domsz);

  AdjList<uint> overapprox_digraph( digraph.nnodes() );
  if (opt.check_ppg_overapprox) {
    mask = delegates;
    mask |= candidates;
    make_overapprox_tile_cont_digraph(overapprox_digraph, mask, domsz);
  }
  else {
    overapprox_digraph.commit_degrees();
  }

  BitTable pending = delegates;
  Table< Tuple<uint,2> > stack;

  for each_in_BitTable(actid , pending) {
    const UniAct act = UniAct::of_id(actid, domsz);
    bool found = false;
    // Find all starting nodes.
    for (PcState d = 0; d < domsz; ++d) {
      const uint node_id = id_of3(act[0], act[1], d, domsz);
      skip_unless (cycle_ck_from(node_id, digraph, stack, mask));
      found = true;
      break;
    }

    if (!found) {
      maybe_livelock = false;
      if (opt.check_ppg_overapprox) {
        for (PcState d = 0; d < domsz; ++d) {
          const uint node_id = id_of3(act[0], act[1], d, domsz);
          skip_unless (cycle_ck_from(node_id, overapprox_digraph, stack, mask));
          found = true;
          break;
        }
        if (!found) {
          return Nil;
        }
      }
      continue;
    }

    for (uint i = 0; i < stack.sz(); ++i) {
      Triple<PcState> node = UniAct::of_id(stack[i][0], domsz);
      const PcState a = node[0];
      const PcState b = node[1];
      const PcState c = ppgfun[id_of2(a, b, domsz)];
      Claim( c < domsz );
      pending.set0(id_of3(a, b, c, domsz));

      // Change this stack to more easily represent the livelock.
      stack[i][0] = a;
      stack[i][1] = b;
    }
    if (opt.max_propagations > opt.max_period &&
        stack.sz() <= opt.max_period &&
        guided_livelock_ck(stack, ppgfun, domsz, mask,
                           opt.max_propagations)) {
#ifdef PruneLivelocks
      uint max_livelock_actids[2] = { 0, 0 };
      for each_in_BitTable( livelock_actid , mask ) {
        max_livelock_actids[0] = max_livelock_actids[1];
        max_livelock_actids[1] = livelock_actid;
      }
      // We should never have a self-loop livelock.
      Claim( max_livelock_actids[0] > 0 );
      for (uint i = 0; i < depth; ++i) {
        if (!candidates_stack[i].ck(max_livelock_actids[1])) {
          candidates_stack[i].wipe(0);
        }
        else if (!candidates_stack[i].ck(max_livelock_actids[0])) {
          // If the penultimate livelock action is picked,
          // forbid the maximal livelock action.
          candidates_stack[i].set0(max_livelock_actids[1]);
        }
      }
#undef PruneLivelocks
#endif
      return Nil;
    }
  }

  return (maybe_livelock ? Yes : May);
}

  void
trim_coexist (BitTable& actset, uint actid, uint domsz,
              bool nw_disabling)
{
  UniAct act( UniAct::of_id(actid, domsz) );

  // Trivial livelock.
  if (act[0]==act[1]) {
    subtract_action_mask(actset, UniAct(act[2], act[2], act[0]), domsz);
  }

  // Trivial livelock.
  if (act[0]==act[2]) {
    subtract_action_mask(actset, UniAct(act[1], act[0], act[1]), domsz);
  }

  // Enforce determinism.
  subtract_action_mask(actset, UniAct(act[0], act[1], domsz), domsz);
  //mask.set0(actid);

  // Enforce W-disabling.
  subtract_action_mask(actset, UniAct(act[0], act[2], domsz), domsz);
  subtract_action_mask(actset, UniAct(act[0], domsz, act[1]), domsz);

  if (!nw_disabling)  return;
  // Enforce N-disabling.
  subtract_action_mask(actset, UniAct(act[2], act[1], domsz), domsz);
  subtract_action_mask(actset, UniAct(domsz, act[1], act[0]), domsz);
}

static
  void
recurse(Table<BitTable>& delegates_stack,
        Table<BitTable>& candidates_stack,
        uint actid,
        const SearchOpt& opt,
        uint depth,
        BitTable& mask)
{
  const uint domsz = opt.domsz;

  BitTable& delegates = delegates_stack[depth];
  delegates = delegates_stack[depth-1];
  delegates.set1(actid);

  BitTable& candidates = candidates_stack[depth];
  candidates = candidates_stack[depth-1];
  Claim( candidates.ck(actid) );
  candidates.set0(actid);
  trim_coexist(candidates, actid, domsz, opt.nw_disabling);

  Table<PcState> ppgfun;
  init_ppgfun(ppgfun, delegates, domsz);
  bool print_delegates = true;
  if (depth == 1 && opt.trust_given) {
    print_delegates = false;
  }
  else {
    switch (periodic_leads_semick(delegates, ppgfun, mask,
                                  candidates_stack, depth, opt)) {
      case Nil: return;
      case May: print_delegates = false;
      case Yes: break;
    }
    if (!canonical_ck(ppgfun, domsz)) {
      return;
    }
  }
  if (print_delegates) {
    oput_b64_ppgfun(opt.id_ofile, ppgfun, domsz);
    if (opt.line_flush)
      opt.id_ofile << opt.id_ofile.endl();
    else
      opt.id_ofile << '\n';
  }

  if (depth == opt.max_depth) {
    UniAct act = UniAct::of_id(actid, domsz);
    if (domsz*domsz - 1 - id_of2(act[0], act[1], domsz) <= opt.dfs_threshold) {
      // No return.
    }
    else if (!!opt.bfs_ofile) {
      oput_b64_ppgfun(opt.bfs_ofile, ppgfun, domsz);
      if (opt.line_flush)
        opt.bfs_ofile << opt.bfs_ofile.endl();
      else
        opt.bfs_ofile << '\n';
      return;
    }
    else {
      return;
    }
  }

  for (uint next_actid = actid+1; next_actid < candidates.sz(); ++next_actid) {
    skip_unless (candidates.ck(next_actid));
    recurse(delegates_stack, candidates_stack,
            next_actid, opt, depth+1, mask);

    // This must be after recurse() so that the livelock
    // detection doesn't remove valid candidates.
    candidates.set0(next_actid);
  }
}

  void
searchit(const SearchOpt& opt)
{
  const uint domsz = opt.domsz;

  BitTable mask( domsz*domsz*domsz, 0 );
  Table<BitTable> delegates_stack;
  delegates_stack.affysz( domsz*domsz+1, mask );
  BitTable& delegates = delegates_stack[0];

  mask.wipe(1);
  Table<BitTable> candidates_stack;
  candidates_stack.affysz( domsz*domsz+1, mask );
  BitTable& candidates = candidates_stack[0];

  // Remove self-loops.
#define REMOVE_ABB \
  for (uint a = 0; a < domsz; ++a) \
    for (uint b = 0; b < domsz; ++b) \
      candidates.set0(id_of3(a, b, b, domsz))

  // Remove all (a, a, b) actions.
#define REMOVE_AAB \
  for (uint a = 0; a < domsz; ++a) \
    for (uint b = 0; b < domsz; ++b) \
      candidates.set0(id_of3(a, a, b, domsz))

  // Remove all (a, b, a) actions.
#define REMOVE_ABA \
  for (uint a = 0; a < domsz; ++a) \
    for (uint b = 0; b < domsz; ++b) \
      candidates.set0(id_of3(a, b, a, domsz))

#define RECURSE(actid) \
  recurse(delegates_stack, candidates_stack, actid, opt, 1, mask)

  // Never need self-loops.
  REMOVE_ABB;

  if (opt.nw_disabling) {
    REMOVE_ABA;
  }

  if (!opt.given_acts.empty_ck()) {
    uint hi_id = 0;
    for (uint i = 0; i < opt.given_acts.sz(); ++i) {
      const UniAct& act = opt.given_acts[i];
      const uint actid = id_of(act, domsz);
      if (actid > hi_id) {
        hi_id = actid;
      }
      delegates.set1(actid);
      if (!candidates.set0(actid)) {
        DBog1("Action #%u was trimmed already!", i+1);
        failout_sysCx("");
      }
      trim_coexist(candidates, actid, domsz, opt.nw_disabling);
    }
    if (!opt.trust_given &&
        !canonical_ck(uniring_ppgfun_of(delegates, domsz), domsz)) {
      OFile ofile( stderr_OFile () );
      oput_list(ofile, uniring_actions_of(mask, domsz));
      failout_sysCx("Assumed a non-canonical set of actions!");
    }

    // Clear out low candidates.
    for (uint actid = 0; actid < hi_id; ++actid) {
      candidates.set0(actid);
    }

    Claim( delegates.ck(id_of3(0, 0, 1, domsz)) ||
           delegates.ck(id_of3(0, 1, 0, domsz)) ||
           delegates.ck(id_of3(0, 1, 2, domsz)) );

    if (!delegates.ck(id_of3(0, 0, 1, domsz))) {
      REMOVE_AAB;
      if (!delegates.ck(id_of3(0, 1, 0, domsz))) {
        REMOVE_ABA;
      }
    }

    delegates.set0(hi_id);
    candidates.set1(hi_id);
    RECURSE(hi_id);
    return;
  }

  RECURSE(id_of3(0, 0, 1, domsz));
  REMOVE_AAB;

  if (!opt.nw_disabling) {
    RECURSE(id_of3(0, 1, 0, domsz));
    REMOVE_ABA;
  }

  RECURSE(id_of3(0, 1, 2, domsz));

#undef REMOVE_ABB
#undef REMOVE_ABA
#undef REMOVE_AAB_ABA
#undef RECURSE
}

static bool TestKnownAperiodic();

int main(int argc, char** argv)
{
  int argi = init_sysCx (&argc, &argv);
  SearchOpt opt;

  C::XFile* given_xfile = 0;
  XFileB given_xfileb;

  while (argi < argc) {
    const char* arg = argv[argi++];
    if (eq_cstr ("-domsz", arg)) {
      if (!xget_uint_cstr (&opt.domsz, argv[argi++]) || opt.domsz == 0)
        failout_sysCx("Argument Usage: -domsz <M>\nWhere <M> is a positive integer!");
    }
    else if (eq_cstr ("-bfs", arg)) {
      opt.bfs_ofile = stdout_OFile ();
      if (!xget_uint_cstr (&opt.max_depth, argv[argi++]))
        failout_sysCx("Argument Usage: -bfs <limit>\nWhere <limit> is a nonnegative integer!");
    }
    else if (eq_cstr ("-dfs-within", arg)) {
      // Only positive integers are useful, but let zero slip through.
      if (!xget_uint_cstr (&opt.dfs_threshold, argv[argi++]))
        failout_sysCx("Argument Usage: -dfs-within <threshold>\nWhere <threshold> is a positive integer!");
    }
    else if (eq_cstr ("-o", arg)) {
      arg = argv[argi++];
      opt.id_ofile = opt.id_ofileb.uopen(0, arg);
      if (!opt.id_ofile)
        failout_sysCx("Argument Usage: -o <file>\nFailed to open the <file>!");
    }
    else if (eq_cstr ("-max-depth", arg)) {
      if (!xget_uint_cstr (&opt.max_depth, argv[argi++]) || opt.max_depth == 0)
        failout_sysCx("Argument Usage: -max-depth <limit>\nWhere <limit> is a positive integer!");
    }
    else if (eq_cstr("-bound", arg) ||
             eq_cstr("-cutoff", arg)) {
      uint cutoff = 0;
      if (!xget_uint_cstr (&cutoff, argv[argi++]) || cutoff == 0)
        failout_sysCx("Argument Usage: -cutoff <limit>\nWhere <limit> is a positive integer!");
      opt.max_period = cutoff;
      opt.max_propagations = cutoff;
    }
    else if (eq_cstr ("-bdd", arg)) {
      opt.use_bdds = true;
    }
    else if (eq_cstr ("-flushoff", arg)) {
      opt.line_flush = false;
    }
    else if (eq_cstr ("-trustme", arg)) {
      opt.trust_given = true;
    }
    else if (eq_cstr ("-max-period", arg)) {
      if (!xget_uint_cstr (&opt.max_period, argv[argi++]) || opt.max_period == 0)
        failout_sysCx("Argument Usage: -max-period <limit>\nWhere <limit> is a positive integer!");
    }
    else if (eq_cstr ("-max-ppgs", arg) ||
             eq_cstr ("-max-propagations", arg)) {
      if (!xget_uint_cstr (&opt.max_propagations, argv[argi++]) || opt.max_propagations == 0)
        failout_sysCx("Argument Usage: -max-ppgs <limit>\nWhere <limit> is a positive integer!");
    }
    else if (eq_cstr ("-nw-disabling", arg)) {
      opt.nw_disabling = true;
    }
    else if (eq_cstr ("-test", arg)) {
      bool passed = TestKnownAperiodic();
      if (passed) {
        DBog0("PASS");
      }
      else {
        DBog0("FAIL");
      }
      lose_sysCx();
      return (passed ? 0 : 1);
    }
    else if (eq_cstr ("-init", arg) ||
             eq_cstr ("-id", arg)) {
      if (!argv[argi])
        failout_sysCx("Argument Usage: -init <id>");

      C::XFile xfile[1];
      init_XFile_olay_cstr (xfile, argv[argi++]);

      Table<PcState> ppgfun;
      opt.domsz = xget_b64_ppgfun(xfile, ppgfun);
      if (opt.domsz == 0) {
        failout_sysCx (0);
      }
      opt.given_acts = uniring_actions_of(ppgfun, opt.domsz);
    }
    else if (eq_cstr ("-x-init", arg)) {
      String fname = argv[argi++];
      if (!fname)
        failout_sysCx("Argument Usage: -x-init <file>");
      given_xfile = given_xfileb.uopen(0, fname);
      if (!given_xfile)
        failout_sysCx("Cannot open init file.");

      Table<PcState> ppgfun;
      opt.domsz = xget_b64_ppgfun(given_xfile, ppgfun);
      if (opt.domsz == 0) {
        failout_sysCx (0);
      }
      opt.given_acts = uniring_actions_of(ppgfun, opt.domsz);
    }
    else  {
      DBog1( "Unrecognized option: %s", arg );
      failout_sysCx (0);
    }
  }

  if (opt.domsz == 0)
    failout_sysCx("Please specify a domain size with the -domsz flag.");

  if (opt.given_acts.sz() > 0) {
    if (opt.max_depth > 0 || !!opt.bfs_ofile) {
      opt.max_depth += 1;
    }
  }
  opt.commit_domsz();

  while (true) {
    searchit(opt);

    if (!given_xfile)  break;

    Table<PcState> ppgfun;
    uint domsz = xget_b64_ppgfun(given_xfile, ppgfun);
    if (opt.domsz != domsz) {
      if (domsz == 0)  break;
      failout_sysCx ("Use the same domain size for all inputs.");
    }
    opt.given_acts = uniring_actions_of(ppgfun, opt.domsz);
  }
  lose_sysCx ();
  return 0;
}

  bool
TestKnownAperiodic()
{
  Table<UniAct> acts;
  const uint domsz = tilings_and_patterns_aperiodic_uniring(acts);

  BitTable delegates( domsz*domsz*domsz, 0 );
  for (uint i = 0; i < acts.sz(); ++i) {
    delegates.set1(id_of(acts[i], domsz));
  }

  Table<PcState> ppgfun;
  init_ppgfun(ppgfun, delegates, domsz);

  switch (livelock_semick(15, ppgfun, domsz)) {
    case Nil: DBog0("livelock: Nil");  return false;
    case May: break;
    case Yes: DBog0("livelock: Yes");  return false;
  }

  BitTable mask( domsz*domsz*domsz, 0 );
  const uint depth = acts.sz();
  Table<BitTable> candidates_stack(depth+1, mask);
  SearchOpt opt;
  opt.domsz = domsz;
  opt.max_period = 10;
  opt.check_ppg_overapprox = false;
  opt.commit_domsz();
  switch (periodic_leads_semick(delegates, ppgfun, mask,
                                candidates_stack, depth, opt)) {
    case Nil: DBog0("hard: Nil");  return false;
    case May: DBog0("hard: May");  return false;
    case Yes: break;
  }
  return true;
}


END_NAMESPACE

int main(int argc, char** argv) {
  return PROJECT_NAMESPACE::main(argc, argv);
}
