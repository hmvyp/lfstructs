#ifndef simple_1way_allocator_h
#define simple_1way_allocator_h

#include <stdint.h>
#include <stdlib.h>
#include <limits>

#include <common.h>

namespace lfstructs_tmp {

template<typename T, size_t capacity>
class Simple1WayAllocator{

    ans::atomic<size_t> idx;
    T buf[capacity];

public:
    T* allocate_one(){
        size_t i = idx.load();

        for(;;){
            if(idx>= capacity){
                return NULL;
            }
            if(idx.compare_exchange_weak(i, i+1)){ // can be weakened
                //  to relaxed mo if T ctor is a constexpr
                break;
            }
        }

        return &buf[i];
    }

    constexpr Simple1WayAllocator(): idx(0) {}
};

}// namespace

#endif
