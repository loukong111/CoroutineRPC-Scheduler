#pragma once
#include <cstddef>
#include <vector>
#include <mutex>

namespace corpcron {

class MemoryPool {
public:
    explicit MemoryPool(size_t object_size, size_t chunk_size = 1024);
    ~MemoryPool();
    void* allocate();
    void deallocate(void* ptr);
private:
    struct Block { Block* next; };
    size_t object_size_;
    size_t chunk_size_;
    Block* free_list_ = nullptr;
    std::vector<char*> chunks_;
    std::mutex mutex_;
    void addChunk();
};

}