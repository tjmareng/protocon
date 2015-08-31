
extern "C" {
#include "cx/syscx.h"
}
#include "prot-ofile.hh"
#include "cx/fileb.hh"

extern "C" {
#include "cx/ospc.h"
}

  void
oput_pla_val (Cx::OFile& of, uint j, uint n)
{
  for (uint i = 0; i < n; ++i)
    of << (i == j ? 1 : 0);
}

  void
oput_pla_act (Cx::OFile& of, const Xn::ActSymm& act)
{
  const Xn::PcSymm& pc_symm = *act.pc_symm;
  for (uint i = 0; i < pc_symm.rvbl_symms.sz(); ++i) {
    const Xn::VblSymm& vbl_symm = *pc_symm.rvbl_symms[i];
    if (vbl_symm.pure_shadow_ck())
      continue;
    of << ' ';
    oput_pla_val (of, act.guard(i), vbl_symm.domsz);
  }
  for (uint i = 0; i < pc_symm.wvbl_symms.sz(); ++i) {
    const Xn::VblSymm& vbl_symm = *pc_symm.wvbl_symms[i];
    of << ' ';
    oput_pla_val (of, act.assign(i), vbl_symm.domsz);
  }
}

  void
oput_pla_pc_acts (Cx::OFile& of, const Xn::PcSymm& pc_symm,
                  const Cx::Table<Xn::ActSymm>& acts)
{
  Claim2( pc_symm.wvbl_symms.sz() ,==, pc_symm.wmap.sz() );

  uint nrvbls = 0;
  for (uint i = 0; i < pc_symm.rvbl_symms.sz(); ++i) {
    if (!pc_symm.rvbl_symms[i]->pure_shadow_ck())
      nrvbls += 1;
  }

  of << ".mv " << (nrvbls + pc_symm.wmap.sz()) << " 0";
  for (uint i = 0; i < pc_symm.rvbl_symms.sz(); ++i) {
    const Xn::VblSymm& vbl_symm = *pc_symm.rvbl_symms[i];
    if (vbl_symm.pure_shadow_ck())
      continue;
    of << ' ' << vbl_symm.domsz;
  }
  for (uint i = 0; i < pc_symm.wvbl_symms.sz(); ++i) {
    const Xn::VblSymm& vbl_symm = *pc_symm.wvbl_symms[i];
    of << ' ' << vbl_symm.domsz;
  }
  of << '\n';

  of << '#';
  for (uint i = 0; i < pc_symm.rvbl_symms.sz(); ++i) {
    if (pc_symm.rvbl_symms[i]->pure_shadow_ck())
      continue;
    of << ' ' << pc_symm.vbl_name(i);
  }

  for (uint i = 0; i < pc_symm.wmap.sz(); ++i)
    of << ' ' << pc_symm.vbl_name(pc_symm.wmap[i]) << '\'';
  of << '\n';

  for (uint i = 0; i < acts.sz(); ++i) {
    const Xn::ActSymm& act = acts[i];
    if (act.pc_symm == &pc_symm) {
      oput_pla_act (of, act);
      of << '\n';
    }
  }
  of << ".e\n";
}

static
  void
oput_protocon_constants (Cx::OFile& of, const Xn::Spec& spec)
{
  for (uint i = 0; i < spec.constant_map.keys.sz(); ++i) {
    of << "constant " << spec.constant_map.keys[i];
    of << " := " << spec.constant_map.vals[i].expression;
    of << ";\n";
  }
}

static
  void
oput_protocon_pc_lets (Cx::OFile& of, const Xn::PcSymm& pc_symm)
{
  for (uint i = 0; i < pc_symm.spec->let_map.keys.sz(); ++i) {
    of << "  let " << pc_symm.spec->let_map.keys[i];
    of << " := " << pc_symm.spec->let_map.vals[i].expression;
    of << ";\n";
  }
}

static
  void
