#ifndef lfstructs_common_h

#define lfstructs_common_h

namespace lfstructs_tmp{
// alias atomic namespace:
#if !LFSTRUCTS_NO_STD_ATOMICS____________________
#   include <atomic>
    namespace ans = std;
#endif

}// namespace

#endif // once
