#include "../gtest.h"

#include <any>
#include <vector>

#include <arbor/common_types.hpp>
#include <arbor/domain_decomposition.hpp>
#include <arbor/load_balance.hpp>
#include <arbor/cable_cell.hpp>
#include <arbor/recipe.hpp>
#include <arbor/simulation.hpp>

#include <arborenv/concurrency.hpp>

#include "util/span.hpp"

#include "../common_cells.hpp"

using namespace arb;
using arb::util::make_span;

namespace {
    class custom_recipe: public recipe {
    public:
        custom_recipe(std::vector<cable_cell> cells,
                      std::vector<cell_size_type> num_sources,
                      std::vector<cell_size_type> num_targets,
                      std::vector<std::vector<cell_connection>> conns,
                      std::vector<std::vector<gap_junction_connection>> gjs,
                      std::vector<std::vector<arb::event_generator>> gens):
            num_cells_(cells.size()),
            num_sources_(num_sources),
            num_targets_(num_targets),
            connections_(conns),
            gap_junctions_(gjs),
            event_generators_(gens),
            cells_(cells) {}

        cell_size_type num_cells() const override {
            return num_cells_;
        }
        arb::util::unique_any get_cell_description(cell_gid_type gid) const override {
            return cells_[gid];
        }
        cell_kind get_cell_kind(cell_gid_type gid) const override {
            return cell_kind::cable;
        }
        std::vector<gap_junction_connection> gap_junctions_on(cell_gid_type gid) const override {
            return gap_junctions_[gid];
        }
        std::vector<cell_connection> connections_on(cell_gid_type gid) const override {
            return connections_[gid];
        }
        cell_size_type num_sources(cell_gid_type gid) const override {
            return num_sources_[gid];
        }
        cell_size_type num_targets(cell_gid_type gid) const override {
            return num_targets_[gid];
        }
        std::vector<arb::event_generator> event_generators(cell_gid_type gid) const override {
            return event_generators_[gid];
        }
        std::any get_global_properties(cell_kind) const override {
            arb::cable_cell_global_properties a;
            a.default_parameters = arb::neuron_parameter_defaults;
            return a;
        }

    private:
        cell_size_type num_cells_;
        std::vector<cell_size_type> num_sources_, num_targets_;
        std::vector<std::vector<cell_connection>> connections_;
        std::vector<std::vector<gap_junction_connection>> gap_junctions_;
        std::vector<std::vector<arb::event_generator>> event_generators_;
        std::vector<cable_cell> cells_;
    };

    cable_cell custom_cell(cell_size_type num_detectors, cell_size_type num_synapses, cell_size_type num_gj) {
        arb::segment_tree tree;
        tree.append(arb::mnpos, {0,0,0,10}, {0,0,20,10}, 1); // soma
        tree.append(0, {0,0, 20, 2}, {0,0, 320, 2}, 3);  // dendrite

        arb::cable_cell cell(tree);

        arb::decor decorations;

        // Add a num_detectors detectors to the cell.
        for (auto i: util::make_span(num_detectors)) {
            decorations.place(arb::mlocation{0,(double)i/num_detectors}, arb::threshold_detector{10});
        }

        // Add a num_synapses synapses to the cell.
        for (auto i: util::make_span(num_synapses)) {
            decorations.place(arb::mlocation{0,(double)i/num_synapses}, "expsyn");
        }

        // Add a num_gj gap_junctions to the cell.
        for (auto i: util::make_span(num_gj)) {
            decorations.place(arb::mlocation{0,(double)i/num_gj}, arb::gap_junction_site{});
        }

        return arb::cable_cell(tree, {}, decorations);
    }
}

// test assumes one domain
TEST(recipe, num_sources)
{
    arb::proc_allocation resources;
    if (auto nt = arbenv::get_env_num_threads()) {
        resources.num_threads = nt;
    }
    else {
        resources.num_threads = arbenv::thread_concurrency();
    }
    auto context = make_context(resources);
    auto cell = custom_cell(1, 0, 0);

    {
        auto recipe_0 = custom_recipe({cell}, {1}, {0}, {{}}, {{}}, {{}});
        auto decomp_0 = partition_load_balance(recipe_0, context);

        EXPECT_NO_THROW(simulation(recipe_0, decomp_0, context));
    }
    {
        auto recipe_1 = custom_recipe({cell}, {2}, {0}, {{}}, {{}}, {{}});
        auto decomp_1 = partition_load_balance(recipe_1, context);

        EXPECT_THROW(simulation(recipe_1, decomp_1, context), arb::bad_source_description);
    }
}

