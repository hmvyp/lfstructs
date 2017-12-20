/*
DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE Version 2

Copyright (C) 2004 hmvyp

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

 1) relax atomic loads.
 2) implement exclude operation (by index returned by put()

     Exclude operation shall replaces a pointer in the buffer by NULL pointer.
     The caller is responsible for eliminating ABA on the pointer, i.e.
     he(she) shall guarantee that the data pointer was not re-inserted
     after it insertion at the given index).

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
public:
    typedef uintptr_t record_t; // buffer element type (return value of get())
protected:
    typedef ans::atomic<record_t> arecord_t;
    typedef size_t count_t;
    typedef ans::atomic<count_t> acount_t;

    // restrict buffer size
    // (reserve some index range for error codes when returning a buffer index):
    static_assert(size_magnitude < sizeof(count_t)*8 - 1, "Circular buffer too large");

    enum {
        bufsize = 1 << size_magnitude,
        idx_mask = bufsize - 1,
    };

    static count_t count2idx(count_t c) {
        return c & idx_mask;
    }
    static record_t mkTag(count_t c) {
        return (c << 2) | 2;
    } // |1 to distinguish from initial zero

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
        ERROR_IDX_VALUE = std::numeric_limits<count_t>::max() - 1
    };

    constexpr CircularBuffer(): wcount(0), rcount(0){}

    static data_t* record2pointer(record_t x) {
        return (data_t*) (x & (record_t) -2); // set last bit to zero
    }

    static bool is_record_valid(record_t x){return (x & 1) != 0;}


    count_t
    put(data_t* pd){

        record_t push_it = pointer2record(pd);

        for (;;) {
            count_t w = ans::atomic_load(&wcount);
            count_t r = ans::atomic_load(&rcount);

            if (w - r >= bufsize) {
                return ERROR_IDX_VALUE; // buffer overrun
            }

            count_t wx = count2idx(w); // current writer's index
            arecord_t* parecord = &buf[wx].a; // ptr to record to write into

            // read previous data from the record to write into:
            record_t v =  ans::atomic_load(parecord);
            if (v & 1) {
                // if the record already contains valid data (not a tag) then
                // try to complete rival's operation (increment the counter):
                ans::atomic_compare_exchange_strong(&wcount, &w, w + 1);
                // and then try again from the beginning:
                continue;
            }

            if ((v = mkTag(w)) // if tag matches the current counter
            || (v == 0)  // or initial state found (zeroed memory)
                    ) {
                if (!ans::atomic_compare_exchange_strong(parecord, &v,
                        push_it)) {
                    // if the record has been changed since 1st atomic load:
                    // try to complete rival's operation (increment the counter):
                    ans::atomic_compare_exchange_strong(&wcount, &w, w + 1);
                    // and then try again from the beginning:
                    continue;
                }
            }

            // Record inserted successfully. Try to increment the counter:
            ans::atomic_compare_exchange_strong(&wcount, &w, w + 1);
            // don't check cas result (failure means the operation is completed by a rival)
            return wx; // Ok
        }
        return ERROR_IDX_VALUE; // unreachable code (calm compiler warning)
    }

    size_t
    size() {
        count_t w = ans::atomic_load(&wcount);
        count_t r = ans::atomic_load(&rcount);
        return w - r;
    }

    // get() function for use in single reader thread.
    // Use is_record_valid() and record2pointer() to deal with data returned
    record_t
    get(){
        count_t w = ans::atomic_load(&wcount);
        count_t r = ans::atomic_load(&rcount);
        if(w==r){ // if no data available
            return 0; // invalid data; check it with is_record_valid()
            // to distinguish from regular data pointer
        }

        count_t ix = count2idx(r);
        arecord_t* parecord = & buf[ix].a;

        record_t res = ans::atomic_load(parecord);

        // mark the record as empty (store tag to help writers against ABA):
        ans::atomic_store(parecord, mkTag(r + bufsize));
        ans::atomic_store(&rcount, r+1); // increment reader's index

        return res;
    }
};

} // namespace

#endif
