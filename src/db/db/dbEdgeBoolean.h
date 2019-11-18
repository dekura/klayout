
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2019 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef HDR_dbEdgeBoolean
#define HDR_dbEdgeBoolean

#include "dbEdge.h"
#include "dbBoxScanner.h"

#include "tlIntervalMap.h"

namespace db
{

/**
 *  @brief A common definition for the boolean operations available on edges
 */
enum EdgeBoolOp { EdgeOr, EdgeNot, EdgeXor, EdgeAnd, EdgeIntersections };

struct OrJoinOp
{
  void operator() (int &v, int n)
  {
    v += n;
  }
};

struct AndJoinOp
{
  void operator() (int &v, int n)
  {
    if (n == 0) {
      v = 0;
    }
  }
};

struct NotJoinOp
{
  void operator() (int &v, int n)
  {
    if (n != 0) {
      v = 0;
    }
  }
};

struct XorJoinOp
{
  void operator() (int &v, int n)
  {
    if (n != 0) {
      if (v == 0) {
        v = (n > 0 ? 1 : -1);
      } else {
        v = 0;
      }
    }
  }
};

template <class OutputContainer>
struct EdgeBooleanCluster 
  : public db::cluster<db::Edge, size_t>
{
  typedef db::Edge::coord_type coord_type;

  EdgeBooleanCluster (OutputContainer *output, EdgeBoolOp op)
    : mp_output (output), m_op (op)
  {
    //  .. nothing yet ..
  }

  void finish ()
  {
    //  determine base edge (longest overall edge)

    //  shortcut for single edge
    if (begin () + 1 == end ()) {
      if (begin ()->second == 0) {
        if (m_op != EdgeAnd) {
          mp_output->insert (*(begin ()->first));
        }
      } else {
        if (m_op != EdgeAnd && m_op != EdgeNot) {
          mp_output->insert (*(begin ()->first));
        }
      }
      return;
    }

    db::Edge r = *begin ()->first;
    double l1 = 0.0, l2 = r.double_length ();
    double n = 1.0 / l2;
    db::Point p1 = r.p1 (), p2 = r.p2 ();

    for (iterator o = begin () + 1; o != end (); ++o) {
      double ll1 = db::sprod (db::Vector (o->first->p1 () - r.p1 ()), r.d ()) * n;
      double ll2 = db::sprod (db::Vector (o->first->p2 () - r.p1 ()), r.d ()) * n;
      if (ll1 < l1) {
        p1 = o->first->p1 ();
        l1 = ll1;
      }
      if (ll2 < l1) {
        p1 = o->first->p2 ();
        l1 = ll2;
      }
      if (ll1 > l2) {
        p2 = o->first->p1 ();
        l2 = ll1;
      }
      if (ll2 > l2) {
        p2 = o->first->p2 ();
        l2 = ll2;
      }
    }

    db::Vector d = db::Vector (p2 - p1);
    n = 1.0 / d.double_length ();

    OrJoinOp or_jop;
    AndJoinOp and_jop;
    NotJoinOp not_jop;
    XorJoinOp xor_jop;

    tl::interval_map<db::Coord, int> a, b;
    a.add (0, db::coord_traits<db::Coord>::rounded (d.double_length ()), 0, or_jop);
    b.add (0, db::coord_traits<db::Coord>::rounded (d.double_length ()), 0, or_jop);

    for (iterator o = begin (); o != end (); ++o) {
      db::Coord l1 = db::coord_traits<db::Coord>::rounded (db::sprod (db::Vector (o->first->p1 () - p1), d) * n);
      db::Coord l2 = db::coord_traits<db::Coord>::rounded (db::sprod (db::Vector (o->first->p2 () - p1), d) * n);
      if (o->second == 0 || m_op == EdgeOr) {
        if (l1 < l2) {
          a.add (l1, l2, 1, or_jop);
        } else if (l1 > l2) {
          a.add (l2, l1, -1, or_jop);
        }
      } else {
        if (l1 < l2) {
          b.add (l1, l2, 1, or_jop);
        } else {
          b.add (l2, l1, -1, or_jop);
        }
      }
    }

    tl::interval_map<db::Coord, int> q;
    for (tl::interval_map<db::Coord, int>::const_iterator ia = a.begin (); ia != a.end (); ++ia) {
      q.add (ia->first.first, ia->first.second, ia->second > 0 ? 1 : (ia->second < 0 ? -1 : 0), or_jop);
    }

    if (b.begin () == b.end ()) {

      //  optimize for empty b
      if (m_op != EdgeAnd) {
        for (tl::interval_map<db::Coord, int>::const_iterator ib = b.begin (); ib != b.end (); ++ib) {
          if (ib->second > 0) {
            mp_output->insert (db::Edge (p1 + db::Vector (d * (ib->first.first * n)), p1 + db::Vector (d * (ib->first.second * n))));
          } else if (ib->second < 0) {
            mp_output->insert (db::Edge (p1 + db::Vector (d * (ib->first.second * n)), p1 + db::Vector (d * (ib->first.first * n))));
          }
        }
      }

    } else {

      if (m_op == EdgeAnd) {
        for (tl::interval_map<db::Coord, int>::const_iterator ib = b.begin (); ib != b.end (); ++ib) {
          q.add (ib->first.first, ib->first.second, ib->second, and_jop);
        }
      } else if (m_op == EdgeNot) {
        for (tl::interval_map<db::Coord, int>::const_iterator ib = b.begin (); ib != b.end (); ++ib) {
          q.add (ib->first.first, ib->first.second, ib->second, not_jop);
        }
      } else if (m_op == EdgeXor) {
        for (tl::interval_map<db::Coord, int>::const_iterator ib = b.begin (); ib != b.end (); ++ib) {
          q.add (ib->first.first, ib->first.second, ib->second, xor_jop);
        }
      }

      for (tl::interval_map<db::Coord, int>::const_iterator iq = q.begin (); iq != q.end (); ++iq) {
        if (iq->second > 0) {
          mp_output->insert (db::Edge (p1 + db::Vector (d * (iq->first.first * n)), p1 + db::Vector (d * (iq->first.second * n))));
        } else if (iq->second < 0) {
          mp_output->insert (db::Edge (p1 + db::Vector (d * (iq->first.second * n)), p1 + db::Vector (d * (iq->first.first * n))));
        }
      }

    }

  }

private:
  OutputContainer *mp_output;
  db::EdgeBoolOp m_op;
};

template <class OutputContainer>
struct EdgeBooleanClusterCollector
  : public db::cluster_collector<db::Edge, size_t, EdgeBooleanCluster<OutputContainer> >
{
  EdgeBooleanClusterCollector (OutputContainer *output, EdgeBoolOp op)
    : db::cluster_collector<db::Edge, size_t, EdgeBooleanCluster<OutputContainer> > (EdgeBooleanCluster<OutputContainer> (output, op == EdgeIntersections ? EdgeAnd : op), op != EdgeAnd && op != EdgeIntersections /*report single*/),
      mp_intersections (op == EdgeIntersections ? output : 0)
  {
    //  .. nothing yet ..
  }

  EdgeBooleanClusterCollector (OutputContainer *output, OutputContainer *intersections, EdgeBoolOp op)
    : db::cluster_collector<db::Edge, size_t, EdgeBooleanCluster<OutputContainer> > (EdgeBooleanCluster<OutputContainer> (output, op), op != EdgeAnd /*report single*/),
      mp_intersections (intersections)
  {
    //  .. nothing yet ..
  }

  void add (const db::Edge *o1, size_t p1, const db::Edge *o2, size_t p2)
  {
    //  Select edges which are:
    //  1.) not degenerate
    //  2.) parallel with some tolerance of roughly 1 dbu
    //  3.) connected
    //  In intersection-detection mode, identify intersection points otherwise
    //  and insert into the intersections container as degenerated edges.

    if (! o1->is_degenerate () && ! o2->is_degenerate () 
        && fabs ((double) db::vprod (*o1, *o2)) < db::coord_traits<db::Coord>::prec_distance () * std::min (o1->double_length (), o2->double_length ())
        && (o1->p1 () == o2->p1 () || o1->p1 () == o2->p2 () || o1->p2 () == o2->p1 () || o1->p2 () == o2->p2 () || o1->coincident (*o2))) {

      db::cluster_collector<db::Edge, size_t, EdgeBooleanCluster<OutputContainer> >::add (o1, p1, o2, p2);

    } else if (mp_intersections && p1 != p2) {

      std::pair<bool, db::Point> ip = o1->intersect_point (*o2);
      if (ip.first) {
        mp_intersections->insert (db::Edge (ip.second, ip.second));
      }

    }
  }

private:
  OutputContainer *mp_intersections;
};

}

#endif

