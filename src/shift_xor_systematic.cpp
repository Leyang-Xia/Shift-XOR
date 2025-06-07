/**
 * @filename: shift_xor_systematic.cpp
 * @desc: Implementation of systematic shift-XOR encoding
 * @author: Based on Chenhao Wu's work (chenhaowu[at]link.cuhk.edu.cn)  
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

/* Configuration parameters*/

/* System parameters */
constexpr int DATA_BLOCKS = 8;    // k message sequences
constexpr int PARITY_BLOCKS = 3;  // n-k parity sequences
constexpr int CODE_BLOCKS = DATA_BLOCKS + PARITY_BLOCKS;  // n total code sequences
constexpr int MAX_SHIFT_TWO_TONE = (PARITY_BLOCKS * (DATA_BLOCKS - 1)) / 2; // Maximum shift value for a Two-Tone system is (n-k)(k-1)/2

// 文件和块大小参数
constexpr size_t FILE_SIZE = 12 * 1024UL;
constexpr size_t CHUNK_SIZE = FILE_SIZE / 8;
constexpr size_t SLICE = 8;
constexpr size_t TOTAL_SLICE_NUM = CHUNK_SIZE / SLICE;
constexpr size_t PARITY_PADDING = SLICE * MAX_SHIFT_TWO_TONE;
constexpr size_t PARITY_SIZE = CHUNK_SIZE + PARITY_PADDING;

// 测试参数
constexpr uint32_t SAMPLE_NUM = 1000UL;
// const char* ENCODE_FILE = "dummy.txt";

/* Data buffers */
std::vector<uint8_t*> message_blocks;
std::vector<uint8_t*> parity_blocks;
std::vector<uint8_t*> output_blocks;

/* Shift matrix for the parity part (Φ) */
// int shift_matrix[PARITY_BLOCKS][DATA_BLOCKS] = {
//     {0, 1, 2, 3, 4, 5, 6, 7},  // First parity block shift pattern
//     {0, 0, 0, 0, 0, 0, 0, 0},  // Second parity block shift pattern
//     {7, 6, 5, 4, 3, 2, 1, 0}   // Third parity block shift pattern
// };

int shift_matrix[PARITY_BLOCKS][DATA_BLOCKS];

void generate_two_tone_shift_matrix() {
    int d = PARITY_BLOCKS / 2 + 1;
    for (int i = 0; i < PARITY_BLOCKS; i++) {       
        for (int j = 0; j < DATA_BLOCKS; j++) {
            if (i < d) {
                shift_matrix[i][j] = (d - (i + 1)) * j;
            } else {
                shift_matrix[i][j] = ((i + 1) - d) * (DATA_BLOCKS - (j + 1));
            }
        }
    }
}

/* Initialize memory buffers */
void initialize_buffers() {
    // Allocate message blocks
    for (int i = 0; i < DATA_BLOCKS; i++) {
        uint8_t* block = new uint8_t[CHUNK_SIZE]();
        message_blocks.push_back(block);
    }
    
    // Allocate parity blocks
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        uint8_t* block = new uint8_t[PARITY_SIZE]();
        parity_blocks.push_back(block);
    }
    
    // Allocate output blocks for decoded data
    for (int i = 0; i < DATA_BLOCKS; i++) {
        uint8_t* block = new uint8_t[CHUNK_SIZE]();
        output_blocks.push_back(block);
    }
}

/* Free memory buffers */
void cleanup_buffers() {
    for (auto block : message_blocks) delete[] block;
    for (auto block : parity_blocks) delete[] block;
    for (auto block : output_blocks) delete[] block;
    
    message_blocks.clear();
    parity_blocks.clear();
    output_blocks.clear();
}

/* Generate random data for testing */
void generate_test_data() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (int i = 0; i < DATA_BLOCKS; i++) {
        for (size_t j = 0; j < CHUNK_SIZE; j++) {
            message_blocks[i][j] = dis(gen);
        }
    }
}

/* Systematic Shift-XOR encoding */
void encode() {
    // Clear parity blocks
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        memset(parity_blocks[i], 0, PARITY_SIZE);
    }
    
    // For each parity block
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        // For each message block
        for (int j = 0; j < DATA_BLOCKS; j++) {
            // Calculate shift amount for this parity-message pair
            int shift_amount = shift_matrix[i][j];
            
            // For each slice in the message block
            for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
                // Calculate offsets
                size_t source_offset = slice_id * SLICE;
                size_t target_offset = (slice_id + shift_amount) * SLICE;
                // Read data
                uint64_t data = *(uint64_t*)(message_blocks[j] + source_offset);
                // XOR with parity block
                *(uint64_t*)(parity_blocks[i] + target_offset) ^= data;
            }
        }
    }
}

