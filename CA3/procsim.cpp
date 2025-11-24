#include "procsim.hpp"
#include <vector>
#include <deque>
#include <algorithm>
#include <iostream>
using namespace std;

// global setup variables 
static uint64_t g_R;  // result buses
static uint64_t g_k0; // number of FU type 0
static uint64_t g_k1; // number of FU type 1
static uint64_t g_k2; // number of FU type 2
static uint64_t g_F;  // fetch width

enum FuType {
    FU_K0 = 0,
    FU_K1 = 1,
    FU_K2 = 2
};

struct FuUnit {
    bool busy = false;
    FuType type; // 0, 1, 2
    uint64_t inst_tag = 0;   // which instruction is here (tag)
    int cycles_left = 0;
};

struct RsEntry {
    proc_inst_t inst;        // copy of instruction (includes tag)

    // Operand readiness
    bool src_ready[2] = {false, false}; // (rs1 ready?, rs2 ready?)
    uint64_t src_tag[2] = {0, 0};   // which inst produces src if not ready

    // Execution state
    bool issued = false;  // has been sent to FU?
    FuType fu_type;
    int fu_index  = -1;     // which FU unit index
    bool completed = false;  // finished execution (waiting for CDB / retire)
};

// vector of size F to store fetched instructions between fetch/dispatch
static vector<proc_inst_t> g_fetched;
// Dispatch queue: unlimited size, FIFO
static deque<proc_inst_t> g_dispatch_q;

// Reservation station: size = 2 * (k0 + k1 + k2)  (initialized in setup_proc)
static vector<RsEntry> g_rs;

// Functional units for each type
static vector<FuUnit> g_fu_k0;
static vector<FuUnit> g_fu_k1;
static vector<FuUnit> g_fu_k2;

// Very simple register readiness model (128 regs, as in handout)
static bool g_reg_ready[128]; //0 -> 127
static uint64_t g_reg_producer_tag[128];

// Tag generator for instructions
static uint64_t g_next_tag = 1;

// Termination flags
static bool g_no_more_fetch = false; // true once read_instruction fails

// Per-cycle stats accumulation
static unsigned long g_cycle_count          = 0;
static unsigned long g_total_dispatch_size  = 0;
static unsigned long g_max_dispatch_size    = 0;
static unsigned long g_total_inst_fired     = 0;
static unsigned long g_total_inst_retired   = 0;

// each stage
static void stage_fetch();
static void stage_dispatch();
static void stage_schedule(int rs_free_start);
static void stage_execute_fire();
static void stage_execute_writeback();
static void stage_state_update();
void print_timing_output();

// Termination check
static bool all_instructions_done();

// big table for output
static vector<proc_inst_t> g_inst_table;  // 1-based index by tag

/**
 * Subroutine for initializing the processor. You many add and initialize any global or heap
 * variables as needed.
 * XXX: You're responsible for completing this routine
 *
 * @r number of result busses
 * @k0 Number of k0 FUs
 * @k1 Number of k1 FUs
 * @k2 Number of k2 FUs
 * @f Number of instructions to fetch
 */
void setup_proc(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f) 
{
    // setup the global variables first
    g_R  = r;
    g_k0 = k0;
    g_k1 = k1;
    g_k2 = k2;
    g_F  = f;

    // RS size: 2 * (k0 + k1 + k2) 
    size_t rs_size = 2 * (g_k0 + g_k1 + g_k2);
    g_rs.assign(rs_size, RsEntry{});

    // Initialize FU arrays
    g_fu_k0.assign(g_k0, FuUnit{});
    g_fu_k1.assign(g_k1, FuUnit{});
    g_fu_k2.assign(g_k2, FuUnit{});

    // reserve g_F entries for the fetch/dispatch
    g_fetched.reserve(g_F);

    for (auto &fu : g_fu_k0) fu.type = FU_K0;
    for (auto &fu : g_fu_k1) fu.type = FU_K1;
    for (auto &fu : g_fu_k2) fu.type = FU_K2;

    // Register file ready bits: assume all regs initially ready
    for (int i = 0; i < 128; i++) {
        g_reg_ready[i] = true;
        g_reg_producer_tag[i] = 0;
    }

    g_next_tag             = 1;
    g_no_more_fetch        = false;
    g_cycle_count          = 0;
    g_total_dispatch_size  = 0;
    g_max_dispatch_size    = 0;
    g_total_inst_fired     = 0;
    g_total_inst_retired   = 0;

    g_inst_table.clear();
    g_inst_table.resize(1); // index 0 unused, tags start at 1

}

