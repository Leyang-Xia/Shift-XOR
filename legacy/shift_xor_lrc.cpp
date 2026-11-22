/**
 * @filename: shift_xor_lrc.cpp
 * @desc: Implementation of (9,6,12,L,3,2,2) shift-XOR LRC encoding
 * @author: Based on shift_xor_9_6.cpp
 */

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <random>
#include <stdint.h>
#include <chrono>
#include <cstring>
#include <vector>
#include <algorithm>

/* Configuration parameters */

/* System parameters */
constexpr int DATA_BLOCKS = 6;    // k message sequences
constexpr int PARITY_BLOCKS = 3;  // n-k parity sequences
constexpr int CODE_BLOCKS = DATA_BLOCKS + PARITY_BLOCKS;  // n=9 total code sequences
constexpr int SUB_PACKETS = 2;    // r=2 sub-packetization
constexpr int TOTAL_DATA_BLOCKS = SUB_PACKETS * DATA_BLOCKS;  // M=rk=12 total data sequences
constexpr int LOCALITY = 2;       // r=2 locality
constexpr int LOCAL_PARITY = 1;   // δ-1=1 local parity per node
constexpr int REPAIR_GROUP_SIZE = SUB_PACKETS + LOCAL_PARITY;  // r+δ-1=3 nodes per repair group
constexpr int MAX_SHIFT_TWO_TONE = (PARITY_BLOCKS * (DATA_BLOCKS - 1)) / 2; // (3*5)/2 = 7
constexpr int LOCAL_MAX_SHIFT = (LOCAL_PARITY * (SUB_PACKETS - 1)) / 2;     // (1*1)/2 = 0

// 文件和块大小参数
constexpr size_t FILE_SIZE = 768 * 4 * 4 * 1024UL;
constexpr size_t CHUNK_SIZE = FILE_SIZE / TOTAL_DATA_BLOCKS;
constexpr size_t SLICE = 8;
constexpr size_t TOTAL_SLICE_NUM = CHUNK_SIZE / SLICE;  // L in the description
constexpr size_t PARITY_PADDING = SLICE * MAX_SHIFT_TWO_TONE;
constexpr size_t PARITY_SIZE = CHUNK_SIZE + PARITY_PADDING;
constexpr size_t LOCAL_PARITY_SIZE = CHUNK_SIZE + SLICE * LOCAL_MAX_SHIFT;

// 测试参数
constexpr uint32_t SAMPLE_NUM = 1000UL;

/* Data structures */
// Global arrays for data blocks
std::vector<std::vector<uint8_t*>> message_blocks;  // [SUB_PACKETS][DATA_BLOCKS]
std::vector<std::vector<uint8_t*>> global_parity_blocks;  // [SUB_PACKETS][PARITY_BLOCKS]
std::vector<std::vector<uint8_t*>> local_parity_blocks;  // [CODE_BLOCKS][LOCAL_PARITY]

// Decoded output blocks for recovery test
std::vector<std::vector<uint8_t*>> output_blocks;  // [SUB_PACKETS][DATA_BLOCKS]

// Node structure to represent storage nodes
struct Node {
    std::vector<std::pair<int, int>> global_blocks;  // (sub_packet_idx, block_idx) for global blocks
    std::vector<std::pair<int, int>> local_blocks;   // (code_block_idx, local_parity_idx) for local blocks
};

// Array of nodes
std::vector<Node> nodes;

// Repair groups
std::vector<std::vector<int>> repair_groups;  // [NUM_REPAIR_GROUPS][REPAIR_GROUP_SIZE]

/* Shift matrices */
int global_shift_matrix[PARITY_BLOCKS][DATA_BLOCKS];  // Φ matrix
int local_shift_matrix[LOCAL_PARITY][SUB_PACKETS];    // Ω matrix

/* Function declarations */
void initialize_node_placement();

/* Generate the two-tone shift matrix for global parity */
void generate_global_shift_matrix() {
    int d = PARITY_BLOCKS / 2 + 1;
    for (int i = 0; i < PARITY_BLOCKS; i++) {       
        for (int j = 0; j < DATA_BLOCKS; j++) {
            if (i < d) {
                global_shift_matrix[i][j] = (d - (i + 1)) * j;
            } else {
                global_shift_matrix[i][j] = ((i + 1) - d) * (DATA_BLOCKS - (j + 1));
            }
        }
    }
    
    // std::cout << "Global Shift Matrix (Φ):" << std::endl;
    // for (int i = 0; i < PARITY_BLOCKS; i++) {
    //     std::cout << "Row " << i << ": ";
    //     for (int j = 0; j < DATA_BLOCKS; j++) {
    //         std::cout << global_shift_matrix[i][j] << " ";
    //     }
    //     std::cout << std::endl;
    // }
}

