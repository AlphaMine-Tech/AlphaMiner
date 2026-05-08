#include "gpu_miner.h"
#include "mining_kernel.cuh"
#include "../K12AndKeyUtil.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstring>
#include <random>

#define CUDA_CHECK(call) do { \
    cudaError_t err = (call); \
    if (err != cudaSuccess) { \
        fprintf(stderr, "[!] CUDA error at %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
        exit(1); \
    } \
} while(0)

struct GpuMiner::Impl {
    int numNonces;    // number of concurrent nonces (= number of blocks)

    unsigned char* d_pool;
    unsigned char* d_publicKey;
    DeviceANN* d_currentANNs;
    DeviceANN* d_bestANNs;
    DeviceInitValue* d_initValues;
    DeviceMiningData* d_miningDatas;
    char* d_outputExpectedValues;
    unsigned long long* d_neuronIndices;
    unsigned long long* d_outputNeuronIndices;
    unsigned char* d_nonces;
    MiningResult* d_results;

    unsigned char* h_nonces;
    MiningResult* h_results;
    unsigned char* h_pool;

    cudaStream_t stream;
};

GpuMiner::GpuMiner(int deviceId, int numThreads)
{
    impl_ = new Impl();
    // numThreads parameter = number of concurrent nonces
    int numNonces = numThreads;
    impl_->numNonces = numNonces;

    CUDA_CHECK(cudaSetDevice(deviceId));

    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, deviceId));
    printf("[GPU] %s — %d SMs, %.1f GB VRAM, compute %d.%d\n",
           prop.name, prop.multiProcessorCount,
           prop.totalGlobalMem / (1024.0 * 1024.0 * 1024.0),
           prop.major, prop.minor);

    // Allocate per-nonce buffers in global memory
    CUDA_CHECK(cudaMalloc(&impl_->d_pool, POOL_VEC_PADDING_SIZE));
    CUDA_CHECK(cudaMalloc(&impl_->d_publicKey, 32));
    CUDA_CHECK(cudaMalloc(&impl_->d_currentANNs, (size_t)numNonces * sizeof(DeviceANN)));
    CUDA_CHECK(cudaMalloc(&impl_->d_bestANNs, (size_t)numNonces * sizeof(DeviceANN)));
    CUDA_CHECK(cudaMalloc(&impl_->d_initValues, (size_t)numNonces * sizeof(DeviceInitValue)));
    CUDA_CHECK(cudaMalloc(&impl_->d_miningDatas, (size_t)numNonces * sizeof(DeviceMiningData)));
    // neuronValueBuffer and previousNeuronValue are now in shared memory — no global alloc needed
    CUDA_CHECK(cudaMalloc(&impl_->d_outputExpectedValues, (size_t)numNonces * NUMBER_OF_OUTPUT_NEURONS));
    CUDA_CHECK(cudaMalloc(&impl_->d_neuronIndices, (size_t)numNonces * NUM_NEURONS * sizeof(unsigned long long)));
    CUDA_CHECK(cudaMalloc(&impl_->d_outputNeuronIndices, (size_t)numNonces * NUMBER_OF_OUTPUT_NEURONS * sizeof(unsigned long long)));
    CUDA_CHECK(cudaMalloc(&impl_->d_nonces, (size_t)numNonces * 32));
    CUDA_CHECK(cudaMalloc(&impl_->d_results, (size_t)numNonces * sizeof(MiningResult)));

    // Pinned host memory for async transfers
    CUDA_CHECK(cudaMallocHost(&impl_->h_nonces, (size_t)numNonces * 32));
    CUDA_CHECK(cudaMallocHost(&impl_->h_results, (size_t)numNonces * sizeof(MiningResult)));
    CUDA_CHECK(cudaMallocHost(&impl_->h_pool, POOL_VEC_PADDING_SIZE));

    CUDA_CHECK(cudaStreamCreate(&impl_->stream));

    size_t totalMB = (
        POOL_VEC_PADDING_SIZE +
        (size_t)numNonces * (2 * sizeof(DeviceANN) + sizeof(DeviceInitValue) + sizeof(DeviceMiningData) +
                              NUMBER_OF_OUTPUT_NEURONS +
                              NUM_NEURONS * sizeof(unsigned long long) +
                              NUMBER_OF_OUTPUT_NEURONS * sizeof(unsigned long long) +
                              32 + sizeof(MiningResult))
    ) / (1024 * 1024);

    printf("[GPU] Allocated %zu MB for %d nonces (%d blocks x %d threads)\n",
           totalMB, numNonces, numNonces, BLOCK_SIZE);
}