TEST(recipe, num_targets)
{
    arb::proc_allocation resources;
    if (auto nt = arbenv::get_env_num_threads()) {
        resources.num_threads = nt;
    }
    else {
        resources.num_threads = arbenv::thread_concurrency();
    }
    auto context = make_context(resources);
    auto cell = custom_cell(0, 2, 0);

    {
        auto recipe_0 = custom_recipe({cell}, {0}, {2}, {{}}, {{}}, {{}});
        auto decomp_0 = partition_load_balance(recipe_0, context);

        EXPECT_NO_THROW(simulation(recipe_0, decomp_0, context));
    }
    {
        auto recipe_1 = custom_recipe({cell}, {0}, {3}, {{}}, {{}}, {{}});
        auto decomp_1 = partition_load_balance(recipe_1, context);

        EXPECT_THROW(simulation(recipe_1, decomp_1, context), arb::bad_target_description);
    }
}

TEST(recipe, gap_junctions)
{
    arb::proc_allocation resources;
    if (auto nt = arbenv::get_env_num_threads()) {
        resources.num_threads = nt;
    }
    else {
        resources.num_threads = arbenv::thread_concurrency();
    }
    auto context = make_context(resources);

    auto cell_0 = custom_cell(0, 0, 3);
    auto cell_1 = custom_cell(0, 0, 3);

    {
        std::vector<arb::gap_junction_connection> gjs_0 = {{{1, 1}, 0, 0.1},
                                                           {{1, 2}, 1, 0.1},
                                                           {{1, 0}, 2, 0.1}};

        std::vector<arb::gap_junction_connection> gjs_1 = {{{0, 0}, 1, 0.1},
                                                           {{0, 1}, 2, 0.1},
                                                           {{0, 2}, 0, 0.1}};

        auto recipe_0 = custom_recipe({cell_0, cell_1}, {0, 0}, {0, 0}, {{}, {}}, {gjs_0, gjs_1}, {{}, {}});
        auto decomp_0 = partition_load_balance(recipe_0, context);

        EXPECT_NO_THROW(simulation(recipe_0, decomp_0, context));
    }
    {
        std::vector<arb::gap_junction_connection> gjs_0 = {{{1, 1}, 0, 0.1},
                                                           {{1, 2}, 1, 0.1},
                                                           {{1, 5}, 2, 0.1}};

        std::vector<arb::gap_junction_connection> gjs_1 = {{{0, 0}, 1, 0.1},
                                                           {{0, 1}, 2, 0.1},
                                                           {{0, 2}, 5, 0.1}};

        auto recipe_1 = custom_recipe({cell_0, cell_1}, {0, 0}, {0, 0}, {{}, {}}, {gjs_0, gjs_1}, {{}, {}});
        auto decomp_1 = partition_load_balance(recipe_1, context);

        EXPECT_THROW(simulation(recipe_1, decomp_1, context), arb::bad_gj_connection_lid);

    }
}