oput_protocon_pc_predicates (Cx::OFile& of, const Xn::PcSymm& pc_symm)
{
  for (uint i = 0; i < pc_symm.predicate_map.keys.sz(); ++i) {
    of << "  predicate " << pc_symm.predicate_map.keys[i];
    of << " := " << pc_symm.predicate_map.vals[i].expression;
    of << ";\n";
  }
}

static
  void
oput_protocon_pc_vbls (Cx::OFile& of, const Xn::PcSymm& pc_symm)
{
  for (uint i = 0; i < pc_symm.rvbl_symms.sz(); ++i) {
    bool symmetric_link_case = false;
    for (uint j = 0; j < pc_symm.spec->link_symmetries.sz(); ++j) {
      const Xn::LinkSymmetry& link_symmetry = pc_symm.spec->link_symmetries[j];
      if (link_symmetry.elem_ck(i)) {
        of << "  symmetric " << link_symmetry.let_expression
          << " <- {# " << link_symmetry.multiset_expression << " #}\n";
        of << "  {\n";
        for (uint v = 0; v < link_symmetry.nvbls; ++v) {
          uint vidx = link_symmetry(v, 0);
          of << "    "
            << (pc_symm.spec->random_write_flags[vidx] ? "random " : "")
            << (pc_symm.write_flags[vidx] ? "write" : "read") << ": "
            << pc_symm.spec->rvbl_symms[vidx]->name
            << "[" << link_symmetry.index_expressions[v] << "];\n";
        }
        of << "  }\n";
        i += link_symmetry.nlinks * link_symmetry.nvbls - 1;
        symmetric_link_case = true;
        break;
      }
    }
    if (symmetric_link_case)  continue;
    of << "  "
      << (pc_symm.spec->random_write_flags[i] ? "random " : "")
      << (pc_symm.write_flags[i] ? "write" : "read") << ": "
      << pc_symm.vbl_name(i)
      << ";\n";
  }
}

  bool
oput_protocon_pc_act (Cx::OFile& of, XFile* xf,
                      const Cx::Table<Cx::String>& guard_vbls,
                      const Cx::Table<Cx::String>& assign_vbls)
{
  DeclLegit( good );
  bool clause = false;
  of << "    ( ";
  for (uint i = 0;
       good && i < (guard_vbls.sz() + assign_vbls.sz());
       ++i)
  {
    const char* tok;
    DoLegitLineP(tok, "read token")
      nextok_XFile (xf, 0, 0);

    uint m = 0;
    Cx::Table<uint> vals;
    while (good && tok[m])
    {
      Claim( tok[m] == '0' || tok[m] == '1' );
      if (tok[m] == '1')
        vals.push(m);
      ++ m;
    }

    if (i >= guard_vbls.sz()) {
      if (i == guard_vbls.sz()) {
        of << " -->";
      }
      Claim2( vals.sz() ,==, 1 );
      of << ' ' << assign_vbls[i-guard_vbls.sz()] << ":=" << vals[0] << ';';
      continue;
    }

    if (vals.sz() == m)  continue;

    if (clause)
      of << " && ";
    clause = true;

    if (vals.sz() == m-1 && m > 2) {
      for (uint j = 0; j < m; ++j) {
        if (!vals.elem_ck(j)) {
          of << guard_vbls[i] << "!=" << j;
          break;
        }
      }
      continue;
    }

    if (vals.sz() > 1)  of << "(";
    for (uint j = 0; good && j < vals.sz(); ++j) {
      if (j > 0)  of << " || ";
      of << guard_vbls[i] << "==" << vals[j];
    }
    if (vals.sz() > 1)  of << ")";
  }
  of << " )\n";
  return good;
}

  void
