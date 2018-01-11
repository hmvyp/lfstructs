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

#ifndef lfstructs_cybuff_h
#define lfstructs_cybuff_h

/**
 One reader multiple writers lock free circular buffer


 The size of the buffer is a degree of 2.
 The buffer itself is implemented as array of atomic records.

 Every record can be either empty or can contain a pointer to some data.

 Data shall be aligned at least by 2 since the least significant bit
 of the record is used to distinguish data from empty record.
 This bit is set to 0 for every empty record and set to 1 for non-empty one
 (i.e. for a record containing data pointer).

 Every empty record is tagged to eliminate ABA problem.
 The empty record contains a number of record that will be placed here
 in the future. Numbers of records are counted from the creation of the world.
 When the reader reads a record number N it can calculate a number of record
 that will be placed here (to the same place in buffer)
 in the future: Nfuture =  N + bufsize. So the reader
 (after reading record data) marks the record as empty
 by writing that calculated number as a tag (technically it writes the record
 number shifted left by 2 since 2 less significant digits of the record are
 reserved to discriminate tag from data and initial 0 value.

 Theoretically ABA can emerge if some thread was stalled through the whole epoch,
 the record number counter overflowed and became exactly the same as epoch ago.
 We ignore such a probability.


 ToDo:

 1) implement exclude operation (by index returned by put()

     Exclude operation shall replace a pointer in the buffer by NULL pointer.
     The caller is responsible for eliminating ABA on the pointer, i.e.
     he(she) shall guarantee that the data pointer was not re-inserted
     after it insertion at the given index).

 2) Allow to align counters to cacheline to prevent ll/sc false negatives

*/


#include <stdint.h>
#include <stdlib.h>
#include <limits>

// alias atomic namespace:
#if !LFSTRUCTS_NO_STD_ATOMICS
#   include <atomic>
    namespace ans = std;
#endif

namespace lfstructs {

// zero-initialized atomic wrapper:
template<typename T>
struct atomic_cell{
    ans::atomic<T> a;
    constexpr atomic_cell(): a{0}{}
};

template<typename data_t, unsigned size_magnitude>
class CircularBuffer {
    static_assert((alignof(data_t) > 1) , "Data type shall be aligned by 2 or greater" );
    typedef uintptr_t record_t; // buffer element type (return value of get())
    typedef ans::atomic<record_t> arecord_t;
    typedef size_t count_t;
    typedef ans::atomic<count_t> acount_t;

    // restrict buffer size
    // (reserve some index range for error codes when returning a buffer index):
    static_assert(size_magnitude < sizeof(count_t)*8 - 1, "Circular buffer too large");


    static const count_t  bufsize = 1 << size_magnitude;
    static const count_t idx_mask = bufsize - 1;

    static count_t count2idx(count_t c) {
        return c & idx_mask;
    }
    static record_t mkTag(count_t c) {
        //return (c << 2) | 2;
        //tag value changes every buffer (for all cells the same):
        return (c & ~idx_mask);
    } // |2 to distinguish from initial zero

    static record_t pointer2record(data_t* pd) {
        return ((record_t) pd) + 1; // +1 to distinguish data from tag
    }

    // message counters since the creation of the world
    // (can overflow):
    acount_t wcount;  // number of messages written by writers
    acount_t rcount;  // number of messages read by the reader

    atomic_cell<record_t> buf[bufsize]; // the buffer

public:

    enum {
        BUFFER_OVERRUN = std::numeric_limits<count_t>::max() - 1,
        IMPOSSIBLE_VALUE = BUFFER_OVERRUN -1
    };

    class get_result_t{
        record_t data;
    public:
        get_result_t(record_t data){this->data = data;} // ctor

        data_t* ptr(){
            return ((data & 1) != 0)
                    ? (data_t*) (data & (record_t) -2)  // set last bit to zero
                    : NULL; // empty case
        }

        bool empty(){return (data & 1) == 0;}
    };

    constexpr CircularBuffer(): wcount(0), rcount(0){}

    count_t
    put(data_t* pd){

        record_t push_it = pointer2record(pd);

        count_t w = ans::atomic_load(&wcount); // do not relax to prevent
        // 1) false overrun reporting if reordering wcount and rcount reads
        // 2) reordering with subsequent record load (by index calculated from w)
        //    I doubt, though, the 2nd reordering can really happen
        //    with sec_cst (not acquire) record loading.
        // Anyway, false overrun is sufficient reason not to relax wcount
        // loading. Even acquire mo isn't adequate here since
        // wcount and rcount are modified by different threads

        for (;;) {
            count_t r =
                ans::atomic_load_explicit(&rcount, ans::memory_order_relaxed);

            if (w - r >= bufsize) {
                return BUFFER_OVERRUN;
            }

            count_t wx = count2idx(w); // current writer's index
            arecord_t* parecord = &buf[wx].a; // ptr to record to write into

            record_t expected =  mkTag(w);

            // Strong CAS matters to prevent erroneous counter increment
            // in false negative case:
            if (ans::atomic_compare_exchange_strong(parecord, &expected,
                    push_it)) {
                // Record inserted successfully. Try to increment the counter
                // don't check cas result: failure means the operation is completed by a rival
                // The cas can be also  weakened (on false negative just leave
                // the operation incompleted for future actors).
                ans::atomic_compare_exchange_strong(&wcount, &w, w + 1);
                return wx; // Ok
            }else{ // CAS failure
                // (due to rival's data or even future tag in the cell)
                //  Try to increment the counter:
                ans::atomic_compare_exchange_strong(&wcount, &w, w + 1);
                // don't check cas result: failure means the operation is completed by a rival
                // The cas can be also  weakened (on false negative just leave
                // the operation incompleted for future actors)
            }
            // (try again)
        } // loop
        return IMPOSSIBLE_VALUE; // unreachable code (calm compiler warning)
    }

    /**
     * actually returns lower bound of current size
     */
    size_t
    size() {
        count_t w = ans::atomic_load(&wcount);
        count_t r = ans::atomic_load_explicit(&rcount, ans::memory_order_relaxed);
        return (w - r < bufsize) ? w-r : 0; // i.e. check w-r<0 as signed ints
    }

    // get() function for use in single reader thread.
    // Use is_record_valid() and record2pointer() to deal with data returned
    get_result_t
    get(){
        count_t w = ans::atomic_load(&wcount); // at least acquire mo
        count_t r = ans::atomic_load_explicit(&rcount, ans::memory_order_relaxed);
        if(w==r){ // if no data available
            return get_result_t(0); // empty data; check it with emty()
            // to distinguish from data pointer
        }

        count_t ix = count2idx(r);
        arecord_t* parecord = & buf[ix].a;

        record_t res = ans::atomic_load(parecord);

        // mark the record as empty (store tag to help writers against ABA):
        ans::atomic_store(parecord, mkTag(r + bufsize));
        ans::atomic_store(&rcount, r+1); // increment reader's index

        return get_result_t(res);
    }
};

} // namespace

#endif
