/**
 * @filename: shift_xor_9_6_simple.cpp
 * @desc: Simple implementation of (9,6) systematic shift-XOR encoding
 * @author: Based on Chenhao Wu's work
 */

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <random>
#include <stdint.h>
#include <chrono>
#include <cstring>
#include <ctime>

/* Configuration parameters*/

/* System parameters */
constexpr int DATA_BLOCKS = 6;    // k message sequences
constexpr int PARITY_BLOCKS = 3;  // n-k parity sequences
constexpr int CODE_BLOCKS = DATA_BLOCKS + PARITY_BLOCKS;  // n=9 total code sequences
constexpr int MAX_SHIFT_TWO_TONE = (PARITY_BLOCKS * (DATA_BLOCKS - 1)) / 2; // (3*5)/2 = 7

// 文件和块大小参数
constexpr size_t FILE_SIZE = 12 * 1024UL;
constexpr size_t CHUNK_SIZE = FILE_SIZE / DATA_BLOCKS;
constexpr size_t SLICE = 8; // 使用8字节的切片
constexpr size_t TOTAL_SLICE_NUM = CHUNK_SIZE / SLICE;
constexpr size_t PARITY_PADDING = SLICE * MAX_SHIFT_TWO_TONE;
constexpr size_t PARITY_SIZE = CHUNK_SIZE + PARITY_PADDING;

// 测试参数
constexpr uint32_t SAMPLE_NUM = 10000UL;

/* Data buffers */
uint8_t* message_blocks[DATA_BLOCKS];
uint8_t* parity_blocks[PARITY_BLOCKS];
uint8_t* output_blocks[DATA_BLOCKS];

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
    
    // Print the shift matrix for debugging
    std::cout << "Shift Matrix:" << std::endl;
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        std::cout << "Row " << i << ": ";
        for (int j = 0; j < DATA_BLOCKS; j++) {
            std::cout << shift_matrix[i][j] << " ";
        }
        std::cout << std::endl;
    }
}

/* Initialize memory buffers */
void initialize_buffers() {
    // Allocate message blocks
    for (int i = 0; i < DATA_BLOCKS; i++) {
        message_blocks[i] = new uint8_t[CHUNK_SIZE]();
    }
    
    // Allocate parity blocks
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        parity_blocks[i] = new uint8_t[PARITY_SIZE]();
    }
    
    // Allocate output blocks for decoded data
    for (int i = 0; i < DATA_BLOCKS; i++) {
        output_blocks[i] = new uint8_t[CHUNK_SIZE]();
    }
}

/* Free memory buffers */
void cleanup_buffers() {
    for (int i = 0; i < DATA_BLOCKS; i++) {
        delete[] message_blocks[i];
        delete[] output_blocks[i];
    }
    
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        delete[] parity_blocks[i];
    }
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

/* Load test data from file */
void load_test_data(const char* filename) {
    std::fstream FileIn(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (FileIn.is_open()) {
        for (int i = 0; i < DATA_BLOCKS; i++) {
            FileIn.read((char*)message_blocks[i], CHUNK_SIZE);
        }
        FileIn.close();
        std::cout << "File " << filename << " has been loaded into memory" << std::endl;
    } else {
        std::cout << "Cannot open file. Using random data instead." << std::endl;
        generate_test_data();
    }
}

/* Systematic Shift-XOR encoding */
void encode() {
    // Clear parity blocks
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        memset(parity_blocks[i], 0, PARITY_SIZE);
    }
    
    // For each slice in the message blocks
    for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t slice_offset = slice_id * SLICE;
        
        // For each parity block
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            // For each message block
            for (int j = 0; j < DATA_BLOCKS; j++) {
                // Calculate shift amount for this parity-message pair
                int shift_amount = shift_matrix[i][j];
                size_t target_offset = (slice_id + shift_amount) * SLICE;
                
                if (target_offset + SLICE <= PARITY_SIZE) {
                    // Read data
                    uint64_t data = *(uint64_t*)(message_blocks[j] + slice_offset);
                    
                    // XOR with parity block
                    *(uint64_t*)(parity_blocks[i] + target_offset) ^= data;
                }
            }
        }
    }
}

