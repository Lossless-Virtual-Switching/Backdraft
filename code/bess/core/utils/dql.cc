#include "dql.h"
#include <limits.h>
// #include <linux/jiffies.h>

// #define DQL_MAX_OBJECT (UINT_MAX / 16)
// #define DQL_MAX_LIMIT ((UINT_MAX / 2) - DQL_MAX_OBJECT)

#define POSDIFF(A, B) ((int)((A) - (B)) > 0 ? (A) - (B) : 0)
#define AFTER_EQ(A, B) ((int)((A) - (B)) >= 0)

namespace bess {
namespace utils {

void dql::dql_queued(unsigned int count) {
  // There should be some maximum checking but forget about it for now
  last_obj_cnt = count;
  num_queued += count;
}

void dql::backdraft_dql_complete(unsigned int count) {
  unsigned int inprogress, new_limit;
  unsigned int ovlimit, completed;

  new_limit = limit;
  completed = num_completed + count;

  // Check wether we are ovlimit or not
  ovlimit = POSDIFF(num_queued - num_completed, new_limit);
  inprogress = num_queued - completed;

  if (ovlimit && !inprogress) {
    // Increase the window
    new_limit = POSDIFF(completed, prev_num_queued) + prev_ovlimit;
  } else {
    // Decrease the window
    new_limit--;
  }

  adj_limit = limit + completed;
  limit = new_limit;
  prev_ovlimit = ovlimit;
  prev_num_queued = num_queued;
  num_completed = completed;
}

void dql::dql_reset() {
  /* Reset all dynamic values */
  limit = 0;
  num_queued = 0;
  num_completed = 0;
  last_obj_cnt = 0;
  prev_num_queued = 0;
  prev_last_obj_cnt = 0;
  prev_ovlimit = 0;
  lowest_slack = UINT_MAX;
  slack_start_time = 100;
}

// dql::dql(unsigned int hold_time) {
dql::dql() {
  //   max_limit = DQL_MAX_LIMIT;
  LOG(INFO) << "dql is there";
  max_limit = 5000;
  min_limit = 0;
  slack_hold_time = 100;
  dql_reset();
}

// void dql::dql_completed(unsinged int count) {
//   unsigned int inprogress, prev_inprogress, new_limit;
//   unsigned int ovlimit, completed;
//   bool all_prev_completed;

//   new_limit = limit;
//   completed = num_completed + count;

//   ovlimit = POSDIFF(num_queued - num_completed, new_limit);
//   inprogress = num_queued - completed;
//   prev_inprogress = prev_num_queued - num_completed;
//   all_prev_completed = AFTER_EQ(completed, prev_num_queued);

//   if ((ovlimit && !inprogress) || (prev_ovlimit && all_prev_completed)) {
//     /*
//      * Queue considered starved if:
//      *   - The queue was over-limit in the last interval,
//      *     and there is no more data in the queue.
//      *  OR
//      *   - The queue was over-limit in the previous interval and
//      *     when enqueuing it was possible that all queued data
//      *     had been consumed.  This covers the case when queue
//      *     may have becomes starved between completion processing
//      *     running and next time enqueue was scheduled.
//      *
//      *     When queue is starved increase the limit by the amount
//      *     of bytes both sent and completed in the last interval,
//      *     plus any previous over-limit.
//      */
//     new_limit += POSDIFF(completed, prev_num_queued) + prev_ovlimit;
//     slack_start_time = jiffies;
//     lowest_slack = UINT_MAX;
//   } else if (inprogress && prev_inprogress && !all_prev_completed) {
//     /*
//      * Queue was not starved, check if the limit can be decreased.
//      * A decrease is only considered if the queue has been busy in
//      * the whole interval (the check above).
//      *
//      * If there is slack, the amount of execess data queued above
//      * the the amount needed to prevent starvation, the queue limit
//      * can be decreased.  To avoid hysteresis we consider the
//      * minimum amount of slack found over several iterations of the
//      * completion routine.
//      */

//     unsigned int slack, slack_last_objs;
//     /*
//      * Slack is the maximum of
//      *   - The queue limit plus previous over-limit minus twice
//      *     the number of objects completed.  Note that two times
//      *     number of completed bytes is a basis for an upper bound
//      *     of the limit.
//      *   - Portion of objects in the last queuing operation that
//      *     was not part of non-zero previous over-limit.  That is
//      *     "round down" by non-overlimit portion of the last
//      *     queueing operation.
//      */

//     slack = POSDIFF(new_limit + prev_ovlimit, 2 * (completed -
//     num_completed)); slack_last_objs =
//         prev_ovlimit ? POSDIFF(prev_last_obj_cnt, prev_ovlimit) : 0;

//     slack = max(slack, slack_last_objs);

//     if (slack < lowest_slack)
//       lowest_slack = slack;

//     if (time_after(jiffies, slack_start_time + slack_hold_time)) {
//       new_limit = POSDIFF(new_limit, lowest_slack);
//       slack_start_time = jiffies;
//       lowest_slack = UINT_MAX;
//     }

//     /* Enforce bounds on new_limit */
//     new_limit = clamp(new_limit, min_limit, max_limit);

//     if (limit != new_limit) {
//       limit = new_limit;
//       ovlimit = 0;
//     }

//     adj_limit = limit + completed;
//     prev_ovlimit = ovlimit;
//     prev_last_obj_cnt = last_obj_cnt;
//     num_completed = completed;
//     prev_num_queued = num_queued;
//   }
// }  // namespace utils

}  // namespace utils

}  // namespace bess