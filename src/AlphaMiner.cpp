#include <iostream>
#include "json.hpp"
#include <chrono>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <array>
#include <queue>
#include <atomic>
#include <vector>
#ifdef _MSC_VER
#include <intrin.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#else
#include <signal.h>
#include <immintrin.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#endif

#include "K12AndKeyUtil.h"
#include "keyUtils.h"

using json = nlohmann::json;
using namespace std;

constexpr unsigned long long POOL_VEC_SIZE = (((1ULL << 32) + 64)) >> 3;                    // 2^32+64 bits ~ 512MB
constexpr unsigned long long POOL_VEC_PADDING_SIZE = (POOL_VEC_SIZE + 200 - 1) / 200 * 200; // padding for multiple of 200

// Clamp the neuron value
template <typename T>
T clampNeuron(T neuronValue)
{
    if (neuronValue > 1)
    {
        return 1;
    }

    if (neuronValue < -1)
    {
        return -1;
    }
    return neuronValue;
}

void generateRandom2Pool(unsigned char miningSeed[32], unsigned char *pool)
{
    unsigned char state[200];
    // same pool to be used by all computors/candidates and pool content changing each phase
    memcpy(&state[0], miningSeed, 32);
    memset(&state[32], 0, sizeof(state) - 32);

    for (unsigned int i = 0; i < POOL_VEC_PADDING_SIZE; i += sizeof(state))
    {
        KeccakP1600_Permute_12rounds(state);
        memcpy(&pool[i], state, sizeof(state));
    }
}

void random2(
    unsigned char seed[32],
    const unsigned char *pool,
    unsigned char *output,
    unsigned long long outputSizeInByte)
{
    unsigned long long paddingOutputSize = (outputSizeInByte + 64 - 1) / 64;
    paddingOutputSize = paddingOutputSize * 64;
    std::vector<unsigned char> paddingOutputVec(paddingOutputSize);
    unsigned char *paddingOutput = paddingOutputVec.data();

    unsigned long long segments = paddingOutputSize / 64;
    unsigned int x[8] = {0};
    for (int i = 0; i < 8; i++)
    {
        x[i] = ((unsigned int *)seed)[i];
    }

    for (int j = 0; j < segments; j++)
    {
        // Each segment will have 8 elements. Each element have 8 bytes
        for (int i = 0; i < 8; i++)
        {
            unsigned int base = (x[i] >> 3) >> 3;
            unsigned int m = x[i] & 63;

            unsigned long long u64_0 = ((unsigned long long *)pool)[base];
            unsigned long long u64_1 = ((unsigned long long *)pool)[base + 1];

            // Move 8 * 8 * j to the current segment. 8 * i to current 8 bytes element
            if (m == 0)
            {
                // some compiler doesn't work with bit shift 64
                *((unsigned long long *)&paddingOutput[j * 8 * 8 + i * 8]) = u64_0;
            }
            else
            {
                *((unsigned long long *)&paddingOutput[j * 8 * 8 + i * 8]) = (u64_0 >> m) | (u64_1 << (64 - m));
            }

            // Increase the positions in the pool for each element.
            x[i] = x[i] * 1664525 + 1013904223; // https://en.wikipedia.org/wiki/Linear_congruential_generator#Parameters_in_common_use
        }
    }

    memcpy(output, paddingOutput, outputSizeInByte);
}

char *nodeIp = NULL;
int nodePort = 0;
// HyperIdentity algorithm parameters (nonce[0] even)
static constexpr unsigned long long HI_K = 512;   // input neurons
static constexpr unsigned long long HI_L = 512;   // output neurons
static constexpr unsigned long long HI_N = 1000;  // ticks
static constexpr unsigned long long HI_2M = 728;  // neighbors (must be even)
static constexpr unsigned long long HI_S = 150;   // mutations
static constexpr unsigned long long HI_P = HI_K + HI_L + HI_S; // population threshold
static constexpr unsigned int HI_THRESHOLD = 321;

// Addition algorithm parameters (nonce[0] odd)
static constexpr unsigned long long ADD_K = 14;    // input neurons
static constexpr unsigned long long ADD_L = 8;     // output neurons
static constexpr unsigned long long ADD_N = 1000;  // ticks
static constexpr unsigned long long ADD_2M = 728;  // max neighbors (must be even)
static constexpr unsigned long long ADD_S = 500;   // mutations
static constexpr unsigned long long ADD_P = ADD_K + ADD_L + ADD_S; // population threshold
static constexpr unsigned int ADD_THRESHOLD = 75700;

static int SUBSCRIBE = 1;
static int NEW_COMPUTOR_ID = 2;
static int NEW_SEED = 3;
static int SUBMIT = 4;
static int REPORT_HASHRATE = 5;
static int NEW_DIFFICULTY = 6;

// qatum variable

static std::atomic<int> difficulty(0);
static unsigned char computorPublicKey[32] = {0};
static unsigned char randomSeed[32] = {0};

static std::atomic<char> state(0);
static std::atomic<long long> numberOfMiningIterations(0);
static std::atomic<unsigned int> numberOfFoundSolutions(0);
static std::queue<std::array<unsigned char, 32>> foundNonce;
std::mutex foundNonceLock;