oput_protocon_pc_act (Cx::OFile& of, const Xn::ActSymm& act)
{
  of << "    ( ";
  const Xn::PcSymm& pc_symm = *act.pc_symm;

  bool need_delim = false;
  for (uint i = 0; i < pc_symm.rvbl_symms.sz(); ++i) {
    if (pc_symm.rvbl_symms[i]->pure_shadow_ck())
      continue;

    if (need_delim)
      of << " && ";
    else
      need_delim = true;
    of << pc_symm.vbl_name(i) << "==" << act.guard(i);
  }
  of << " --> ";
  bool puppet_self_loop = act.puppet_self_loop_ck();
  for (uint i = 0; i < pc_symm.wvbl_symms.sz(); ++i) {
    if (puppet_self_loop && !pc_symm.wvbl_symms[i]->pure_shadow_ck())
      continue;
    of << pc_symm.vbl_name(pc_symm.wmap[i]) << ":=";
    if (pc_symm.spec->random_write_flags[pc_symm.wmap[i]])
      of << "_; ";
    else
      of << act.assign(i) << "; ";
  }
  of << ")\n";
}

  bool
oput_protocon_pc_acts (Cx::OFile& of, const Xn::PcSymm& pc_symm,
                       const Cx::Table<Xn::ActSymm>& acts,
                       OSPc* ospc,
                       bool use_espresso)
{
  DeclLegit( good );
  const Xn::PcSymmSpec& pc_symm_spec = *pc_symm.spec;
  if (pc_symm_spec.shadow_act_strings.sz() > 0) {
    of << "  shadow action:\n";
    for (uint i = 0; i < pc_symm_spec.shadow_act_strings.sz(); ++i) {
      of << "    ( " << pc_symm_spec.shadow_act_strings[i] << " )\n";
    }
    of << "    ;\n";
  }
  if (pc_symm_spec.permit_act_strings.sz() > 0) {
    of << "  permit action:\n";
    for (uint i = 0; i < pc_symm_spec.permit_act_strings.sz(); ++i) {
      of << "    ( " << pc_symm_spec.permit_act_strings[i] << " )\n";
    }
    of << "    ;\n";
  }
  if (pc_symm_spec.forbid_act_strings.sz() > 0) {
    of << "  forbid action:\n";
    for (uint i = 0; i < pc_symm_spec.forbid_act_strings.sz(); ++i) {
      of << "    ( " << pc_symm_spec.forbid_act_strings[i] << " )\n";
    }
    of << "    ;\n";
  }

  // Return early if there are no puppet actions.
  {
    bool have_actions = false;
    for (uint i = 0; i < acts.sz(); ++i) {
      if (acts[i].pc_symm == &pc_symm) {
        have_actions = true;
        break;
      }
    }
    if (!have_actions)
      return true;
  }

  // Names for variables.
  Cx::Table<Cx::String> guard_vbls;
  Cx::Table<Cx::String> assign_vbls( pc_symm.wmap.sz() );
  for (uint i = 0; i < pc_symm.rvbl_symms.sz(); ++i) {
    if (pc_symm.rvbl_symms[i]->pure_shadow_ck())
      continue;
    guard_vbls.push(pc_symm.vbl_name(i));
  }
  for (uint i = 0; i < pc_symm.wvbl_symms.sz(); ++i) {
    assign_vbls[i] = pc_symm.vbl_name(pc_symm.wmap[i]);
  }

  of << "  puppet action:\n";
  if (!use_espresso) {
    for (uint i = 0; i < acts.sz(); ++i) {
      if (acts[i].pc_symm == &pc_symm) {
        oput_protocon_pc_act (of, acts[i]);
      }
    }
    of << "    ;\n";
    return true;
  }

  DoLegitLine( "spawn espresso" )
    spawn_OSPc (ospc);

  if (good) {
    Cx::OFile espresso_xf(ospc->of);
    oput_pla_pc_acts (espresso_xf, pc_symm, acts);
    close_OFile (ospc->of);
  }

  while (good && !skip_cstr_XFile (ospc->xf, ".p"))
  {
    DoLegitLine("get line")
      !!getline_XFile (ospc->xf);
  }

  uint n = 0;
  DoLegitLine("read number of lines from espresso")
    xget_uint_XFile (ospc->xf, &n);

  getline_XFile (ospc->xf);

  for (uint i = 0; good && i < n; ++i) {
    XFile olay[1];

    DoLegitLine("get line from espresso")
      getlined_olay_XFile (olay, ospc->xf, "\n");

    DoLegitLine(0)
      oput_protocon_pc_act (of, olay, guard_vbls, assign_vbls);
  }
  of << "    ;\n";

  close_OSPc (ospc);
  return good;
}

