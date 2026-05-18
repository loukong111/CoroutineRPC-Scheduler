#include "corpcron/common/memory_pool.hpp"
#include <cstdlib>

namespace corpcron {

MemoryPool::MemoryPool(size_t object_size, size_t chunk_size)
    : object_size_(object_size), chunk_size_(chunk_size) {
    if (object_size_ < sizeof(Block)) object_size_ = sizeof(Block);
}

MemoryPool::~MemoryPool() {
    for (char* chunk : chunks_) delete[] chunk;
}

void MemoryPool::addChunk() {
    char* chunk = new char[object_size_ * chunk_size_];
    chunks_.push_back(chunk);
    for (size_t i = 0; i < chunk_size_; ++i) {
        Block* block = reinterpret_cast<Block*>(chunk + i * object_size_);
        block->next = free_list_;
        free_list_ = block;
    }
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!free_list_) addChunk();
    Block* block = free_list_;
    free_list_ = block->next;
    return block;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Block* block = static_cast<Block*>(ptr);
    block->next = free_list_;
    free_list_ = block;
}

}