/**
 * @filename: shift_xor_lrc_avx2.cpp
 * @desc: Implementation of (9,6,12,L,3,2,2) shift-XOR LRC encoding with AVX2 acceleration
 * @author: Based on shift_xor_lrc.cpp
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

// Add OpenMP header
#include <omp.h>

// AVX2 headers
#include <immintrin.h>
#include <emmintrin.h>

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
constexpr int LOCAL_MAX_SHIFT = (LOCAL_PARITY * (SUB_PACKETS - 1)) == 0 ? 1 : (LOCAL_PARITY * (SUB_PACKETS - 1)) / 2;     // Prevent division by zero

// 文件和块大小参数
constexpr size_t FILE_SIZE = 768 * 4 * 1024UL;
constexpr size_t CHUNK_SIZE = FILE_SIZE / TOTAL_DATA_BLOCKS;
constexpr size_t SLICE = 32;  // 使用32字节的切片以适应AVX2的256位操作
constexpr size_t TOTAL_SLICE_NUM = CHUNK_SIZE / SLICE;  // L in the description
constexpr size_t PARITY_PADDING = SLICE * MAX_SHIFT_TWO_TONE;
constexpr size_t PARITY_SIZE = CHUNK_SIZE + PARITY_PADDING;
constexpr size_t LOCAL_PARITY_SIZE = CHUNK_SIZE + SLICE * LOCAL_MAX_SHIFT;

// 测试参数
constexpr uint32_t SAMPLE_NUM = 50UL;

/* Data structures */
// Flattened buffer declarations (replacing nested vectors)
uint8_t** flat_message_blocks;      // [SUB_PACKETS * DATA_BLOCKS]
uint8_t** flat_global_parity_blocks; // [SUB_PACKETS * PARITY_BLOCKS]
uint8_t** flat_local_parity_blocks;  // [CODE_BLOCKS * LOCAL_PARITY]
uint8_t** flat_output_blocks;       // [SUB_PACKETS * DATA_BLOCKS]

// Helper macros for indexing
#define MSG_BLOCK(sub_idx, block_idx) flat_message_blocks[(sub_idx) * DATA_BLOCKS + (block_idx)]
#define PARITY_BLOCK(sub_idx, parity_idx) flat_global_parity_blocks[(sub_idx) * PARITY_BLOCKS + (parity_idx)]
#define LOCAL_PARITY_BLOCK(code_idx, local_idx) flat_local_parity_blocks[(code_idx) * LOCAL_PARITY + (local_idx)]
#define OUTPUT_BLOCK(sub_idx, block_idx) flat_output_blocks[(sub_idx) * DATA_BLOCKS + (block_idx)]

// Keep the Node structure for reference
struct Node {
    std::vector<std::pair<int, int>> global_blocks;
    std::vector<std::pair<int, int>> local_blocks;
};

// Array of nodes and repair groups
std::vector<Node> nodes;
std::vector<std::vector<int>> repair_groups;

/* Shift matrices */
int global_shift_matrix[PARITY_BLOCKS][DATA_BLOCKS];  // Φ matrix
int local_shift_matrix[LOCAL_PARITY][SUB_PACKETS];    // Ω matrix

/* Code Conversion Parameters*/
constexpr int LAMBDA = 2;                      // Number of initial codewords
constexpr int INITIAL_DATA_BLOCKS = DATA_BLOCKS;    // k^I = 6
constexpr int INITIAL_PARITY_BLOCKS = PARITY_BLOCKS; // g^I = 3
constexpr int INITIAL_CODE_BLOCKS = CODE_BLOCKS;    // n^I = 9
constexpr int INITIAL_SUB_PACKETS = SUB_PACKETS;    // r = 2
constexpr int INITIAL_REPAIR_GROUP_SIZE = REPAIR_GROUP_SIZE; // r_δ = 3

// Final code parameters after conversion
constexpr int FINAL_DATA_BLOCKS = LAMBDA * INITIAL_DATA_BLOCKS;  // k^F = 12
constexpr int FINAL_PARITY_BLOCKS = 3;        // g^F = 3 (λg^I - tr_δ with t=1)
constexpr int FINAL_CODE_BLOCKS = FINAL_DATA_BLOCKS + FINAL_PARITY_BLOCKS; // n^F = 15

// Data structures for two initial codewords and the final merged codeword
uint8_t*** initial_message_blocks;      // [LAMBDA][SUB_PACKETS * DATA_BLOCKS]
uint8_t*** initial_global_parity_blocks; // [LAMBDA][SUB_PACKETS * PARITY_BLOCKS]
uint8_t*** initial_local_parity_blocks;  // [LAMBDA][CODE_BLOCKS * LOCAL_PARITY]