/* Generate the two-tone shift matrix for local parity */
void generate_local_shift_matrix() {
    int d = LOCAL_PARITY / 2 + 1;
    // Simple Two-Tone pattern for local parity
    for (int i = 0; i < LOCAL_PARITY; i++) {
        for (int j = 0; j < SUB_PACKETS; j++) {
            if (i < d) {
                local_shift_matrix[i][j] = (d - (i + 1)) * j;
            } else {
                local_shift_matrix[i][j] = ((i + 1) - d) * (SUB_PACKETS - (j + 1));
            }
        }
    }
    
    // std::cout << "Local Shift Matrix (Ω):" << std::endl;
    // for (int i = 0; i < LOCAL_PARITY; i++) {
    //     std::cout << "Row " << i << ": ";
    //     for (int j = 0; j < SUB_PACKETS; j++) {
    //         std::cout << local_shift_matrix[i][j] << " ";
    //     }
    //     std::cout << std::endl;
    // }
}

/* Initialize memory buffers */
void initialize_buffers() {
    // Initialize message blocks - [SUB_PACKETS][DATA_BLOCKS]
    message_blocks.resize(SUB_PACKETS);
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < DATA_BLOCKS; j++) {
            uint8_t* block = new uint8_t[CHUNK_SIZE]();
            message_blocks[i].push_back(block);
        }
    }
    
    // Initialize global parity blocks - [SUB_PACKETS][PARITY_BLOCKS]
    global_parity_blocks.resize(SUB_PACKETS);
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < PARITY_BLOCKS; j++) {
            uint8_t* block = new uint8_t[PARITY_SIZE]();
            global_parity_blocks[i].push_back(block);
        }
    }
    
    // Initialize local parity blocks - [CODE_BLOCKS][LOCAL_PARITY]
    local_parity_blocks.resize(CODE_BLOCKS);
    for (int i = 0; i < CODE_BLOCKS; i++) {
        for (int j = 0; j < LOCAL_PARITY; j++) {
            uint8_t* block = new uint8_t[LOCAL_PARITY_SIZE]();
            local_parity_blocks[i].push_back(block);
        }
    }
    
    // Initialize output blocks for recovery - [SUB_PACKETS][DATA_BLOCKS]
    output_blocks.resize(SUB_PACKETS);
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < DATA_BLOCKS; j++) {
            uint8_t* block = new uint8_t[CHUNK_SIZE]();
            output_blocks[i].push_back(block);
        }
    }
    
    // Initialize nodes with placement
    initialize_node_placement();
}

/* Clean up memory buffers */
void cleanup_buffers() {
    // Free message blocks
    for (auto& packet : message_blocks) {
        for (auto block : packet) {
            delete[] block;
        }
    }
    
    // Free global parity blocks
    for (auto& packet : global_parity_blocks) {
        for (auto block : packet) {
            delete[] block;
        }
    }
    
    // Free local parity blocks
    for (auto& code_block : local_parity_blocks) {
        for (auto block : code_block) {
            delete[] block;
        }
    }
    
    // Free output blocks
    for (auto& packet : output_blocks) {
        for (auto block : packet) {
            delete[] block;
        }
    }
    
    // Clear vectors
    message_blocks.clear();
    global_parity_blocks.clear();
    local_parity_blocks.clear();
    output_blocks.clear();
    nodes.clear();
    repair_groups.clear();
}

