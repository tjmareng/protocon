/**
 * \file inst-matching.c
 * Maximal matching instance!
 **/

static
    XnSys
inst_matching_XnSys (uint npcs)
{
    DeclTable( XnDomSz, vs );
    XnSys sys[] = default;
    OFile name[] = default;

    /* Make processes and variables.*/
    {:for (r ; npcs)
        XnVbl vbl = dflt_XnVbl ();

        vbl.domsz = 3;

        flush_OFile (name);
        printf_OFile (name, "m%u", r);
        copy_AlphaTab_OFile (&vbl.name, name);

        PushTable( sys->pcs, dflt_XnPc () );
        PushTable( sys->vbls, vbl );
    }

    /* Make bidirectional ring topology.*/
    {:for (r ; npcs)
        assoc_XnSys (sys, r, r, Yes);
        assoc_XnSys (sys, r, dec1mod (r, npcs), May);
        assoc_XnSys (sys, r, inc1mod (r, npcs), May);
    }

    accept_topology_XnSys (sys);

    {:for (sidx ; sys->nstates)
        bool good = true;
        statevs_of_XnSys (&vs, sys, sidx);
        {:for (r ; npcs)
            bool self =
                (vs.s[r] == 0) &&
                (vs.s[dec1mod (r, npcs)] == 1) &&
                (vs.s[inc1mod (r, npcs)] == 2);
            bool left =
                (vs.s[r] == 1) &&
                (vs.s[dec1mod (r, npcs)] == 2);
            bool right =
                (vs.s[r] == 2) &&
                (vs.s[inc1mod (r, npcs)] == 1);
            good = (self || left || right);
            if (!good)  break;
        }
        setb_BitTable (sys->legit, sidx, good);
    }

    lose_OFile (name);
    LoseTable( vs );
    return *sys;
}

