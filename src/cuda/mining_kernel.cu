#include "mining_kernel.cuh"
#include <cstring>
#include <cstdio>

// ════════════════════════════════════════════════════════════════
// Keccak-p[1600,12] — array-based implementation for GPU
// ════════════════════════════════════════════════════════════════

#define ROL64(a, offset) (((unsigned long long)(a) << (offset)) ^ ((unsigned long long)(a) >> (64 - (offset))))

__device__ static const unsigned long long KeccakRoundConstants[12] = {
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

__device__ static const int KeccakRotations[25] = {
    0, 1, 62, 28, 27, 36, 44, 6, 55, 20,
    3, 10, 43, 25, 39, 41, 45, 15, 21, 8,
    18, 2, 61, 56, 14
};

__device__ static const int KeccakPi[25] = {
    0, 10, 20, 5, 15, 16, 1, 11, 21, 6,
    7, 17, 2, 12, 22, 23, 8, 18, 3, 13,
    14, 24, 9, 19, 4
};

__device__ void d_KeccakP1600_Permute_12rounds(unsigned char* state)
{
    unsigned long long* s = (unsigned long long*)state;

    for (int round = 0; round < 12; round++)
    {
        // theta
        unsigned long long C[5], D[5];
        for (int x = 0; x < 5; x++)
            C[x] = s[x] ^ s[x + 5] ^ s[x + 10] ^ s[x + 15] ^ s[x + 20];
        for (int x = 0; x < 5; x++)
        {
            D[x] = C[(x + 4) % 5] ^ ROL64(C[(x + 1) % 5], 1);
            for (int y = 0; y < 25; y += 5)
                s[y + x] ^= D[x];
        }

        // rho
        for (int i = 1; i < 25; i++)
            s[i] = ROL64(s[i], KeccakRotations[i]);

        // pi
        unsigned long long tmp[25];
        for (int i = 0; i < 25; i++)
            tmp[i] = s[KeccakPi[i]];

        // chi
        for (int j = 0; j < 25; j += 5)
        {
            unsigned long long t0 = tmp[j], t1 = tmp[j+1], t2 = tmp[j+2], t3 = tmp[j+3], t4 = tmp[j+4];
            s[j]   = t0 ^ ((~t1) & t2);
            s[j+1] = t1 ^ ((~t2) & t3);
            s[j+2] = t2 ^ ((~t3) & t4);
            s[j+3] = t3 ^ ((~t4) & t0);
            s[j+4] = t4 ^ ((~t0) & t1);
        }

        // iota
        s[0] ^= KeccakRoundConstants[round];
    }
}

// ════════════════════════════════════════════════════════════════
// KangarooTwelve (simplified for 64-byte input, 32-byte output)
// ════════════════════════════════════════════════════════════════

__device__ void d_KangarooTwelve(const unsigned char* input, unsigned int inputLen,
                                  unsigned char* output, unsigned int outputLen)
{
    unsigned char state[200];
    memset(state, 0, 200);
    memcpy(state, input, inputLen);
    state[inputLen] = 0x07;
    state[167] ^= 0x80;
    d_KeccakP1600_Permute_12rounds(state);
    memcpy(output, state, outputLen);
}

// ════════════════════════════════════════════════════════════════
// random2 — pool-based deterministic RNG
// ════════════════════════════════════════════════════════════════

__device__ void d_random2(
    unsigned char seed[32],
    const unsigned char* __restrict__ pool,
    unsigned char* output,
    unsigned long long outputSizeInByte)
{
    unsigned long long paddingOutputSize = ((outputSizeInByte + 63) / 64) * 64;
    unsigned int x[8];
    for (int i = 0; i < 8; i++)
        x[i] = ((unsigned int*)seed)[i];

    unsigned long long segments = paddingOutputSize / 64;
    for (unsigned long long j = 0; j < segments; j++)
    {
        for (int i = 0; i < 8; i++)
        {
            unsigned int base = (x[i] >> 3) >> 3;
            unsigned int m = x[i] & 63;

            unsigned long long u64_0 = __ldg(&((const unsigned long long*)pool)[base]);
            unsigned long long u64_1 = __ldg(&((const unsigned long long*)pool)[base + 1]);

            unsigned long long val;
            if (m == 0)
                val = u64_0;
            else
                val = (u64_0 >> m) | (u64_1 << (64 - m));

            unsigned long long offset = j * 64 + i * 8;
            if (offset + 8 <= outputSizeInByte)
                *((unsigned long long*)&output[offset]) = val;
            else if (offset < outputSizeInByte)
            {
                for (unsigned long long b = 0; b < outputSizeInByte - offset; b++)
                    output[offset + b] = ((unsigned char*)&val)[b];
            }

            x[i] = x[i] * 1664525 + 1013904223;
        }
    }
}

// ════════════════════════════════════════════════════════════════
// ANN helper functions
// ════════════════════════════════════════════════════════════════

__device__ void d_extract64Bits(unsigned long long number, char* output)
{
    for (int i = 0; i < 64; ++i)
        output[i] = ((number >> i) & 1);
}

__device__ __forceinline__ DeviceSynapse* d_getSynapses(DeviceANN* ann, unsigned long long neuronIndex)
{
    return &ann->synapses[neuronIndex * MAX_NEIGHBOR_NEURONS];
}

// Fast modulo replacement — conditional subtract instead of division
__device__ __forceinline__ unsigned long long d_clampNeuronIndex(
    unsigned long long population, unsigned long long neuronIdx, long long value)
{
    long long nnIndex;
    if (value >= 0)
        nnIndex = (long long)neuronIdx + value;
    else
        nnIndex = (long long)neuronIdx + (long long)population + value;

    // Conditional subtract/add replaces expensive modulo
    if (nnIndex >= (long long)population) nnIndex -= (long long)population;
    if (nnIndex < 0) nnIndex += (long long)population;
    // Safety: handle edge case where offset > population
    if (nnIndex >= (long long)population) nnIndex %= (long long)population;
    if (nnIndex < 0) nnIndex = ((nnIndex % (long long)population) + (long long)population) % (long long)population;
    return (unsigned long long)nnIndex;
}

__device__ long long d_getIndexInSynapsesBuffer(unsigned long long neuronIdx, long long neighborOffset)
{
    if (neighborOffset == 0 ||
        neighborOffset < -(long long)(MAX_NEIGHBOR_NEURONS / 2) ||
        neighborOffset > (long long)(MAX_NEIGHBOR_NEURONS / 2))
        return -1;

    long long synapseIdx = (long long)(MAX_NEIGHBOR_NEURONS / 2) + neighborOffset;
    if (neighborOffset >= 0)
        synapseIdx = synapseIdx - 1;
    return synapseIdx;
}

__device__ unsigned long long d_getNeighborNeuronIndex(
    unsigned long long population, unsigned long long neuronIndex, unsigned long long neighborOffset)
{
    if (neighborOffset < (MAX_NEIGHBOR_NEURONS / 2))
        return d_clampNeuronIndex(population, neuronIndex + neighborOffset, -(long long)(MAX_NEIGHBOR_NEURONS / 2));
    else
        return d_clampNeuronIndex(population, neuronIndex + neighborOffset + 1, -(long long)(MAX_NEIGHBOR_NEURONS / 2));
}

// ════════════════════════════════════════════════════════════════
// Serial ANN operations (run on thread 0 only)
// ════════════════════════════════════════════════════════════════

__device__ void d_removeNeuron(DeviceANN* ann, unsigned long long neuronIdx)
{
    unsigned long long population = ann->population;

    for (long long neighborOffset = -(long long)(MAX_NEIGHBOR_NEURONS / 2);
         neighborOffset <= (long long)(MAX_NEIGHBOR_NEURONS / 2); neighborOffset++)
    {
        unsigned long long nnIdx = d_clampNeuronIndex(population, neuronIdx, neighborOffset);
        DeviceSynapse* pNNSynapses = d_getSynapses(ann, nnIdx);
        long long synapseIndexOfNN = d_getIndexInSynapsesBuffer(nnIdx, -neighborOffset);
        if (synapseIndexOfNN < 0) continue;

        if (synapseIndexOfNN >= (long long)(MAX_NEIGHBOR_NEURONS / 2))
        {
            for (long long k = synapseIndexOfNN; k < (long long)MAX_NEIGHBOR_NEURONS - 1; ++k)
                pNNSynapses[k] = pNNSynapses[k + 1];
            pNNSynapses[MAX_NEIGHBOR_NEURONS - 1].weight = 0;
        }
        else
        {
            for (long long k = synapseIndexOfNN; k > 0; --k)
                pNNSynapses[k] = pNNSynapses[k - 1];
            pNNSynapses[0].weight = 0;
        }
    }

    for (unsigned long long shiftIdx = neuronIdx; shiftIdx < ann->population; shiftIdx++)
    {
        ann->neurons[shiftIdx] = ann->neurons[shiftIdx + 1];
        memcpy(d_getSynapses(ann, shiftIdx), d_getSynapses(ann, shiftIdx + 1),
               MAX_NEIGHBOR_NEURONS * sizeof(DeviceSynapse));
    }
    ann->population--;
}

__device__ void d_insertNeuron(DeviceANN* ann, unsigned long long synapseIdx)
{
    unsigned long long incomingNeighborSynapseIdx = synapseIdx % MAX_NEIGHBOR_NEURONS;
    unsigned long long outgoingNeuron = synapseIdx / MAX_NEIGHBOR_NEURONS;
    unsigned long long& population = ann->population;

    DeviceNeuron insertNeuron;
    insertNeuron = ann->neurons[outgoingNeuron];
    insertNeuron.type = DeviceNeuron::kEvolution;
    unsigned long long insertedNeuronIdx = outgoingNeuron + 1;

    char originalWeight = ann->synapses[synapseIdx].weight;

    for (unsigned long long i = population; i > outgoingNeuron; --i)
    {
        ann->neurons[i] = ann->neurons[i - 1];
        memcpy(d_getSynapses(ann, i), d_getSynapses(ann, i - 1),
               MAX_NEIGHBOR_NEURONS * sizeof(DeviceSynapse));
    }
    ann->neurons[insertedNeuronIdx] = insertNeuron;
    population++;

    DeviceSynapse* pInsertSyn = d_getSynapses(ann, insertedNeuronIdx);
    for (unsigned long long i = 0; i < MAX_NEIGHBOR_NEURONS; i++)
        pInsertSyn[i].weight = 0;

    if (incomingNeighborSynapseIdx < MAX_NEIGHBOR_NEURONS / 2)
    {
        if (incomingNeighborSynapseIdx > 0)
            pInsertSyn[incomingNeighborSynapseIdx - 1].weight = originalWeight;
    }
    else
    {
        pInsertSyn[incomingNeighborSynapseIdx].weight = originalWeight;
    }

    for (long long delta = -(long long)(MAX_NEIGHBOR_NEURONS / 2);
         delta <= (long long)(MAX_NEIGHBOR_NEURONS / 2); ++delta)
    {
        if (delta == 0) continue;
        unsigned long long updatedNeuronIdx = d_clampNeuronIndex(population, insertedNeuronIdx, delta);

        long long insertedNeuronIdxInNeighborList = -1;
        for (long long k = 0; k < (long long)MAX_NEIGHBOR_NEURONS; k++)
        {
            unsigned long long nnIndex = d_getNeighborNeuronIndex(population, updatedNeuronIdx, k);
            if (nnIndex == insertedNeuronIdx) { insertedNeuronIdxInNeighborList = k; break; }
        }
        if (insertedNeuronIdxInNeighborList < 0) continue;

        DeviceSynapse* pUpdatedSyn = d_getSynapses(ann, updatedNeuronIdx);

        if (delta < 0)
        {
            for (long long k = (long long)MAX_NEIGHBOR_NEURONS - 1; k >= insertedNeuronIdxInNeighborList; --k)
                pUpdatedSyn[k] = pUpdatedSyn[k - 1];
            if (delta == -1)
                pUpdatedSyn[insertedNeuronIdxInNeighborList].weight = 0;
        }
        else
        {
            for (long long k = 0; k < insertedNeuronIdxInNeighborList; ++k)
                pUpdatedSyn[k] = pUpdatedSyn[k + 1];
        }
    }
}

__device__ unsigned long long d_scanRedundantNeurons(DeviceANN* ann)
{
    unsigned long long population = ann->population;
    unsigned long long count = 0;

    for (unsigned long long i = 0; i < population; i++)
    {
        ann->neurons[i].markForRemoval = false;
        if (ann->neurons[i].type == DeviceNeuron::kEvolution)
        {
            bool allOutGoingZeros = true;
            bool allIncomingZeros = true;

            for (unsigned long long n = 0; n < MAX_NEIGHBOR_NEURONS; n++)
            {
                if (ann->synapses[i * MAX_NEIGHBOR_NEURONS + n].weight != 0)
                { allOutGoingZeros = false; break; }
            }

            for (long long neighborOffset = -(long long)(MAX_NEIGHBOR_NEURONS / 2);
                 neighborOffset <= (long long)(MAX_NEIGHBOR_NEURONS / 2); neighborOffset++)
            {
                unsigned long long nnIdx = d_clampNeuronIndex(population, i, neighborOffset);
                long long sIdx = d_getIndexInSynapsesBuffer(nnIdx, -neighborOffset);
                if (sIdx < 0) continue;
                if (d_getSynapses(ann, nnIdx)[sIdx].weight != 0)
                { allIncomingZeros = false; break; }
            }

            if (allOutGoingZeros || allIncomingZeros)
            { ann->neurons[i].markForRemoval = true; count++; }
        }
    }
    return count;
}

__device__ void d_cleanANN(DeviceANN* ann)
{
    unsigned long long neuronIdx = 0;
    while (neuronIdx < ann->population)
    {
        if (ann->neurons[neuronIdx].markForRemoval)
            d_removeNeuron(ann, neuronIdx);
        else
            neuronIdx++;
    }
}

__device__ void d_mutate(DeviceANN* ann, DeviceInitValue* initValue, int mutateStep)
{
    unsigned long long population = ann->population;
    unsigned long long synapseCount = population * MAX_NEIGHBOR_NEURONS;

    unsigned long long synapseMutation = initValue->synpaseMutation[mutateStep];
    unsigned long long synapseIdx = (synapseMutation >> 1) % synapseCount;
    char weightChange = ((synapseMutation & 1ULL) == 0) ? -1 : 1;

    char newWeight = ann->synapses[synapseIdx].weight + weightChange;

    if (newWeight >= -1 && newWeight <= 1)
        ann->synapses[synapseIdx].weight = newWeight;
    else
        d_insertNeuron(ann, synapseIdx);

    while (d_scanRedundantNeurons(ann) > 0)
        d_cleanANN(ann);
}

__device__ unsigned int d_computeNonMatchingOutput(DeviceANN* ann, char* expectedOutput)
{
    unsigned int R = 0;
    unsigned long long outputIdx = 0;
    for (unsigned long long i = 0; i < ann->population; i++)
    {
        if (ann->neurons[i].type == DeviceNeuron::kOutput)
        {
            if (ann->neurons[i].value != expectedOutput[outputIdx]) R++;
            outputIdx++;
        }
    }
    return R;
}

// ════════════════════════════════════════════════════════════════
// Block-cooperative functions (all 256 threads participate)
// ════════════════════════════════════════════════════════════════

// Cooperative memcpy for large structs (256 threads copy in parallel)
__device__ void d_cooperativeANNCopy(DeviceANN* dst, const DeviceANN* src)
{
    const int* s = (const int*)src;
    int* d = (int*)dst;
    constexpr int count = sizeof(DeviceANN) / sizeof(int);
    for (int i = threadIdx.x; i < count; i += BLOCK_SIZE)
        d[i] = s[i];
    // Handle remainder bytes
    constexpr int rem = sizeof(DeviceANN) % sizeof(int);
    if (rem > 0 && threadIdx.x == 0)
    {
        const char* sb = (const char*)src + count * sizeof(int);
        char* db = (char*)dst + count * sizeof(int);
        for (int i = 0; i < rem; i++) db[i] = sb[i];
    }
    __syncthreads();
}

// ──────────── processTick — cooperative version ────────────
// 256 threads divide 612 neurons. neuronValueBuffer in shared memory.
// Uses atomicAdd for shared memory accumulation.
__device__ void d_processTick_cooperative(
    DeviceANN* ann, SharedState* shmem, unsigned long long population)
{
    // Phase 1: Zero neuronValueBuffer (cooperative)
    for (unsigned long long i = threadIdx.x; i < population; i += BLOCK_SIZE)
        shmem->neuronValueBuffer[i] = 0;
    __syncthreads();

    // Phase 2: Accumulate (cooperative — each thread handles a subset of neurons)
    for (unsigned long long n = threadIdx.x; n < population; n += BLOCK_SIZE)
    {
        const DeviceSynapse* kSyn = d_getSynapses(ann, n);
        int neuronValue = (int)ann->neurons[n].value;

        // Skip neurons with zero value — no contribution possible
        if (neuronValue == 0) continue;

        for (unsigned long long m = 0; m < MAX_NEIGHBOR_NEURONS; m++)
        {
            char synapseWeight = kSyn[m].weight;
            // Skip zero-weight synapses — major optimization
            if (synapseWeight == 0) continue;

            unsigned long long nnIndex;
            if (m < MAX_NEIGHBOR_NEURONS / 2)
                nnIndex = d_clampNeuronIndex(population, n + m, -(long long)(MAX_NEIGHBOR_NEURONS / 2));
            else
                nnIndex = d_clampNeuronIndex(population, n + m + 1, -(long long)(MAX_NEIGHBOR_NEURONS / 2));

            atomicAdd(&shmem->neuronValueBuffer[nnIndex], (int)(synapseWeight * neuronValue));
        }
    }
    __syncthreads();

    // Phase 3: Clamp and write back (cooperative)
    for (unsigned long long n = threadIdx.x; n < population; n += BLOCK_SIZE)
    {
        int v = shmem->neuronValueBuffer[n];
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        ann->neurons[n].value = (char)v;
    }
    __syncthreads();
}

// ──────────── runTickSimulation — cooperative version ────────────
__device__ void d_runTickSimulation_cooperative(
    DeviceANN* ann, SharedState* shmem)
{
    unsigned long long population = shmem->population;

    // Save previous neuron values (cooperative)
    for (unsigned long long i = threadIdx.x; i < population; i += BLOCK_SIZE)
        shmem->previousNeuronValue[i] = ann->neurons[i].value;
    __syncthreads();

    for (unsigned long long tick = 0; tick < NUMBER_OF_TICKS; ++tick)
    {
        d_processTick_cooperative(ann, shmem, population);

        // Early exit check (cooperative reduction with atomicOr)
        if (threadIdx.x == 0) shmem->earlyExitFlag = 0;
        __syncthreads();

        // Bit 0: found a changed neuron (NOT all unchanged)
        // Bit 1: found a zero output (NOT all outputs nonzero)
        int myFlags = 0;
        for (unsigned long long n = threadIdx.x; n < population; n += BLOCK_SIZE)
        {
            if (shmem->previousNeuronValue[n] != ann->neurons[n].value)
                myFlags |= 1;
            if (ann->neurons[n].type == DeviceNeuron::kOutput && ann->neurons[n].value == 0)
                myFlags |= 2;
        }
        if (myFlags) atomicOr(&shmem->earlyExitFlag, myFlags);
        __syncthreads();

        int flags = shmem->earlyExitFlag;
        bool allNeuronsUnchanged = !(flags & 1);
        bool allOutputNonZeros = !(flags & 2);

        if (allOutputNonZeros || allNeuronsUnchanged) break;

        // Save current as previous (cooperative)
        for (unsigned long long n = threadIdx.x; n < population; n += BLOCK_SIZE)
            shmem->previousNeuronValue[n] = ann->neurons[n].value;
        __syncthreads();
    }
}

// ════════════════════════════════════════════════════════════════
// findSolution — block-cooperative version
// Thread 0 handles serial init/mutate, all threads cooperate on ticks
// ════════════════════════════════════════════════════════════════

__device__ int d_findSolution_cooperative(
    DeviceANN* currentANN,
    DeviceANN* bestANN,
    DeviceInitValue* initValue,
    DeviceMiningData* miningData,
    const unsigned char* __restrict__ pool,
    const unsigned char* publicKey,
    unsigned char* nonce,
    char* outputExpectedValue,
    unsigned long long* neuronIndices,
    unsigned long long* outputNeuronIndices,
    SharedState* shmem)
{
    // ──── Serial initialization (thread 0 only) ────
    if (threadIdx.x == 0)
    {
        // Hash publicKey + nonce
        unsigned char hash[32];
        unsigned char combined[64];
        memcpy(combined, publicKey, 32);
        memcpy(combined + 32, nonce, 32);
        d_KangarooTwelve(combined, 64, hash, 32);

        currentANN->population = NUM_NEURONS;
        d_random2(hash, pool, (unsigned char*)initValue, sizeof(DeviceInitValue));

        // Initialize neuron types
        for (unsigned long long i = 0; i < currentANN->population; ++i)
        {
            neuronIndices[i] = i;
            currentANN->neurons[i].type = DeviceNeuron::kInput;
            currentANN->neurons[i].value = 0;
            currentANN->neurons[i].markForRemoval = false;
        }

        // Assign output neurons
        unsigned long long neuronCount = currentANN->population;
        for (unsigned long long i = 0; i < NUMBER_OF_OUTPUT_NEURONS; ++i)
        {
            unsigned long long outputNeuronIdx = initValue->outputNeuronPositions[i] % neuronCount;
            currentANN->neurons[neuronIndices[outputNeuronIdx]].type = DeviceNeuron::kOutput;
            outputNeuronIndices[i] = neuronIndices[outputNeuronIdx];
            neuronCount--;
            neuronIndices[outputNeuronIdx] = neuronIndices[neuronCount];
        }

        // Synapse weight initialization
        for (unsigned long long i = 0; i < (INIT_SYNAPSES / 32); ++i)
        {
            const unsigned long long mask = 0b11;
            for (int j = 0; j < 32; ++j)
            {
                int shiftVal = j * 2;
                unsigned char extractValue = (unsigned char)((initValue->synapseWeight[i] >> shiftVal) & mask);
                switch (extractValue)
                {
                case 2: currentANN->synapses[32 * i + j].weight = -1; break;
                case 3: currentANN->synapses[32 * i + j].weight = 1; break;
                default: currentANN->synapses[32 * i + j].weight = 0;
                }
            }
        }

        // Init mining data from pool
        memcpy((unsigned char*)miningData, pool, sizeof(DeviceMiningData));

        // Init input neuron values
        unsigned long long inputIdx = 0;
        char neuronArray[64];
        for (unsigned long long i = 0; i < currentANN->population; ++i)
        {
            if (currentANN->neurons[i].type == DeviceNeuron::kInput)
            {
                if (inputIdx % 64 == 0)
                    d_extract64Bits(miningData->inputNeuronRandomNumber[inputIdx / 64], neuronArray);
                char v = neuronArray[inputIdx % 64];
                currentANN->neurons[i].value = (v == 0) ? -1 : v;
                inputIdx++;
            }
        }

        // Init output neuron values to 0
        for (unsigned long long i = 0; i < currentANN->population; ++i)
            if (currentANN->neurons[i].type == DeviceNeuron::kOutput)
                currentANN->neurons[i].value = 0;

        // Init expected output
        for (unsigned long long i = 0; i < NUMBER_OF_OUTPUT_NEURONS; ++i)
        {
            if (i % 64 == 0)
                d_extract64Bits(miningData->outputNeuronRandomNumber[i / 64], neuronArray);
            char v = neuronArray[i % 64];
            outputExpectedValue[i] = (v == 0) ? -1 : v;
        }

        shmem->population = currentANN->population;
    }
    __syncthreads();

    // ──── Cooperative initial tick simulation ────
    d_runTickSimulation_cooperative(currentANN, shmem);

    // ──── Thread 0: compute initial score, copy best ────
    if (threadIdx.x == 0)
    {
        shmem->bestR = d_computeNonMatchingOutput(currentANN, outputExpectedValue);
    }
    __syncthreads();

    // Cooperative copy: currentANN -> bestANN
    d_cooperativeANNCopy(bestANN, currentANN);

    // ──── Main mutation loop ────
    for (unsigned long long s = 0; s < NUMBER_OF_MUTATIONS; ++s)
    {
        // Thread 0: mutate (serial)
        if (threadIdx.x == 0)
        {
            d_mutate(currentANN, initValue, s);
            shmem->population = currentANN->population;
        }
        __syncthreads();

        // Check population threshold
        if (shmem->population >= POPULATION_THRESHOLD) break;

        // ──── Cooperative tick simulation (THE HOT PATH) ────
        d_runTickSimulation_cooperative(currentANN, shmem);

        // Thread 0: evaluate score, set rollback flag
        if (threadIdx.x == 0)
        {
            unsigned int R = d_computeNonMatchingOutput(currentANN, outputExpectedValue);
            if (R > shmem->bestR)
                shmem->earlyExitFlag = 1; // rollback
            else
            {
                shmem->bestR = R;
                shmem->earlyExitFlag = 0; // save new best
            }
        }
        __syncthreads();

        // Cooperative ANN copy (rollback or save)
        if (shmem->earlyExitFlag == 1)
            d_cooperativeANNCopy(currentANN, bestANN); // rollback
        else
            d_cooperativeANNCopy(bestANN, currentANN); // save new best
    }

    // Final result
    // Return score (NUMBER_OF_OUTPUT_NEURONS - bestR)
    if (threadIdx.x == 0)
        shmem->earlyExitFlag = (int)(NUMBER_OF_OUTPUT_NEURONS - shmem->bestR);
    __syncthreads();
    return shmem->earlyExitFlag;
}

// ════════════════════════════════════════════════════════════════
// Main kernel — one block (256 threads) per nonce
// ════════════════════════════════════════════════════════════════

__global__ void __launch_bounds__(256, 1) miningKernelCooperative(
    const unsigned char* __restrict__ pool,
    const unsigned char* publicKey,
    DeviceANN* currentANNs,
    DeviceANN* bestANNs,
    DeviceInitValue* initValues,
    DeviceMiningData* miningDatas,
    char* outputExpectedValues,
    unsigned long long* neuronIndicesAll,
    unsigned long long* outputNeuronIndicesAll,
    unsigned char* nonces,
    MiningResult* results,
    int numNonces)
{
    __shared__ SharedState shmem;

    int nonceIdx = blockIdx.x;
    if (nonceIdx >= numNonces) return;

    // Per-nonce pointers (indexed by block, not thread)
    DeviceANN* myCurrentANN = &currentANNs[nonceIdx];
    DeviceANN* myBestANN = &bestANNs[nonceIdx];
    DeviceInitValue* myInitValue = &initValues[nonceIdx];
    DeviceMiningData* myMiningData = &miningDatas[nonceIdx];
    char* myOutputExpected = &outputExpectedValues[nonceIdx * NUMBER_OF_OUTPUT_NEURONS];
    unsigned long long* myNeuronIndices = &neuronIndicesAll[nonceIdx * NUM_NEURONS];
    unsigned long long* myOutputNeuronIndices = &outputNeuronIndicesAll[nonceIdx * NUMBER_OF_OUTPUT_NEURONS];
    unsigned char* myNonce = &nonces[nonceIdx * 32];

    int score = d_findSolution_cooperative(
        myCurrentANN, myBestANN, myInitValue, myMiningData,
        pool, publicKey, myNonce,
        myOutputExpected, myNeuronIndices, myOutputNeuronIndices,
        &shmem);

    if (threadIdx.x == 0)
    {
        results[nonceIdx].score = (unsigned int)score;
        results[nonceIdx].isSolution = (score >= SOLUTION_THRESHOLD);
        memcpy(results[nonceIdx].nonce, myNonce, 32);
    }
}

// ──────────── Host-side kernel launcher ────────────
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
    cudaStream_t stream)
{
    miningKernelCooperative<<<numNonces, BLOCK_SIZE, 0, stream>>>(
        d_pool, d_publicKey,
        d_currentANNs, d_bestANNs,
        d_initValues, d_miningDatas,
        d_outputExpectedValues, d_neuronIndices, d_outputNeuronIndices,
        d_nonces, d_results, numNonces);
}