/* Initialize node placement according to the LRC structure */
void initialize_node_placement() {
    // Calculate the number of repair groups
    int num_repair_groups = CODE_BLOCKS / REPAIR_GROUP_SIZE;
    if (CODE_BLOCKS % REPAIR_GROUP_SIZE != 0) {
        num_repair_groups++;
    }
    
    // Initialize nodes
    nodes.resize(CODE_BLOCKS);
    
    // Initialize repair groups
    repair_groups.resize(num_repair_groups);
    
    // Organize nodes into repair groups
    for (int group_idx = 0; group_idx < num_repair_groups; group_idx++) {
        for (int j = 0; j < REPAIR_GROUP_SIZE && (group_idx * REPAIR_GROUP_SIZE + j < CODE_BLOCKS); j++) {
            int node_idx = group_idx * REPAIR_GROUP_SIZE + j;
            repair_groups[group_idx].push_back(node_idx);
        }
    }
    
    // Assign sequences to nodes following cyclic shift pattern
    for (int group_idx = 0; group_idx < num_repair_groups; group_idx++) {
        for (int node_pos = 0; node_pos < repair_groups[group_idx].size(); node_pos++) {
            int node_idx = repair_groups[group_idx][node_pos];
            
            // Assign global blocks (r sequences from different sub-packets)
            for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
                int block_idx = (group_idx * REPAIR_GROUP_SIZE + (node_pos + sub_idx) % REPAIR_GROUP_SIZE);
                if (block_idx < CODE_BLOCKS) {
                    int packet_idx = sub_idx;
                    
                    // Determine if it's a data block or parity block
                    if (block_idx < DATA_BLOCKS) {
                        // Data block
                        nodes[node_idx].global_blocks.push_back(std::make_pair(packet_idx, block_idx));
                    } else {
                        // Parity block
                        nodes[node_idx].global_blocks.push_back(std::make_pair(packet_idx, block_idx));
                    }
                }
            }
            
            // Assign local parity blocks (δ-1 sequences)
            for (int local_idx = 0; local_idx < LOCAL_PARITY; local_idx++) {
                int code_idx = (group_idx * REPAIR_GROUP_SIZE + (node_pos + SUB_PACKETS + local_idx) % REPAIR_GROUP_SIZE);
                if (code_idx < CODE_BLOCKS) {
                    nodes[node_idx].local_blocks.push_back(std::make_pair(code_idx, local_idx));
                }
            }
        }
    }
    
    // Print node placement for verification
    std::cout << "Node Placement:" << std::endl;
    for (int i = 0; i < nodes.size(); i++) {
        std::cout << "Node " << i << ":" << std::endl;
        
        std::cout << "  Global blocks: ";
        for (auto& pair : nodes[i].global_blocks) {
            int sub_idx = pair.first;
            int block_idx = pair.second;
            if (block_idx < DATA_BLOCKS) {
                std::cout << "x_" << block_idx+1 << "^" << sub_idx+1 << " ";
            } else {
                std::cout << "y_" << (block_idx-DATA_BLOCKS+1) << "^" << sub_idx+1 << " ";
            }
        }
        std::cout << std::endl;
        
        std::cout << "  Local blocks: ";
        for (auto& pair : nodes[i].local_blocks) {
            int code_idx = pair.first;
            int local_idx = pair.second;
            std::cout << "p_" << code_idx+1 << "^" << local_idx+1 << " ";
        }
        std::cout << std::endl;
    }
    
    // Print repair groups
    std::cout << "Repair Groups:" << std::endl;
    for (int i = 0; i < repair_groups.size(); i++) {
        std::cout << "Group " << i << ": ";
        for (int node_idx : repair_groups[i]) {
            std::cout << node_idx << " ";
        }
        std::cout << std::endl;
    }
}

/* Generate random data for testing */
void generate_test_data() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < DATA_BLOCKS; j++) {
            for (size_t k = 0; k < CHUNK_SIZE; k++) {
                message_blocks[i][j][k] = dis(gen);
            }
        }
    }
}

/* Step 1: Global Parity Generation using shift-XOR */
void encode_global_parity() {
    // For each sub-packet
    for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
        // Clear global parity blocks
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            memset(global_parity_blocks[sub_idx][i], 0, PARITY_SIZE);
        }
        
        // For each parity block
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            // For each message block
            for (int j = 0; j < DATA_BLOCKS; j++) {
                // Calculate shift amount for this parity-message pair
                int shift_amount = global_shift_matrix[i][j];
                
                // For each slice in the message block
                for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
                    // Calculate offsets
                    size_t source_offset = slice_id * SLICE;
                    size_t target_offset = (slice_id + shift_amount) * SLICE;
                    
                    if (target_offset + SLICE <= PARITY_SIZE) {
                        // Read data from message block
                        uint64_t data = *(uint64_t*)(message_blocks[sub_idx][j] + source_offset);
                        // XOR with global parity block
                        *(uint64_t*)(global_parity_blocks[sub_idx][i] + target_offset) ^= data;
                    }
                }
            }
        }
    }
}