/**
 * Subroutine that simulates the processor.
 *   The processor should fetch instructions as appropriate, until all instructions have executed
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void run_proc(proc_stats_t* p_stats)
{
    // Loop until no new instructions AND everything is retired
    while (true) {
        if (all_instructions_done()) {
            break;
        }

        g_cycle_count++;
        /*
        bool free_spot = false;
        int free_count = 0;
        // check for free spots, if no free spots, skip schedule
        for (size_t i = 0; i < g_rs.size(); i++) {
            if (g_rs[i].inst.tag == 0) {
                // This entry is free (tag == 0 means uninitialized/free)
                free_spot = true;
                free_count++;
            }
        }
            */
        //cout << "FREE_SPOT_COUNT: " << free_count << " at cycle: " << g_cycle_count << endl;
        // 1) snapshot free slots at start of cycle
        int rs_free_start = 0;
        for (auto &e : g_rs) {
            if (e.inst.tag == 0) rs_free_start++;
        }
        stage_state_update();
        stage_execute_writeback();  // handle completed instructions + CDB
        stage_execute_fire();       // issue ready instructions into FUs
        
        stage_schedule(rs_free_start);           // move from dispatch_q -> RS (if space)
        stage_dispatch();           // (dispatch is essentially enqueuing)
        stage_fetch();              // read new instructions (front-end)
        
        // Track dispatch queue stats per cycle
        g_total_dispatch_size += g_dispatch_q.size();
        if (g_dispatch_q.size() > g_max_dispatch_size) {
            g_max_dispatch_size = g_dispatch_q.size();
        }
        
    }

    // Copy raw counters into p_stats; averages are computed in complete_proc()
    if (p_stats) {
        p_stats->cycle_count         = g_cycle_count-1;
        p_stats->retired_instruction = g_total_inst_retired;
    }
}

