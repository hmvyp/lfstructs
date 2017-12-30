/*
DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE Version 2

Copyright (C) 2017 hmvyp

Everyone is permitted to copy and distribute verbatim or modified
copies of this software, and changing it is allowed as long
as the name is changed.

           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

 0. You just DO WHAT THE FUCK YOU WANT TO.
*/

/**
 Test CircularBuffer with relacy race detector
 (https://github.com/dvyukov/relacy);
*/

// define LFSTRUCTS_CYBUFFER_TEST_enabled=1 to compile the test:
#if LFSTRUCTS_CYBUFFER_TEST_enabled

// replace std atomics with relace atomics:
#define LFSTRUCTS_NO_STD_ATOMICS 1

#include <relacy/relacy_std.hpp>

namespace ans = rl; // redirect atomics from std to relacy

namespace rl{
//  add free atomic functions to relacy namespace:

    template<typename T>
    T atomic_load(atomic<T>* pa) {
        return pa->load(ans::mo_seq_cst, $);
    }

    template<typename T>
    bool atomic_compare_exchange_strong(atomic<T>* pa, T* cmp, T xchg){
        return  pa->compare_exchange_strong(*cmp, xchg, ans::mo_seq_cst, $ );
    }

    template<typename T>
    void atomic_store(atomic<T>* pa, T val){
        pa->store(val, ans::mo_seq_cst, $ );
    }

    template< class T >
    T atomic_load_explicit(atomic<T>* pa, memory_order order, debug_info_param info ){
        return pa->load(order, info);
    }
}

#include <lfstructs/cybuff.h>

using namespace lfstructs;

#include <iostream>

struct Data{
    unsigned data;
    Data(unsigned d): data{d}{}
};

static unsigned iter_count =0;
static unsigned error_count = 0;

template<
  size_t size_magnitude,
  unsigned num_of_producers,
  unsigned num_of_steps
>

struct Test1
  : rl::test_suite
    <
      Test1 // (CRTP derived)
      <
        size_magnitude,
        num_of_producers,
        num_of_steps
      >
      ,num_of_producers+1 // num of threads
    >
{


    typedef CircularBuffer<Data, size_magnitude> BufType;
    
    enum{ num_of_msgs = num_of_producers * num_of_steps};

    volatile unsigned send_count = 0;
    int active_producers = 0;

    BufType buf;
    
    bool receive_confirmations[num_of_msgs];

    Test1(): receive_confirmations{0}{
        std::cout << std::endl << "Iteration No: " <<   iter_count++ << std::endl;
    }

    void thread(unsigned index)
    {
        if (index<num_of_producers){ // if producer thread

            for(unsigned i=0; i != num_of_steps; ++i){
                Data* d = new Data((send_count++));//, send_count*9/10));
                //std::cout << "Thread" << index << ": to put data: " << send_count << std::endl;
                while(buf.put(d) == BufType::BUFFER_OVERRUN){
                    //std::cout << "Thread: "<< index << " overrun. yield"<< std::endl;
                    rl::yield(1, $);
                }
                //std::cout << "Data is send thread "<< index << std::endl;
            }
        }else{// consumer:
            for(unsigned i=0; i != num_of_msgs; ++i){
                auto res = buf.get();
                for(; res.empty(); res= buf.get()){
                  //std::cout << "consumer: no data " << std::endl;
                  rl::yield(1, $);
                }
                Data* d = res.ptr();
                //std::cout << " consumer: data received: " << d->data << std::endl;
                receive_confirmations[d->data] = true;

                delete d;
            }
            
            bool missed= false;
            for(unsigned i; i < num_of_msgs; ++ i){
                if(!receive_confirmations[i]){
                    if(!missed){
                        missed= true;
                        ++error_count;
                        std::cout << std::endl << "Missed messages" << std::endl;
                    }
                    std::cout << "missed message No " << i << std::endl;
                }
            }
            
            std::cout 
               << (missed? "----------------------" : " all messages received") 
               << std::endl;
        }// ...consumer
    }
};

void test(){
    {
        rl::test_params params;
        params.search_type= rl::sched_random;
        params.iteration_count = 1000;
        params.execution_depth_limit = 20000;

        std::cout << "......... starting........ "<< std::endl;

        //--------------------------------------------------
        rl::simulate<Test1<
        3, // bufsize magnitude
        2, // 2 producers
        20  // num of steps
        > >(params);
        //--------------------------------------------------
    }
    {
        rl::test_params params;
        params.search_type= rl::sched_random;
        params.iteration_count = 1000;
        params.execution_depth_limit = 20000;

        rl::simulate<Test1<
        3, // bufsize magnitude
        8, //  producers
        20  // num of steps
        > >(params);
    }
    
    std::cout << std::endl 
            << "Custom invariant errors: " << error_count << std::endl;
}

int main(){
    test();
}

#endif // compile guard