/* Step 2: Local Parity Generation using shift-XOR */
void encode_local_parity() {
    // Clear all local parity blocks
    for (int i = 0; i < CODE_BLOCKS; i++) {
        for (int j = 0; j < LOCAL_PARITY; j++) {
            memset(local_parity_blocks[i][j], 0, LOCAL_PARITY_SIZE);
        }
    }
    
    // For each code block (column in the placement table)
    for (int code_idx = 0; code_idx < CODE_BLOCKS; code_idx++) {
        // For each local parity block
        for (int local_idx = 0; local_idx < LOCAL_PARITY; local_idx++) {
            // For each sub-packet
            for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
                // Calculate shift amount for this local parity - global block pair
                int shift_amount = local_shift_matrix[local_idx][sub_idx];
                
                // For each slice in the block
                for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
                    // Calculate offsets
                    size_t source_offset = slice_id * SLICE;
                    size_t target_offset = (slice_id + shift_amount) * SLICE;
                    
                    if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
                        uint64_t data;
                        // Check if code_idx refers to a data block or parity block
                        if (code_idx < DATA_BLOCKS) {
                            // Data block
                            data = *(uint64_t*)(message_blocks[sub_idx][code_idx] + source_offset);
                        } else {
                            // Parity block
                            data = *(uint64_t*)(global_parity_blocks[sub_idx][code_idx - DATA_BLOCKS] + source_offset);
                        }
                        
                        // XOR with local parity block
                        *(uint64_t*)(local_parity_blocks[code_idx][local_idx] + target_offset) ^= data;
                    }
                }
            }
        }
    }
}

/* Full encoding process */
void encode() {
    // Step 1: Generate global parity using shift-XOR
    encode_global_parity();
    
    // Step 2: Generate local parity using shift-XOR
    encode_local_parity();
}

/* Decode when blocks 3-5 of both sub-packets are missing */
void decode_missing_blocks() {
    // Define missing indices (we'll assume blocks 3-5 from both sub-packets are missing)
    std::vector<int> missing_indices = {3, 4, 5};  // 0-based indices
    
    // Process each sub-packet
    for (int missing_sub_packet = 0; missing_sub_packet < SUB_PACKETS; missing_sub_packet++) {
        
        // Create temporary parity blocks for decoding
        std::vector<uint8_t*> decode_arr;
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            uint8_t* block = new uint8_t[PARITY_SIZE]();
            memcpy(block, global_parity_blocks[missing_sub_packet][i], PARITY_SIZE);
            decode_arr.push_back(block);
        }
        
        // Back substitution for available blocks (0-2)
        // For each parity block
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            // For each available data block
            for (int j = 0; j < DATA_BLOCKS - missing_indices.size(); j++) {
                // Get shift amount for this parity-message pair
                int shift_amount = global_shift_matrix[i][j];
                
                // For each slice in the block
                for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
                    // Calculate offsets
                    size_t source_offset = slice_id * SLICE;
                    size_t target_offset = (slice_id + shift_amount) * SLICE;              
                    
                    if (target_offset + SLICE <= PARITY_SIZE) {
                        // Read data from available block
                        uint64_t data = *(uint64_t*)(message_blocks[missing_sub_packet][j] + source_offset);                  
                        // XOR with corresponding parity block (back substitution)
                        *(uint64_t*)(decode_arr[i] + target_offset) ^= data;
                    }
                }
            }
        }
        
        // For each slice
        for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            // Step 1: solve block with index 3 (missing block #1)
            size_t slice_offset_2 = slice_id * SLICE;
            size_t slice_offset_0 = (slice_id + global_shift_matrix[0][3]) * SLICE;
            size_t slice_offset_1 = (slice_id + global_shift_matrix[1][3]) * SLICE;
            size_t slice_offset_3 = (slice_id + global_shift_matrix[2][3]) * SLICE;
            
            if (slice_offset_0 + SLICE <= PARITY_SIZE) {
                // Directly use parity block 0 to recover block with index 3
                *(uint64_t*)(output_blocks[missing_sub_packet][3] + slice_offset_2) = *(uint64_t*)(decode_arr[0] + slice_offset_0);
                
                // Back substitute block with index 3 to other parity blocks
                if (slice_offset_1 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[1] + slice_offset_1) ^= *(uint64_t*)(output_blocks[missing_sub_packet][3] + slice_offset_2);
                }
                if (slice_offset_3 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[2] + slice_offset_3) ^= *(uint64_t*)(output_blocks[missing_sub_packet][3] + slice_offset_2);
                }
            }
            
            // Step 2: solve block with index 5 (missing block #3)
            slice_offset_0 = (slice_id + global_shift_matrix[0][5]) * SLICE;
            slice_offset_1 = (slice_id + global_shift_matrix[1][5]) * SLICE;
            slice_offset_3 = (slice_id + global_shift_matrix[2][5]) * SLICE;
            
            if (slice_offset_3 + SLICE <= PARITY_SIZE) {
                // Directly use parity block 2 to recover block with index 5
                *(uint64_t*)(output_blocks[missing_sub_packet][5] + slice_offset_2) = *(uint64_t*)(decode_arr[2] + slice_offset_3);
                
                // Back substitute block with index 5 to other parity blocks
                if (slice_offset_0 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[0] + slice_offset_0) ^= *(uint64_t*)(output_blocks[missing_sub_packet][5] + slice_offset_2);
                }
                if (slice_offset_1 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[1] + slice_offset_1) ^= *(uint64_t*)(output_blocks[missing_sub_packet][5] + slice_offset_2);
                }
            }
            
            // Step 3: solve block with index 4 (missing block #2)
            slice_offset_0 = (slice_id + global_shift_matrix[0][4]) * SLICE;
            slice_offset_1 = (slice_id + global_shift_matrix[1][4]) * SLICE;
            slice_offset_3 = (slice_id + global_shift_matrix[2][4]) * SLICE;
            
            if (slice_offset_1 + SLICE <= PARITY_SIZE) {
                // Directly use parity block 1 to recover block with index 4
                *(uint64_t*)(output_blocks[missing_sub_packet][4] + slice_offset_2) = *(uint64_t*)(decode_arr[1] + slice_offset_1);
                
                // Back substitute block with index 4 to other parity blocks (not necessary for last block)
                if (slice_offset_0 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[0] + slice_offset_0) ^= *(uint64_t*)(output_blocks[missing_sub_packet][4] + slice_offset_2);
                }
                if (slice_offset_3 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[2] + slice_offset_3) ^= *(uint64_t*)(output_blocks[missing_sub_packet][4] + slice_offset_2);
                }
            }
        }
        
        // Clean up temporary parity blocks
        for (auto block : decode_arr) delete[] block;
    }
}