/**
 * Subroutine for cleaning up any outstanding instructions and calculating overall statistics
 * such as average IPC, average fire rate etc.
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void complete_proc(proc_stats_t *p_stats) 
{
    // Average instructions fired per cycle
    p_stats->avg_inst_fired   = static_cast<float>(g_total_inst_fired) /
                                static_cast<float>(g_cycle_count);

    // Average instructions retired per cycle (IPC)
    p_stats->avg_inst_retired = static_cast<float>(g_total_inst_retired) /
                                static_cast<float>(g_cycle_count);

    // Average dispatch queue size
    p_stats->avg_disp_size    = static_cast<float>(g_total_dispatch_size) /
                                static_cast<float>(g_cycle_count);

    // Max dispatch queue size
    p_stats->max_disp_size    = g_max_dispatch_size;

    print_timing_output();
}


static void stage_fetch(){
    g_fetched.clear();
    // if no more to fetch
    if (g_no_more_fetch) return;
    
    // clear the pipeline register between fetch and dispatch

    // Try to fetch up to g_F instructions per cycle
    for (uint64_t i = 0; i < g_F; i++) {
        proc_inst_t inst{};
        if (!read_instruction(&inst)) {
            g_no_more_fetch = true;
            break;
        }

        inst.tag = g_next_tag++;
        inst.fetch_cycle = g_cycle_count; // set the fetch cycle
        inst.disp_cycle  = 0;
        inst.sched_cycle = 0;
        inst.exec_cycle  = 0;
        inst.state_cycle = 0;

        // add another index to the table for output
        if (g_inst_table.size() <= inst.tag) {
            g_inst_table.resize(inst.tag + 1);
        }
        // set the instruction and update the fetch cycle.
        g_inst_table[inst.tag] = inst;

        // Put into fetch/dispatch pipeline
        g_fetched.push_back(inst);
    }
}

// ----------------- DISPATCH -----------------
static void stage_dispatch()
{
    // Loop through all fetched instructions
    for (auto& inst : g_fetched) {
        g_dispatch_q.push_back(inst);
        g_inst_table[inst.tag].disp_cycle = g_cycle_count; // set the dispatch cycle
    }
}

// ----------------- SCHEDULE (DISPATCH -> RS) -----------------
static void stage_schedule(int rs_free_start)
{
    int used_this_cycle = 0;
    while (!g_dispatch_q.empty()){
        if (used_this_cycle >= rs_free_start) {
            break; // cannot use more slots than were free at cycle start
        }

        int free_rs_idx = -1;
        for (size_t i = 0; i < g_rs.size(); i++) {
            if (g_rs[i].inst.tag == 0) {
                free_rs_idx = i;
                break;
            }
        }

        // If no free RS entry, stop scheduling
        if (free_rs_idx == -1) {
            break;
        }

        // Get instruction from front of dispatch queue
        proc_inst_t inst = g_dispatch_q.front();
        g_dispatch_q.pop_front();
        used_this_cycle++;
        // Set schedule cycle
        g_inst_table[inst.tag].sched_cycle = g_cycle_count;

        // Create RS entry
        RsEntry &entry = g_rs[free_rs_idx];
        entry.inst = inst;
        entry.issued = false;
        entry.completed = false;
        entry.fu_index = -1;


        // Determine FU type based on opcode
        if (inst.op_code == 0) {
            entry.fu_type = FU_K0;
        } else if (inst.op_code == 1 || inst.op_code == -1) {
            entry.fu_type = FU_K1;
        } else {
            entry.fu_type = FU_K2;
        }

        // Check source operand 1
        if (inst.src_reg[0] == -1) { // -1 means register not used so always ready
            entry.src_ready[0] = true;
        } else {
            if (g_reg_ready[inst.src_reg[0]]) { // if the register ready says it is ready
                entry.src_ready[0] = true;
            } else {
               // cout << inst.src_reg[0] << " not ready" << endl;
                entry.src_ready[0] = false;
            }
        }

        // Check source operand 2
        if (inst.src_reg[1] == -1) {
            entry.src_ready[1] = true;
        } else {
            if (g_reg_ready[inst.src_reg[1]]) {
                entry.src_ready[1] = true;
            } else {
                //cout << inst.src_reg[1] << " not ready" << endl;
                entry.src_ready[1] = false;
            }
        }

        // Mark destination register as being used so not ready
        if (inst.dest_reg != -1) {
            g_reg_ready[inst.dest_reg] = false;
            g_reg_producer_tag[inst.dest_reg] = inst.tag;
        }
    }
}


// ----------------- EXECUTE_FIRE (RS -> FU) -----------------
static void stage_execute_fire()
{
    // TODO:
    // 1. Find all RS entries that:
    //      - valid == true
    //      - issued == false
    //      - src_ready[0/1] == true (for those that apply)
    // 2. Among these, service in *tag order* (lowest tag first).
    // 3. For each candidate, find a free FU of the correct type (based on op_code).
    //    - If FU available:
    //         - Mark FU busy, set inst_tag, cycles_left = 1 (latency = 1 for all FUs here)
    //         - Mark RS entry issued=true, record fu_type & fu_index.
    //         - Increment g_total_inst_fired.
    //
    // NOTE: You should NOT decrement cycles_left here; that typically happens
    //       in stage_execute_writeback() when modeling multi-cycle FUs.

    // Collect all ready-to-issue RS entries
    vector<int> ready_indices;
    for (size_t i = 0; i < g_rs.size(); i++) {
        RsEntry &entry = g_rs[i];
        if (entry.inst.tag != 0 && !entry.issued &&
            entry.src_ready[0] && entry.src_ready[1]) {
            ready_indices.push_back(i);
        }
    }

    // Sort by tag (lowest first)
    sort(ready_indices.begin(), ready_indices.end(), 
         [](int a, int b) { return g_rs[a].inst.tag < g_rs[b].inst.tag; });

    // Try to issue each ready instruction
    for (int idx : ready_indices) {
        RsEntry &entry = g_rs[idx];
        
        // Find free FU of correct type
        vector<FuUnit> *fu_array = nullptr;
        if (entry.fu_type == FU_K0) fu_array = &g_fu_k0;
        else if (entry.fu_type == FU_K1) fu_array = &g_fu_k1;
        else fu_array = &g_fu_k2;

        int free_fu = -1;
        for (size_t i = 0; i < fu_array->size(); i++) {
            if (!(*fu_array)[i].busy) {
                free_fu = i;
                break;
            }
        }

        // If FU available, issue the instruction
        if (free_fu != -1) {
            (*fu_array)[free_fu].busy = true;
            (*fu_array)[free_fu].inst_tag = entry.inst.tag;
            (*fu_array)[free_fu].cycles_left = 1;
            
            entry.issued = true;
            entry.fu_index = free_fu;
            g_inst_table[entry.inst.tag].exec_cycle = g_cycle_count;
            
            g_total_inst_fired++;
        }
    }
}

// ----------------- EXECUTE_WRITEBACK (FUs + Result Buses) -----------------
static void stage_execute_writeback()
{
    // TODO:
    // 1. Decrement cycles_left for all busy FUs.
    // 2. Collect all FUs whose cycles_left == 0 (completed this cycle).
    // 3. Sort these completed instructions by tag.
    // 4. Use up to g_R result buses:
    //    - For the first g_R completed instructions:
    //        a) Find their RS entry by tag; mark completed = true.
    //        b) "Broadcast" on CDB:
    //             - For every RS entry waiting on that tag, mark src_ready=true.
    //             - For any register whose producer_tag == that tag, set g_reg_ready[reg]=true.
    //        c) Free the FU (busy=false, inst_tag=0, etc.).
    //    - Any remaining completed FUs (beyond g_R) must stay busy and
    //      keep inst_tag until a CDB is available in a later cycle.
    // Collect all instructions that completed execution this cycle
    vector<uint64_t> completed_tags;

    // Check all FU types for completed instructions
    for (auto &fu : g_fu_k0) {
        if (fu.busy && fu.inst_tag != 0 && fu.cycles_left == 1) {
            completed_tags.push_back(fu.inst_tag);
        }
    }
    for (auto &fu : g_fu_k1) {
        if (fu.busy && fu.inst_tag != 0 && fu.cycles_left == 1) {
            completed_tags.push_back(fu.inst_tag);
        }
    }
    for (auto &fu : g_fu_k2) {
        if (fu.busy && fu.inst_tag != 0 && fu.cycles_left == 1) {
            completed_tags.push_back(fu.inst_tag);
        }
    }
    // Sort by exec_cycle first, then by tag
    sort(completed_tags.begin(), completed_tags.end(), 
         [](uint64_t tag_a, uint64_t tag_b) {
             uint64_t exec_a = g_inst_table[tag_a].exec_cycle;
             uint64_t exec_b = g_inst_table[tag_b].exec_cycle;
             
             if (exec_a != exec_b) {
                 return exec_a < exec_b;  // Lower exec_cycle first
             }
             return tag_a < tag_b;  // If exec_cycle is same, lower tag first
         });

    // Use up to g_R result buses
    size_t broadcasts = min(static_cast<size_t>(g_R), completed_tags.size());
    
    for (size_t i = 0; i < broadcasts; i++) {
        uint64_t tag = completed_tags[i];

        // Find RS entry with this tag and mark as complete and set state cycle
        for (auto &entry : g_rs) {
            if (entry.inst.tag == tag) {
                entry.completed = true;
                g_inst_table[entry.inst.tag].state_cycle = g_cycle_count;
                break;
            }
        }

        // Free the FU
        for (auto &fu : g_fu_k0) {
            if (fu.inst_tag == tag) {
                fu.busy = false;
                fu.inst_tag = 0;
                fu.cycles_left = 0;
            }
        }
        for (auto &fu : g_fu_k1) {
            if (fu.inst_tag == tag) {
                fu.busy = false;
                fu.inst_tag = 0;
                fu.cycles_left = 0;
            }
        }
        for (auto &fu : g_fu_k2) {
            if (fu.inst_tag == tag) {
                fu.busy = false;
                fu.inst_tag = 0;
                fu.cycles_left = 0;
            }
        }
    }
}

// ----------------- STATE_UPDATE (Retirement) -----------------
// RETIRE: Remove from RS, increment retirement counter, and set ST cycle to when it enters here.
static void stage_state_update()
{
    vector<int> completed_indices;
    for (size_t i = 0; i < g_rs.size(); i++) {
        if (g_rs[i].completed && g_rs[i].inst.tag != 0) {
            completed_indices.push_back(i);
        }
    }

    // Sort by tag (retire oldest first)
    //cout << "Size of retire: " << completed_indices.size() << endl;
    sort(completed_indices.begin(), completed_indices.end(),
         [](int a, int b) { return g_rs[a].inst.tag < g_rs[b].inst.tag; });

    // Retire all completed instructions
    for (int idx : completed_indices) {
        g_total_inst_retired++;
        // mark destination register as ready
        //cout << "destination: " << g_rs[idx].inst.dest_reg << " cycle: " << g_cycle_count << endl;

        if (g_rs[idx].inst.dest_reg != -1) {
            // if Latest Tag Dependent matches Retiring Tag
            if (g_reg_producer_tag[g_rs[idx].inst.dest_reg] == g_rs[idx].inst.tag)
                g_reg_ready[g_rs[idx].inst.dest_reg] = true; //set as ready
            // iterate through and set as ready in RS
            for (auto &entry : g_rs) {
                // find cases where the src matches the destination register
                if (entry.inst.src_reg[0] == g_rs[idx].inst.dest_reg) {
                    // only if the tag of this instruction is greater than the retiring instruction
                    //cout << "Latest Tag Dependent: " << g_reg_producer_tag[g_rs[idx].inst.dest_reg] << endl;
                    //cout << "Found src0 tag match dest: " << entry.inst.tag << endl;
                   // cout << "Retiring Instruction Tag: " << g_rs[idx].inst.tag << endl << endl;
                    // if the src instr tag matching dest is in between the Latest Tag Dependent and the Retiring Tag
                    if (g_reg_producer_tag[g_rs[idx].inst.dest_reg] == g_rs[idx].inst.tag ||
                        entry.inst.tag > g_rs[idx].inst.tag && entry.inst.tag <= g_reg_producer_tag[g_rs[idx].inst.dest_reg])
                        entry.src_ready[0] = true;
                }
                if (entry.inst.src_reg[1] == g_rs[idx].inst.dest_reg){
                    // only if the tag of this instruction is greater than the retiring instruction
                   // cout << "Latest Tag Dependent: " << g_reg_producer_tag[g_rs[idx].inst.dest_reg] << endl;
                   // cout << "Found src1 tag match dest: " << entry.inst.tag << endl;
                   // cout << "Retiring Instruction Tag: " << g_rs[idx].inst.tag << endl << endl;
                    // if the src instr tag matching dest is in between the Latest Tag Dependent and the Retiring Tag
                    if (g_reg_producer_tag[g_rs[idx].inst.dest_reg] == g_rs[idx].inst.tag ||
                        entry.inst.tag > g_rs[idx].inst.tag && entry.inst.tag <= g_reg_producer_tag[g_rs[idx].inst.dest_reg])
                        entry.src_ready[1] = true;
                }
            }
        }
        // Clear the RS entry
        g_rs[idx] = RsEntry{};  // Reset to default
    }
}

// helper function to check busy of fu
static bool any_busy(const vector<FuUnit> &fus){
    for (const auto &fu : fus) {
        if (fu.busy) return true;
    }
    return false;
}

static bool rs_has_active() {
    for (const auto &e : g_rs) {
        if (e.inst.tag != 0) {   // or some other "valid" flag
            return true;
        }
    }
    return false; // all flags are 0
}

static bool all_instructions_done()
{
    if (!g_no_more_fetch) {
        return false; // we may still read more from trace
    }

     //If dispatch_q still has entries, not done
    if (!g_dispatch_q.empty()) {
        return false;
    } 
        

     //If g_rs still has entries, not done
    if (rs_has_active()) {
        return false;
    } 
    
    if (any_busy(g_fu_k0) || any_busy(g_fu_k1) || any_busy(g_fu_k2)) {
        return false;
    }
        

    // Otherwise, everything is fetched, scheduled, executed, and retired
    return true;
}

void print_timing_output()
{
    // Header – MUST be tabs, not spaces
    //printf("INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE\n");

    // tags go from 1 to g_inst_table.size()-1
    for (size_t tag = 1; tag < g_inst_table.size(); ++tag) {
        const auto &inst = g_inst_table[tag];

        printf("%zu\t%lu\t%lu\t%lu\t%lu\t%lu\n",
               tag,
               inst.fetch_cycle,
               inst.disp_cycle,
               inst.sched_cycle,
               inst.exec_cycle,
               inst.state_cycle);
    }
}