uint8_t** final_message_blocks;      // [SUB_PACKETS * FINAL_DATA_BLOCKS]
uint8_t** final_global_parity_blocks; // [SUB_PACKETS * FINAL_PARITY_BLOCKS]
uint8_t** final_local_parity_blocks;  // [FINAL_CODE_BLOCKS * LOCAL_PARITY]

// Node access tracking 
bool* node_accessed;  // [LAMBDA * INITIAL_CODE_BLOCKS]

// Helper macros for indexing into the initial and final structures
#define INITIAL_MSG_BLOCK(lambda, sub_idx, block_idx) initial_message_blocks[lambda][(sub_idx) * DATA_BLOCKS + (block_idx)]
#define INITIAL_PARITY_BLOCK(lambda, sub_idx, parity_idx) initial_global_parity_blocks[lambda][(sub_idx) * PARITY_BLOCKS + (parity_idx)]
#define INITIAL_LOCAL_PARITY_BLOCK(lambda, code_idx, local_idx) initial_local_parity_blocks[lambda][(code_idx) * LOCAL_PARITY + (local_idx)]

#define FINAL_MSG_BLOCK(sub_idx, block_idx) final_message_blocks[(sub_idx) * FINAL_DATA_BLOCKS + (block_idx)]
#define FINAL_PARITY_BLOCK(sub_idx, parity_idx) final_global_parity_blocks[(sub_idx) * FINAL_PARITY_BLOCKS + (parity_idx)]
#define FINAL_LOCAL_PARITY_BLOCK(code_idx, local_idx) final_local_parity_blocks[(code_idx) * LOCAL_PARITY + (local_idx)]

// Global shift and local shift matrices for the final code
int final_global_shift_matrix[FINAL_PARITY_BLOCKS][FINAL_DATA_BLOCKS];
int final_local_shift_matrix[LOCAL_PARITY][SUB_PACKETS];

/* Function declarations */
void initialize_node_placement();
void generate_global_shift_matrix();
void generate_local_shift_matrix();
void initialize_buffers();
void cleanup_buffers();
void generate_test_data();
void encode_global_parity();
void encode_local_parity();
void encode();
void decode_missing_blocks();
bool validate_decoding();
void repair_node_1();
void measure_node_1_repair_performance();
void perform_cross_file_parity_operations();

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
    
    std::cout << "Global Shift Matrix (Φ):" << std::endl;
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        std::cout << "Row " << i << ": ";
        for (int j = 0; j < DATA_BLOCKS; j++) {
            std::cout << global_shift_matrix[i][j] << " ";
        }
        std::cout << std::endl;
    }
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
    // Allocate flat arrays
    flat_message_blocks = new uint8_t*[SUB_PACKETS * DATA_BLOCKS];
    flat_global_parity_blocks = new uint8_t*[SUB_PACKETS * PARITY_BLOCKS];
    flat_local_parity_blocks = new uint8_t*[CODE_BLOCKS * LOCAL_PARITY];
    flat_output_blocks = new uint8_t*[SUB_PACKETS * DATA_BLOCKS];
    
    // Initialize message blocks
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < DATA_BLOCKS; j++) {
            MSG_BLOCK(i, j) = new uint8_t[CHUNK_SIZE]();
        }
    }
    
    // Initialize global parity blocks
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < PARITY_BLOCKS; j++) {
            PARITY_BLOCK(i, j) = new uint8_t[PARITY_SIZE]();
        }
    }
    
    // Initialize local parity blocks
    for (int i = 0; i < CODE_BLOCKS; i++) {
        for (int j = 0; j < LOCAL_PARITY; j++) {
            LOCAL_PARITY_BLOCK(i, j) = new uint8_t[LOCAL_PARITY_SIZE]();
        }
    }
    
    // Initialize output blocks
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < DATA_BLOCKS; j++) {
            OUTPUT_BLOCK(i, j) = new uint8_t[CHUNK_SIZE]();
        }
    }
    
    // Initialize nodes with placement
    initialize_node_placement();
}