/* Validate decoding results */
bool validate_decoding() {
    std::vector<int> missing_indices = {3, 4, 5};
    bool success = true;
    
    for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
        for (int idx : missing_indices) {
            if (memcmp(message_blocks[sub_idx][idx], 
                      output_blocks[sub_idx][idx], CHUNK_SIZE) != 0) {
                std::cout << "Block " << idx << " of sub-packet " << sub_idx 
                          << " was not correctly recovered!" << std::endl;
                success = false;
            }
        }
    }
    
    return success;
}

/* Test local repair of a failed node */
void repair_failed_node(int failed_node_idx) {
    std::cout << "Testing repair of node " << failed_node_idx << std::endl;
    
    // Identify which repair group the failed node belongs to
    int group_idx = -1;
    int node_pos_in_group = -1;
    
    for (int i = 0; i < repair_groups.size(); i++) {
        auto& group = repair_groups[i];
        auto it = std::find(group.begin(), group.end(), failed_node_idx);
        if (it != group.end()) {
            group_idx = i;
            node_pos_in_group = std::distance(group.begin(), it);
            break;
        }
    }
    
    if (group_idx == -1) {
        std::cout << "Failed node not found in any repair group!" << std::endl;
        return;
    }
    
    std::cout << "Failed node belongs to repair group " << group_idx 
              << " at position " << node_pos_in_group << std::endl;
    
    // Get helper nodes from the same repair group (any r=2 nodes)
    std::vector<int> helper_nodes;
    int helpers_needed = LOCALITY;  // r=2
    
    for (int node_idx : repair_groups[group_idx]) {
        if (node_idx != failed_node_idx && helpers_needed > 0) {
            helper_nodes.push_back(node_idx);
            helpers_needed--;
        }
    }
    
    if (helper_nodes.size() < LOCALITY) {
        std::cout << "Not enough helper nodes in the repair group!" << std::endl;
        return;
    }
    
    std::cout << "Helper nodes: ";
    for (int node_idx : helper_nodes) {
        std::cout << node_idx << " ";
    }
    std::cout << std::endl;
    
    // For each global block in the failed node, try to recover it
    bool recovery_successful = true;
    
    // First, clear output blocks to store recovered data
    for (auto& pair : nodes[failed_node_idx].global_blocks) {
        int sub_idx = pair.first;
        int block_idx = pair.second;
        
        if (block_idx < DATA_BLOCKS) {
            // Clear the output block for data blocks
            memset(output_blocks[sub_idx][block_idx], 0, CHUNK_SIZE);
        }
    }
    
    // Actual recovery using local parity and helper nodes
    for (auto& pair : nodes[failed_node_idx].global_blocks) {
        int sub_idx = pair.first;
        int block_idx = pair.second;
        
        std::cout << "Attempting to recover ";
        if (block_idx < DATA_BLOCKS) {
            std::cout << "data block x_" << block_idx+1 << "^" << sub_idx+1;
        } else {
            std::cout << "parity block y_" << (block_idx-DATA_BLOCKS+1) << "^" << sub_idx+1;
        }
        std::cout << std::endl;
        
        // Find the local parity block that protects this block
        int local_parity_node = -1;
        int local_parity_idx = -1;
        
        for (int node_idx : repair_groups[group_idx]) {
            for (auto& local_pair : nodes[node_idx].local_blocks) {
                if (local_pair.first == block_idx) {
                    local_parity_node = node_idx;
                    local_parity_idx = local_pair.second;
                    break;
                }
            }
            if (local_parity_node != -1) break;
        }
        
        if (local_parity_node == -1) {
            std::cout << "Could not find local parity for this block!" << std::endl;
            recovery_successful = false;
            continue;
        }
        
        // Perform XOR-based recovery using local parity and helper nodes
        if (block_idx < DATA_BLOCKS) {
            // Get the local parity block
            uint8_t* local_parity = local_parity_blocks[block_idx][local_parity_idx];
            
            // Start with the local parity
            for (size_t i = 0; i < CHUNK_SIZE; i++) {
                output_blocks[sub_idx][block_idx][i] = local_parity[i];
            }
            
            // XOR with all other blocks in the repair group that are protected by this local parity
            for (int helper_idx : helper_nodes) {
                for (auto& helper_pair : nodes[helper_idx].global_blocks) {
                    int helper_sub_idx = helper_pair.first;
                    int helper_block_idx = helper_pair.second;
                    
                    // If this helper block is in the same repair group and uses the same local parity
                    if (helper_sub_idx != sub_idx && helper_block_idx == block_idx) {
                        // Apply appropriate shift based on local parity matrix
                        int shift_amount = local_shift_matrix[local_parity_idx][helper_sub_idx];
                        
                        // For each slice in the block
                        for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
                            size_t source_offset = slice_id * SLICE;
                            size_t target_offset = (slice_id + shift_amount) * SLICE;
                            
                            if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
                                // Get data from the helper block
                                uint64_t data;
                                if (helper_block_idx < DATA_BLOCKS) {
                                    data = *(uint64_t*)(message_blocks[helper_sub_idx][helper_block_idx] + source_offset);
                                } else {
                                    data = *(uint64_t*)(global_parity_blocks[helper_sub_idx][helper_block_idx - DATA_BLOCKS] + source_offset);
                                }
                                
                                // XOR with the output block at the appropriate position
                                *(uint64_t*)(output_blocks[sub_idx][block_idx] + target_offset) ^= data;
                            }
                        }
                    }
                }
            }
            
            // Validate the recovery
            bool match = memcmp(message_blocks[sub_idx][block_idx], 
                               output_blocks[sub_idx][block_idx], CHUNK_SIZE) == 0;
            std::cout << "Recovery check: " << (match ? "SUCCESSFUL" : "FAILED") << std::endl;
            if (!match) recovery_successful = false;
        }
    }
    
    // For local parity blocks, recovery is straightforward
    for (auto& pair : nodes[failed_node_idx].local_blocks) {
        int code_idx = pair.first;
        int local_idx = pair.second;
        
        std::cout << "Local parity block p_" << code_idx+1 << "^" << local_idx+1 
                  << " can be directly recomputed" << std::endl;
        
        // Clear the local parity block
        memset(local_parity_blocks[code_idx][local_idx], 0, LOCAL_PARITY_SIZE);
        
        // Recompute the local parity
        for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
            int shift_amount = local_shift_matrix[local_idx][sub_idx];
            
            for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
                size_t source_offset = slice_id * SLICE;
                size_t target_offset = (slice_id + shift_amount) * SLICE;
                
                if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
                    uint64_t data;
                    if (code_idx < DATA_BLOCKS) {
                        data = *(uint64_t*)(message_blocks[sub_idx][code_idx] + source_offset);
                    } else {
                        data = *(uint64_t*)(global_parity_blocks[sub_idx][code_idx - DATA_BLOCKS] + source_offset);
                    }
                    
                    *(uint64_t*)(local_parity_blocks[code_idx][local_idx] + target_offset) ^= data;
                }
            }
        }
    }
    
    std::cout << "Node repair simulation: " 
              << (recovery_successful ? "SUCCESSFUL" : "PARTIAL/FAILED") << std::endl;
}

