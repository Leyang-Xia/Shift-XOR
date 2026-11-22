/**
 * @filename: benchmark_comparison.cpp
 * @desc: Benchmark comparing Two-Tone Shift-XOR with Reed-Solomon implementations
 * @author: Generated for performance comparison
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
#include <iomanip>
#include <string>

// Two-Tone Shift-XOR implementation
namespace shift_xor {
    /* System parameters */
    constexpr int DATA_BLOCKS = 6;    // k message sequences
    constexpr int PARITY_BLOCKS = 3;  // n-k parity sequences
    constexpr int CODE_BLOCKS = DATA_BLOCKS + PARITY_BLOCKS;  // n=9 total code sequences
    constexpr int MAX_SHIFT_TWO_TONE = (PARITY_BLOCKS * (DATA_BLOCKS - 1)) / 2; // (3*5)/2 = 7

    // File and block size parameters
    constexpr size_t FILE_SIZE = 12 * 1024UL;
    constexpr size_t CHUNK_SIZE = FILE_SIZE / DATA_BLOCKS;
    constexpr size_t SLICE = 8;
    constexpr size_t TOTAL_SLICE_NUM = CHUNK_SIZE / SLICE;
    constexpr size_t PARITY_PADDING = SLICE * MAX_SHIFT_TWO_TONE;
    constexpr size_t PARITY_SIZE = CHUNK_SIZE + PARITY_PADDING;

    // Test parameters
    constexpr uint32_t SAMPLE_NUM = 1000UL;

    /* Data buffers */
    std::vector<uint8_t*> message_blocks;
    std::vector<uint8_t*> parity_blocks;
    std::vector<uint8_t*> output_blocks;

    /* Shift matrix for the parity part (Φ) */
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
                    
                    if (target_offset + SLICE <= PARITY_SIZE) {
                        // Read data
                        uint64_t data = *(uint64_t*)(message_blocks[j] + source_offset);
                        // XOR with parity block
                        *(uint64_t*)(parity_blocks[i] + target_offset) ^= data;
                    }
                }
            }
        }
    }

    /* Decode using the simple approach */
    void decode() {
        std::vector<int> missing_indices = {3, 4, 5};  // 0-based indices (blocks 3, 4, 5)
        
        // Create temporary parity blocks for decoding
        std::vector<uint8_t*> decode_arr;
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            uint8_t* block = new uint8_t[PARITY_SIZE]();
            memcpy(block, parity_blocks[i], PARITY_SIZE);
            decode_arr.push_back(block);
        }
        
        // Back substitution for available blocks (0-2)
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
                    
                    if (target_offset + SLICE <= PARITY_SIZE) {
                        // Read data from available block
                        uint64_t data = *(uint64_t*)(message_blocks[j] + source_offset);                  
                        // XOR with corresponding parity block (back substitution)
                        *(uint64_t*)(decode_arr[i] + target_offset) ^= data;
                    }
                }
            }
        }
        
        
        // For each slice
        for (int slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
            size_t slice_offset_2 = slice_id * SLICE;
            
            // Step 1: solve block with index 3 (missing block #1)
            size_t slice_offset_0 = (slice_id + shift_matrix[0][3]) * SLICE;
            size_t slice_offset_1 = (slice_id + shift_matrix[1][3]) * SLICE;
            size_t slice_offset_3 = (slice_id + shift_matrix[2][3]) * SLICE;
            
            if (slice_offset_0 + SLICE <= PARITY_SIZE) {
                // Directly use parity block 0 to recover block with index 3
                *(uint64_t*)(output_blocks[3] + slice_offset_2) = *(uint64_t*)(decode_arr[0] + slice_offset_0);
                
                // Back substitute block with index 3 to other parity blocks
                if (slice_offset_1 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[1] + slice_offset_1) ^= *(uint64_t*)(output_blocks[3] + slice_offset_2);
                }
                if (slice_offset_3 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[2] + slice_offset_3) ^= *(uint64_t*)(output_blocks[3] + slice_offset_2);
                }
            }
            
            // Step 2: solve block with index 5 (missing block #3)
            slice_offset_0 = (slice_id + shift_matrix[0][5]) * SLICE;
            slice_offset_1 = (slice_id + shift_matrix[1][5]) * SLICE;
            slice_offset_3 = (slice_id + shift_matrix[2][5]) * SLICE;
            
            if (slice_offset_3 + SLICE <= PARITY_SIZE) {
                // Directly use parity block 2 to recover block with index 5
                *(uint64_t*)(output_blocks[5] + slice_offset_2) = *(uint64_t*)(decode_arr[2] + slice_offset_3);
                
                // Back substitute block with index 5 to other parity blocks
                if (slice_offset_0 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[0] + slice_offset_0) ^= *(uint64_t*)(output_blocks[5] + slice_offset_2);
                }
                if (slice_offset_1 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[1] + slice_offset_1) ^= *(uint64_t*)(output_blocks[5] + slice_offset_2);
                }
            }
            
            // Step 3: solve block with index 4 (missing block #2)
            slice_offset_0 = (slice_id + shift_matrix[0][4]) * SLICE;
            slice_offset_1 = (slice_id + shift_matrix[1][4]) * SLICE;
            slice_offset_3 = (slice_id + shift_matrix[2][4]) * SLICE;
            
            if (slice_offset_1 + SLICE <= PARITY_SIZE) {
                // Directly use parity block 1 to recover block with index 4
                *(uint64_t*)(output_blocks[4] + slice_offset_2) = *(uint64_t*)(decode_arr[1] + slice_offset_1);
                
                // Back substitute block with index 4 to other parity blocks (not necessary for last block)
                if (slice_offset_0 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[0] + slice_offset_0) ^= *(uint64_t*)(output_blocks[4] + slice_offset_2);
                }
                if (slice_offset_3 + SLICE <= PARITY_SIZE) {
                    *(uint64_t*)(decode_arr[2] + slice_offset_3) ^= *(uint64_t*)(output_blocks[4] + slice_offset_2);
                }
            }
        }
        
        // Clean up temporary parity blocks
        for (auto block : decode_arr) delete[] block;
    }

    /* Validate decoding results */
    bool validate_decoding() {
        std::vector<int> missing_indices = {3, 4, 5};
        bool success = true;
        
        for (int idx : missing_indices) {
            if (memcmp(message_blocks[idx], output_blocks[idx], CHUNK_SIZE) != 0) {
                std::cout << "Block " << idx << " was not correctly recovered!" << std::endl;
                success = false;
            }
        }
        
        return success;
    }

    /* Run benchmark */
    void run_benchmark(uint32_t sample_num) {
        // Generate the shift matrix
        generate_two_tone_shift_matrix();

        // Initialize buffers
        initialize_buffers();
        
        // Generate test data
        generate_test_data();
        
        // Measure encoding performance
        auto start = std::chrono::high_resolution_clock::now();
        
        for (uint32_t i = 0; i < sample_num; i++) {
            encode();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        double encode_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        // Measure decoding performance
        start = std::chrono::high_resolution_clock::now();
        
        for (uint32_t i = 0; i < sample_num; i++) {
            decode();
        }
        
        end = std::chrono::high_resolution_clock::now();
        double decode_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        // Calculate throughput
        double encode_time_s = encode_time_us / 1000000.0;
        double decode_time_s = decode_time_us / 1000000.0;
        double encode_data_size_MB = (sample_num * (DATA_BLOCKS * CHUNK_SIZE)) / (1024.0 * 1024.0);
        double decode_data_size_MB = (sample_num * (3 * CHUNK_SIZE)) / (1024.0 * 1024.0);
        double encode_throughput_MBps = encode_data_size_MB / encode_time_s;
        double decode_throughput_MBps = decode_data_size_MB / decode_time_s;
        
        // Validate results
        bool validation_result = validate_decoding();
        
        // Print results
        std::cout << "Two-Tone Shift-XOR Results:" << std::endl;
        std::cout << "  Encoding time: " << encode_time_us << " us" << std::endl;
        std::cout << "  Decoding time: " << decode_time_us << " us" << std::endl;
        std::cout << "  Encoding throughput: " << encode_throughput_MBps << " MB/s" << std::endl;
        std::cout << "  Decoding throughput: " << decode_throughput_MBps << " MB/s" << std::endl;
        std::cout << "  Validation: " << (validation_result ? "PASSED" : "FAILED") << std::endl;
        
        // Clean up
        cleanup_buffers();
    }
}