GpuMiner::~GpuMiner()
{
    cudaFree(impl_->d_pool);
    cudaFree(impl_->d_publicKey);
    cudaFree(impl_->d_currentANNs);
    cudaFree(impl_->d_bestANNs);
    cudaFree(impl_->d_initValues);
    cudaFree(impl_->d_miningDatas);
    cudaFree(impl_->d_outputExpectedValues);
    cudaFree(impl_->d_neuronIndices);
    cudaFree(impl_->d_outputNeuronIndices);
    cudaFree(impl_->d_nonces);
    cudaFree(impl_->d_results);
    cudaFreeHost(impl_->h_nonces);
    cudaFreeHost(impl_->h_results);
    cudaFreeHost(impl_->h_pool);
    cudaStreamDestroy(impl_->stream);
    delete impl_;
}

int GpuMiner::getNumThreads() const { return impl_->numNonces; }

void GpuMiner::updatePool(const unsigned char* miningSeed)
{
    unsigned char state[200];
    memcpy(&state[0], miningSeed, 32);
    memset(&state[32], 0, sizeof(state) - 32);

    for (unsigned long long i = 0; i < POOL_VEC_PADDING_SIZE; i += sizeof(state))
    {
        KeccakP1600_Permute_12rounds(state);
        memcpy(&impl_->h_pool[i], state, sizeof(state));
    }

    CUDA_CHECK(cudaMemcpyAsync(impl_->d_pool, impl_->h_pool, POOL_VEC_PADDING_SIZE,
                                cudaMemcpyHostToDevice, impl_->stream));
    CUDA_CHECK(cudaStreamSynchronize(impl_->stream));
}

void GpuMiner::updatePublicKey(const unsigned char* pubKey)
{
    CUDA_CHECK(cudaMemcpyAsync(impl_->d_publicKey, pubKey, 32,
                                cudaMemcpyHostToDevice, impl_->stream));
    CUDA_CHECK(cudaStreamSynchronize(impl_->stream));
}

int GpuMiner::mineBatch(std::vector<std::array<unsigned char, 32>>& foundNonces)
{
    int n = impl_->numNonces;

    // Generate random nonces on CPU
    std::random_device rd;
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            uint32_t r = rd();
            memcpy(&impl_->h_nonces[i * 32 + j * 4], &r, 4);
        }
    }

    // Upload nonces
    CUDA_CHECK(cudaMemcpyAsync(impl_->d_nonces, impl_->h_nonces, (size_t)n * 32,
                                cudaMemcpyHostToDevice, impl_->stream));

    // Launch cooperative kernel: 1 block per nonce, BLOCK_SIZE threads per block
    launchMiningKernel(
        impl_->d_pool, impl_->d_publicKey,
        impl_->d_currentANNs, impl_->d_bestANNs,
        impl_->d_initValues, impl_->d_miningDatas,
        impl_->d_outputExpectedValues,
        impl_->d_neuronIndices, impl_->d_outputNeuronIndices,
        impl_->d_nonces, impl_->d_results,
        n, impl_->stream);

    // Download results
    CUDA_CHECK(cudaMemcpyAsync(impl_->h_results, impl_->d_results, (size_t)n * sizeof(MiningResult),
                                cudaMemcpyDeviceToHost, impl_->stream));
    CUDA_CHECK(cudaStreamSynchronize(impl_->stream));

    // Collect solutions and track best score
    foundNonces.clear();
    unsigned int bestScore = 0;
    for (int i = 0; i < n; i++)
    {
        if (impl_->h_results[i].score > bestScore)
            bestScore = impl_->h_results[i].score;
        if (impl_->h_results[i].isSolution)
        {
            std::array<unsigned char, 32> nonce;
            memcpy(nonce.data(), impl_->h_results[i].nonce, 32);
            foundNonces.push_back(nonce);
        }
    }


    return n;
}