/* Decode */
void decode() {
    // We'll recover blocks 3, 4, and 5
    int missing_indices[3] = {3, 4, 5};
    
    // Create temporary parity blocks for decoding
    uint8_t* decode_arr[PARITY_BLOCKS];
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        decode_arr[i] = new uint8_t[PARITY_SIZE]();
        memcpy(decode_arr[i], parity_blocks[i], PARITY_SIZE);
    }
    
    // Back substitution for available blocks (0-2)
    for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t slice_offset = slice_id * SLICE;
        
        // For each parity block
        for (int i = 0; i < PARITY_BLOCKS; i++) {
            // For each available data block (0, 1, 2)
            for (int j = 0; j < DATA_BLOCKS - 3; j++) {
                // Get shift amount for this parity-message pair
                int shift_amount = shift_matrix[i][j];
                size_t target_offset = (slice_id + shift_amount) * SLICE;
                
                if (target_offset + SLICE <= PARITY_SIZE) {
                    // Read data from available block
                    uint64_t data = *(uint64_t*)(message_blocks[j] + slice_offset);
                    
                    // XOR with corresponding parity block (back substitution)
                    *(uint64_t*)(decode_arr[i] + target_offset) ^= data;
                }
            }
        }
    }
    
    // For each slice
    for (size_t slice_id = 0; slice_id < TOTAL_SLICE_NUM; slice_id++) {
        size_t slice_offset = slice_id * SLICE;
        
        // Step 1: solve block with index 3 (missing block #1)
        size_t offset_0 = (slice_id + shift_matrix[0][3]) * SLICE;
        size_t offset_1 = (slice_id + shift_matrix[1][3]) * SLICE;
        size_t offset_2 = (slice_id + shift_matrix[2][3]) * SLICE;
        
        if (offset_0 + SLICE <= PARITY_SIZE) {
            // Directly use parity block 0 to recover block with index 3
            *(uint64_t*)(output_blocks[3] + slice_offset) = *(uint64_t*)(decode_arr[0] + offset_0);
            
            // Back substitute block with index 3 to other parity blocks
            if (offset_1 + SLICE <= PARITY_SIZE) {
                *(uint64_t*)(decode_arr[1] + offset_1) ^= *(uint64_t*)(output_blocks[3] + slice_offset);
            }
            
            if (offset_2 + SLICE <= PARITY_SIZE) {
                *(uint64_t*)(decode_arr[2] + offset_2) ^= *(uint64_t*)(output_blocks[3] + slice_offset);
            }
        }
        
        // Step 2: solve block with index 5 (missing block #3)
        offset_0 = (slice_id + shift_matrix[0][5]) * SLICE;
        offset_1 = (slice_id + shift_matrix[1][5]) * SLICE;
        offset_2 = (slice_id + shift_matrix[2][5]) * SLICE;
        
        if (offset_2 + SLICE <= PARITY_SIZE) {
            // Directly use parity block 2 to recover block with index 5
            *(uint64_t*)(output_blocks[5] + slice_offset) = *(uint64_t*)(decode_arr[2] + offset_2);
            
            // Back substitute block with index 5 to other parity blocks
            if (offset_0 + SLICE <= PARITY_SIZE) {
                *(uint64_t*)(decode_arr[0] + offset_0) ^= *(uint64_t*)(output_blocks[5] + slice_offset);
            }
            
            if (offset_1 + SLICE <= PARITY_SIZE) {
                *(uint64_t*)(decode_arr[1] + offset_1) ^= *(uint64_t*)(output_blocks[5] + slice_offset);
            }
        }
        
        // Step 3: solve block with index 4 (missing block #2)
        offset_0 = (slice_id + shift_matrix[0][4]) * SLICE;
        offset_1 = (slice_id + shift_matrix[1][4]) * SLICE;
        offset_2 = (slice_id + shift_matrix[2][4]) * SLICE;
        
        if (offset_1 + SLICE <= PARITY_SIZE) {
            // Directly use parity block 1 to recover block with index 4
            *(uint64_t*)(output_blocks[4] + slice_offset) = *(uint64_t*)(decode_arr[1] + offset_1);
            
            // Back substitute block with index 4 to other parity blocks (not necessary for last block)
            if (offset_0 + SLICE <= PARITY_SIZE) {
                *(uint64_t*)(decode_arr[0] + offset_0) ^= *(uint64_t*)(output_blocks[4] + slice_offset);
            }
            
            if (offset_2 + SLICE <= PARITY_SIZE) {
                *(uint64_t*)(decode_arr[2] + offset_2) ^= *(uint64_t*)(output_blocks[4] + slice_offset);
            }
        }
    }
    
    // Clean up temporary parity blocks
    for (int i = 0; i < PARITY_BLOCKS; i++) {
        delete[] decode_arr[i];
    }
}