/* 专门修复节点1的函数 - 为性能测试优化 */
void repair_node_1() {
    const int failed_node_idx = 1;
    
    // 根据节点布局，确定节点1包含的块
    // 从节点布局输出可知道节点1包含的块：
    // Global blocks: x_2^1 x_3^2
    // Local blocks: p_3^1
    
    // 1. 修复全局块：x_2^1（sub_packet=0, block_idx=1）
    // 清空输出块
    memset(output_blocks[0][1], 0, CHUNK_SIZE);
    
    // 针对这个特定的块，找到保护它的局部奇偶校验块
    // 从节点布局可知，p_2^1通常保护x_2块
    uint8_t* local_parity = local_parity_blocks[1][0];
    
    // 使用局部奇偶校验和其他辅助块恢复数据
    // 复制局部奇偶校验作为起点
    for (size_t i = 0; i < CHUNK_SIZE; i++) {
        output_blocks[0][1][i] = local_parity[i];
    }
    
    // 与帮助节点的数据进行XOR操作
    // 这里我们知道节点0中包含x_2^2，用它来帮助恢复
    int shift_amount = local_shift_matrix[0][1]; // 获取x_2^2在局部奇偶校验中的偏移
    
    for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t source_offset = slice_id * SLICE;
        size_t target_offset = (slice_id + shift_amount) * SLICE;
        
        if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
            // 获取x_2^2数据
            uint64_t data = *(uint64_t*)(message_blocks[1][1] + source_offset);
            
            // 与输出块XOR
            *(uint64_t*)(output_blocks[0][1] + target_offset) ^= data;
        }
    }
    
    // 2. 修复全局块：x_3^2（sub_packet=1, block_idx=2）
    // 清空输出块
    memset(output_blocks[1][2], 0, CHUNK_SIZE);
    
    // 针对这个特定的块，找到保护它的局部奇偶校验块
    // 从节点布局可知，p_3^1通常保护x_3块
    local_parity = local_parity_blocks[2][0];
    
    // 复制局部奇偶校验作为起点
    for (size_t i = 0; i < CHUNK_SIZE; i++) {
        output_blocks[1][2][i] = local_parity[i];
    }
    
    // 与帮助节点的数据进行XOR操作
    // 这里我们知道节点0中包含x_3^1，用它来帮助恢复
    shift_amount = local_shift_matrix[0][0]; // 获取x_3^1在局部奇偶校验中的偏移
    
    for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t source_offset = slice_id * SLICE;
        size_t target_offset = (slice_id + shift_amount) * SLICE;
        
        if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
            // 获取x_3^1数据
            uint64_t data = *(uint64_t*)(message_blocks[0][2] + source_offset);
            
            // 与输出块XOR
            *(uint64_t*)(output_blocks[1][2] + target_offset) ^= data;
        }
    }
    
    // 3. 修复局部奇偶校验块：p_3^1
    // 清空局部奇偶校验块
    memset(local_parity_blocks[2][0], 0, LOCAL_PARITY_SIZE);
    
    // 重新计算局部奇偶校验
    for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
        shift_amount = local_shift_matrix[0][sub_idx];
        
        for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            size_t source_offset = slice_id * SLICE;
            size_t target_offset = (slice_id + shift_amount) * SLICE;
            
            if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
                uint64_t data = *(uint64_t*)(message_blocks[sub_idx][2] + source_offset);
                *(uint64_t*)(local_parity_blocks[2][0] + target_offset) ^= data;
            }
        }
    }
}