TEST(recipe, connections)
{
    arb::proc_allocation resources;
    if (auto nt = arbenv::get_env_num_threads()) {
        resources.num_threads = nt;
    }
    else {
        resources.num_threads = arbenv::thread_concurrency();
    }
    auto context = make_context(resources);

    auto cell_0 = custom_cell(1, 2, 0);
    auto cell_1 = custom_cell(2, 1, 0);
    std::vector<arb::cell_connection> conns_0, conns_1;
    {
        conns_0 = {{{1, 0}, 0, 0.1, 0.1},
                   {{1, 1}, 0, 0.1, 0.1},
                   {{1, 0}, 1, 0.2, 0.4}};

        conns_1 = {{{0, 0}, 0, 0.1, 0.2},
                   {{0, 0}, 0, 0.3, 0.1},
                   {{0, 0}, 0, 0.1, 0.8}};

        auto recipe_0 = custom_recipe({cell_0, cell_1}, {1, 2}, {2, 1}, {conns_0, conns_1}, {{}, {}},  {{}, {}});
        auto decomp_0 = partition_load_balance(recipe_0, context);

        EXPECT_NO_THROW(simulation(recipe_0, decomp_0, context));
    }
    {
        conns_0 = {{{1, 0}, 0, 0.1, 0.1},
                   {{2, 1}, 0, 0.1, 0.1},
                   {{1, 0}, 1, 0.2, 0.4}};

        conns_1 = {{{0, 0}, 0, 0.1, 0.2},
                   {{0, 0}, 0, 0.3, 0.1},
                   {{0, 0}, 0, 0.1, 0.8}};

        auto recipe_1 = custom_recipe({cell_0, cell_1}, {1, 2}, {2, 1}, {conns_0, conns_1}, {{}, {}},  {{}, {}});
        auto decomp_1 = partition_load_balance(recipe_1, context);

        EXPECT_THROW(simulation(recipe_1, decomp_1, context), arb::bad_connection_source_gid);
    }
    {
        conns_0 = {{{1, 0}, 0, 0.1, 0.1},
                   {{1, 1}, 0, 0.1, 0.1},
                   {{1, 3}, 1, 0.2, 0.4}};

        conns_1 = {{{0, 0}, 0, 0.1, 0.2},
                   {{0, 0}, 0, 0.3, 0.1},
                   {{0, 0}, 0, 0.1, 0.8}};

        auto recipe_2 = custom_recipe({cell_0, cell_1}, {1, 2}, {2, 1}, {conns_0, conns_1}, {{}, {}},  {{}, {}});
        auto decomp_2 = partition_load_balance(recipe_2, context);

        EXPECT_THROW(simulation(recipe_2, decomp_2, context), arb::bad_connection_source_lid);
    }
    {
        conns_0 = {{{1, 0}, 0, 0.1, 0.1},
                   {{1, 1}, 0, 0.1, 0.1},
                   {{1, 0}, 1, 0.2, 0.4}};

        conns_1 = {{{0, 0}, 0, 0.1, 0.2},
                   {{0, 0}, 9, 0.3, 0.1},
                   {{0, 0}, 0, 0.1, 0.8}};

        auto recipe_4 = custom_recipe({cell_0, cell_1}, {1, 2}, {2, 1}, {conns_0, conns_1}, {{}, {}},  {{}, {}});
        auto decomp_4 = partition_load_balance(recipe_4, context);

        EXPECT_THROW(simulation(recipe_4, decomp_4, context), arb::bad_connection_target_lid);
    }
}

TEST(recipe, event_generators) {
    arb::proc_allocation resources;
    if (auto nt = arbenv::get_env_num_threads()) {
        resources.num_threads = nt;
    }
    else {
        resources.num_threads = arbenv::thread_concurrency();
    }
    auto context = make_context(resources);

    auto cell_0 = custom_cell(1, 2, 0);
    auto cell_1 = custom_cell(2, 1, 0);
    std::vector<arb::event_generator> gens_0, gens_1;
    {
        gens_0 = {arb::explicit_generator(arb::pse_vector{{0, 1.0, 0.1}, {1, 2.0, 0.1}})};

        gens_1 = {arb::explicit_generator(arb::pse_vector{{0, 1.0, 0.1}})};

        auto recipe_0 = custom_recipe({cell_0, cell_1}, {1, 2}, {2, 1}, {{}, {}}, {{}, {}},  {gens_0, gens_1});
        auto decomp_0 = partition_load_balance(recipe_0, context);

        EXPECT_NO_THROW(simulation(recipe_0, decomp_0, context));
    }
    {
        gens_0 = {arb::explicit_generator(arb::pse_vector{{0, 1.0, 0.1}, {3, 2.0, 0.1}})};
        gens_1.clear();

        auto recipe_0 = custom_recipe({cell_0, cell_1}, {1, 2}, {2, 1}, {{}, {}}, {{}, {}},  {gens_0, gens_1});
        auto decomp_0 = partition_load_balance(recipe_0, context);

        EXPECT_THROW(simulation(recipe_0, decomp_0, context), arb::bad_event_generator_target_lid);
    }
}