static
  bool
oput_protocon_pc_assume (Cx::OFile& of, const Xn::PcSymm& pc_symm)
{
  const Cx::String& assume_expression = pc_symm.spec->closed_assume_expression;
  if (assume_expression.empty_ck())
    return true;
  of << "  (assume & closed)\n    (" << assume_expression << ");\n";
  return true;
}

static
  Cx::String
string_of_invariant_style (Xn::InvariantStyle style, Xn::InvariantScope scope)
{
  Cx::String s = "";
  const char* pfx = "";
  const char* sfx = "";

  switch (scope)
  {
    case Xn::DirectInvariant:
      break;
    case Xn::ShadowInvariant:
      s = "(";
      sfx = " % puppet)";
      break;
    case Xn::FutureInvariant:
      pfx = "future ";
      break;
    case Xn::NInvariantScopes:
      Claim( 0 );
  }

  s << "(future & " << pfx;
  switch (style)
  {
    case Xn::FutureAndClosed:
      s << "closed";
      break;
    case Xn::FutureAndSilent:
      s << "silent";
      break;
    case Xn::FutureAndShadow:
      s << "shadow";
      break;
    case Xn::FutureAndActiveShadow:
      s << "active shadow";
      break;
    case Xn::NInvariantStyles:
      Claim( 0 );
  }
  s << ")" << sfx;
  return s;
}

static
  const char*
string_of_invariant_behav (Xn::InvariantBehav behav)
{
  switch (behav)
  {
    case Xn::FutureSilent:
      return "future silent";
    case Xn::FutureActiveShadow:
      return "future active shadow";
    case Xn::NInvariantBehavs:
      return 0;
  }
  return 0;
}

static
  bool
oput_protocon_pc_invariant (Cx::OFile& of, const Xn::PcSymm& pc_symm,
                            const Cx::String& style_str)
{
  const Cx::String& invariant_expression = pc_symm.spec->invariant_expression;
  if (invariant_expression.empty_ck())
    return true;

  of << "  " << style_str
    << "\n    (" << invariant_expression << ");\n";
  return true;
}

  bool
