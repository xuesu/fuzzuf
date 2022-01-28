/*
 * fuzzuf
 * Copyright (C) 2021 Ricerca Security
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */
/**
 * @file VUzzer.cpp
 * @brief Fuzzing loop of VUzzer.
 * @author Ricerca Security <fuzzuf-dev@ricsec.co.jp>
 */

#include <cstddef>
#include <unistd.h>
#include <sys/ioctl.h>

#include "config.h"

#include "fuzzuf/utils/common.hpp"
#include "fuzzuf/utils/workspace.hpp"
#include "fuzzuf/feedback/exit_status_feedback.hpp"

#include "fuzzuf/algorithms/vuzzer/vuzzer.hpp"
#include "fuzzuf/algorithms/vuzzer/vuzzer_mutator.hpp"
#include "fuzzuf/algorithms/vuzzer/vuzzer_util.hpp"
#include "fuzzuf/algorithms/vuzzer/vuzzer_setting.hpp"
#include "fuzzuf/algorithms/vuzzer/vuzzer_state.hpp"
#include "fuzzuf/algorithms/vuzzer/vuzzer_mutation_hierarflow_routines.hpp"
#include "fuzzuf/algorithms/vuzzer/vuzzer_update_hierarflow_routines.hpp"
#include "fuzzuf/algorithms/vuzzer/vuzzer_other_hierarflow_routines.hpp"

#include "fuzzuf/hierarflow/hierarflow_routine.hpp"
#include "fuzzuf/hierarflow/hierarflow_node.hpp"
#include "fuzzuf/hierarflow/hierarflow_intermediates.hpp"

#include "fuzzuf/executor/pintool_executor.hpp"
#include "fuzzuf/executor/polytracker_executor.hpp"

