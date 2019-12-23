#ifndef BESS_UTILS_DQL_H_
#define BESS_UTILS_DQL_H_

namespace bess {
namespace utils {

class dql {
 public:
  int dql_avail() { return adj_limit - num_queued; };

  //   void dql_completed(unsigned int count);

  void dql_reset();

  void dql_init(unsigned int hold_time);

  void dql_queued(unsigned int count);

  void backdraft_dql_complete(unsigned int count);

  dql();

 private:
  /* Fields accessed in enqueue path (dql_queued) */
  unsigned int num_queued;   /* Total ever queued */
  unsigned int adj_limit;    /* limit + num_completed */
  unsigned int last_obj_cnt; /* Count at last queuing */

  /* Fields accessed only by completion path (dql_completed) */

  unsigned int limit;         /* Current limit */
  unsigned int num_completed; /* Total ever completed */

  unsigned int prev_ovlimit;      /* Previous over limit */
  unsigned int prev_num_queued;   /* Previous queue total */
  unsigned int prev_last_obj_cnt; /* Previous queuing cnt */

  unsigned int lowest_slack;      /* Lowest slack found */
  unsigned long slack_start_time; /* Time slacks seen */

  /* Configuration */
  unsigned int max_limit;       /* Max limit */
  unsigned int min_limit;       /* Minimum limit */
  unsigned int slack_hold_time; /* Time to measure slack */
};
}  // namespace utils
}  // namespace bess

#endif