/* Clean up memory buffers */
void cleanup_buffers() {
    // Free message blocks
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < DATA_BLOCKS; j++) {
            delete[] MSG_BLOCK(i, j);
        }
    }
    
    // Free global parity blocks
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < PARITY_BLOCKS; j++) {
            delete[] PARITY_BLOCK(i, j);
        }
    }
    
    // Free local parity blocks
    for (int i = 0; i < CODE_BLOCKS; i++) {
        for (int j = 0; j < LOCAL_PARITY; j++) {
            delete[] LOCAL_PARITY_BLOCK(i, j);
        }
    }
    
    // Free output blocks
    for (int i = 0; i < SUB_PACKETS; i++) {
        for (int j = 0; j < DATA_BLOCKS; j++) {
            delete[] OUTPUT_BLOCK(i, j);
        }
    }
    
    // Free flat arrays
    delete[] flat_message_blocks;
    delete[] flat_global_parity_blocks;
    delete[] flat_local_parity_blocks;
    delete[] flat_output_blocks;
    
    // Clear vectors
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
                MSG_BLOCK(i, j)[k] = dis(gen);
            }
        }
    }
}

/* Step 1: Global Parity Generation using shift-XOR with AVX2 acceleration */
void encode_global_parity() {
    // Parallelize across sub-packets
    #pragma omp parallel for
    for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
        // Clear global parity blocks
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            memset(PARITY_BLOCK(sub_idx, i), 0, PARITY_SIZE);
        }
        
        // Use registers more efficiently
        __m256i msg_data, parity_data, xor_result;
        
        // For each slice in the message blocks
        for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            size_t slice_offset = slice_id * SLICE;
            
            // For each parity block
            for (int i = 0; i < PARITY_BLOCKS; i++) {
                // For each message block
                for (int j = 0; j < DATA_BLOCKS; j++) {
                    // Calculate shift amount for this parity-message pair
                    int shift_amount = global_shift_matrix[i][j];
                    size_t target_offset = (slice_id + shift_amount) * SLICE;
                    
                    if (target_offset + SLICE <= PARITY_SIZE) {
                        // Load message data directly (no temporary variables)
                        msg_data = _mm256_loadu_si256((__m256i*)(MSG_BLOCK(sub_idx, j) + slice_offset));
                        parity_data = _mm256_loadu_si256((__m256i*)(PARITY_BLOCK(sub_idx, i) + target_offset));
                        xor_result = _mm256_xor_si256(msg_data, parity_data);
                        _mm256_storeu_si256((__m256i*)(PARITY_BLOCK(sub_idx, i) + target_offset), xor_result);
                    }
                }
            }
        }
    }
}