/* 测量节点1修复性能的函数 */
void measure_node_1_repair_performance() {
    std::cout << "\nMeasuring repair performance for node 1..." << std::endl;
    
    // 先执行一次修复，确认正确性
    repair_node_1();
    
    // 验证修复是否成功
    bool success = true;
    
    // 验证全局块：x_2^1
    if (memcmp(message_blocks[0][1], output_blocks[0][1], CHUNK_SIZE) != 0) {
        std::cout << "Block x_2^1 was not correctly recovered!" << std::endl;
        success = false;
    }
    
    // 验证全局块：x_3^2
    if (memcmp(message_blocks[1][2], output_blocks[1][2], CHUNK_SIZE) != 0) {
        std::cout << "Block x_3^2 was not correctly recovered!" << std::endl;
        success = false;
    }
    
    if (success) {
        std::cout << "Initial repair verification successful!" << std::endl;
    } else {
        std::cout << "Initial repair verification failed!" << std::endl;
        return;
    }
    
    // 计算需要修复的数据大小 (两个数据块 + 一个局部奇偶校验块)
    size_t repair_data_size = 2 * CHUNK_SIZE + LOCAL_PARITY_SIZE;
    
    // 测量修复性能
    std::cout << "Starting repair performance test with " << SAMPLE_NUM << " iterations..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (uint32_t i = 0; i < SAMPLE_NUM; i++) {
        repair_node_1();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double exec_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    double exec_time_s = exec_time_us / 1000000.0;
    double data_size_MB = (SAMPLE_NUM * repair_data_size) / (1024.0 * 1024.0);
    double throughput_MBps = data_size_MB / exec_time_s;
    
    std::cout << "Repair completed in " << exec_time_us << " microseconds" << std::endl;
    std::cout << "Repair throughput: " << throughput_MBps << " MB/s" << std::endl;
}