oput_protocon_file (Cx::OFile& of, const Xn::Sys& sys,
                    const Xn::Net& o_topology,
                    const vector<uint>& actions,
                    bool use_espresso,
                    const char* comment)
{
  DeclLegit( good );
  OSPc ospc[1];
  *ospc = dflt_OSPc ();

  stdxpipe_OSPc (ospc);
  stdopipe_OSPc (ospc);
  ospc->cmd = cons1_AlphaTab ("espresso");
  /* Using -Dexact can take a long time.*/
  //PushTable( ospc->args, cons1_AlphaTab ("-Dexact") );

  if (false) {
    // Use this to capture espresso input/output.
    ospc->cmd = cons1_AlphaTab ("sh");
    PushTable( ospc->args, cons1_AlphaTab ("-c") );
    PushTable( ospc->args, cons1_AlphaTab ("tee in.pla | espresso | tee out.pla") );
  }

  const Xn::Net& topo = sys.topology;
  if (comment) {
    of << "// " << comment << "\n";
  }
  oput_protocon_constants (of, *o_topology.spec);
  for (uint i = 0; i < topo.vbl_symms.sz(); ++i) {
    const Xn::VblSymm& vbl_symm = topo.vbl_symms[i];
    if (vbl_symm.pure_shadow_ck())
      of << "shadow ";
    if (vbl_symm.pure_puppet_ck())
      of << "puppet ";

    of << "variable " << vbl_symm.spec->name
      << "[Nat % " << vbl_symm.spec->nmembs_expression
      << "] <- Nat % " << vbl_symm.spec->domsz_expression << ";\n";
  }

  for (uint i = 0; i < sys.predicate_map.sz(); ++i) {
    of << "predicate " << sys.predicate_map.keys[i]
      << " :=\n  " << sys.predicate_map.expressions[i] << ";\n";
  }

  if (!!sys.spec->closed_assume_expression) {
    of << "(assume & closed)\n  (" << sys.spec->closed_assume_expression << ")\n  ;\n";
  }

  Cx::String style_str =
    string_of_invariant_style (sys.spec->invariant_style,
                               sys.spec->invariant_scope);
  if (!sys.spec->invariant_expression.empty_ck()) {
    Cx::String legit_str = sys.spec->invariant_expression;
    of << style_str << "\n  (" << legit_str << ")\n  ;\n";
  }

  if (sys.spec->invariant_behav != Xn::NInvariantBehavs) {
    of << string_of_invariant_behav (sys.spec->invariant_behav) << ";\n";
  }


  Cx::Table<Xn::ActSymm> acts;
  for (uint i = 0; i < actions.size(); ++i) {
    for (uint j = 0; j < sys.topology.represented_actions[actions[i]].sz(); ++j) {
      sys.topology.action(acts.grow1(), sys.topology.represented_actions[actions[i]][j]);
    }
  }

  for (uint i = 0; i < sys.topology.pc_symms.sz(); ++i) {
    const Xn::PcSymm& pc_symm = sys.topology.pc_symms[i];
    of << "process " << pc_symm.spec->name
      << "[" << pc_symm.spec->idx_name << " <- Nat % "
      << o_topology.pc_symms[i].spec->nmembs_expression << "]\n";
    of << "{\n";
    oput_protocon_pc_lets (of, pc_symm);
    oput_protocon_pc_vbls (of, pc_symm);
    oput_protocon_pc_predicates (of, pc_symm);
    DoLegitLine("output assume")
      oput_protocon_pc_assume (of, pc_symm);
    DoLegitLine("output invariant")
      oput_protocon_pc_invariant (of, pc_symm, style_str);
    DoLegitLine("output actions")
      oput_protocon_pc_acts (of, pc_symm, acts, ospc, use_espresso);
    of << "}\n";
  }

  lose_OSPc (ospc);
  return good;
}

  bool
oput_protocon_file (Cx::OFile& of, const Xn::Sys& sys, const vector<uint>& actions,
                    bool use_espresso, const char* comment)
{
  return oput_protocon_file (of, sys, sys.topology, actions, use_espresso, comment);
}

  bool
oput_protocon_file (Cx::OFile& of, const Xn::Sys& sys,
                    bool use_espresso, const char* comment)
{
  return oput_protocon_file (of, sys, sys.actions, use_espresso, comment);
}

  bool
oput_protocon_file (const Cx::String& ofilename,
                    const Xn::Sys& sys,
                    const Xn::Net& o_topology,
                    const vector<uint>& actions,
                    bool use_espresso,
                    const char* comment)
{
  Cx::OFileB ofb;
  Cx::OFile ofile = ofb.uopen(ofilename);
  if (!ofile)  return false;
  return oput_protocon_file (ofile, sys, o_topology, actions, use_espresso, comment);
}

  bool
oput_protocon_file (const Cx::String& ofilename, const Xn::Sys& sys,
                    bool use_espresso,
                    const char* comment)
{
  return oput_protocon_file (ofilename, sys, sys.topology, sys.actions, use_espresso, comment);
}