template <unsigned long long num>
bool isZeros(const unsigned char *value)
{
    bool allZeros = true;
    for (unsigned long long i = 0; i < num; ++i)
    {
        if (value[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void extract64Bits(unsigned long long number, char *output)
{
    int count = 0;
    for (int i = 0; i < 64; ++i)
    {
        output[i] = ((number >> i) & 1);
    }
}

template <
    unsigned long long numberOfInputNeurons,  // K
    unsigned long long numberOfOutputNeurons, // L
    unsigned long long numberOfTicks,         // N
    unsigned long long numberOfNeighbors,     // 2M
    unsigned long long populationThreshold,   // P
    unsigned long long numberOfMutations,     // S
    unsigned int solutionThreshold>
struct Miner
{
    unsigned char computorPublicKey[32];
    unsigned char currentRandomSeed[32];
    int difficulty;

    static constexpr unsigned long long numberOfNeurons = numberOfInputNeurons + numberOfOutputNeurons;
    static constexpr unsigned long long maxNumberOfNeurons = populationThreshold;
    static constexpr unsigned long long maxNumberOfSynapses = populationThreshold * numberOfNeighbors;
    static constexpr unsigned long long initNumberOfSynapses = numberOfNeurons * numberOfNeighbors;

    static_assert(numberOfInputNeurons % 64 == 0, "numberOfInputNeurons must be divided by 64");
    static_assert(numberOfOutputNeurons % 64 == 0, "numberOfOutputNeurons must be divided by 64");
    static_assert(maxNumberOfSynapses <= (0xFFFFFFFFFFFFFFFF << 1ULL), "maxNumberOfSynapses must less than or equal MAX_UINT64/2");
    static_assert(initNumberOfSynapses % 32 == 0, "initNumberOfSynapses must be divided by 32");
    static_assert(numberOfNeighbors % 2 == 0, "numberOfNeighbors must divided by 2");
    static_assert(populationThreshold > numberOfNeurons, "populationThreshold must be greater than numberOfNeurons");
    static_assert(numberOfNeurons > numberOfNeighbors, "Number of neurons must be greater than the number of neighbors");

    std::vector<unsigned char> poolVec;

    bool updateLatestQatumData()
    {
        bool seedChanged = memcmp(this->currentRandomSeed, ::randomSeed, sizeof(currentRandomSeed)) != 0;
        memcpy(this->currentRandomSeed, ::randomSeed, sizeof(currentRandomSeed));
        memcpy(this->computorPublicKey, ::computorPublicKey, sizeof(computorPublicKey));
        this->difficulty = ::difficulty;

        if (seedChanged && !isZeros<32>(this->currentRandomSeed))
        {
            generateRandom2Pool(this->currentRandomSeed, poolVec.data());
        }
        setComputorPublicKey(this->computorPublicKey);

        return !isZeros<32>(this->computorPublicKey) && !isZeros<32>(this->currentRandomSeed) && this->difficulty != 0;
    }

    static bool checkGlobalQatumDataAvailability()
    {
        return !isZeros<32>(::computorPublicKey) && !isZeros<32>(::randomSeed) && ::difficulty != 0;
    }

    void setCurrentDifficulty(int difficulty)
    {
        this->difficulty = difficulty;
    }

    int getCurrentDifficulty()
    {
        return this->difficulty;
    }

    void getCurrentRandomSeed(unsigned char randomSeed[32])
    {
        memcpy(randomSeed, this->currentRandomSeed, sizeof(this->currentRandomSeed));
    }

    void getComputorPublicKey(unsigned char computorPublicKey[32])
    {
        memcpy(computorPublicKey, this->computorPublicKey, sizeof(this->computorPublicKey));
    }

    void setComputorPublicKey(unsigned char computorPublicKey[32])
    {
        memcpy(this->computorPublicKey, computorPublicKey, sizeof(this->computorPublicKey));
    }

    void initialize(unsigned char miningSeed[32])
    {
        // Init random2 pool with mining seed
        poolVec.resize(POOL_VEC_PADDING_SIZE);
        generateRandom2Pool(miningSeed, poolVec.data());
    }

    struct Synapse
    {
        char weight;
    };

    // Data for running the ANN
    struct Neuron
    {
        enum Type
        {
            kInput,
            kOutput,
            kEvolution,
        };
        Type type;
        char value;
        bool markForRemoval;
    };

    // Data for roll back
    struct ANN
    {
        Neuron neurons[maxNumberOfNeurons];
        Synapse synapses[maxNumberOfSynapses];
        unsigned long long population;
    };
    ANN bestANN;
    ANN currentANN;

    // Intermediate data
    struct InitValue
    {
        unsigned long long outputNeuronPositions[numberOfOutputNeurons];
        unsigned long long synapseWeight[initNumberOfSynapses / 32]; // each 64bits elements will decide value of 32 synapses
        unsigned long long synpaseMutation[numberOfMutations];
    } initValue;

    struct MiningData
    {
        unsigned long long inputNeuronRandomNumber[numberOfInputNeurons / 64];   // each bit will use for generate input neuron value
        unsigned long long outputNeuronRandomNumber[numberOfOutputNeurons / 64]; // each bit will use for generate expected output neuron value
    } miningData;

    unsigned long long neuronIndices[numberOfNeurons];
    char previousNeuronValue[maxNumberOfNeurons];

    unsigned long long outputNeuronIndices[numberOfOutputNeurons];
    char outputNeuronExpectedValue[numberOfOutputNeurons];

    long long neuronValueBuffer[maxNumberOfNeurons];

    // --- Bitmask optimization data ---
    // Neuron bitmasks with circular padding (radius on each side)
    static constexpr unsigned long long neuronRadius = numberOfNeighbors / 2;
    static constexpr unsigned long long windowBits = numberOfNeighbors + 1; // includes self-position (always zero)
    static constexpr unsigned long long windowQwords = (windowBits + 63) / 64;
    // Padded neuron array: radius + maxPop + radius bits
    static constexpr unsigned long long maxPaddedBits = populationThreshold + 2 * neuronRadius;
    static constexpr unsigned long long maxPaddedQwords = (maxPaddedBits + 63) / 64 + 1; // +1 for safe unaligned load

    unsigned long long neuronPlusBits[maxPaddedQwords];
    unsigned long long neuronMinusBits[maxPaddedQwords];
    // Incoming synapse bitmasks per target neuron (windowQwords per neuron)
    unsigned long long inSynPlusBits[populationThreshold * windowQwords];
    unsigned long long inSynMinusBits[populationThreshold * windowQwords];
    // Buffer for new neuron values during tick
    char neuronTickBuffer[populationThreshold];

    // Load 64 bits starting at arbitrary bit position from a qword array
    static inline unsigned long long loadBits64(const unsigned long long *data, unsigned long long bitStart)
    {
        unsigned long long wordIdx = bitStart / 64;
        unsigned long long bitOff = bitStart % 64;
        if (bitOff == 0)
            return data[wordIdx];
        return (data[wordIdx] >> bitOff) | (data[wordIdx + 1] << (64 - bitOff));
    }

    void packNeuronBitmasks()
    {
        unsigned long long pop = currentANN.population;
        memset(neuronPlusBits, 0, sizeof(neuronPlusBits));
        memset(neuronMinusBits, 0, sizeof(neuronMinusBits));

        // Pack actual neurons at positions [radius, radius + pop)
        for (unsigned long long i = 0; i < pop; i++)
        {
            unsigned long long paddedIdx = i + neuronRadius;
            char val = currentANN.neurons[i].value;
            if (val > 0)
                neuronPlusBits[paddedIdx / 64] |= (1ULL << (paddedIdx % 64));
            else if (val < 0)
                neuronMinusBits[paddedIdx / 64] |= (1ULL << (paddedIdx % 64));
        }

        // Head circular padding: copy last `radius` neurons to positions [0, radius)
        for (unsigned long long i = 0; i < neuronRadius; i++)
        {
            unsigned long long srcNeuron = pop - neuronRadius + i;
            unsigned long long srcIdx = srcNeuron + neuronRadius;
            unsigned long long dstIdx = i;
            if (neuronPlusBits[srcIdx / 64] & (1ULL << (srcIdx % 64)))
                neuronPlusBits[dstIdx / 64] |= (1ULL << (dstIdx % 64));
            if (neuronMinusBits[srcIdx / 64] & (1ULL << (srcIdx % 64)))
                neuronMinusBits[dstIdx / 64] |= (1ULL << (dstIdx % 64));
        }

        // Tail circular padding: copy first `radius` neurons to positions [radius+pop, radius+pop+radius)
        for (unsigned long long i = 0; i < neuronRadius; i++)
        {
            unsigned long long srcIdx = i + neuronRadius;
            unsigned long long dstIdx = neuronRadius + pop + i;
            if (dstIdx / 64 < maxPaddedQwords)
            {
                if (neuronPlusBits[srcIdx / 64] & (1ULL << (srcIdx % 64)))
                    neuronPlusBits[dstIdx / 64] |= (1ULL << (dstIdx % 64));
                if (neuronMinusBits[srcIdx / 64] & (1ULL << (srcIdx % 64)))
                    neuronMinusBits[dstIdx / 64] |= (1ULL << (dstIdx % 64));
            }
        }
    }

    void transposeAndPackSynapses()
    {
        unsigned long long pop = currentANN.population;
        memset(inSynPlusBits, 0, pop * windowQwords * sizeof(unsigned long long));
        memset(inSynMinusBits, 0, pop * windowQwords * sizeof(unsigned long long));

        for (unsigned long long n = 0; n < pop; n++)
        {
            const Synapse *syn = getSynapses(n);
            for (unsigned long long m = 0; m < numberOfNeighbors; m++)
            {
                char weight = syn[m].weight;
                if (weight == 0) continue;

                // Find target neuron of this outgoing synapse
                long long target;
                if (m < neuronRadius)
                    target = (long long)n + (long long)m - (long long)neuronRadius;
                else
                    target = (long long)n + (long long)m + 1 - (long long)neuronRadius;

                while (target < 0) target += pop;
                while (target >= (long long)pop) target -= pop;

                // Window position: source n is at offset (n - target) from target
                long long diff = (long long)n - (long long)target;
                if (diff < -(long long)(pop / 2)) diff += pop;
                if (diff > (long long)(pop / 2)) diff -= pop;
                // diff in [-radius, +radius], 0 = self (shouldn't happen since we skip self in outgoing)
                unsigned long long pos = (unsigned long long)(diff + (long long)neuronRadius);

                unsigned long long idx = target * windowQwords + pos / 64;
                unsigned long long bit = pos % 64;

                if (weight > 0)
                    inSynPlusBits[idx] |= (1ULL << bit);
                else
                    inSynMinusBits[idx] |= (1ULL << bit);
            }
        }
    }

    void mutate(unsigned char nonce[32], int mutateStep)
    {
        // Mutation
        unsigned long long population = currentANN.population;
        unsigned long long synapseCount = population * numberOfNeighbors;
        Synapse *synapses = currentANN.synapses;

        // Randomly pick a synapse, randomly increase or decrease its weight by 1 or -1
        unsigned long long synapseMutation = initValue.synpaseMutation[mutateStep];
        unsigned long long synapseIdx = (synapseMutation >> 1) % synapseCount;
        // Randomly increase or decrease its value
        char weightChange = 0;
        if ((synapseMutation & 1ULL) == 0)
        {
            weightChange = -1;
        }
        else
        {
            weightChange = 1;
        }

        char newWeight = synapses[synapseIdx].weight + weightChange;

        // Valid weight. Update it
        if (newWeight >= -1 && newWeight <= 1)
        {
            synapses[synapseIdx].weight = newWeight;
        }
        else // Invalid weight. Insert a neuron
        {
            // Insert the neuron
            insertNeuron(synapseIdx);
        }

        // Clean the ANN
        while (scanRedundantNeurons() > 0)
        {
            cleanANN();
        }
    }

    // Get the pointer to all outgoing synapse of a neurons
    Synapse *getSynapses(unsigned long long neuronIndex)
    {
        return &currentANN.synapses[neuronIndex * numberOfNeighbors];
    }

    // Circulate the neuron index
    unsigned long long clampNeuronIndex(long long neuronIdx, long long value)
    {
        unsigned long long population = currentANN.population;
        long long nnIndex = 0;
        // Calculate the neuron index (ring structure)
        if (value >= 0)
        {
            nnIndex = neuronIdx + value;
        }
        else
        {
            nnIndex = neuronIdx + population + value;
        }
        nnIndex = nnIndex % population;
        return (unsigned long long)nnIndex;
    }

    // Remove a neuron and all synapses relate to it
    void removeNeuron(unsigned long long neuronIdx)
    {
        // Scan all its neigbor to remove their outgoing synapse point to the neuron
        for (long long neighborOffset = -(long long)numberOfNeighbors / 2; neighborOffset <= (long long)numberOfNeighbors / 2; neighborOffset++)
        {
            unsigned long long nnIdx = clampNeuronIndex(neuronIdx, neighborOffset);
            Synapse *pNNSynapses = getSynapses(nnIdx);

            long long synapseIndexOfNN = getIndexInSynapsesBuffer(nnIdx, -neighborOffset);
            if (synapseIndexOfNN < 0)
            {
                continue;
            }

            // The synapse array need to be shifted regard to the remove neuron
            // Also neuron need to have 2M neighbors, the addtional synapse will be set as zero weight
            // Case1 [S0 S1 S2 - SR S5 S6]. SR is removed, [S0 S1 S2 S5 S6 0]
            // Case2 [S0 S1 SR - S3 S4 S5]. SR is removed, [0 S0 S1 S3 S4 S5]
            if (synapseIndexOfNN >= numberOfNeighbors / 2)
            {
                for (long long k = synapseIndexOfNN; k < numberOfNeighbors - 1; ++k)
                {
                    pNNSynapses[k] = pNNSynapses[k + 1];
                }
                pNNSynapses[numberOfNeighbors - 1].weight = 0;
            }
            else
            {
                for (long long k = synapseIndexOfNN; k > 0; --k)
                {
                    pNNSynapses[k] = pNNSynapses[k - 1];
                }
                pNNSynapses[0].weight = 0;
            }
        }

        // Shift the synapse array and the neuron array
        for (unsigned long long shiftIdx = neuronIdx; shiftIdx < currentANN.population; shiftIdx++)
        {
            currentANN.neurons[shiftIdx] = currentANN.neurons[shiftIdx + 1];

            // Also shift the synapses
            memcpy(getSynapses(shiftIdx), getSynapses(shiftIdx + 1), numberOfNeighbors * sizeof(Synapse));
        }
        currentANN.population--;
    }

    unsigned long long getNeighborNeuronIndex(unsigned long long neuronIndex, unsigned long long neighborOffset)
    {
        unsigned long long nnIndex = 0;
        if (neighborOffset < (numberOfNeighbors / 2))
        {
            nnIndex = clampNeuronIndex(neuronIndex + neighborOffset, -(long long)numberOfNeighbors / 2);
        }
        else
        {
            nnIndex = clampNeuronIndex(neuronIndex + neighborOffset + 1, -(long long)numberOfNeighbors / 2);
        }
        return nnIndex;
    }

    void insertNeuron(unsigned long long synapseIdx)
    {
        // A synapse have incomingNeighbor and outgoingNeuron, direction incomingNeuron -> outgoingNeuron
        unsigned long long incomingNeighborSynapseIdx = synapseIdx % numberOfNeighbors;
        unsigned long long outgoingNeuron = synapseIdx / numberOfNeighbors;

        Synapse *synapses = currentANN.synapses;
        Neuron *neurons = currentANN.neurons;
        unsigned long long &population = currentANN.population;

        // Copy original neuron to the inserted one and set it as  Neuron::kEvolution type
        Neuron insertNeuron;
        insertNeuron = neurons[outgoingNeuron];
        insertNeuron.type = Neuron::kEvolution;
        unsigned long long insertedNeuronIdx = outgoingNeuron + 1;

        char originalWeight = synapses[synapseIdx].weight;

        // Insert the neuron into array, population increased one, all neurons next to original one need to shift right
        for (unsigned long long i = population; i > outgoingNeuron; --i)
        {
            neurons[i] = neurons[i - 1];

            // Also shift the synapses to the right
            memcpy(getSynapses(i), getSynapses(i - 1), numberOfNeighbors * sizeof(Synapse));
        }
        neurons[insertedNeuronIdx] = insertNeuron;
        population++;

        // Try to update the synapse of inserted neuron. All outgoing synapse is init as zero weight
        Synapse *pInsertNeuronSynapse = getSynapses(insertedNeuronIdx);
        for (unsigned long long synIdx = 0; synIdx < numberOfNeighbors; ++synIdx)
        {
            pInsertNeuronSynapse[synIdx].weight = 0;
        }

        // Copy the outgoing synapse of original neuron
        // Outgoing points to the left
        if (incomingNeighborSynapseIdx < numberOfNeighbors / 2)
        {
            if (incomingNeighborSynapseIdx > 0)
            {
                // Decrease by one because the new neuron is next to the original one
                pInsertNeuronSynapse[incomingNeighborSynapseIdx - 1].weight = originalWeight;
            }
            // Incase of the outgoing synapse point too far, don't add the synapse
        }
        else
        {
            // No need to adjust the added neuron but need to remove the synapse of the original neuron
            pInsertNeuronSynapse[incomingNeighborSynapseIdx].weight = originalWeight;
        }

        // The change of synapse only impact neuron in [originalNeuronIdx - numberOfNeighbors / 2 + 1, originalNeuronIdx +  numberOfNeighbors / 2]
        // In the new index, it will be  [originalNeuronIdx + 1 - numberOfNeighbors / 2, originalNeuronIdx + 1 + numberOfNeighbors / 2]
        // [N0 N1 N2 original inserted N4 N5 N6], M = 2.
        for (long long delta = -(long long)numberOfNeighbors / 2; delta <= (long long)numberOfNeighbors / 2; ++delta)
        {
            // Only process the neigbors
            if (delta == 0)
            {
                continue;
            }
            unsigned long long updatedNeuronIdx = clampNeuronIndex(insertedNeuronIdx, delta);

            // Generate a list of neighbor index of current updated neuron NN
            // Find the location of the inserted neuron in the list of neighbors
            long long insertedNeuronIdxInNeigborList = -1;
            for (long long k = 0; k < numberOfNeighbors; k++)
            {
                unsigned long long nnIndex = getNeighborNeuronIndex(updatedNeuronIdx, k);
                if (nnIndex == insertedNeuronIdx)
                {
                    insertedNeuronIdxInNeigborList = k;
                }
            }

            assert(insertedNeuronIdxInNeigborList >= 0);

            Synapse *pUpdatedSynapses = getSynapses(updatedNeuronIdx);
            // [N0 N1 N2 original inserted N4 N5 N6], M = 2.
            // Case: neurons in range [N0 N1 N2 original], right synapses will be affected
            if (delta < 0)
            {
                // Left side is kept as it is, only need to shift to the right side
                for (long long k = numberOfNeighbors - 1; k >= insertedNeuronIdxInNeigborList; --k)
                {
                    // Updated synapse
                    pUpdatedSynapses[k] = pUpdatedSynapses[k - 1];
                }

                // Incomming synapse from original neuron -> inserted neuron must be zero
                if (delta == -1)
                {
                    pUpdatedSynapses[insertedNeuronIdxInNeigborList].weight = 0;
                }
            }
            else // Case: neurons in range [inserted N4 N5 N6], left synapses will be affected
            {
                // Right side is kept as it is, only need to shift to the left side
                for (unsigned long long k = 0; k < insertedNeuronIdxInNeigborList; ++k)
                {
                    // Updated synapse
                    pUpdatedSynapses[k] = pUpdatedSynapses[k + 1];
                }
            }
        }
    }

    long long getIndexInSynapsesBuffer(unsigned long long neuronIdx, long long neighborOffset)
    {
        // Skip the case neuron point to itself and too far neighbor
        if (neighborOffset == 0 || neighborOffset < -(long long)numberOfNeighbors / 2 || neighborOffset > (long long)numberOfNeighbors / 2)
        {
            return -1;
        }

        long long synapseIdx = (long long)numberOfNeighbors / 2 + neighborOffset;
        if (neighborOffset >= 0)
        {
            synapseIdx = synapseIdx - 1;
        }

        return synapseIdx;
    }

    // Check which neurons/synapse need to be removed after mutation
    unsigned long long scanRedundantNeurons()
    {
        unsigned long long population = currentANN.population;
        Synapse *synapses = currentANN.synapses;
        Neuron *neurons = currentANN.neurons;

        unsigned long long numberOfRedundantNeurons = 0;
        // After each mutation, we must verify if there are neurons that do not affect the ANN output.
        // These are neurons that either have all incoming synapse weights as 0,
        // or all outgoing synapse weights as 0. Such neurons must be removed.
        for (unsigned long long i = 0; i < population; i++)
        {
            neurons[i].markForRemoval = false;
            if (neurons[i].type == Neuron::kEvolution)
            {
                bool allOutGoingZeros = true;
                bool allIncommingZeros = true;

                // Loop though its synapses for checkout outgoing synapses
                for (unsigned long long n = 0; n < numberOfNeighbors; n++)
                {
                    char synapseW = synapses[i * numberOfNeighbors + n].weight;
                    if (synapseW != 0)
                    {
                        allOutGoingZeros = false;
                        break;
                    }
                }

                // Loop through the neighbor neurons to check all incoming synapses
                for (long long neighborOffset = -(long long)numberOfNeighbors / 2; neighborOffset <= (long long)numberOfNeighbors / 2; neighborOffset++)
                {
                    unsigned long long nnIdx = clampNeuronIndex(i, neighborOffset);
                    Synapse *nnSynapses = getSynapses(nnIdx);

                    long long synapseIdx = getIndexInSynapsesBuffer(nnIdx, -neighborOffset);
                    if (synapseIdx < 0)
                    {
                        continue;
                    }
                    char synapseW = nnSynapses[synapseIdx].weight;

                    if (synapseW != 0)
                    {
                        allIncommingZeros = false;
                        break;
                    }
                }
                if (allOutGoingZeros || allIncommingZeros)
                {
                    neurons[i].markForRemoval = true;
                    numberOfRedundantNeurons++;
                }
            }
        }
        return numberOfRedundantNeurons;
    }

    // Remove neurons and synapses that do not affect the ANN
    void cleanANN()
    {
        Synapse *synapses = currentANN.synapses;
        Neuron *neurons = currentANN.neurons;
        unsigned long long &population = currentANN.population;

        // Scan and remove neurons/synapses
        unsigned long long neuronIdx = 0;
        while (neuronIdx < population)
        {
            if (neurons[neuronIdx].markForRemoval)
            {
                // Remove it from the neuron list. Overwrite data
                // Remove its synapses in the synapses array
                removeNeuron(neuronIdx);
            }
            else
            {
                neuronIdx++;
            }
        }
    }

    void processTickBitmask()
    {
        unsigned long long pop = currentANN.population;

        // Pack neuron values into bitmasks
        packNeuronBitmasks();

        // For each target neuron, compute weighted sum using bitmask AND + popcount
        for (unsigned long long t = 0; t < pop; t++)
        {
            long long score = 0;

            const unsigned long long *sPlus = &inSynPlusBits[t * windowQwords];
            const unsigned long long *sMinus = &inSynMinusBits[t * windowQwords];

            // Neuron window starts at padded bit position t
            for (unsigned long long q = 0; q < windowQwords; q++)
            {
                unsigned long long nPlus = loadBits64(neuronPlusBits, t + q * 64);
                unsigned long long nMinus = loadBits64(neuronMinusBits, t + q * 64);

                // Ternary multiply: (+1)*(+1)=+1, (-1)*(-1)=+1, (+1)*(-1)=-1, (-1)*(+1)=-1
                unsigned long long plusBits = (nPlus & sPlus[q]) | (nMinus & sMinus[q]);
                unsigned long long minusBits = (nPlus & sMinus[q]) | (nMinus & sPlus[q]);

                score += __builtin_popcountll(plusBits) - __builtin_popcountll(minusBits);
            }

            // Branchless clamp to [-1, +1]
            neuronTickBuffer[t] = (char)((score > 0) - (score < 0));
        }

        // Write back to neurons
        for (unsigned long long i = 0; i < pop; i++)
        {
            currentANN.neurons[i].value = neuronTickBuffer[i];
        }
    }


    void runTickSimulation()
    {
        unsigned long long population = currentANN.population;
        Neuron *neurons = currentANN.neurons;

        // Transpose synapses to incoming bitmask format
        transposeAndPackSynapses();

        // Save the neuron value for comparison
        for (unsigned long long i = 0; i < population; ++i)
        {
            previousNeuronValue[i] = neurons[i].value;
        }

        for (unsigned long long tick = 0; tick < numberOfTicks; ++tick)
        {
            processTickBitmask();

            // Check exit conditions
            bool allNeuronsUnchanged = true;
            bool allOutputNeuronsNonZero = true;
            for (unsigned long long n = 0; n < population; ++n)
            {
                if (previousNeuronValue[n] != neurons[n].value)
                {
                    allNeuronsUnchanged = false;
                }
                if (neurons[n].type == Neuron::kOutput && neurons[n].value == 0)
                {
                    allOutputNeuronsNonZero = false;
                }
            }

            if (allNeuronsUnchanged || allOutputNeuronsNonZero)
            {
                break;
            }

            for (unsigned long long n = 0; n < population; ++n)
            {
                previousNeuronValue[n] = neurons[n].value;
            }
        }
    }

    unsigned int computeNonMatchingOutput()
    {
        unsigned long long population = currentANN.population;
        Neuron *neurons = currentANN.neurons;

        // Compute the non-matching value R between output neuron value and initial value
        // Because the output neuron order never changes, the order is preserved
        unsigned int R = 0;
        unsigned long long outputIdx = 0;
        for (unsigned long long i = 0; i < population; i++)
        {
            if (neurons[i].type == Neuron::kOutput)
            {
                if (neurons[i].value != outputNeuronExpectedValue[outputIdx])
                {
                    R++;
                }
                outputIdx++;
            }
        }
        return R;
    }

    void initInputNeuron()
    {
        unsigned long long population = currentANN.population;
        Neuron *neurons = currentANN.neurons;
        unsigned long long inputNeuronInitIndex = 0;

        char neuronArray[64] = {0};
        for (unsigned long long i = 0; i < population; ++i)
        {
            // Input will use the init value
            if (neurons[i].type == Neuron::kInput)
            {
                // Prepare new pack
                if (inputNeuronInitIndex % 64 == 0)
                {
                    extract64Bits(miningData.inputNeuronRandomNumber[inputNeuronInitIndex / 64], neuronArray);
                }
                char neuronValue = neuronArray[inputNeuronInitIndex % 64];

                // Convert value of neuron to trits (keeping 1 as 1, and changing 0 to -1.).
                neurons[i].value = (neuronValue == 0) ? -1 : neuronValue;

                inputNeuronInitIndex++;
            }
        }
    }

    void initOutputNeuron()
    {
        unsigned long long population = currentANN.population;
        Neuron *neurons = currentANN.neurons;
        for (unsigned long long i = 0; i < population; ++i)
        {
            if (neurons[i].type == Neuron::kOutput)
            {
                neurons[i].value = 0;
            }
        }
    }

    void initExpectedOutputNeuron()
    {
        char neuronArray[64] = {0};
        for (unsigned long long i = 0; i < numberOfOutputNeurons; ++i)
        {
            // Prepare new pack
            if (i % 64 == 0)
            {
                extract64Bits(miningData.outputNeuronRandomNumber[i / 64], neuronArray);
            }
            char neuronValue = neuronArray[i % 64];
            // Convert value of neuron (keeping 1 as 1, and changing 0 to -1.).
            outputNeuronExpectedValue[i] = (neuronValue == 0) ? -1 : neuronValue;
        }
    }

    unsigned int initializeANN(unsigned char *publicKey, unsigned char *nonce)
    {
        unsigned char hash[32];
        unsigned char combined[64];
        memcpy(combined, publicKey, 32);
        memcpy(combined + 32, nonce, 32);
        KangarooTwelve(combined, 64, hash, 32);

        unsigned long long &population = currentANN.population;
        Synapse *synapses = currentANN.synapses;
        Neuron *neurons = currentANN.neurons;

        // Initialization
        population = numberOfNeurons;

        // Initalize with nonce and public key
        random2(hash, poolVec.data(), (unsigned char *)&initValue, sizeof(InitValue));

        // Randomly choose the positions of neurons types
        for (unsigned long long i = 0; i < population; ++i)
        {
            neuronIndices[i] = i;
            neurons[i].type = Neuron::kInput;
        }
        unsigned long long neuronCount = population;
        for (unsigned long long i = 0; i < numberOfOutputNeurons; ++i)
        {
            unsigned long long outputNeuronIdx = initValue.outputNeuronPositions[i] % neuronCount;

            // Fill the neuron type
            neurons[neuronIndices[outputNeuronIdx]].type = Neuron::kOutput;
            outputNeuronIndices[i] = neuronIndices[outputNeuronIdx];

            // This index is used, copy the end of indices array to current position and decrease the number of picking neurons
            neuronCount = neuronCount - 1;
            neuronIndices[outputNeuronIdx] = neuronIndices[neuronCount];
        }

        // Synapse weight initialization
        for (unsigned long long i = 0; i < (initNumberOfSynapses / 32); ++i)
        {
            const unsigned long long mask = 0b11;

            for (int j = 0; j < 32; ++j)
            {
                int shiftVal = j * 2;
                unsigned char extractValue = (unsigned char)((initValue.synapseWeight[i] >> shiftVal) & mask);
                switch (extractValue)
                {
                case 2:
                    synapses[32 * i + j].weight = -1;
                    break;
                case 3:
                    synapses[32 * i + j].weight = 1;
                    break;
                default:
                    synapses[32 * i + j].weight = 0;
                }
            }
        }

        // Init the neuron input and expected output value
        memcpy((unsigned char *)&miningData, poolVec.data(), sizeof(miningData));

        // Init input neuron value and output neuron
        initInputNeuron();
        initOutputNeuron();

        // Init expected output neuron
        initExpectedOutputNeuron();

        // Ticks simulation
        runTickSimulation();

        // Copy the state for rollback later
        memcpy(&bestANN, &currentANN, sizeof(ANN));

        // Compute R and roll back if neccessary
        unsigned int R = computeNonMatchingOutput();

        return R;
    }

    // Main function for mining
    bool findSolution(unsigned char *publicKey, unsigned char *nonce)
    {
        // Initialize
        unsigned int bestR = initializeANN(publicKey, nonce);

        for (unsigned long long s = 0; s < numberOfMutations; ++s)
        {

            // Do the mutation
            mutate(nonce, s);

            // Exit if the number of population reaches the maximum allowed
            if (currentANN.population >= populationThreshold)
            {
                break;
            }

            // Ticks simulation
            runTickSimulation();

            // Compute R and roll back if neccessary
            unsigned int R = computeNonMatchingOutput();
            if (R > bestR)
            {
                // Roll back
                memcpy(&currentANN, &bestANN, sizeof(bestANN));
            }
            else
            {
                bestR = R;

                // Better R. Save the state
                memcpy(&bestANN, &currentANN, sizeof(bestANN));
            }

            assert(bestANN.population <= populationThreshold);
        }

        // Check score
        unsigned int score = numberOfOutputNeurons - bestR;
        if (score >= solutionThreshold)
        {
            return true;
        }

        return false;
    }
};

#ifdef _MSC_VER
static BOOL WINAPI ctrlCHandlerRoutine(DWORD dwCtrlType)
{
    if (!state)
    {
        state = 1;
    }
    else // User force exit quickly
    {
        std::exit(1);
    }
    return TRUE;
}
#else
void ctrlCHandlerRoutine(int signum)
{
    if (!state)
    {
        state = 1;
    }
    else // User force exit quickly
    {
        std::exit(1);
    }
}
#endif

void consoleCtrlHandler()
{
#ifdef _MSC_VER
    SetConsoleCtrlHandler(ctrlCHandlerRoutine, TRUE);
#else
    signal(SIGINT, ctrlCHandlerRoutine);
#endif
}

int getSystemProcs()
{
#ifdef _MSC_VER
#else
#endif
    return 0;
}

// =============================================================================
// Addition Algorithm Miner
// Trains ANN on all 2^K (A+B=C) pairs. Score = total correct outputs across
// all samples. Fundamentally different from HyperIdentity.
// =============================================================================
template <
    unsigned long long numberOfInputNeurons,   // K (14)
    unsigned long long numberOfOutputNeurons,  // L (8)
    unsigned long long numberOfTicks,          // N (1000)
    unsigned long long maxNumberOfNeighbors,   // 2M (728)
    unsigned long long populationThreshold,    // P (522)
    unsigned long long numberOfMutations,      // S (500)
    unsigned int solutionThreshold>            // (75700)
struct AdditionMiner
{
    unsigned char computorPublicKey[32];
    unsigned char currentRandomSeed[32];
    int difficulty;

    static constexpr unsigned long long numberOfNeurons = numberOfInputNeurons + numberOfOutputNeurons;
    static constexpr unsigned long long maxNumberOfNeurons = populationThreshold;
    static constexpr unsigned long long maxNumberOfSynapses = populationThreshold * maxNumberOfNeighbors;
    static constexpr unsigned long long trainingSetSize = 1ULL << numberOfInputNeurons; // 2^K = 16384
    static constexpr unsigned long long paddingNumberOfSynapses = (maxNumberOfSynapses + 31) / 32 * 32;

    static_assert(maxNumberOfSynapses <= (0xFFFFFFFFFFFFFFFF >> 1), "maxNumberOfSynapses overflow");
    static_assert(maxNumberOfNeighbors % 2 == 0, "maxNumberOfNeighbors must be even");
    static_assert(populationThreshold > numberOfNeurons, "populationThreshold must exceed numberOfNeurons");

    std::vector<unsigned char> poolVec;

    bool updateLatestQatumData()
    {
        bool seedChanged = memcmp(this->currentRandomSeed, ::randomSeed, sizeof(currentRandomSeed)) != 0;
        memcpy(this->currentRandomSeed, ::randomSeed, sizeof(currentRandomSeed));
        memcpy(this->computorPublicKey, ::computorPublicKey, sizeof(computorPublicKey));
        this->difficulty = ::difficulty;
        if (seedChanged && !isZeros<32>(this->currentRandomSeed))
        {
            generateRandom2Pool(this->currentRandomSeed, poolVec.data());
        }
        return !isZeros<32>(this->computorPublicKey) && !isZeros<32>(this->currentRandomSeed) && this->difficulty != 0;
    }

    static bool checkGlobalQatumDataAvailability()
    {
        return !isZeros<32>(::computorPublicKey) && !isZeros<32>(::randomSeed) && ::difficulty != 0;
    }

    void initialize(unsigned char miningSeed[32])
    {
        poolVec.resize(POOL_VEC_PADDING_SIZE);
        generateRandom2Pool(miningSeed, poolVec.data());
        generateTrainingSet();
    }

    typedef char Synapse;

    struct ANN
    {
        unsigned char neuronTypes[maxNumberOfNeurons];
        Synapse synapses[maxNumberOfSynapses];
        unsigned long long population;
    };

    struct InitValue
    {
        unsigned long long outputNeuronPositions[numberOfOutputNeurons];
        unsigned long long synapseWeight[paddingNumberOfSynapses / 32];
        unsigned long long synpaseMutation[numberOfMutations];
    };
    static constexpr unsigned long long paddingInitValueSizeInBytes = (sizeof(InitValue) + 64 - 1) / 64 * 64;
    unsigned char paddingInitValue[paddingInitValueSizeInBytes];

    static constexpr unsigned char INPUT_NEURON_TYPE = 0;
    static constexpr unsigned char OUTPUT_NEURON_TYPE = 1;
    static constexpr unsigned char EVOLUTION_NEURON_TYPE = 2;

    // Training data (generated once at init)
    char trainingInputs[numberOfInputNeurons][trainingSetSize];
    char trainingOutputs[numberOfOutputNeurons][trainingSetSize];

    // Per-sample neuron values: neuronValues[neuronIdx][sampleIdx]
    char *neuronValues;
    char *prevNeuronValues;
    char neuronValuesBuffer0[maxNumberOfNeurons * trainingSetSize];
    char neuronValuesBuffer1[maxNumberOfNeurons * trainingSetSize];

    // Sample tracking
    unsigned int sampleMapping[trainingSetSize];
    unsigned int sampleScores[trainingSetSize];
    unsigned long long activeCount;

    // ANN structures
    ANN bestANN;
    ANN currentANN;

    // Index caches
    unsigned long long neuronIndices[numberOfNeurons];
    unsigned long long outputNeuronIndices[numberOfOutputNeurons];

    // Temp buffers for training set generation
    char inputBits[numberOfInputNeurons];
    char outputBits[numberOfOutputNeurons];

    // Convert integer to ternary bits: bit i = ((A >> i) & 1), then 0 -> -1, 1 -> 1
    template <unsigned long long bitCount>
    static void toTernaryBits(long long A, char *bits)
    {
        for (unsigned long long i = 0; i < bitCount; ++i)
        {
            char bitValue = static_cast<char>((A >> i) & 1);
            bits[i] = (bitValue == 0) ? -1 : bitValue;
        }
    }

    void generateTrainingSet()
    {
        static constexpr long long halfK = numberOfInputNeurons / 2;
        static constexpr long long boundValue = (1LL << halfK) / 2;
        memset(trainingInputs, 0, sizeof(trainingInputs));
        memset(trainingOutputs, 0, sizeof(trainingOutputs));

        unsigned long long sampleIdx = 0;
        for (long long A = -boundValue; A < boundValue; A++)
        {
            for (long long B = -boundValue; B < boundValue; B++)
            {
                long long C = A + B;
                toTernaryBits<halfK>(A, inputBits);
                toTernaryBits<halfK>(B, inputBits + halfK);
                toTernaryBits<numberOfOutputNeurons>(C, outputBits);

                for (unsigned long long n = 0; n < numberOfInputNeurons; n++)
                    trainingInputs[n][sampleIdx] = inputBits[n];
                for (unsigned long long n = 0; n < numberOfOutputNeurons; n++)
                    trainingOutputs[n][sampleIdx] = outputBits[n];

                sampleIdx++;
            }
        }
    }

    // --- Dynamic neighbor helpers ---
    unsigned long long getActualNeighborCount() const
    {
        unsigned long long pop = currentANN.population;
        unsigned long long maxN = pop - 1;
        return maxNumberOfNeighbors > maxN ? maxN : maxNumberOfNeighbors;
    }
    unsigned long long getLeftNeighborCount() const { return (getActualNeighborCount() + 1) / 2; }
    unsigned long long getRightNeighborCount() const { return getActualNeighborCount() - getLeftNeighborCount(); }
    unsigned long long getSynapseStartIndex() const
    {
        return maxNumberOfNeighbors / 2 - getLeftNeighborCount();
    }
    unsigned long long getSynapseEndIndex() const
    {
        return maxNumberOfNeighbors / 2 + getRightNeighborCount();
    }

    long long bufferIndexToOffset(unsigned long long bufferIdx) const
    {
        long long center = (long long)(maxNumberOfNeighbors / 2);
        if ((long long)bufferIdx < center)
            return (long long)bufferIdx - center;
        else
            return (long long)bufferIdx - center + 1;
    }

    long long offsetToBufferIndex(long long offset) const
    {
        long long center = (long long)(maxNumberOfNeighbors / 2);
        if (offset == 0) return -1;
        if (offset < 0) return center + offset;
        return center + offset - 1;
    }

    long long getIndexInSynapsesBuffer(long long neighborOffset) const
    {
        long long leftCount = (long long)getLeftNeighborCount();
        long long rightCount = (long long)getRightNeighborCount();
        if (neighborOffset == 0 || neighborOffset < -leftCount || neighborOffset > rightCount)
            return -1;
        return offsetToBufferIndex(neighborOffset);
    }

    Synapse *getSynapses(unsigned long long neuronIndex)
    {
        return &currentANN.synapses[neuronIndex * maxNumberOfNeighbors];
    }

    unsigned long long clampNeuronIndex(long long neuronIdx, long long value) const
    {
        long long pop = (long long)currentANN.population;
        long long nnIndex = neuronIdx + value;
        nnIndex += (pop & (nnIndex >> 63));
        long long over = nnIndex - pop;
        nnIndex -= (pop & ~(over >> 63));
        return (unsigned long long)nnIndex;
    }

    unsigned long long getNeighborNeuronIndex(unsigned long long neuronIndex, unsigned long long neighborOffset)
    {
        unsigned long long leftNeighbors = getLeftNeighborCount();
        if (neighborOffset < leftNeighbors)
            return clampNeuronIndex(neuronIndex + neighborOffset, -(long long)leftNeighbors);
        else
            return clampNeuronIndex(neuronIndex + neighborOffset + 1, -(long long)leftNeighbors);
    }

    // --- Neuron insertion ---
    void insertNeuron(unsigned long long neuronIndex, unsigned long long synapseIndex)
    {
        unsigned long long oldStartIdx = getSynapseStartIndex();
        unsigned long long oldEndIdx = getSynapseEndIndex();
        long long oldLeftCount = (long long)getLeftNeighborCount();
        long long oldRightCount = (long long)getRightNeighborCount();
        constexpr unsigned long long halfMax = maxNumberOfNeighbors / 2;

        Synapse *synapses = currentANN.synapses;
        unsigned char *neuronTypes = currentANN.neuronTypes;
        unsigned long long &population = currentANN.population;

        unsigned long long insertedNeuronIdx = neuronIndex + 1;
        char originalWeight = synapses[neuronIndex * maxNumberOfNeighbors + synapseIndex];

        // Shift neurons right to make room
        for (unsigned long long i = population; i > neuronIndex; --i)
        {
            neuronTypes[i] = neuronTypes[i - 1];
            memcpy(getSynapses(i), getSynapses(i - 1), maxNumberOfNeighbors * sizeof(Synapse));
        }
        neuronTypes[insertedNeuronIdx] = EVOLUTION_NEURON_TYPE;
        population++;

        // Recalculate after population change
        unsigned long long newActualNeighbors = getActualNeighborCount();
        unsigned long long newStartIdx = getSynapseStartIndex();
        unsigned long long newEndIdx = getSynapseEndIndex();

        // Init inserted neuron's synapses to zero
        Synapse *pInsert = getSynapses(insertedNeuronIdx);
        memset(pInsert, 0, maxNumberOfNeighbors * sizeof(Synapse));

        // Copy the original synapse weight
        if (synapseIndex < halfMax)
        {
            if (synapseIndex > newStartIdx)
                pInsert[synapseIndex - 1] = originalWeight;
        }
        else
        {
            pInsert[synapseIndex] = originalWeight;
        }

        // Update neighbor synapses
        for (long long delta = -oldLeftCount; delta <= oldRightCount; ++delta)
        {
            if (delta == 0) continue;
            unsigned long long updatedNeuronIdx = clampNeuronIndex(insertedNeuronIdx, delta);

            long long insertedInNeighborList = -1;
            for (unsigned long long k = 0; k < newActualNeighbors; k++)
            {
                if (getNeighborNeuronIndex(updatedNeuronIdx, k) == insertedNeuronIdx)
                {
                    insertedInNeighborList = (long long)(newStartIdx + k);
                    break;
                }
            }
            if (insertedInNeighborList < 0) continue;

            Synapse *pUpdated = getSynapses(updatedNeuronIdx);
            if (delta < 0)
            {
                for (long long k = (long long)newEndIdx - 1; k >= insertedInNeighborList; --k)
                    pUpdated[k] = pUpdated[k - 1];
                if (delta == -1)
                    pUpdated[insertedInNeighborList] = 0;
            }
            else
            {
                for (long long k = (long long)newStartIdx; k < insertedInNeighborList; ++k)
                    pUpdated[k] = pUpdated[k + 1];
            }
        }
    }

    // --- Neuron removal ---
    void removeNeuron(unsigned long long neuronIdx)
    {
        long long leftCount = (long long)getLeftNeighborCount();
        long long rightCount = (long long)getRightNeighborCount();
        unsigned long long startIdx = getSynapseStartIndex();
        unsigned long long endIdx = getSynapseEndIndex();
        constexpr unsigned long long halfMax = maxNumberOfNeighbors / 2;

        for (long long offset = -leftCount; offset <= rightCount; offset++)
        {
            if (offset == 0) continue;
            unsigned long long nnIdx = clampNeuronIndex(neuronIdx, offset);
            Synapse *pNN = getSynapses(nnIdx);
            long long synIdx = getIndexInSynapsesBuffer(-offset);
            if (synIdx < 0) continue;

            if (synIdx >= (long long)halfMax)
            {
                for (long long k = synIdx; k < (long long)endIdx - 1; ++k)
                    pNN[k] = pNN[k + 1];
                pNN[endIdx - 1] = 0;
            }
            else
            {
                for (long long k = synIdx; k > (long long)startIdx; --k)
                    pNN[k] = pNN[k - 1];
                pNN[startIdx] = 0;
            }
        }

        for (unsigned long long i = neuronIdx; i < currentANN.population - 1; i++)
        {
            currentANN.neuronTypes[i] = currentANN.neuronTypes[i + 1];
            memcpy(getSynapses(i), getSynapses(i + 1), maxNumberOfNeighbors * sizeof(Synapse));
        }
        currentANN.population--;
    }

    // --- Redundancy detection and cleanup ---
    unsigned long long removalNeurons[maxNumberOfNeurons];
    unsigned long long numberOfRedundantNeurons;

    unsigned long long scanRedundantNeurons()
    {
        unsigned long long pop = currentANN.population;
        Synapse *synapses = currentANN.synapses;
        unsigned long long startIdx = getSynapseStartIndex();
        unsigned long long endIdx = getSynapseEndIndex();
        long long leftCount = (long long)getLeftNeighborCount();
        long long rightCount = (long long)getRightNeighborCount();

        numberOfRedundantNeurons = 0;
        for (unsigned long long i = 0; i < pop; i++)
        {
            if (currentANN.neuronTypes[i] != EVOLUTION_NEURON_TYPE) continue;

            bool allOutZero = true;
            for (unsigned long long m = startIdx; m < endIdx && allOutZero; m++)
                if (synapses[i * maxNumberOfNeighbors + m] != 0)
                    allOutZero = false;

            bool allInZero = true;
            for (long long off = -leftCount; off <= rightCount && allInZero; off++)
            {
                if (off == 0) continue;
                unsigned long long nnIdx = clampNeuronIndex(i, off);
                long long synIdx = getIndexInSynapsesBuffer(-off);
                if (synIdx < 0) continue;
                if (getSynapses(nnIdx)[synIdx] != 0)
                    allInZero = false;
            }

            if (allOutZero || allInZero)
                removalNeurons[numberOfRedundantNeurons++] = i;
        }
        return numberOfRedundantNeurons;
    }

    void cleanANN()
    {
        for (unsigned long long i = 0; i < numberOfRedundantNeurons; i++)
        {
            removeNeuron(removalNeurons[i]);
            for (unsigned long long j = i + 1; j < numberOfRedundantNeurons; j++)
                removalNeurons[j]--;
        }
    }

    // --- Mutation ---
    void mutate(unsigned long long mutateStep)
    {
        unsigned long long pop = currentANN.population;
        unsigned long long actualNeighbors = getActualNeighborCount();
        Synapse *synapses = currentANN.synapses;
        InitValue *initValue = (InitValue *)paddingInitValue;

        unsigned long long synapseMutation = initValue->synpaseMutation[mutateStep];
        unsigned long long totalValidSynapses = pop * actualNeighbors;
        unsigned long long flatIdx = (synapseMutation >> 1) % totalValidSynapses;

        unsigned long long neuronIdx = flatIdx / actualNeighbors;
        unsigned long long localSynapseIdx = flatIdx % actualNeighbors;
        unsigned long long synapseIndex = localSynapseIdx + getSynapseStartIndex();
        unsigned long long synapseFullIdx = neuronIdx * maxNumberOfNeighbors + synapseIndex;

        char weightChange = ((synapseMutation & 1ULL) == 0) ? (char)-1 : (char)1;
        char newWeight = synapses[synapseFullIdx] + weightChange;

        if (newWeight >= -1 && newWeight <= 1)
            synapses[synapseFullIdx] = newWeight;
        else
            insertNeuron(neuronIdx, synapseIndex);

        while (scanRedundantNeurons() > 0)
            cleanANN();
    }

    // --- Tick simulation (scalar, processes all samples) ---
    void loadTrainingData()
    {
        unsigned long long pop = currentANN.population;
        unsigned long long inputIdx = 0;
        for (unsigned long long n = 0; n < pop; n++)
        {
            char *nv = &neuronValues[n * trainingSetSize];
            char *pnv = &prevNeuronValues[n * trainingSetSize];
            if (currentANN.neuronTypes[n] == INPUT_NEURON_TYPE)
            {
                memcpy(nv, trainingInputs[inputIdx], trainingSetSize);
                memcpy(pnv, trainingInputs[inputIdx], trainingSetSize);
                inputIdx++;
            }
            else
            {
                memset(nv, 0, trainingSetSize);
                memset(pnv, 0, trainingSetSize);
            }
        }
    }

    // Precompute incoming positive/negative synapse sources for each neuron
    unsigned int incomingPositiveSource[maxNumberOfNeurons * maxNumberOfNeighbors];
    unsigned int incomingNegativeSource[maxNumberOfNeurons * maxNumberOfNeighbors];
    unsigned int incomingPositiveCount[maxNumberOfNeurons];
    unsigned int incomingNegativeCount[maxNumberOfNeurons];

    void convertToIncomingSynapses()
    {
        unsigned long long pop = currentANN.population;
        Synapse *synapses = currentANN.synapses;
        unsigned long long startIdx = getSynapseStartIndex();
        unsigned long long endIdx = getSynapseEndIndex();

        memset(incomingPositiveCount, 0, sizeof(incomingPositiveCount));
        memset(incomingNegativeCount, 0, sizeof(incomingNegativeCount));

        for (unsigned long long n = 0; n < pop; n++)
        {
            const Synapse *kSynapses = &synapses[n * maxNumberOfNeighbors];
            for (unsigned long long synIdx = startIdx; synIdx < endIdx; synIdx++)
            {
                char weight = kSynapses[synIdx];
                if (weight == 0) continue;
                long long offset = bufferIndexToOffset(synIdx);
                unsigned long long nnIndex = clampNeuronIndex((long long)n, offset);

                if (weight > 0)
                {
                    unsigned int idx = incomingPositiveCount[nnIndex]++;
                    incomingPositiveSource[nnIndex * maxNumberOfNeighbors + idx] = (unsigned int)n;
                }
                else
                {
                    unsigned int idx = incomingNegativeCount[nnIndex]++;
                    incomingNegativeSource[nnIndex * maxNumberOfNeighbors + idx] = (unsigned int)n;
                }
            }
        }
    }

    // Cache output and evolution neuron indices
    unsigned long long outputNeuronIdxCache[numberOfOutputNeurons];
    unsigned long long numCachedOutputs;
    unsigned long long evolutionNeuronIdxCache[numberOfMutations];
    unsigned long long numCachedEvolution;
    // Pre-multiplied offsets for neuron data access
    unsigned long long processNeuronOffsets[maxNumberOfNeurons];
    unsigned long long numProcessNeurons;

    // Accumulator buffer for auto-vectorized tick processing
    alignas(32) short neuronAccBuffer[trainingSetSize];

    void cacheNeuronIndices()
    {
        numCachedOutputs = 0;
        numCachedEvolution = 0;
        numProcessNeurons = 0;
        for (unsigned long long i = 0; i < currentANN.population; i++)
        {
            if (currentANN.neuronTypes[i] == OUTPUT_NEURON_TYPE)
            {
                outputNeuronIdxCache[numCachedOutputs++] = i;
                processNeuronOffsets[numProcessNeurons++] = i;
            }
            else if (currentANN.neuronTypes[i] == EVOLUTION_NEURON_TYPE)
            {
                evolutionNeuronIdxCache[numCachedEvolution++] = i;
                processNeuronOffsets[numProcessNeurons++] = i;
            }
        }
    }

    void processNeuronTick(unsigned long long targetNeuron, unsigned long long activeSampleCount)
    {
        unsigned int numPos = incomingPositiveCount[targetNeuron];
        unsigned int numNeg = incomingNegativeCount[targetNeuron];
        char *dst = &neuronValues[targetNeuron * trainingSetSize];
        const unsigned int *posSrc = &incomingPositiveSource[targetNeuron * maxNumberOfNeighbors];
        const unsigned int *negSrc = &incomingNegativeSource[targetNeuron * maxNumberOfNeighbors];

        // Source-outer loop enables auto-vectorization across samples
        // Use int16 accumulator (safe for up to 32767 sources)
        memset(neuronAccBuffer, 0, activeSampleCount * sizeof(short));

        for (unsigned int i = 0; i < numPos; i++)
        {
            const char *srcData = &prevNeuronValues[(unsigned long long)posSrc[i] * trainingSetSize];
            for (unsigned long long s = 0; s < activeSampleCount; s++)
                neuronAccBuffer[s] += srcData[s];
        }
        for (unsigned int i = 0; i < numNeg; i++)
        {
            const char *srcData = &prevNeuronValues[(unsigned long long)negSrc[i] * trainingSetSize];
            for (unsigned long long s = 0; s < activeSampleCount; s++)
                neuronAccBuffer[s] -= srcData[s];
        }

        // Clamp to [-1, +1]
        for (unsigned long long s = 0; s < activeSampleCount; s++)
        {
            short v = neuronAccBuffer[s];
            dst[s] = (char)((v > 0) - (v < 0));
        }
    }

    void processTick(unsigned long long activeSampleCount)
    {
        for (unsigned long long idx = 0; idx < numCachedOutputs; ++idx)
            processNeuronTick(outputNeuronIdxCache[idx], activeSampleCount);
        for (unsigned long long idx = 0; idx < numCachedEvolution; ++idx)
            processNeuronTick(evolutionNeuronIdxCache[idx], activeSampleCount);
    }

    // Compact active samples + score converged ones
    void compactAndScore()
    {
        unsigned long long pop = currentANN.population;
        unsigned long long writePos = 0;

        for (unsigned long long s = 0; s < activeCount; s++)
        {
            // Check if all output+evolution neurons unchanged
            bool allUnchanged = true;
            for (unsigned long long i = 0; i < numCachedOutputs && allUnchanged; i++)
            {
                unsigned long long n = outputNeuronIdxCache[i];
                if (neuronValues[n * trainingSetSize + s] != prevNeuronValues[n * trainingSetSize + s])
                    allUnchanged = false;
            }
            for (unsigned long long i = 0; i < numCachedEvolution && allUnchanged; i++)
            {
                unsigned long long n = evolutionNeuronIdxCache[i];
                if (neuronValues[n * trainingSetSize + s] != prevNeuronValues[n * trainingSetSize + s])
                    allUnchanged = false;
            }

            // Check if all output neurons non-zero
            bool allOutputsNonZero = true;
            for (unsigned long long i = 0; i < numCachedOutputs && allOutputsNonZero; i++)
            {
                unsigned long long n = outputNeuronIdxCache[i];
                if (neuronValues[n * trainingSetSize + s] == 0)
                    allOutputsNonZero = false;
            }

            bool isDone = allUnchanged || allOutputsNonZero;

            if (isDone)
            {
                // Score this sample
                unsigned int origSample = sampleMapping[s];
                for (unsigned long long i = 0; i < numCachedOutputs; i++)
                {
                    unsigned long long n = outputNeuronIdxCache[i];
                    if (neuronValues[n * trainingSetSize + s] == trainingOutputs[i][origSample])
                        sampleScores[origSample]++;
                }
            }
            else
            {
                // Compact: move to writePos
                if (writePos != s)
                {
                    sampleMapping[writePos] = sampleMapping[s];
                    for (unsigned long long n = 0; n < pop; n++)
                    {
                        neuronValues[n * trainingSetSize + writePos] = neuronValues[n * trainingSetSize + s];
                        prevNeuronValues[n * trainingSetSize + writePos] = prevNeuronValues[n * trainingSetSize + s];
                    }
                }
                writePos++;
            }
        }
        activeCount = writePos;
    }

    void runTickSimulation()
    {
        unsigned long long pop = currentANN.population;

        // Init sample tracking
        for (unsigned long long i = 0; i < trainingSetSize; i++)
        {
            sampleMapping[i] = (unsigned int)i;
            sampleScores[i] = 0;
        }
        activeCount = trainingSetSize;

        loadTrainingData();
        cacheNeuronIndices();
        convertToIncomingSynapses();

        for (unsigned long long tick = 0; tick < numberOfTicks; ++tick)
        {
            if (activeCount == 0) break;

            // Swap current/prev pointers
            char *tmp = neuronValues;
            neuronValues = prevNeuronValues;
            prevNeuronValues = tmp;

            processTick(activeCount);
            compactAndScore();
        }

        // Score remaining active samples
        for (unsigned long long s = 0; s < activeCount; s++)
        {
            unsigned int origSample = sampleMapping[s];
            for (unsigned long long i = 0; i < numCachedOutputs; i++)
            {
                unsigned long long n = outputNeuronIdxCache[i];
                if (neuronValues[n * trainingSetSize + s] == trainingOutputs[i][origSample])
                    sampleScores[origSample]++;
            }
        }
    }

    unsigned int getTotalSamplesScore()
    {
        unsigned int total = 0;
        for (unsigned long long i = 0; i < trainingSetSize; i++)
            total += sampleScores[i];
        return total;
    }

    void copyANN(ANN &dst, const ANN &src)
    {
        unsigned long long pop = src.population;
        memcpy(dst.synapses, src.synapses, pop * maxNumberOfNeighbors * sizeof(Synapse));
        memcpy(dst.neuronTypes, src.neuronTypes, pop);
        dst.population = pop;
    }

    // --- Main scoring function ---
    unsigned int initializeANN(unsigned char *publicKey, unsigned char *nonce)
    {
        unsigned char hash[32];
        unsigned char combined[64];
        memcpy(combined, publicKey, 32);
        memcpy(combined + 32, nonce, 32);
        KangarooTwelve(combined, 64, hash, 32);

        unsigned long long &population = currentANN.population;
        Synapse *synapses = currentANN.synapses;
        unsigned char *neuronTypes = currentANN.neuronTypes;

        population = numberOfNeurons;
        neuronValues = neuronValuesBuffer0;
        prevNeuronValues = neuronValuesBuffer1;

        // Generate InitValue from random2
        random2(hash, poolVec.data(), (unsigned char *)paddingInitValue, sizeof(InitValue));
        InitValue *initValue = (InitValue *)paddingInitValue;

        // Randomly assign neuron types
        for (unsigned long long i = 0; i < population; ++i)
        {
            neuronIndices[i] = i;
            neuronTypes[i] = INPUT_NEURON_TYPE;
        }
        unsigned long long neuronCount = population;
        for (unsigned long long i = 0; i < numberOfOutputNeurons; ++i)
        {
            unsigned long long idx = initValue->outputNeuronPositions[i] % neuronCount;
            neuronTypes[neuronIndices[idx]] = OUTPUT_NEURON_TYPE;
            outputNeuronIndices[i] = neuronIndices[idx];
            neuronCount--;
            neuronIndices[idx] = neuronIndices[neuronCount];
        }

        // Synapse weight initialization via 2-bit LUT
        {
            const unsigned long long initSynapses = populationThreshold * maxNumberOfNeighbors;
            const unsigned long long initSynapsesPadded = (initSynapses + 31) / 32 * 32;
            for (unsigned long long i = 0; i < initSynapsesPadded / 32; ++i)
            {
                for (int j = 0; j < 32; ++j)
                {
                    unsigned long long idx = 32 * i + j;
                    if (idx >= maxNumberOfSynapses) break;
                    unsigned char ev = (unsigned char)((initValue->synapseWeight[i] >> (j * 2)) & 3);
                    synapses[idx] = (ev == 2) ? (char)-1 : (ev == 3) ? (char)1 : (char)0;
                }
            }
        }

        // Run first inference
        runTickSimulation();
        unsigned int score = getTotalSamplesScore();
        return score;
    }

    bool findSolution(unsigned char *publicKey, unsigned char *nonce)
    {
        unsigned int bestScore = initializeANN(publicKey, nonce);
        copyANN(bestANN, currentANN);

        for (unsigned long long s = 0; s < numberOfMutations; ++s)
        {
            mutate(s);

            if (currentANN.population >= populationThreshold)
                break;

            runTickSimulation();
            unsigned int score = getTotalSamplesScore();

            if (score >= bestScore)
            {
                bestScore = score;
                copyANN(bestANN, currentANN);
            }
            else
            {
                copyANN(currentANN, bestANN);
            }
        }

        return bestScore >= solutionThreshold;
    }
};

typedef Miner<HI_K, HI_L, HI_N, HI_2M, HI_P, HI_S, HI_THRESHOLD> HyperIdentityMiner;
typedef AdditionMiner<ADD_K, ADD_L, ADD_N, ADD_2M, ADD_P, ADD_S, ADD_THRESHOLD> AdditionMinerType;

int miningThreadProc()
{
    auto hiMiner = std::make_unique<HyperIdentityMiner>();
    auto addMiner = std::make_unique<AdditionMinerType>();
    hiMiner->initialize(randomSeed);
    hiMiner->setComputorPublicKey(computorPublicKey);
    addMiner->initialize(randomSeed);

    std::array<unsigned char, 32> nonce;
    while (!state)
    {
        _rdrand64_step((unsigned long long *)&nonce.data()[0]);
        _rdrand64_step((unsigned long long *)&nonce.data()[8]);
        _rdrand64_step((unsigned long long *)&nonce.data()[16]);
        _rdrand64_step((unsigned long long *)&nonce.data()[24]);

        bool isAddition = (nonce[0] & 1) != 0;
        bool ready = false;
        bool found = false;

        if (isAddition)
        {
            ready = addMiner->updateLatestQatumData();
            if (ready)
            {
                found = addMiner->findSolution(addMiner->computorPublicKey, nonce.data());
            }
        }
        else
        {
            ready = hiMiner->updateLatestQatumData();
            if (ready)
            {
                found = hiMiner->findSolution(hiMiner->computorPublicKey, nonce.data());
            }
        }

        if (ready)
        {
            if (found)
            {
                std::lock_guard<std::mutex> guard(foundNonceLock);
                foundNonce.push(nonce);
                numberOfFoundSolutions++;
            }
            numberOfMiningIterations++;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(1000));
        }
    }
    return 0;
}

struct ServerSocket
{
#ifdef _MSC_VER
    ServerSocket()
    {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~ServerSocket()
    {
        WSACleanup();
    }

    void closeConnection()
    {
        closesocket(serverSocket);
    }

    bool establishConnection(char *address)
    {
        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET)
        {
            printf("Fail to create a socket (%d)!\n", WSAGetLastError());
            return false;
        }

        sockaddr_in addr;
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(nodePort);
        sscanf_s(address, "%hhu.%hhu.%hhu.%hhu", &addr.sin_addr.S_un.S_un_b.s_b1, &addr.sin_addr.S_un.S_un_b.s_b2, &addr.sin_addr.S_un.S_un_b.s_b3, &addr.sin_addr.S_un.S_un_b.s_b4);
        if (connect(serverSocket, (const sockaddr *)&addr, sizeof(addr)))
        {
            printf("Fail to connect to %d.%d.%d.%d (%d)!\n", addr.sin_addr.S_un.S_un_b.s_b1, addr.sin_addr.S_un.S_un_b.s_b2, addr.sin_addr.S_un.S_un_b.s_b3, addr.sin_addr.S_un.S_un_b.s_b4, WSAGetLastError());
            closeConnection();
            return false;
        }

        return true;
    }

    SOCKET serverSocket;
#else
    void closeConnection()
    {
        close(serverSocket);
    }
    bool establishConnection(char *address)
    {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == -1)
        {
            printf("Fail to create a socket (%d)!\n", errno);
            return false;
        }
        timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        setsockopt(serverSocket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(nodePort);
        if (inet_pton(AF_INET, address, &addr.sin_addr) <= 0)
        {
            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            if (getaddrinfo(address, NULL, &hints, &res) != 0 || !res)
            {
                printf("[!] Cannot resolve hostname: %s\n", address);
                return false;
            }
            addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
            char resolved[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, resolved, sizeof(resolved));
            printf("[+] Resolved %s → %s\n", address, resolved);
            freeaddrinfo(res);
        }

        if (connect(serverSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            printf("Fail to connect to %s (%d)\n", address, errno);
            closeConnection();
            return false;
        }

        return true;
    }

    int serverSocket;
#endif

    bool sendData(char *buffer, unsigned int size)
    {
        while (size)
        {
            int numberOfBytes;
            if ((numberOfBytes = send(serverSocket, buffer, size, 0)) <= 0)
            {
                return false;
            }
            buffer += numberOfBytes;
            size -= numberOfBytes;
        }

        return true;
    }
    int receiveData(uint8_t *buffer, int sz)
    {
        return recv(serverSocket, (char *)buffer, sz, 0);
    }

    bool receiveDataAll(std::vector<uint8_t> &receivedData)
    {
        receivedData.resize(0);
        uint8_t tmp[1024];
        int recvByte = receiveData(tmp, 1024);
        while (recvByte > 0)
        {
            receivedData.resize(recvByte + receivedData.size());
            memcpy(receivedData.data() + receivedData.size() - recvByte, tmp, recvByte);
            recvByte = receiveData(tmp, 1024);
        }
        if (receivedData.size() == 0)
        {
            return false;
        }

        return true;
    }
};

static void hexToByte(const char *hex, uint8_t *byte, const int sizeInByte)
{
    for (int i = 0; i < sizeInByte; i++)
    {
        sscanf(hex + i * 2, "%2hhx", &byte[i]);
    }
}
static void byteToHex(const uint8_t *byte, char *hex, const int sizeInByte)
{
    for (int i = 0; i < sizeInByte; i++)
    {
        sprintf(hex + i * 2, "%02x", byte[i]);
    }
}

void handleQatumData(std::string data)
{
    json j = json::parse(data);
    int id = j["id"];
    if (id == SUBSCRIBE)
    {
        bool result = j["result"];
        if (!result)
        {
            string err = j.value("error", "unknown");
            if (err.find("No computor id") != string::npos)
            {
                printf("[~] Pool has no computor IDs yet — waiting for assignment...\n");
            }
            else
            {
                printf("[!] Subscribe failed: %s\n", err.c_str());
                state = 1;
            }
        }
        else
        {
            printf("[+] Subscribed to pool\n");
        }
    }
    else if (id == NEW_COMPUTOR_ID)
    {
        string computorId = j["computorId"];
        getPublicKeyFromIdentity((char *)computorId.c_str(), computorPublicKey);
    }
    else if (id == NEW_SEED)
    {
        string seed = j["seed"];
        hexToByte(seed.c_str(), randomSeed, 32);
    }
    else if (id == NEW_DIFFICULTY)
    {
        int diff = j["difficulty"];
        difficulty = diff;
    }
    else if (id == SUBMIT)
    {
        bool result = j["result"];
        if (!result)
        {
            cout << "Failed to submit nonce " << j["error"] << endl;
        }
    }
}

static const char *DEFAULT_POOL_IP = "131.106.76.202";
static const int DEFAULT_POOL_PORT = 7777;
static const char *ALPHAMINER_VERSION = "0.2.0";

int main(int argc, char *argv[])
{
    char miningID[61];
    miningID[60] = 0;
    string qatumBuffer = "";
    std::vector<std::thread> miningThreads;

    printf("\n");
    printf("  \033[1;33m   █████  ██      ██████  ██   ██  █████  ███   ███ ██ ███  ██ ███████ ██████ \033[0m\n");
    printf("  \033[1;33m  ██   ██ ██      ██   ██ ██   ██ ██   ██ ████ ████ ██ ████ ██ ██      ██   ██\033[0m\n");
    printf("  \033[1;33m  ███████ ██      ██████  ███████ ███████ ██ ███ ██ ██ ██ ████ █████   ██████ \033[0m\n");
    printf("  \033[1;33m  ██   ██ ██      ██      ██   ██ ██   ██ ██  █  ██ ██ ██  ███ ██      ██  ██ \033[0m\n");
    printf("  \033[1;33m  ██   ██ ███████ ██      ██   ██ ██   ██ ██     ██ ██ ██   ██ ███████ ██   ██\033[0m\n");
    printf("  \033[36m                      ⛏  Qubic UPoW  ⛏  qubic.alphapool.tech\033[0m\n");
    printf("  \033[1;36m  ════════════════════════════════════════════════════════════════════════════\033[0m\n\n");

    if (argc < 3)
    {
        printf("Usage:   AlphaMiner <Wallet> <Worker> [Threads] [Pool IP] [Pool Port]\n\n");
        printf("  Wallet    Your 60-character Qubic address\n");
        printf("  Worker    Worker name (e.g. rig01)\n");
        printf("  Threads   CPU threads (default: all cores)\n");
        printf("  Pool IP   Qatum pool IP (default: %s)\n", DEFAULT_POOL_IP);
        printf("  Pool Port Qatum pool port (default: %d)\n\n", DEFAULT_POOL_PORT);
        printf("Example:\n");
        printf("  AlphaMiner EQVUBBETJJUY...MAGTFPF rig01 32\n\n");
        return 1;
    }

    char *wallet = argv[1];
    char *worker = argv[2];

    unsigned int numberOfThreads = 0;
    if (argc >= 4)
    {
        numberOfThreads = atoi(argv[3]);
    }
    if (numberOfThreads == 0)
    {
        numberOfThreads = std::thread::hardware_concurrency();
        if (numberOfThreads == 0) numberOfThreads = 4;
    }

    if (argc >= 5)
    {
        nodeIp = argv[4];
    }
    else
    {
        nodeIp = (char *)DEFAULT_POOL_IP;
    }

    if (argc >= 6)
    {
        nodePort = std::atoi(argv[5]);
    }
    else
    {
        nodePort = DEFAULT_POOL_PORT;
    }

    setbuf(stdout, NULL);
    printf("  Pool:    %s:%d\n", nodeIp, nodePort);
    printf("  Wallet:  %.15s...%.7s\n", wallet, wallet + strlen(wallet) - 7);
    printf("  Worker:  %s\n", worker);
    printf("  Threads: %u\n\n", numberOfThreads);

    json j;
    j["id"] = 1;
    j["wallet"] = wallet;
    j["worker"] = worker;
    std::string s = j.dump() + "\n";
    char *buffer = new char[s.size() + 1];
    memcpy(buffer, s.c_str(), s.size());
    ServerSocket serverSocket;
    bool ok = serverSocket.establishConnection(nodeIp);
    if (!ok)
    {
        printf("[!] Failed to connect to pool at %s:%d\n", nodeIp, nodePort);
        return 1;
    }
    printf("[+] Connected to pool\n");
    serverSocket.sendData(buffer, s.size());
    delete[] buffer;

    consoleCtrlHandler();

    {
        miningThreads.resize(numberOfThreads);
        for (unsigned int i = numberOfThreads; i-- > 0;)
        {
            miningThreads.emplace_back(miningThreadProc);
        }

        auto timestamp = std::chrono::steady_clock::now();
        long long prevNumberOfMiningIterations = 0;
        long long lastIts = 0;
        unsigned long long loopCount = 0;
        while (!state)
        {
            if (loopCount % 30 == 0 && loopCount > 0)
            {
                json j;
                j["id"] = REPORT_HASHRATE;
                j["computorId"] = miningID;
                j["hashrate"] = lastIts;
                string buffer = j.dump() + "\n";
                serverSocket.sendData((char *)buffer.c_str(), buffer.size());
            }

            std::vector<uint8_t> receivedData;
            serverSocket.receiveDataAll(receivedData);
            std::string str(receivedData.begin(), receivedData.end());
            qatumBuffer += str;

            while (qatumBuffer.find("\n") != std::string::npos)
            {
                std::string data = qatumBuffer.substr(0, qatumBuffer.find("\n"));
                handleQatumData(data);
                qatumBuffer = qatumBuffer.substr(qatumBuffer.find("\n") + 1);
            }

            getIdentityFromPublicKey(computorPublicKey, miningID, false);

            bool haveNonceToSend = false;
            size_t itemToSend = 0;
            std::array<unsigned char, 32> sendNonce;
            {
                std::lock_guard<std::mutex> guard(foundNonceLock);
                haveNonceToSend = foundNonce.size() > 0;
                if (haveNonceToSend)
                {
                    sendNonce = foundNonce.front();
                }
                itemToSend = foundNonce.size();
            }

            if (haveNonceToSend)
            {
                char nonceHex[65];
                char seedHex[65];
                char id[61];
                id[60] = 0;
                nonceHex[64] = 0;
                seedHex[64] = 0;
                getIdentityFromPublicKey(computorPublicKey, id, false);
                byteToHex(sendNonce.data(), nonceHex, 32);
                byteToHex(randomSeed, seedHex, 32);
                json j;
                j["id"] = SUBMIT;
                j["nonce"] = nonceHex;
                j["seed"] = seedHex;
                j["computorId"] = id;
                string buffer = j.dump() + "\n";
                serverSocket.sendData((char *)buffer.c_str(), buffer.size());
                foundNonce.pop();
            }

            unsigned long long delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - timestamp).count();
            if (delta >= 1000)
            {
                if (HyperIdentityMiner::checkGlobalQatumDataAvailability())
                {
                    lastIts = (numberOfMiningIterations - prevNumberOfMiningIterations) * 1000 / delta;
                    std::time_t now_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    std::tm *utc_time = std::gmtime(&now_time);
                    printf("[AlphaMiner]  %04d-%02d-%02d %02d:%02d:%02d UTC  |  %llu it/s  |  %d solutions  |  %.7s...%.7s  |  diff %d\n",
                           utc_time->tm_year + 1900, utc_time->tm_mon + 1, utc_time->tm_mday, utc_time->tm_hour, utc_time->tm_min, utc_time->tm_sec,
                           lastIts, numberOfFoundSolutions.load(), miningID, &miningID[53], difficulty.load());
                    prevNumberOfMiningIterations = numberOfMiningIterations;
                    timestamp = std::chrono::steady_clock::now();
                }
                else
                {
                    if (isZeros<32>(computorPublicKey))
                    {
                        printf("[~] Waiting for computor public key...\n");
                    }
                    else if (isZeros<32>(randomSeed))
                    {
                        printf("[~] Waiting for mining seed (idle phase)...\n");
                    }
                    else if (difficulty == 0)
                    {
                        printf("[~] Waiting for difficulty...\n");
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(1000));
            loopCount++;
        }
    }
    printf("\n[*] Shutting down... Press Ctrl+C again to force stop.\n");

    for (auto &miningTh : miningThreads)
    {
        if (miningTh.joinable())
        {
            miningTh.join();
        }
    }
    printf("[*] AlphaMiner stopped.\n");

    return 0;
}