/* Step 2: Local Parity Generation using shift-XOR with AVX2 acceleration */
void encode_local_parity() {
    // Clear all local parity blocks
    #pragma omp parallel for
    for (int i = 0; i < CODE_BLOCKS; i++) {
        for (int j = 0; j < LOCAL_PARITY; j++) {
            memset(LOCAL_PARITY_BLOCK(i, j), 0, LOCAL_PARITY_SIZE);
        }
    }
    
    __m256i block_data, parity_data, xor_result;
    
    // For each slice in the blocks
    #pragma omp parallel for private(block_data, parity_data, xor_result)
    for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t slice_offset = slice_id * SLICE;
        
        // For each code block (column in the placement table)
        for (int code_idx = 0; code_idx < CODE_BLOCKS; code_idx++) {
            // For each local parity block
            for (int local_idx = 0; local_idx < LOCAL_PARITY; local_idx++) {
                // For each sub-packet
                for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
                    // Calculate shift amount for this local parity - global block pair
                    int shift_amount = local_shift_matrix[local_idx][sub_idx];
                    size_t target_offset = (slice_id + shift_amount) * SLICE;
                    
                    if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
                        // Check if code_idx refers to a data block or parity block
                        if (code_idx < DATA_BLOCKS) {
                            // Data block
                            block_data = _mm256_loadu_si256((__m256i*)(MSG_BLOCK(sub_idx, code_idx) + slice_offset));
                        } else {
                            // Parity block
                            block_data = _mm256_loadu_si256((__m256i*)(PARITY_BLOCK(sub_idx, code_idx - DATA_BLOCKS) + slice_offset));
                        }
                        
                        // XOR with local parity block using AVX2
                        parity_data = _mm256_loadu_si256((__m256i*)(LOCAL_PARITY_BLOCK(code_idx, local_idx) + target_offset));
                        xor_result = _mm256_xor_si256(block_data, parity_data);
                        _mm256_storeu_si256((__m256i*)(LOCAL_PARITY_BLOCK(code_idx, local_idx) + target_offset), xor_result);
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

/* Decode when blocks 3-5 of both sub-packets are missing using AVX2 */
void decode_missing_blocks() {
    // Define missing indices (we'll assume blocks 3-5 from both sub-packets are missing)
    std::vector<int> missing_indices = {3, 4, 5};  // 0-based indices
    
    // Process each sub-packet
    #pragma omp parallel for
    for (int missing_sub_packet = 0; missing_sub_packet < SUB_PACKETS; missing_sub_packet++) {
        // Create temporary parity blocks for decoding - use flat array
        uint8_t* decode_arr[PARITY_BLOCKS];
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            decode_arr[i] = new uint8_t[PARITY_SIZE]();
            memcpy(decode_arr[i], PARITY_BLOCK(missing_sub_packet, i), PARITY_SIZE);
        }
        
        __m256i msg_data, parity_data, xor_result;
        
        // Back substitution for available blocks (0-2)
        // For each slice in the block
        for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            size_t slice_offset = slice_id * SLICE;
            
            // For each parity block
            for (int i = 0; i < PARITY_BLOCKS; i++) {
                // For each available data block
                for (int j = 0; j < DATA_BLOCKS - missing_indices.size(); j++) {
                    // Get shift amount for this parity-message pair
                    int shift_amount = global_shift_matrix[i][j];
                    size_t target_offset = (slice_id + shift_amount) * SLICE;               
                    
                    if (target_offset + SLICE <= PARITY_SIZE) {
                        // Load message data using AVX2
                        msg_data = _mm256_loadu_si256((__m256i*)(MSG_BLOCK(missing_sub_packet, j) + slice_offset));
                        
                        // XOR with parity block using AVX2 (back substitution)
                        parity_data = _mm256_loadu_si256((__m256i*)(decode_arr[i] + target_offset));
                        xor_result = _mm256_xor_si256(msg_data, parity_data);
                        _mm256_storeu_si256((__m256i*)(decode_arr[i] + target_offset), xor_result);
                    }
                }
            }
        }
        
        // For each slice
        for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            size_t slice_offset = slice_id * SLICE;
            
            // Step 1: solve block with index 3 (missing block #1)
            size_t offset_0 = (slice_id + global_shift_matrix[0][3]) * SLICE;
            size_t offset_1 = (slice_id + global_shift_matrix[1][3]) * SLICE;
            size_t offset_2 = (slice_id + global_shift_matrix[2][3]) * SLICE;
            
            if (offset_0 + SLICE <= PARITY_SIZE) {
                // Load data from parity block 0 using AVX2
                msg_data = _mm256_loadu_si256((__m256i*)(decode_arr[0] + offset_0));
                
                // Store to output block
                _mm256_storeu_si256((__m256i*)(OUTPUT_BLOCK(missing_sub_packet, 3) + slice_offset), msg_data);
                
                // Back substitute block with index 3 to other parity blocks
                if (offset_1 + SLICE <= PARITY_SIZE) {
                    parity_data = _mm256_loadu_si256((__m256i*)(decode_arr[1] + offset_1));
                    xor_result = _mm256_xor_si256(msg_data, parity_data);
                    _mm256_storeu_si256((__m256i*)(decode_arr[1] + offset_1), xor_result);
                }
                
                if (offset_2 + SLICE <= PARITY_SIZE) {
                    parity_data = _mm256_loadu_si256((__m256i*)(decode_arr[2] + offset_2));
                    xor_result = _mm256_xor_si256(msg_data, parity_data);
                    _mm256_storeu_si256((__m256i*)(decode_arr[2] + offset_2), xor_result);
                }
            }
            
            // Step 2: solve block with index 5 (missing block #3)
            offset_0 = (slice_id + global_shift_matrix[0][5]) * SLICE;
            offset_1 = (slice_id + global_shift_matrix[1][5]) * SLICE;
            offset_2 = (slice_id + global_shift_matrix[2][5]) * SLICE;
            
            if (offset_2 + SLICE <= PARITY_SIZE) {
                // Load data from parity block 2 using AVX2
                msg_data = _mm256_loadu_si256((__m256i*)(decode_arr[2] + offset_2));
                
                // Store to output block
                _mm256_storeu_si256((__m256i*)(OUTPUT_BLOCK(missing_sub_packet, 5) + slice_offset), msg_data);
                
                // Back substitute block with index 5 to other parity blocks
                if (offset_0 + SLICE <= PARITY_SIZE) {
                    parity_data = _mm256_loadu_si256((__m256i*)(decode_arr[0] + offset_0));
                    xor_result = _mm256_xor_si256(msg_data, parity_data);
                    _mm256_storeu_si256((__m256i*)(decode_arr[0] + offset_0), xor_result);
                }
                
                if (offset_1 + SLICE <= PARITY_SIZE) {
                    parity_data = _mm256_loadu_si256((__m256i*)(decode_arr[1] + offset_1));
                    xor_result = _mm256_xor_si256(msg_data, parity_data);
                    _mm256_storeu_si256((__m256i*)(decode_arr[1] + offset_1), xor_result);
                }
            }
            
            // Step 3: solve block with index 4 (missing block #2)
            offset_0 = (slice_id + global_shift_matrix[0][4]) * SLICE;
            offset_1 = (slice_id + global_shift_matrix[1][4]) * SLICE;
            offset_2 = (slice_id + global_shift_matrix[2][4]) * SLICE;
            
            if (offset_1 + SLICE <= PARITY_SIZE) {
                // Load data from parity block 1 using AVX2
                msg_data = _mm256_loadu_si256((__m256i*)(decode_arr[1] + offset_1));
                
                // Store to output block
                _mm256_storeu_si256((__m256i*)(OUTPUT_BLOCK(missing_sub_packet, 4) + slice_offset), msg_data);
                
                // Back substitute block with index 4 to other parity blocks (not necessary for last block)
                if (offset_0 + SLICE <= PARITY_SIZE) {
                    parity_data = _mm256_loadu_si256((__m256i*)(decode_arr[0] + offset_0));
                    xor_result = _mm256_xor_si256(msg_data, parity_data);
                    _mm256_storeu_si256((__m256i*)(decode_arr[0] + offset_0), xor_result);
                }
                
                if (offset_2 + SLICE <= PARITY_SIZE) {
                    parity_data = _mm256_loadu_si256((__m256i*)(decode_arr[2] + offset_2));
                    xor_result = _mm256_xor_si256(msg_data, parity_data);
                    _mm256_storeu_si256((__m256i*)(decode_arr[2] + offset_2), xor_result);
                }
            }
        }
        
        // Clean up temporary parity blocks
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            delete[] decode_arr[i];
        }
    }
}