// ISA-L Reed-Solomon implementation
namespace isal_rs {
    // Placeholder for ISA-L implementation
    void run_benchmark(uint32_t sample_num) {
        std::cout << "ISA-L Reed-Solomon Results:" << std::endl;
        std::cout << "  Not implemented yet - requires ISA-L library" << std::endl;
    }
}

// Jerasure Reed-Solomon implementation
namespace jerasure_rs {
    // Placeholder for Jerasure implementation
    void run_benchmark(uint32_t sample_num) {
        std::cout << "Jerasure Reed-Solomon Results:" << std::endl;
        std::cout << "  Not implemented yet - requires Jerasure library" << std::endl;
    }
}

// Longhair Reed-Solomon implementation
namespace longhair_rs {
    // Placeholder for Longhair implementation
    void run_benchmark(uint32_t sample_num) {
        std::cout << "Longhair Reed-Solomon Results:" << std::endl;
        std::cout << "  Not implemented yet - requires Longhair library" << std::endl;
    }
}

// Main benchmark function
void run_all_benchmarks(uint32_t sample_num) {
    std::cout << "=== Erasure Coding Benchmark (9,6) System ===" << std::endl;
    std::cout << "Running " << sample_num << " iterations for each implementation" << std::endl;
    std::cout << std::endl;
    
    // Run Two-Tone Shift-XOR benchmark
    shift_xor::run_benchmark(sample_num);
    std::cout << std::endl;
    
    // Run ISA-L Reed-Solomon benchmark
    isal_rs::run_benchmark(sample_num);
    std::cout << std::endl;
    
    // Run Jerasure Reed-Solomon benchmark
    jerasure_rs::run_benchmark(sample_num);
    std::cout << std::endl;
    
    // Run Longhair Reed-Solomon benchmark
    longhair_rs::run_benchmark(sample_num);
}

int main(int argc, char* argv[]) {
    uint32_t sample_num = 1000;
    
    if (argc > 1) {
        sample_num = std::stoi(argv[1]);
    }
    
    run_all_benchmarks(sample_num);
    
    return 0;
} 