/* Validate decoding results */
bool validate_decoding() {
    int missing_indices[3] = {3, 4, 5};
    bool success = true;
    
    for (int i = 0; i < 3; i++) {
        int idx = missing_indices[i];
        if (memcmp(message_blocks[idx], output_blocks[idx], CHUNK_SIZE) != 0) {
            std::cout << "Block " << idx << " was not correctly recovered!" << std::endl;
            success = false;
        }
    }
    
    if (success) {
        std::cout << "All blocks were correctly recovered!" << std::endl;
    }
    
    return success;
}

/* Main function */
int main() {
    std::cout << "Systematic Two-Tone Shift-XOR Encoding Test (9,6) System" << std::endl;
    std::cout << "Data blocks: " << DATA_BLOCKS << ", Parity blocks: " << PARITY_BLOCKS << std::endl;
    std::cout << "Chunk size: " << CHUNK_SIZE << " bytes, Slice size: " << SLICE << " bytes" << std::endl;
    
    // Generate the shift matrix
    generate_two_tone_shift_matrix();

    // Initialize buffers
    initialize_buffers();
    
    // Load or generate test data
    load_test_data("dummy.txt");
    
    // Measure encoding performance
    double tic, toc;
    double exec_time_in_sec, throughput_in_MBps;
    
    std::cout << "Starting encoding test with " << SAMPLE_NUM << " iterations..." << std::endl;
    tic = clock();
    for (uint32_t i = 0; i < SAMPLE_NUM; i++) {
        encode();
    }
    toc = clock();
    
    printf("Clock ticks: tic=%f, toc=%f, diff=%f\n", (double)tic, (double)toc, (double)(toc - tic));
    printf("CLOCKS_PER_SEC=%d\n", CLOCKS_PER_SEC);
    
    exec_time_in_sec = (toc - tic) / (double)CLOCKS_PER_SEC;
    printf("Execution time: %f seconds\n", exec_time_in_sec);
    
    double data_size_MB = (SAMPLE_NUM * (DATA_BLOCKS * CHUNK_SIZE)) / (1024.0 * 1024.0);
    printf("Data size: %f MB\n", data_size_MB);
    
    throughput_in_MBps = data_size_MB / exec_time_in_sec;
    printf("Encoding finished!\n");
    printf("Encoding throughput: %.4f MBps\n", throughput_in_MBps);
    
    // Test decoding with blocks 3, 4, and 5 missing
    std::cout << "Testing decoding with blocks 3, 4, and 5 missing..." << std::endl;
    
    // Measure decoding performance
    tic = clock();
    for (uint32_t i = 0; i < SAMPLE_NUM; i++) {
        decode();
    }
    toc = clock();
    
    printf("Clock ticks: tic=%f, toc=%f, diff=%f\n", (double)tic, (double)toc, (double)(toc - tic));
    
    exec_time_in_sec = (toc - tic) / (double)CLOCKS_PER_SEC;
    printf("Execution time: %f seconds\n", exec_time_in_sec);
    
    data_size_MB = (SAMPLE_NUM * (3 * CHUNK_SIZE)) / (1024.0 * 1024.0);
    printf("Data size: %f MB\n", data_size_MB);
    
    throughput_in_MBps = data_size_MB / exec_time_in_sec;
    printf("Decoding finished!\n");
    printf("Decoding throughput: %.4f MBps\n", throughput_in_MBps);
    
    // Validate results
    validate_decoding();
    
    // Clean up
    cleanup_buffers();
    
    return 0;
} 