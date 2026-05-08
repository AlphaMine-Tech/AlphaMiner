#pragma once
#include <cstdint>

// ──────────── Constants (must match CPU side exactly) ────────────
static constexpr unsigned long long NUMBER_OF_INPUT_NEURONS  = 256;
static constexpr unsigned long long NUMBER_OF_OUTPUT_NEURONS = 256;
static constexpr unsigned long long NUMBER_OF_TICKS          = 120;
static constexpr unsigned long long MAX_NEIGHBOR_NEURONS     = 256;
static constexpr unsigned long long NUMBER_OF_MUTATIONS      = 100;
static constexpr unsigned long long POPULATION_THRESHOLD     = NUMBER_OF_INPUT_NEURONS + NUMBER_OF_OUTPUT_NEURONS + NUMBER_OF_MUTATIONS;
static constexpr unsigned int       SOLUTION_THRESHOLD       = NUMBER_OF_OUTPUT_NEURONS * 4 / 5;

static constexpr unsigned long long NUM_NEURONS       = NUMBER_OF_INPUT_NEURONS + NUMBER_OF_OUTPUT_NEURONS;
static constexpr unsigned long long MAX_NEURONS       = POPULATION_THRESHOLD;
static constexpr unsigned long long MAX_SYNAPSES      = POPULATION_THRESHOLD * MAX_NEIGHBOR_NEURONS;
static constexpr unsigned long long INIT_SYNAPSES     = NUM_NEURONS * MAX_NEIGHBOR_NEURONS;

static constexpr unsigned long long POOL_VEC_SIZE         = (((1ULL << 32) + 64)) >> 3;
static constexpr unsigned long long POOL_VEC_PADDING_SIZE = (POOL_VEC_SIZE + 200 - 1) / 200 * 200;

// Block-cooperative kernel constants
static constexpr int BLOCK_SIZE = 256;

// ──────────── Device-side structs ────────────
struct DeviceSynapse {
    char weight;
};

struct DeviceNeuron {
    enum Type : int { kInput = 0, kOutput = 1, kEvolution = 2 };
    Type type;
    char value;
    bool markForRemoval;
};

struct DeviceANN {
    DeviceNeuron neurons[MAX_NEURONS];
    DeviceSynapse synapses[MAX_SYNAPSES];
    unsigned long long population;
};

struct DeviceInitValue {
    unsigned long long outputNeuronPositions[NUMBER_OF_OUTPUT_NEURONS];
    unsigned long long synapseWeight[INIT_SYNAPSES / 32];
    unsigned long long synpaseMutation[NUMBER_OF_MUTATIONS];
};

struct DeviceMiningData {
    unsigned long long inputNeuronRandomNumber[NUMBER_OF_INPUT_NEURONS / 64];
    unsigned long long outputNeuronRandomNumber[NUMBER_OF_OUTPUT_NEURONS / 64];
};

// Shared memory layout for block-cooperative kernel
struct SharedState {
    int neuronValueBuffer[MAX_NEURONS];          // 612 * 4 = 2448 bytes
    char previousNeuronValue[MAX_NEURONS];       // 612 bytes
    unsigned long long population;               // 8 bytes
    int earlyExitFlag;                           // 4 bytes
    unsigned int bestR;                          // 4 bytes
    // Total: ~3076 bytes
};

// Result from a single mining nonce
struct MiningResult {
    unsigned int score;
    unsigned char nonce[32];
    bool isSolution;
};

// ──────────── Kernel launch prototype ────────────
void launchMiningKernel(
    const unsigned char* d_pool,
    const unsigned char* d_publicKey,
    DeviceANN* d_currentANNs,
    DeviceANN* d_bestANNs,
    DeviceInitValue* d_initValues,
    DeviceMiningData* d_miningDatas,
    char* d_outputExpectedValues,
    unsigned long long* d_neuronIndices,
    unsigned long long* d_outputNeuronIndices,
    unsigned char* d_nonces,
    MiningResult* d_results,
    int numNonces,
    cudaStream_t stream
);