/* Validate decoding results */
bool validate_decoding() {
    std::vector<int> missing_indices = {3, 4, 5};
    bool success = true;
    
    #pragma omp parallel for reduction(&&:success)
    for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
        for (int idx : missing_indices) {
            if (memcmp(MSG_BLOCK(sub_idx, idx), 
                      OUTPUT_BLOCK(sub_idx, idx), CHUNK_SIZE) != 0) {
                #pragma omp critical
                {
                    std::cout << "Block " << idx << " of sub-packet " << sub_idx 
                              << " was not correctly recovered!" << std::endl;
                }
                success = false;
            }
        }
    }
    
    return success;
}

/* 专门修复节点1的函数 - 使用扁平化数组结构 */
void repair_node_1() {
    const int failed_node_idx = 1;
    
    // 1. 修复全局块：x_2^1（sub_packet=0, block_idx=1）
    // 清空输出块
    memset(OUTPUT_BLOCK(0, 1), 0, CHUNK_SIZE);
    
    // 针对这个特定的块，找到保护它的局部奇偶校验块
    uint8_t* local_parity = LOCAL_PARITY_BLOCK(1, 0);
    
    // 使用局部奇偶校验作为起点
    __m256i lp, data, output, result, parity_data;
    
    #pragma omp parallel for private(lp)
    for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t slice_offset = slice_id * SLICE;
        lp = _mm256_loadu_si256((__m256i*)(local_parity + slice_offset));
        _mm256_storeu_si256((__m256i*)(OUTPUT_BLOCK(0, 1) + slice_offset), lp);
    }
    
    // 与帮助节点的数据进行XOR操作
    int shift_amount = local_shift_matrix[0][1];
    
    // #pragma omp parallel for private(data, output, result)
    for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t source_offset = slice_id * SLICE;
        size_t target_offset = (slice_id + shift_amount) * SLICE;
        
        if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
            data = _mm256_loadu_si256((__m256i*)(MSG_BLOCK(1, 1) + source_offset));
            output = _mm256_loadu_si256((__m256i*)(OUTPUT_BLOCK(0, 1) + target_offset));
            result = _mm256_xor_si256(data, output);
            _mm256_storeu_si256((__m256i*)(OUTPUT_BLOCK(0, 1) + target_offset), result);
        }
    }
    
    // 2. 修复全局块：x_3^2（sub_packet=1, block_idx=2）
    // 清空输出块
    memset(OUTPUT_BLOCK(1, 2), 0, CHUNK_SIZE);
    
    // 针对这个特定的块，找到保护它的局部奇偶校验块
    local_parity = LOCAL_PARITY_BLOCK(2, 0);
    
    #pragma omp parallel for private(lp)
    for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t slice_offset = slice_id * SLICE;
        lp = _mm256_loadu_si256((__m256i*)(local_parity + slice_offset));
        _mm256_storeu_si256((__m256i*)(OUTPUT_BLOCK(1, 2) + slice_offset), lp);
    }
    
    // 与帮助节点的数据进行XOR操作
    shift_amount = local_shift_matrix[0][0];
    
    #pragma omp parallel for private(data, output, result)
    for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t source_offset = slice_id * SLICE;
        size_t target_offset = (slice_id + shift_amount) * SLICE;
        
        if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
            data = _mm256_loadu_si256((__m256i*)(MSG_BLOCK(0, 2) + source_offset));
            output = _mm256_loadu_si256((__m256i*)(OUTPUT_BLOCK(1, 2) + target_offset));
            result = _mm256_xor_si256(data, output);
            _mm256_storeu_si256((__m256i*)(OUTPUT_BLOCK(1, 2) + target_offset), result);
        }
    }
    
    // 3. 修复局部奇偶校验块：p_3^1
    // 清空局部奇偶校验块
    memset(LOCAL_PARITY_BLOCK(2, 0), 0, LOCAL_PARITY_SIZE);
    
    // 重新计算局部奇偶校验 - this loop has dependencies, so parallelize the inner loop
    for (int sub_idx = 0; sub_idx < SUB_PACKETS; sub_idx++) {
        shift_amount = local_shift_matrix[0][sub_idx];
        
        #pragma omp parallel for private(data, parity_data, result)
        for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            size_t source_offset = slice_id * SLICE;
            size_t target_offset = (slice_id + shift_amount) * SLICE;
            
            if (target_offset + SLICE <= LOCAL_PARITY_SIZE) {
                data = _mm256_loadu_si256((__m256i*)(MSG_BLOCK(sub_idx, 2) + source_offset));
                parity_data = _mm256_loadu_si256((__m256i*)(LOCAL_PARITY_BLOCK(2, 0) + target_offset));
                result = _mm256_xor_si256(data, parity_data);
                _mm256_storeu_si256((__m256i*)(LOCAL_PARITY_BLOCK(2, 0) + target_offset), result);
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
    if (memcmp(MSG_BLOCK(0, 1), OUTPUT_BLOCK(0, 1), CHUNK_SIZE) != 0) {
        std::cout << "Block x_2^1 was not correctly recovered!" << std::endl;
        success = false;
    }
    
    // 验证全局块：x_3^2
    if (memcmp(MSG_BLOCK(1, 2), OUTPUT_BLOCK(1, 2), CHUNK_SIZE) != 0) {
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

/* Perform cross-file parity operations and measure throughput */
void perform_cross_file_parity_operations() {
    std::cout << "\nPerforming cross-file parity operations..." << std::endl;
    
    // First, we need to create another set of encoded data (for the second file)
    // We'll temporarily store the first file's parity blocks
    uint8_t** temp_global_parity = new uint8_t*[SUB_PACKETS * PARITY_BLOCKS];
    for (int i = 0; i < SUB_PACKETS * PARITY_BLOCKS; i++) {
        temp_global_parity[i] = new uint8_t[PARITY_SIZE];
        memcpy(temp_global_parity[i], flat_global_parity_blocks[i], PARITY_SIZE);
    }
    
    // Generate new random data for the second file
    for (int i = 0; i < SUB_PACKETS * DATA_BLOCKS; i++) {
        for (size_t j = 0; j < CHUNK_SIZE; j += sizeof(uint32_t)) {
            *reinterpret_cast<uint32_t*>(flat_message_blocks[i] + j) = static_cast<uint32_t>(rand());
        }
    }
    
    // Encode the second file
    encode();
    
    // Now flat_global_parity_blocks contains the parity blocks for file (2)
    // and temp_global_parity contains the parity blocks for file (1)
    
    // Create buffers for the new parity blocks (9 of them)
    uint8_t** new_parity_blocks = new uint8_t*[9];
    for (int i = 0; i < 9; i++) {
        // The size should accommodate the shift operations (z^6 means adding 6*SLICE)
        new_parity_blocks[i] = new uint8_t[PARITY_SIZE + 6*SLICE];
        memset(new_parity_blocks[i], 0, PARITY_SIZE + 6*SLICE);
    }
    
    // Define constants for better readability
    const size_t SHIFT_AMOUNT = 6 * SLICE; // z^6 means shift by 6 slices
    
    // Measure throughput
    auto start = std::chrono::high_resolution_clock::now();
    
    for (uint32_t sample = 0; sample < SAMPLE_NUM; sample++) {
        // For each slice in the parity blocks
        #pragma omp parallel for
        for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            size_t slice_offset = slice_id * SLICE;
            __m256i data1, data2, data3, data4, result;
            
            // 1. y_1^{1,(1)} + z^6y_1^{1,(2)}
            if (slice_offset + SLICE <= PARITY_SIZE) {
                // Load data from file 1 (temp_global_parity)
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[0] + slice_offset));
                
                // XOR with shifted data from file 2 (if within bounds)
                if (slice_id >= 6 && (slice_offset - SHIFT_AMOUNT + SLICE) <= PARITY_SIZE) {
                    data2 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[0] + (slice_offset - SHIFT_AMOUNT)));
                    result = _mm256_xor_si256(data1, data2);
                } else {
                    result = data1;
                }
                
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[0] + slice_offset), result);
            }
            
            // 2. y_2^{1,(1)} + y_2^{1,(2)}
            if (slice_offset + SLICE <= PARITY_SIZE) {
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[1] + slice_offset));
                data2 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[1] + slice_offset));
                result = _mm256_xor_si256(data1, data2);
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[1] + slice_offset), result);
            }
            
            // 3. z^6y_3^{1,(1)} + y_3^{1,(2)}
            if ((slice_offset + SHIFT_AMOUNT + SLICE) <= (PARITY_SIZE + SHIFT_AMOUNT)) {
                // Apply shift to file 1 data
                size_t shifted_offset = slice_offset + SHIFT_AMOUNT;
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[2] + slice_offset));
                
                // XOR with data from file 2
                if (shifted_offset < PARITY_SIZE + SHIFT_AMOUNT) {
                    data2 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[2] + shifted_offset));
                    result = _mm256_xor_si256(data1, data2);
                } else {
                    result = data1;
                }
                
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[2] + shifted_offset), result);
            }
            
            // 4. y_1^{2,(1)} + z^6y_1^{2,(2)}
            if (slice_offset + SLICE <= PARITY_SIZE) {
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[3] + slice_offset));
                
                if (slice_id >= 6 && (slice_offset - SHIFT_AMOUNT + SLICE) <= PARITY_SIZE) {
                    data2 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[3] + (slice_offset - SHIFT_AMOUNT)));
                    result = _mm256_xor_si256(data1, data2);
                } else {
                    result = data1;
                }
                
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[3] + slice_offset), result);
            }
            
            // 5. y_2^{2,(1)} + y_2^{2,(2)}
            if (slice_offset + SLICE <= PARITY_SIZE) {
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[4] + slice_offset));
                data2 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[4] + slice_offset));
                result = _mm256_xor_si256(data1, data2);
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[4] + slice_offset), result);
            }
            
            // 6. z^6y_3^{2,(1)} + y_3^{2,(2)}
            if ((slice_offset + SHIFT_AMOUNT + SLICE) <= (PARITY_SIZE + SHIFT_AMOUNT)) {
                size_t shifted_offset = slice_offset + SHIFT_AMOUNT;
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[5] + slice_offset));
                
                if (shifted_offset < PARITY_SIZE + SHIFT_AMOUNT) {
                    data2 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[5] + shifted_offset));
                    result = _mm256_xor_si256(data1, data2);
                } else {
                    result = data1;
                }
                
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[5] + shifted_offset), result);
            }
            
            // 7. y_1^{1,(1)} + z^6y_1^{1,(2)} + y_1^{2,(1)} + z^6y_1^{2,(2)}
            if (slice_offset + SLICE <= PARITY_SIZE) {
                // Start with y_1^{1,(1)}
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[0] + slice_offset));
                
                // Add y_1^{2,(1)}
                data2 = _mm256_loadu_si256((__m256i*)(temp_global_parity[3] + slice_offset));
                result = _mm256_xor_si256(data1, data2);
                
                // Add z^6y_1^{1,(2)} if within bounds
                if (slice_id >= 6 && (slice_offset - SHIFT_AMOUNT + SLICE) <= PARITY_SIZE) {
                    data3 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[0] + (slice_offset - SHIFT_AMOUNT)));
                    result = _mm256_xor_si256(result, data3);
                }
                
                // Add z^6y_1^{2,(2)} if within bounds
                if (slice_id >= 6 && (slice_offset - SHIFT_AMOUNT + SLICE) <= PARITY_SIZE) {
                    data4 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[3] + (slice_offset - SHIFT_AMOUNT)));
                    result = _mm256_xor_si256(result, data4);
                }
                
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[6] + slice_offset), result);
            }
            
            // 8. y_2^{1,(1)} + y_2^{1,(2)} + y_2^{2,(1)} + y_2^{2,(2)}
            if (slice_offset + SLICE <= PARITY_SIZE) {
                // y_2^{1,(1)}
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[1] + slice_offset));
                
                // Add y_2^{1,(2)}
                data2 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[1] + slice_offset));
                result = _mm256_xor_si256(data1, data2);
                
                // Add y_2^{2,(1)}
                data3 = _mm256_loadu_si256((__m256i*)(temp_global_parity[4] + slice_offset));
                result = _mm256_xor_si256(result, data3);
                
                // Add y_2^{2,(2)}
                data4 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[4] + slice_offset));
                result = _mm256_xor_si256(result, data4);
                
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[7] + slice_offset), result);
            }
            
            // 9. z^6y_3^{1,(1)} + y_3^{1,(2)} + z^6y_3^{2,(1)} + y_3^{2,(2)}
            if ((slice_offset + SHIFT_AMOUNT + SLICE) <= (PARITY_SIZE + SHIFT_AMOUNT)) {
                size_t shifted_offset = slice_offset + SHIFT_AMOUNT;
                
                // Initialize with zeros
                result = _mm256_setzero_si256();
                
                // Add z^6y_3^{1,(1)}
                data1 = _mm256_loadu_si256((__m256i*)(temp_global_parity[2] + slice_offset));
                result = _mm256_xor_si256(result, data1);
                
                // Add z^6y_3^{2,(1)}
                data2 = _mm256_loadu_si256((__m256i*)(temp_global_parity[5] + slice_offset));
                result = _mm256_xor_si256(result, data2);
                
                // Add y_3^{1,(2)} if within bounds
                if (shifted_offset < PARITY_SIZE + SHIFT_AMOUNT) {
                    data3 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[2] + shifted_offset));
                    result = _mm256_xor_si256(result, data3);
                }
                
                // Add y_3^{2,(2)} if within bounds
                if (shifted_offset < PARITY_SIZE + SHIFT_AMOUNT) {
                    data4 = _mm256_loadu_si256((__m256i*)(flat_global_parity_blocks[5] + shifted_offset));
                    result = _mm256_xor_si256(result, data4);
                }
                
                _mm256_storeu_si256((__m256i*)(new_parity_blocks[8] + shifted_offset), result);
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double exec_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    double exec_time_s = exec_time_us / 1000000.0;
    
    // The data processed includes the global parity blocks from two files
    double data_size_MB = (SAMPLE_NUM * (2 * SUB_PACKETS * PARITY_BLOCKS * PARITY_SIZE)) / (1024.0 * 1024.0);
    double throughput_MBps = data_size_MB / exec_time_s;
    
    std::cout << "Cross-file parity operations completed in " << exec_time_us << " microseconds" << std::endl;
    std::cout << "Cross-file parity throughput: " << throughput_MBps << " MB/s" << std::endl;
    
    // Clean up temporary buffers
    for (int i = 0; i < SUB_PACKETS * PARITY_BLOCKS; i++) {
        delete[] temp_global_parity[i];
    }
    delete[] temp_global_parity;
    
    for (int i = 0; i < 9; i++) {
        delete[] new_parity_blocks[i];
    }
    delete[] new_parity_blocks;
}

int main() {
    std::cout << "Shift-XOR LRC Test (9,6,12,L,3,2,2) System" << std::endl;
    std::cout << "Data blocks per sub-packet: " << DATA_BLOCKS << std::endl;
    std::cout << "Global parity blocks per sub-packet: " << PARITY_BLOCKS << std::endl;
    std::cout << "Sub-packets: " << SUB_PACKETS << std::endl;
    std::cout << "Locality: " << LOCALITY << std::endl;
    std::cout << "Local parity per node: " << LOCAL_PARITY << std::endl;
    std::cout << "Total storage nodes: " << CODE_BLOCKS << std::endl;
    std::cout << "Chunk size: " << CHUNK_SIZE << " bytes" << std::endl;
    std::cout << "Using optimized flat memory layout for better cache performance" << std::endl;
    
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
    // int failed_node_idx = 2;  // Example: Node 2 has failed
    
    // 测量节点1修复性能
    measure_node_1_repair_performance();
    
    // Perform cross-file parity operations and measure throughput
    perform_cross_file_parity_operations();
    
    // Clean up
    cleanup_buffers();
    
    return 0;
} 