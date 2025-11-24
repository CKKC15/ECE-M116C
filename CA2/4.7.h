// my_predictor.h
// TAGE Branch Predictor - improved version

#include <string.h>

// Configuration
#define NUM_TABLES 7
#define BASE_BITS 14               // 16K entries
#define TABLE_BITS 12              // 4K entries per table
#define TAG_BITS 10
#define MAX_HIST 200

// Geometric history lengths
const int HIST_LEN[NUM_TABLES] = {4, 8, 16, 32, 64, 128, 200};

// TAGE entry
struct tage_entry {
    unsigned int tag : TAG_BITS;
    signed char ctr : 3;           // -4 to 3
    unsigned char u : 2;           // useful
};

class my_update : public branch_update {
public:
    unsigned int base_idx;
    unsigned int idx[NUM_TABLES];
    unsigned int tag[NUM_TABLES];
    int provider;
    int altpred;
    bool pred;
};

class my_predictor : public branch_predictor {
public:
    my_update u;
    branch_info bi;
    
    // History register - store last 200 bits
    unsigned long long ghist[4];  // 4 * 64 = 256 bits
    
    // Base bimodal predictor
    unsigned char base[1 << BASE_BITS];
    
    // TAGE tables
    tage_entry tables[NUM_TABLES][1 << TABLE_BITS];
    
    unsigned int clock;
    
    my_predictor(void) : clock(0) {
        memset(ghist, 0, sizeof(ghist));
        memset(base, 2, sizeof(base));
        memset(tables, 0, sizeof(tables));
    }
    
    // Get specific bits from history
    unsigned long long get_history(int len) {
        if (len <= 64) {
            return ghist[0] & ((1ULL << len) - 1);
        } else if (len <= 128) {
            unsigned long long h = ghist[0] | (ghist[1] << 64);
            return h & ((1ULL << len) - 1);
        } else if (len <= 192) {
            return ghist[0] ^ (ghist[1] << (len - 64)) ^ (ghist[2] << (len - 128));
        } else {
            return ghist[0] ^ (ghist[1] << 8) ^ (ghist[2] << 16) ^ (ghist[3] << 24);
        }
    }
    
    // Hash for index
    unsigned int get_index(unsigned int pc, int table_id) {
        unsigned long long h = get_history(HIST_LEN[table_id]);
        unsigned int fold = (unsigned int)(h ^ (h >> TABLE_BITS) ^ (h >> (2 * TABLE_BITS)));
        return ((pc ^ fold ^ (pc >> TABLE_BITS)) & ((1 << TABLE_BITS) - 1));
    }
    
    // Hash for tag
    unsigned int get_tag(unsigned int pc, int table_id) {
        unsigned long long h = get_history(HIST_LEN[table_id]);
        unsigned int fold = (unsigned int)(h ^ (h >> TAG_BITS) ^ (h >> (2 * TAG_BITS)));
        return ((pc ^ fold ^ (pc >> (TAG_BITS + 1))) & ((1 << TAG_BITS) - 1));
    }
    
    branch_update* predict(branch_info& b) {
        bi = b;
        
        if (!(b.br_flags & BR_CONDITIONAL)) {
            u.direction_prediction(true);
            u.target_prediction(0);
            return &u;
        }
        
        // Compute all indices and tags
        u.base_idx = (b.address >> 2) & ((1 << BASE_BITS) - 1);
        
        for (int i = 0; i < NUM_TABLES; i++) {
            u.idx[i] = get_index(b.address, i);
            u.tag[i] = get_tag(b.address, i);
        }
        
        // Find provider (longest matching) and alternate
        u.provider = -1;
        u.altpred = -1;
        
        for (int i = NUM_TABLES - 1; i >= 0; i--) {
            if (tables[i][u.idx[i]].tag == u.tag[i]) {
                if (u.provider == -1) {
                    u.provider = i;
                } else if (u.altpred == -1) {
                    u.altpred = i;
                    break;
                }
            }
        }
        
        // Get predictions
        bool prov_pred, alt_pred;
        
        if (u.provider >= 0) {
            tage_entry* pe = &tables[u.provider][u.idx[u.provider]];
            prov_pred = (pe->ctr >= 0);
            
            if (u.altpred >= 0) {
                tage_entry* ae = &tables[u.altpred][u.idx[u.altpred]];
                alt_pred = (ae->ctr >= 0);
            } else {
                alt_pred = (base[u.base_idx] >= 2);
            }
            
            // Use alternate if provider has low confidence and low usefulness
            if (pe->u == 0 && (pe->ctr == 0 || pe->ctr == -1)) {
                u.pred = alt_pred;
            } else {
                u.pred = prov_pred;
            }
        } else {
            // No hit, use base
            u.pred = (base[u.base_idx] >= 2);
        }
        
        u.direction_prediction(u.pred);
        u.target_prediction(0);
        return &u;
    }
    
    void update(branch_update* up, bool taken, unsigned int target) {
        if (!(bi.br_flags & BR_CONDITIONAL)) {
            return;
        }
        
        my_update* mu = (my_update*)up;
        
        // Update base predictor
        unsigned char* bc = &base[mu->base_idx];
        if (taken) {
            if (*bc < 3) (*bc)++;
        } else {
            if (*bc > 0) (*bc)--;
        }
        
        // Update provider table
        if (mu->provider >= 0) {
            tage_entry* pe = &tables[mu->provider][mu->idx[mu->provider]];
            
            // Update counter
            if (taken) {
                if (pe->ctr < 3) pe->ctr++;
            } else {
                if (pe->ctr > -4) pe->ctr--;
            }
            
            // Update usefulness
            bool ppred = (pe->ctr >= 0);
            bool apred;
            if (mu->altpred >= 0) {
                apred = (tables[mu->altpred][mu->idx[mu->altpred]].ctr >= 0);
            } else {
                apred = (base[mu->base_idx] >= 2);
            }
            
            if (ppred != apred) {
                if (ppred == taken) {
                    if (pe->u < 3) pe->u++;
                } else {
                    if (pe->u > 0) pe->u--;
                }
            }
        }
        
        // Allocate new entry on misprediction
        if (mu->pred != taken) {
            // Find tables with longer history than provider
            int start = (mu->provider >= 0) ? mu->provider + 1 : 0;
            
            // Try to allocate in one of the longer history tables
            for (int i = start; i < NUM_TABLES; i++) {
                tage_entry* e = &tables[i][mu->idx[i]];
                
                // Allocate if entry is not useful
                if (e->u == 0) {
                    e->tag = mu->tag[i];
                    e->ctr = taken ? 0 : -1;
                    e->u = 0;
                    break;
                }
            }
            
            // Decay useful bits more aggressively on misprediction
            for (int i = start; i < NUM_TABLES; i++) {
                tage_entry* e = &tables[i][mu->idx[i]];
                if (e->u > 0) e->u--;
            }
        }
        
        // Periodic useful bit reset
        clock++;
        if ((clock & 0x7FFF) == 0) {  // Every 32K branches
            for (int i = 0; i < NUM_TABLES; i++) {
                for (int j = 0; j < (1 << TABLE_BITS); j++) {
                    if (tables[i][j].u > 0) {
                        tables[i][j].u--;
                    }
                }
            }
        }
        
        // Update global history
        for (int i = 3; i > 0; i--) {
            ghist[i] = (ghist[i] << 1) | (ghist[i-1] >> 63);
        }
        ghist[0] = (ghist[0] << 1) | (taken ? 1ULL : 0ULL);
    }
};