/* Decode using the simple approach from shift_xor_simple.cpp */
void decode() {
    std::vector<int> missing_indices = {5, 6, 7};  // 0-based indices (blocks 5, 6, 7)
    
    // Create temporary parity blocks for decoding
    std::vector<uint8_t*> decode_arr;
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        uint8_t* block = new uint8_t[PARITY_SIZE]();
        memcpy(block, parity_blocks[i], PARITY_SIZE);
        decode_arr.push_back(block);
    }
    
    // Back substitution for available blocks (1-5)
    // For each parity block
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        // For each available data block
        for (int j = 0; j < DATA_BLOCKS - missing_indices.size(); j++) {
            // Get shift amount for this parity-message pair
            int shift_amount = shift_matrix[i][j];
            
            // For each slice in the block
            for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
                // Calculate offsets
                size_t source_offset = slice_id * SLICE;
                size_t target_offset = (slice_id + shift_amount) * SLICE;              
                // Read data from available block
                uint64_t data = *(uint64_t*)(message_blocks[j] + source_offset);                  
                // XOR with corresponding parity block (back substitution)
                *(uint64_t*)(decode_arr[i] + target_offset) ^= data;
            }
        }
    }
    
    
    // For each slice
    for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        // Step 1: solve block 6
        size_t slice_offset_1 = (slice_id + shift_matrix[0][5]) * SLICE;
        size_t slice_offset_2 = slice_id * SLICE;
        size_t slice_offset_3 = (slice_id + shift_matrix[2][5]) * SLICE;
        
        // Directly use parity block 1 to recover block 6
        *(uint64_t*)(output_blocks[5] + slice_offset_2) = *(uint64_t*)(decode_arr[0] + slice_offset_1);
        
        // Back substitute block 6 to other parity blocks
        *(uint64_t*)(decode_arr[1] + slice_offset_2) ^= *(uint64_t*)(output_blocks[5] + slice_offset_2);
        *(uint64_t*)(decode_arr[2] + slice_offset_3) ^= *(uint64_t*)(output_blocks[5] + slice_offset_2);
        
        // Step 2: solve block 8
        slice_offset_1 = (slice_id + shift_matrix[0][7]) * SLICE;
        slice_offset_2 = slice_id * SLICE;
        slice_offset_3 = (slice_id + shift_matrix[2][7]) * SLICE;
        
        // Directly use parity block 3 to recover block with index 7
        *(uint64_t*)(output_blocks[7] + slice_offset_2) = *(uint64_t*)(decode_arr[2] + slice_offset_3);
        
        // Back substitute block 8      
        *(uint64_t*)(decode_arr[0] + slice_offset_1) ^= *(uint64_t*)(output_blocks[7] + slice_offset_2);
        *(uint64_t*)(decode_arr[1] + slice_offset_2) ^= *(uint64_t*)(output_blocks[7] + slice_offset_2);
        
        // Step 3: solve block 7
        slice_offset_1 = (slice_id + shift_matrix[0][6]) * SLICE;
        slice_offset_2 = slice_id * SLICE;
        slice_offset_3 = (slice_id + shift_matrix[2][6]) * SLICE;
        
        // Directly use parity block 2 to recover block 7    
        *(uint64_t*)(output_blocks[6] + slice_offset_2) = *(uint64_t*)(decode_arr[1] + slice_offset_2);
        
        // Back substitute block 7 to other parity blocks
        *(uint64_t*)(decode_arr[0] + slice_offset_1) ^= *(uint64_t*)(output_blocks[6] + slice_offset_2);
        *(uint64_t*)(decode_arr[2] + slice_offset_3) ^= *(uint64_t*)(output_blocks[6] + slice_offset_2);
    }
    
    // Clean up temporary parity blocks
    for (auto block : decode_arr) delete[] block;
}


/* Validate decoding results */
bool validate_decoding() {
    bool success = true;
    std::vector<int> missing_indices = {5, 6, 7};
    
    for (int idx : missing_indices) {
        if (memcmp(message_blocks[idx], output_blocks[idx], CHUNK_SIZE) != 0) {
            std::cout << "Block " << idx << " was not correctly recovered!" << std::endl;
            success = false;
        }
    }
    
    return success;
}

/* Main function */
int main() {
    std::cout << "Systematic Two-Tone Shift-XOR Encoding Test" << std::endl;
    std::cout << "Data blocks: " << DATA_BLOCKS << ", Parity blocks: " << PARITY_BLOCKS << std::endl;
    std::cout << "Chunk size: " << CHUNK_SIZE << " bytes" << std::endl;
    
    // Generate the shift matrix
    generate_two_tone_shift_matrix();

    // Initialize buffers
    initialize_buffers();
    
    // Generate test data
    generate_test_data();
    
    // Measure encoding performance
    std::cout << "Starting encoding test with " << SAMPLE_NUM << " iterations..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (uint32_t i = 0; i < SAMPLE_NUM; i++) {
        encode();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double exec_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double exec_time_s = exec_time_us / 1000000.0;  // 微秒转秒
    double data_size_MB = (SAMPLE_NUM * (DATA_BLOCKS * CHUNK_SIZE)) / (1024.0 * 1024.0);  // 字节转MB
    double throughput_MBps = data_size_MB / exec_time_s;
    
    std::cout << "Encoding completed in " << exec_time_us << " microseconds" << std::endl;
    std::cout << "Encoding throughput: " << throughput_MBps << " MB/s" << std::endl;
    
    // Test decoding with blocks 5, 6, and 7 missing
    std::cout << "Testing decoding with blocks 5, 6, and 7 missing..." << std::endl;
    
    // Measure decoding performance
    start = std::chrono::high_resolution_clock::now();
    
    for (uint32_t i = 0; i < SAMPLE_NUM; i++) {
        decode();
    }
    
    end = std::chrono::high_resolution_clock::now();
    exec_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    exec_time_s = exec_time_us / 1000000.0;  // 微秒转秒
    data_size_MB = (SAMPLE_NUM * (3 * CHUNK_SIZE)) / (1024.0 * 1024.0);  // 字节转MB（只计算恢复的3个块）
    throughput_MBps = data_size_MB / exec_time_s;
    
    std::cout << "Decoding completed in " << exec_time_us << " microseconds" << std::endl;
    std::cout << "Decoding throughput: " << throughput_MBps << " MB/s" << std::endl;
    
    // Validate results
    if (validate_decoding()) {
        std::cout << "Decoding validation successful! All blocks were correctly recovered." << std::endl;
    } else {
        std::cout << "Decoding validation failed! Some blocks were not correctly recovered." << std::endl;
    }
    
    // Clean up
    cleanup_buffers();
    
    return 0;
} 