namespace fuzzuf::algorithm::vuzzer {

/**
 * @brief Determine EHBs by executing PUT with initial seeds. 
 * @param (state) VUzzer state
 */
void VUzzer::PerformDryRun(VUzzerState &state) {
    ExitStatusFeedback exit_status;
    DEBUG("Starting dry run now...");

    if (state.pending_queue.size() < 3) {
        ERROR("Not sufficient initial files");
    }

    /* Execute PUT with initial seeds (valid inputs) */
    for (const auto& testcase : state.pending_queue) {
            testcase->input->Load();
            auto inp_feed = state.RunExecutor(testcase->input->GetBuf(), testcase->input->GetLen(), exit_status);
            std::map<u64, u32> bb_cov;
            boost::dynamic_bitset<> bb_set;
            vuzzer::util::ParseBBCov(inp_feed, bb_cov);
            for (const auto &bbc : bb_cov)
                state.good_bbs.insert(bbc.first);
            testcase->input->Unload();
    }

    DEBUG("Good BBs");
    for (const auto& bb : state.good_bbs)
        DEBUG("0x%llx,",bb);

    /* Execute PUT with invalid inputs. */
    /* Generate invalid random inputs by mutating initial seeds. */
    DEBUG("Starting bad inputs...");
    /* Original VUzzer execute create_files_dry(30) twice. */
    for (int i = 0; i < 60; i++) {
        const auto& testcase = state.pending_queue[rand() % state.pending_queue.size()];
        testcase->input->Load();
        auto mutator = VUzzerMutator( *testcase->input, state );
        mutator.TotallyRandom();

        auto inp_feed = state.RunExecutor(mutator.GetBuf(), mutator.GetLen(), exit_status);
        std::map<u64, u32> bb_cov;
        vuzzer::util::ParseBBCov(inp_feed, bb_cov);
        for (const auto &bbc : bb_cov) {
            u64 addr = bbc.first;
            if (state.good_bbs.find(addr) == state.good_bbs.end())
                state.ehb.insert(addr);
        }
        testcase->input->Unload();
    }

    DEBUG("EHBs");
    for (const auto& bb : state.ehb)
        DEBUG("0x%llx,",bb);

    /* Get taint info from execution with initial seeds 
    ** It'll be used in FillSeeds
    */
   DEBUG("Get taint info from initial seeds");
    for (const auto& testcase : state.pending_queue) {
        testcase->input->Load();
        auto inp_feed = state.RunTaintExecutor(testcase->input->GetBuf(), testcase->input->GetLen(), exit_status);    
        vuzzer::util::ParseTaintInfo(state, testcase, inp_feed);
        testcase->input->Unload();
    }
}

/**
 * @brief Fill seed queue with a certain number of seeds generated by mutators
 * @param (state) VUzzer state
 * @param (size) number of seeds
 */
void VUzzer::FillSeeds(VUzzerState &state, u32 size) {
    std::random_device rd;
    std::default_random_engine eng(rd());
    std::uniform_real_distribution<> distr_cut(0.1, 1.0);

    u32 _sz = size;
    DEBUG("FillSeeds with size (%u)", _sz);
    /* Generate a certain number of seeds by mutating initial seeds */
    std::vector<std::shared_ptr<VUzzerTestcase>> initial_queue = state.pending_queue;
    for (u32 sz = 0; sz < size;) {
        std::vector<std::shared_ptr<VUzzerTestcase>> parents;
        if (distr_cut(eng) > (1.0 - state.setting->fill_seeds_with_crossover_prob) && size - sz > 1) {
            DEBUG("Crossover");
            std::sample(initial_queue.begin(),
                        initial_queue.end(),
                        std::back_inserter(parents),
                        2,
                        std::mt19937{std::random_device{}()});

            DEBUG("Chose %s, %s", parents[0]->input->GetPath().c_str(), parents[1]->input->GetPath().c_str());

            parents[0]->input->LoadByMmap();
            parents[1]->input->LoadByMmap();        

            auto crossover = VUzzerMutator( *parents[0]->input, state );
            /* FIXME: Do not use ExecInputSet(input_set) */
            auto seeds = crossover.CrossOver(*parents[1]->input);

            auto mutator1 = VUzzerMutator( *(seeds.first), state );
            auto mutator2 = VUzzerMutator( *(seeds.second), state );

            mutator1.TaintBasedChange();
            mutator2.TaintBasedChange();

            parents[0]->input->Unload();
            parents[1]->input->Unload();

            std::string fn = Util::StrPrintf("%s/queue/id:%06u",
                                            state.setting->out_dir.c_str(),
                                            state.queued_paths);
            state.AddToQueue(state.pending_queue, fn, mutator1.GetBuf(), mutator1.GetLen());

            fn = Util::StrPrintf("%s/queue/id:%06u",
                                state.setting->out_dir.c_str(),
                                state.queued_paths);
            state.AddToQueue(state.pending_queue, fn, mutator2.GetBuf(), mutator2.GetLen());

            sz+=2;
        } else {
            std::sample(state.pending_queue.begin(),
                        state.pending_queue.end(),
                        std::back_inserter(parents),
                        1,
                        std::mt19937{std::random_device{}()});

            parents[0]->input->LoadByMmap();

            auto mutator = VUzzerMutator( *parents[0]->input, state );

            mutator.MutateRandom();
            mutator.TaintBasedChange();

            std::string fn = Util::StrPrintf("%s/queue/id:%06u",
                                            state.setting->out_dir.c_str(),
                                            state.queued_paths);
            state.AddToQueue(state.pending_queue, fn, mutator.GetBuf(), mutator.GetLen());

            parents[0]->input->Unload();
            sz++;
        }
    }
}

VUzzer::VUzzer(
    std::unique_ptr<VUzzerState>&& state_ref
) :
    state(std::move(state_ref)) 
{
    /* Parse BB weight file */
    vuzzer::util::ParseBBWeights(*state, state->setting->path_to_weight_file);
    
    /* Load dictionaries generated by static analysis tool (BB-weight.py) */
    fuzzuf::algorithm::afl::dictionary::load(
        state->setting->path_to_full_dict.native(),
        state->full_bytes_dict,
        false,
        []( std::string &&m ) {
            ERROR("%s", m.c_str());
        }
    );

    fuzzuf::algorithm::afl::dictionary::load(
        state->setting->path_to_unique_dict.native(),
        state->unique_bytes_dict,
        false,
        []( std::string &&m ) {
            ERROR("%s", m.c_str());
        }
    );    

    if (state->full_bytes_dict.size()) {
        state->all_dicts.emplace_back(&(state->full_bytes_dict));
        state->all_dicts.emplace_back(&(state->full_bytes_dict));
        state->all_dicts.emplace_back(&(state->high_chars_dict));
        state->all_dicts.emplace_back(&(state->unique_bytes_dict));
    } else if (state->unique_bytes_dict.size()) {
        state->all_dicts.emplace_back(&(state->unique_bytes_dict));
        state->all_dicts.emplace_back(&(state->unique_bytes_dict));
        state->all_dicts.emplace_back(&(state->high_chars_dict));
    } else {
        state->all_dicts.emplace_back(&(state->all_chars_dict));
    }

    BuildFuzzFlow();
    state->ReadTestcases();

    PerformDryRun(*state);

    u32 seed_size = static_cast<u32>(state->pending_queue.size());

    if (seed_size < state->setting->pop_size)
        FillSeeds(*state, state->setting->pop_size - seed_size);
}

VUzzer::~VUzzer() {}

void VUzzer::BuildFuzzFlow() {
    using namespace fuzzuf::algorithm::vuzzer::routine::other;
    using namespace fuzzuf::algorithm::vuzzer::routine::mutation;
    using namespace fuzzuf::algorithm::vuzzer::routine::update;

    using fuzzuf::hierarflow::CreateNode;
    using fuzzuf::hierarflow::CreateDummyParent;

    // the head node
    fuzz_loop = CreateNode<FuzzLoop>(*state);

    // middle nodes
    auto decide_keep = CreateNode<DecideKeep>(*state);
    auto run_ehb = CreateNode<RunEHB>(*state);
    auto execute = CreateNode<ExecutePUT>(*state);
    auto update_fitness = CreateNode<UpdateFitness>(*state);
    auto trim_queue = CreateNode<TrimQueue>(*state);
    auto execute_taint = CreateNode<ExecuteTaintPUT>(*state);
    auto update_taint = CreateNode<UpdateTaint>(*state);
    auto mutate = CreateNode<Mutate>(*state);
    auto update_queue = CreateNode<UpdateQueue>(*state);

    fuzz_loop <<
        decide_keep <<
        run_ehb << (
            execute << update_fitness << trim_queue
        ||  execute_taint << update_taint
        ||  mutate
        ||  update_queue
        );
}

// FIXME: CullQueue can be a node
void VUzzer::OneLoop(void) {
    fuzz_loop();
}

// Do not call non aync-signal-safe functions inside
// because this function can be called during signal handling
void VUzzer::ReceiveStopSignal(void) {
    state->ReceiveStopSignal();
}

} // namespace fuzzuf::algorithm::vuzzer
