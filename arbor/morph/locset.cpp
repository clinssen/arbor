#include <algorithm>
#include <iostream>
#include <numeric>

#include <arbor/math.hpp>
#include <arbor/morph/label_parse.hpp>
#include <arbor/morph/locset.hpp>
#include <arbor/morph/morphexcept.hpp>
#include <arbor/morph/morphology.hpp>
#include <arbor/morph/mprovider.hpp>
#include <arbor/morph/primitives.hpp>
#include <arbor/morph/region.hpp>

#include "util/cbrng.hpp"
#include "util/partition.hpp"
#include "util/rangeutil.hpp"
#include "util/transform.hpp"
#include "util/span.hpp"
#include "util/strprintf.hpp"
#include "util/unique.hpp"

namespace arb {
namespace ls {

// Throw on invalid mlocation.
void assert_valid(mlocation x) {
    if (!test_invariants(x)) {
        throw invalid_mlocation(x);
    }
}

// Empty locset.

struct nil_: locset_tag {};

locset nil() {
    return locset{nil_{}};
}

mlocation_list thingify_(const nil_& x, const mprovider&) {
    return {};
}

std::ostream& operator<<(std::ostream& o, const nil_& x) {
    return o << "nil";
}

// An explicit location.

struct location_: locset_tag {
    explicit location_(mlocation loc): loc(loc) {}
    mlocation loc;
};

locset location(msize_t branch, double pos) {
    mlocation loc{branch, pos};
    assert_valid(loc);
    return locset{location_{loc}};
}

mlocation_list thingify_(const location_& x, const mprovider& p) {
    assert_valid(x.loc);
    if (x.loc.branch>=p.morphology().num_branches()) {
        throw no_such_branch(x.loc.branch);
    }
    return {x.loc};
}

std::ostream& operator<<(std::ostream& o, const location_& x) {
    return o << "(location " << x.loc.branch << " " << x.loc.pos << ")";
}

// Wrap mlocation_list (not part of public API).

struct location_list_: locset_tag {
    explicit location_list_(mlocation_list ll): ll(std::move(ll)) {}
    mlocation_list ll;
};

locset location_list(mlocation_list ll) {
    return locset{location_list_{std::move(ll)}};
}

mlocation_list thingify_(const location_list_& x, const mprovider& p) {
    auto n_branch = p.morphology().num_branches();
    for (mlocation loc: x.ll) {
        if (loc.branch>=n_branch) {
            throw no_such_branch(loc.branch);
        }
    }
    return x.ll;
}

std::ostream& operator<<(std::ostream& o, const location_list_& x) {
    o << "(sum";
    for (mlocation loc: x.ll) { o << ' ' << location_(loc); }
    return o << ')';
}

// Set of terminal points (most distal points).

struct terminal_: locset_tag {};

locset terminal() {
    return locset{terminal_{}};
}

mlocation_list thingify_(const terminal_&, const mprovider& p) {
    mlocation_list locs;
    util::assign(locs, util::transform_view(p.morphology().terminal_branches(),
        [](msize_t bid) { return mlocation{bid, 1.}; }));

    return locs;
}

std::ostream& operator<<(std::ostream& o, const terminal_& x) {
    return o << "(terminal)";
}

// Root location (most proximal point).

struct root_: locset_tag {};

locset root() {
    return locset{root_{}};
}

mlocation_list thingify_(const root_&, const mprovider& p) {
    return {mlocation{0, 0.}};
}

std::ostream& operator<<(std::ostream& o, const root_& x) {
    return o << "(root)";
}

// Locations that mark interface between segments.

struct segments_: locset_tag {};

locset segment_boundaries() {
    return locset{segments_{}};
}

mlocation_list thingify_(const segments_&, const mprovider& p) {
    return p.embedding().segment_ends();
}

std::ostream& operator<<(std::ostream& o, const segments_& x) {
    return o << "(segment_boundaries)";
}


// Proportional location on every branch.

struct on_branches_ { double pos; };

locset on_branches(double pos) {
    return locset{on_branches_{pos}};
}

mlocation_list thingify_(const on_branches_& ob, const mprovider& p) {
    msize_t n_branch = p.morphology().num_branches();

    mlocation_list locs;
    locs.reserve(n_branch);
    for (msize_t b = 0; b<n_branch; ++b) {
        locs.push_back({b, ob.pos});
    }
    return locs;
}

std::ostream& operator<<(std::ostream& o, const on_branches_& x) {
    return o << "(on_branches " << x.pos << ")";
}

// Named locset.

struct named_: locset_tag {
    explicit named_(std::string name): name(std::move(name)) {}
    std::string name;
};

locset named(std::string name) {
    return locset(named_{std::move(name)});
}

mlocation_list thingify_(const named_& n, const mprovider& p) {
    return p.locset(n.name);
}

std::ostream& operator<<(std::ostream& o, const named_& x) {
    return o << "(locset \"" << x.name << "\")";
}

// Most distal points of a region

struct most_distal_: locset_tag {
    explicit most_distal_(region reg): reg(std::move(reg)) {}
    region reg;
};

locset most_distal(region reg) {
    return locset(most_distal_{std::move(reg)});
}

mlocation_list thingify_(const most_distal_& n, const mprovider& p) {
    // Make a list of the distal ends of each cable segment.
    mlocation_list L;
    for (auto& c: thingify(n.reg, p)) {
        L.push_back({c.branch, c.dist_pos});
    }
    return maxset(p.morphology(), L);
}

std::ostream& operator<<(std::ostream& o, const most_distal_& x) {
    return o << "(distal " << x.reg << ")";
}

// Most proximal points of a region

struct most_proximal_: locset_tag {
    explicit most_proximal_(region reg): reg(std::move(reg)) {}
    region reg;
};

locset most_proximal(region reg) {
    return locset(most_proximal_{std::move(reg)});
}

mlocation_list thingify_(const most_proximal_& n, const mprovider& p) {
    // Make a list of the proximal ends of each cable segment.
    mlocation_list L;
    for (auto& c: thingify(n.reg, p)) {
        L.push_back({c.branch, c.prox_pos});
    }

    return minset(p.morphology(), L);
}

std::ostream& operator<<(std::ostream& o, const most_proximal_& x) {
    return o << "(proximal " << x.reg << ")";
}

// Boundary points of a region.
//
// The boundary points of a region R are defined as the most proximal
// and most distal locations in the components of R.

struct boundary_: locset_tag {
    explicit boundary_(region reg): reg(std::move(reg)) {}
    region reg;
};

locset boundary(region reg) {
    return locset(boundary_(std::move(reg)));
};

mlocation_list thingify_(const boundary_& n, const mprovider& p) {
    std::vector<mextent> comps = components(p.morphology(), thingify(n.reg, p));

    mlocation_list L;

    for (const mextent& comp: comps) {
        arb_assert(!comp.empty());
        arb_assert(thingify_(most_proximal_{region{comp}}, p).size()==1u);

        mlocation_list distal_set;
        util::assign(distal_set, util::transform_view(comp, [](auto c) { return dist_loc(c); }));

        L = sum(L, {prox_loc(comp.front())});
        L = sum(L, maxset(p.morphology(), distal_set));
    }
    return support(std::move(L));
}

std::ostream& operator<<(std::ostream& o, const boundary_& x) {
    return o << "(boundary " << x.reg << ")";
}

// Completed boundary points of a region.
//
// The completed boundary is the boundary of the completion of
// each component.

struct cboundary_: locset_tag {
    explicit cboundary_(region reg): reg(std::move(reg)) {}
    region reg;
};

locset cboundary(region reg) {
    return locset(cboundary_(std::move(reg)));
};

mlocation_list thingify_(const cboundary_& n, const mprovider& p) {
    std::vector<mextent> comps = components(p.morphology(), thingify(n.reg, p));

    mlocation_list L;

    for (const mextent& comp: comps) {
        mextent ccomp = thingify(reg::complete(comp), p);

        // Note: if component contains the head of a top-level cable,
        // the completion might not be connected (!).

        mlocation_list proximal_set;
        util::assign(proximal_set, util::transform_view(ccomp, [](auto c) { return prox_loc(c); }));

        mlocation_list distal_set;
        util::assign(distal_set, util::transform_view(ccomp, [](auto c) { return dist_loc(c); }));

        L = sum(L, minset(p.morphology(), proximal_set));
        L = sum(L, maxset(p.morphology(), distal_set));
    }
    return support(std::move(L));
}

std::ostream& operator<<(std::ostream& o, const cboundary_& x) {
    return o << "(cboundary " << x.reg << ")";
}

// Proportional on components of a region.

struct on_components_: locset_tag {
    explicit on_components_(double relpos, region reg):
        relpos(relpos), reg(std::move(reg)) {}
    double relpos;
    region reg;
};

locset on_components(double relpos, region reg) {
    return locset(on_components_(relpos, std::move(reg)));
}

mlocation_list thingify_(const on_components_& n, const mprovider& p) {
    if (n.relpos<0 || n.relpos>1) {
        return {};
    }

    std::vector<mextent> comps = components(p.morphology(), thingify(n.reg, p));
    std::vector<mlocation> L;

    for (const mextent& comp: comps) {
        arb_assert(!comp.empty());
        arb_assert(thingify_(most_proximal_{region{comp}}, p).size()==1u);

        mlocation prox = prox_loc(comp.front());
        auto d_from_prox = [&](mlocation x) { return p.embedding().integrate_length(prox, x); };

        if (n.relpos==0) {
            L.push_back(prox);
        }
        else if (n.relpos==1) {
            double diameter = 0;
            mlocation_list most_distal = {prox};

            for (mcable c: comp) {
                mlocation x = dist_loc(c);
                double d = d_from_prox(x);

                if (d>diameter) {
                    most_distal = {x};
                    diameter = d;
                }
                else if (d==diameter) {
                    most_distal.push_back(x);
                }
            }

            util::append(L, most_distal);
        }
        else {
            double diameter = util::max_value(util::transform_view(comp,
                [&](auto c) { return d_from_prox(dist_loc(c)); }));

            double d = n.relpos*diameter;
            for (mcable c: comp) {
                double d0 = d_from_prox(prox_loc(c));
                double d1 = d_from_prox(dist_loc(c));

                if (d0<=d && d<=d1) {
                    double s = d0==d1? 0: (d-d0)/(d1-d0);
                    s = std::min(1.0, std::fma(s, c.dist_pos-c.prox_pos, c.prox_pos));
                    L.push_back(mlocation{c.branch, s});
                }
            }

        }
    }

    util::sort(L);
    return L;
}

std::ostream& operator<<(std::ostream& o, const on_components_& x) {
    return o << "(on-components " << x.relpos << " " << x.reg << ")";
}

// Uniform locset.

struct uniform_ {
    region reg;
    unsigned left;
    unsigned right;
    uint64_t seed;
};

locset uniform(arb::region reg, unsigned left, unsigned right, uint64_t seed) {
    return locset(uniform_{reg, left, right, seed});
}

mlocation_list thingify_(const uniform_& u, const mprovider& p) {
    mlocation_list L;
    auto morpho = p.morphology();
    auto embed = p.embedding();

    // Thingify the region and store relevant data
    mextent reg_extent = thingify(u.reg, p);
    const mcable_list& reg_cables = reg_extent.cables();

    std::vector<double> lengths_bounds;
    auto lengths_part = util::make_partition(lengths_bounds,
                                       util::transform_view(reg_cables, [&embed](const auto& c) {
                                           return embed.integrate_length(c);
                                       }));

    auto region_length = lengths_part.bounds().second;

    // Generate uniform random positions along the extent of the full region
    auto random_pos = util::uniform(u.seed, u.left, u.right);
    std::transform(random_pos.begin(), random_pos.end(), random_pos.begin(),
            [&region_length](auto& c){return c*region_length;});
    util::sort(random_pos);

    // Match random_extents to cables and find position on the associated branch
    unsigned cable_idx = 0;
    auto range = lengths_part[cable_idx];

    for (auto e: random_pos) {
        while (e > range.second) {
            range = lengths_part[++cable_idx];
        }
        auto cable = reg_cables[cable_idx];
        auto pos_on_cable = (e - range.first)/(range.second - range.first);
        auto pos_on_branch = math::lerp(cable.prox_pos, cable.dist_pos, pos_on_cable);
        L.push_back({cable.branch, pos_on_branch});
    }

    return L;
}

std::ostream& operator<<(std::ostream& o, const uniform_& u) {
    return o << "(uniform " << u.reg << " " << u.left << " " << u.right << " " << u.seed << ")";
}

// Intersection of two point sets.

struct land: locset_tag {
    locset lhs;
    locset rhs;
    land(locset lhs, locset rhs): lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

mlocation_list thingify_(const land& P, const mprovider& p) {
    return intersection(thingify(P.lhs, p), thingify(P.rhs, p));
}

std::ostream& operator<<(std::ostream& o, const land& x) {
    return o << "(intersect " << x.lhs << " " << x.rhs << ")";
}

// Union of two point sets.

struct lor: locset_tag {
    locset lhs;
    locset rhs;
    lor(locset lhs, locset rhs): lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

mlocation_list thingify_(const lor& P, const mprovider& p) {
    return join(thingify(P.lhs, p), thingify(P.rhs, p));
}

std::ostream& operator<<(std::ostream& o, const lor& x) {
    return o << "(join " << x.lhs << " " << x.rhs << ")";
}

// Sum of two point sets.

struct lsum: locset_tag {
    locset lhs;
    locset rhs;
    lsum(locset lhs, locset rhs): lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

mlocation_list thingify_(const lsum& P, const mprovider& p) {
    return sum(thingify(P.lhs, p), thingify(P.rhs, p));
}

std::ostream& operator<<(std::ostream& o, const lsum& x) {
    return o << "(sum " << x.lhs << " " << x.rhs << ")";
}

// Support of point set.

struct lsup_: locset_tag {
    locset arg;
    lsup_(locset arg): arg(std::move(arg)) {}
};

locset support(locset arg) {
    return locset{lsup_{std::move(arg)}};
}

mlocation_list thingify_(const lsup_& P, const mprovider& p) {
    return support(thingify(P.arg, p));
};

std::ostream& operator<<(std::ostream& o, const lsup_& x) {
    return o << "(support " << x.arg << ")";
}

// Restrict a locset on to a region: returns all locations in the locset that
// are also in the region.

struct lrestrict_ {
    locset ls;
    region reg;
};

mlocation_list thingify_(const lrestrict_& P, const mprovider& p) {
    mlocation_list L;

    auto cables = thingify(P.reg, p).cables();
    auto ends = util::transform_view(cables, [](const auto& c){return mlocation{c.branch, c.dist_pos};});

    for (auto l: thingify(P.ls, p)) {
        auto it = std::lower_bound(ends.begin(), ends.end(), l);
        if (it==ends.end()) continue;
        const auto& c = cables[std::distance(ends.begin(), it)];
        if (c.branch==l.branch && c.prox_pos<=l.pos) {
            L.push_back(l);
        }
    }

    return L;
}

locset restrict(locset ls, region reg) {
    return locset{lrestrict_{std::move(ls), std::move(reg)}};
}

std::ostream& operator<<(std::ostream& o, const lrestrict_& x) {
    return o << "(restrict " << x.ls << " " << x.reg << ")";
}

} // namespace ls

// The intersect and join operations in the arb:: namespace with locset so that
// ADL allows for construction of expressions with locsets without having
// to namespace qualify the intersect/join.

locset intersect(locset lhs, locset rhs) {
    return locset(ls::land(std::move(lhs), std::move(rhs)));
}

locset join(locset lhs, locset rhs) {
    return locset(ls::lor(std::move(lhs), std::move(rhs)));
}

locset sum(locset lhs, locset rhs) {
    return locset(ls::lsum(std::move(lhs), std::move(rhs)));
}

// Implicit constructors.

locset::locset() {
    *this = ls::nil();
}

locset::locset(mlocation loc) {
    *this = ls::location(loc.branch, loc.pos);
}

locset::locset(mlocation_list ll) {
    *this = ls::location_list(std::move(ll));
}

locset::locset(const std::string& desc) {
    if (auto r=parse_locset_expression(desc)) {
        *this = *r;
    }
    else {
        throw r.error();
    }
}

locset::locset(const char* label): locset(std::string(label)) {}

} // namespace arb