/* Main function */
int main() {
    std::cout << "Shift-XOR LRC Test (9,6,12,L,3,2,2) System" << std::endl;
    std::cout << "Data blocks per sub-packet: " << DATA_BLOCKS << std::endl;
    std::cout << "Global parity blocks per sub-packet: " << PARITY_BLOCKS << std::endl;
    std::cout << "Sub-packets: " << SUB_PACKETS << std::endl;
    std::cout << "Locality: " << LOCALITY << std::endl;
    std::cout << "Local parity per node: " << LOCAL_PARITY << std::endl;
    std::cout << "Total storage nodes: " << CODE_BLOCKS << std::endl;
    std::cout << "Chunk size: " << CHUNK_SIZE << " bytes" << std::endl;
    
    // Generate the shift matrices
    generate_global_shift_matrix();
    generate_local_shift_matrix();

    // Initialize buffers and node placement
    initialize_buffers();
    
    // Generate test data
    generate_test_data();
    
    // Measure encoding performance
    std::cout << "\nStarting encoding test with " << SAMPLE_NUM << " iterations..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (uint32_t i = 0; i < SAMPLE_NUM; i++) {
        encode();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double exec_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double exec_time_s = exec_time_us / 1000000.0;
    double data_size_MB = (SAMPLE_NUM * (TOTAL_DATA_BLOCKS * CHUNK_SIZE)) / (1024.0 * 1024.0);
    double throughput_MBps = data_size_MB / exec_time_s;
    
    std::cout << "Encoding completed in " << exec_time_us << " microseconds" << std::endl;
    std::cout << "Encoding throughput: " << throughput_MBps << " MB/s" << std::endl;
    
    // Test decoding with blocks 3, 4, and 5 missing from both sub-packets
    std::cout << "\nTesting decoding with blocks 3, 4, and 5 missing from both sub-packets..." << std::endl;
    
    // Measure decoding performance
    start = std::chrono::high_resolution_clock::now();
    
    for (uint32_t i = 0; i < SAMPLE_NUM; i++) {
        decode_missing_blocks();
    }
    
    end = std::chrono::high_resolution_clock::now();
    exec_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    exec_time_s = exec_time_us / 1000000.0;
    data_size_MB = (SAMPLE_NUM * (SUB_PACKETS * 3 * CHUNK_SIZE)) / (1024.0 * 1024.0);  // 恢复2个子包中各3个块
    throughput_MBps = data_size_MB / exec_time_s;
    
    std::cout << "Decoding completed in " << exec_time_us << " microseconds" << std::endl;
    std::cout << "Decoding throughput: " << throughput_MBps << " MB/s" << std::endl;
    
    // Validate results
    if (validate_decoding()) {
        std::cout << "Decoding validation successful! All blocks were correctly recovered." << std::endl;
    } else {
        std::cout << "Decoding validation failed! Some blocks were not correctly recovered." << std::endl;
    }
    
    // Test local repair capability
    std::cout << "\nTesting local repair capability..." << std::endl;
    int failed_node_idx = 2;  // Example: Node 2 has failed
    repair_failed_node(failed_node_idx);
    
    // 测量节点1修复性能
    measure_node_1_repair_performance();
    
    // Clean up
    cleanup_buffers();
    
    return 0;
} 