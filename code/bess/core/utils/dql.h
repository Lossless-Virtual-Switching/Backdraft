#ifndef BESS_UTILS_DQL_H_
#define BESS_UTILS_DQL_H_

#include "../kmod/llring.h"
#include "../pktbatch.h"

namespace bess {
namespace utils {

// class BackdraftBQL {
//  public:
//   BackdraftBQL() {
//     max_queue_number = 10;
//     queue_list =
//         (struct llring **)malloc(sizeof(struct llring *) * max_queue_number);
//   };

//   ~BackdraftBQL() {
//     if (queue_list) {
//       bess::Packet *pkt;
//       struct llring *queue;
//       for (queue_t qid = 0; qid < max_queue_number; qid++) {
//         queue = queue_list[qid];
//         if (queue) {
//           while (llring_sc_dequeue(queue, reinterpret_cast<void **>(&pkt)) ==
//                  0) {
//             bess::Packet::Free(pkt);
//           }

//           std::free(queue);
//         }
//       }
//     }
//   }

//  private:
//   queue_t max_queue_number;
//   struct llring **queue_list;
// };
}  // namespace utils
}  // namespace bess

#endif