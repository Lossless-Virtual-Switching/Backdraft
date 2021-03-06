#include <Homa/Debug.h>
#include <Homa/Drivers/DPDK/DpdkDriver.h>
#include <Homa/Homa.h>
#include <unistd.h>
#include <Cycles.h>

#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <sys/time.h>


#include "StringUtil.h"
#include "docopt.h"
#include "../Output.h"
#include "Perf.h"


// C includes
#include <sys/mman.h> // mmap
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "llring.h"
#include <rte_launch.h>
#include <rte_lcore.h>

#include  "Generator.h"

#define VICTIM_INCAST_RATIO 1.0


static const char USAGE[] = R"(Homa System Test.

    Usage:
        system_test <count> [-v | -vv | -vvv | -vvvv] [--dpdk-extra=<arg>]... [options]
        system_test (-h | --help)
        system_test --version

    Options:
        -h --help       Show this screen.
        --version       Show version.
        -v --verbose    Show verbose output.
        --barriers=<n>  Number of people to wait.
        --id=<n>        Identification number for HOMA messages.
        --servers=<n>   Number of virtual servers [default: 1].
        --size=<n>      Number of bytes to send as a payload [default: 10].
        --lossRate=<f>  Rate at which packets are lost [default: 0.0].
        --vhost-port          Vhost port config which should fill the iface if added.
        --slow-down=CYCLES    Cycles to wait before receiving the packet batch [default: 0].
        --tx-pkt-length=SIZE  Sender packet length [default 64].
        --rx-pkt-length=SIZE  Receiver packet length [default 64].
        --vhost-port-ip=IP    Vhost port ip, this is highly useful for vhost port.
        --vhost-port-mac=MAC  Vhost port mac address, this is highly useful for vhost port.
        --iface=IFACE         Interface for the vhost port mostly
        --delay=DELAY         Number of nanoseconds a client should wait before sending next RPC
        --victim              This client should send victim flow
)";

bool _PRINT_CLIENT_ = false;
bool _PRINT_SERVER_ = false;

// This is related to measuring the goodput
static uint64_t start_time;

struct MessageHeader {
  uint64_t id;
  uint64_t length;
} __attribute__((packed));


struct Node {
  explicit Node(uint64_t id, std::string ip, std::string mac, int
      dpdk_param_size, char **dpdk_params)
    : id(id)
      , driver("ens3f0", ip.c_str(), mac.c_str(), dpdk_param_size, dpdk_params)
      , transport(Homa::Transport::create(&driver, id))
      , thread()
      , run(false)
  {}

  const uint64_t id;
  Homa::Drivers::DPDK::DpdkDriver driver;
  Homa::Transport* transport;
  std::thread thread;
  volatile std::atomic<bool> run;
};

/* Wait for some nano seconds
 * */
void wait(int ns) {
    uint64_t start , now;
    start = PerfUtils::Cycles::rdtsc();
    now = start;
    while (PerfUtils::Cycles::toNanoseconds(now - start) < ns) {
      now = PerfUtils::Cycles::rdtsc();
    }
}

int clientPollWorker(void *_arg)
{
  Node *client = (Node *)_arg;
  while (client->run) {
    client->transport->poll();
  }
  std::cout << "Poll worker done" << std::endl;
  return 0;
}

struct MessageStat {
  uint64_t start;
  uint32_t addrIndex;
  // uint64_t end;
};

static std::vector<Homa::IpAddress> addresses;
static int pkt_to_finish;
static int num_failed_pkt;
// static std::map<uint32_t, uint64_t> timebook;
// static std::map<uint32_t, uint32_t> tag_type;
struct MessageStat *stat_book;
static uint64_t finish = 0;
static bool isVictim = false;
static uint64_t success_counter[2] = {};

int clientRxWorker(void *_arg)
{
  Node *client = (Node *)_arg;
  uint64_t done;
  Homa::OutMessage::Status status;
  const int size = 32;
  Homa::OutMessage *messages[size];
  Homa::OutMessage *message;
  std::vector<Output::Latency> times[addresses.size()];
  uint64_t start, stop;
  size_t time_list_index;
  int numFailed = 0;
  int numComplete = 0;

  while (numFailed + numComplete < pkt_to_finish) {
    done = client->transport->getDonePackets((Homa::OutMessage **)messages,
        size);
    for(int i = 0; i < done; i++) {
      message = messages[i];
      status = message->getStatus();
      if (status == Homa::OutMessage::Status::COMPLETED) {
        stop = PerfUtils::Cycles::rdtsc();
        numComplete++;
        uint32_t id = message->getTag();
        client->transport->unsafe_free_message(message);
        {
          start = stat_book[id].start;
          time_list_index = stat_book[id].addrIndex;
        }
        times[time_list_index].emplace_back(
            PerfUtils::Cycles::toSeconds(stop - start));
        success_counter[time_list_index]++;
        continue;
      } else if (status == Homa::OutMessage::Status::FAILED) {
        numFailed++;
      } else {
        std::cout << "Unknown packet" << std::endl;
      }
      client->transport->unsafe_free_message(message);
    }
    // wait(500);
  }

  uint64_t end_time = PerfUtils::Cycles::rdtsc();

  std::cout << "Failed: " << numFailed << " Completed: " << numComplete <<
    std::endl;

  std::cout << "Rx worker done" << std::endl;
  num_failed_pkt = numFailed;
  client->run = false;

  for (int i = 0; i < addresses.size(); i++) {
    // std::cout << "Result for IP: " << (uint32_t)addresses[i] << std::endl;
    if(start_time != 0) { // sanity check
      std::cout << "Result for IP: " << (uint32_t)addresses[i] << "Goodput: "
        << success_counter[i] << " "  <<
        success_counter[i]/PerfUtils::Cycles::toSeconds(end_time - start_time)
        << " RPCps" << std::endl;
    } else  {
      std::cout << "ERRRRROR in latency measurement" << std::endl;
    }

    if (times[i].size() > 0) {
      std::cout << Output::basicHeader() << std::endl;
      times[i].erase(times[i].begin(), times[i].begin() + 1000);
      times[i].erase(times[i].end() - 1000, times[i].end());
      std::cout << Output::basic(times[i], "Homa Transport Testing") <<
        std::endl;
    } else {
      std::cout << "No latency record found!" << std::endl;
    }
    std::cout << "=================================================" <<
      std::endl;
  }
  return 0;
}

typedef double (*gen_fn_t)(Generator *);
typedef struct tx_worker_args {
  Node *client;
  Homa::IpAddress destAddress;
  Generator *size_dist;
  Generator *ia_dist;
  // std::function<double()> *ia_dist_fn;
  gen_fn_t ia_dist_fn;
  size_t count_message;
  size_t base_id;
  size_t addr_index;
} tx_worker_args_t;

int clientTxWorker(void *_arg)
{
  tx_worker_args_t *args = static_cast<tx_worker_args_t *>(_arg);

  std::random_device rd;
  std::mt19937 gen(rd());
  // std::uniform_int_distribution<> randAddr(0, addresses.size() - 1);
  std::exponential_distribution<> randAddr(1);
  std::uniform_int_distribution<char> randData(0);
  size_t wait_time = 0;
  uint64_t start;

  // NOTE: Currently therse is only one server IP
  Homa::IpAddress destAddress = args->destAddress;
  size_t count = args->count_message;

  // A fix payload is used for all packets
  size_t size = 5000000;
  char payload[size];
  for (int i = 0; i < size; ++i) {
    payload[i] = randData(gen);
  }

  // Check that we are not violating payload bounds
  if (count > size) {
    std::cout << "The maximum number of messages could be " << size <<
      std::endl;
    exit(1);
  }

  Generator *fbvalue = args->size_dist;
  Generator *ourgen = args->ia_dist;
  int addrIndex = args->addr_index;
  uint64_t nextId = args->base_id;
  uint32_t tag;
  uint32_t counter = 0;
  Homa::OutMessage *message;
  for (int i = 0; i < count; i++) {
    // sending on port zero!
    uint64_t id = nextId++;
    size = fbvalue->generate();
    message = args->client->transport->unsafe_alloc(0);
    MessageHeader header;
    header.id = id;
    header.length = size;
    message->append(&header, sizeof(MessageHeader));
    message->append(payload, size);
    message->setTag(id);
    // if (_PRINT_CLIENT_) {
    //   std::cout << "Client -> (opId: " << header.id << ")"
    //     << std::endl;
    // }

    // Inter arrival
    wait_time = args->ia_dist_fn(ourgen);
    wait(wait_time);

    // set time stamp
    start = PerfUtils::Cycles::rdtsc();
    stat_book[id].start = start;
    stat_book[id].addrIndex = addrIndex;
    // send the message
    message->send(Homa::SocketAddress{destAddress, 60001});
  }

  std::cout << "Tx worker done" << std::endl;

  return 0;
}

/**
 * @return
 *      Number of Op that failed.
 */
int clientMain(int count, int size, std::vector<Homa::IpAddress> addresses,
    std::string ip, std::string mac, int
    dpdk_param_size, char **dpdk_params, int barrier_count, int param_id,
    int wait_time)
{
  size_t umem_size = sizeof(pthread_barrier_t) + 100;
  char SHM_PATH[60] = "/homa_openloop_dpdk_test";
  int ret;
  double distribution_wait_time;
  pthread_barrier_t * bufs;

  // Setup shared memory it is a C code.
  // a pthread_barrier_t is shared between clients
  pthread_barrierattr_t attr;

  int shm_fd = shm_open(SHM_PATH, (O_CREAT | O_EXCL | O_RDWR),
      (S_IREAD | S_IWRITE));
  if (shm_fd > 0) {
    ret = ftruncate(shm_fd, umem_size);
    if (ret != 0) {
      return -1;
    }
    bufs = (pthread_barrier_t *)mmap(NULL, umem_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, shm_fd, 0);
    ret = pthread_barrierattr_init(&attr);
    if (ret != 0) {
      std::cout << "unable to init the attr\n";
      return -1;
    }
    ret = pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (ret != 0) {
      std::cout << "unable to set property of attr\n";
      return -1;
    }
    ret = pthread_barrier_init(bufs, &attr, barrier_count);
    if (ret != 0) {
      std::cout << "unable to set the barrier\n";
      return -1;
    }
    std::cout << "Shared CREATED\n";
  } else {
    shm_fd = shm_open(SHM_PATH, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE));
    if (shm_fd < 0) {
      exit(-1);
    }
    struct stat statbuf;
    ret = fstat(shm_fd, &statbuf);
    if (ret != 0) {
      return -1;
    }
    if (statbuf.st_size < umem_size) {
      // invalid shared memory!
      return -1;
    }

    bufs = (pthread_barrier_t *)mmap(NULL, umem_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, shm_fd, 0);
    std::cout << "REUSED\n";
  }

  size_t id = param_id;
  Node client(id, ip, mac, dpdk_param_size, dpdk_params);

  client.driver.setBandwidth(100000);
  std::cout << "set bandwidth " << 100000 << std::endl;

  client.run = true;
  std::cout << "ID: " << id << std::endl;
  id = 0; // now that transport id is different we can use same sequence ids

  // Check if enough lcores are available.
  // Then launch polling thread.
  int count_lcore = rte_lcore_count();
  int needed_lcores = isVictim ? 4 : 3;
  if  (count_lcore < needed_lcores) {
    std::cout << "Need 3|4 lcores for running client" << std::endl;
    return 1;
  }
  int poll_lcore_id = rte_get_next_lcore(-1, 1, 0);
  ret = rte_eal_remote_launch(clientPollWorker, &client, poll_lcore_id);
  if (ret != 0) {
    std::cout << "Failed to start Poll worker!" << std::endl;
    return -1;
  }

  // Setting packet to finish condition before we start the RX thread
  pkt_to_finish = 0;
  pkt_to_finish += isVictim ? count + VICTIM_INCAST_RATIO*count : count;

  // Allocate per message stat array
  stat_book = new struct MessageStat[pkt_to_finish];

  int rx_lcore_id = rte_get_next_lcore(poll_lcore_id, 1, 0);
  ret = rte_eal_remote_launch(clientRxWorker, &client, rx_lcore_id);
  if (ret != 0) {
    std::cout << "Failed to start Rx worker!" << std::endl;
    return -1;
  }

  // Wait until all client process are ready
  pthread_barrier_wait(bufs);
  std::cout << "STAAAAAAAAAAAARRRRRRRRTTTTTTTTTTTTTTTTTT" << std::endl;

  start_time = PerfUtils::Cycles::rdtsc();

  int victim_tx_lcore_id = -1;
  tx_worker_args_t victim_arg;
  if (isVictim) {
    victim_arg = (tx_worker_args_t) {
          client: &client,
          destAddress: addresses[1],
          size_dist: createFacebookValue(),
          ia_dist: new Exponential(0.00005),
          ia_dist_fn: [](Generator *g) -> double {return (g->generate());},
          count_message: (size_t)(count * VICTIM_INCAST_RATIO),
          base_id: id + count + 1000,
          addr_index: 1,
    };

    victim_tx_lcore_id = rte_get_next_lcore(rx_lcore_id, 1, 0);
    ret = rte_eal_remote_launch(clientTxWorker, &victim_arg, victim_tx_lcore_id);
    if (ret != 0) {
      std::cout << "Failed to start Rx worker!" << std::endl;
      return -1;
    }
  }

  tx_worker_args_t incast_arg = {
    client: &client,
    destAddress: addresses[0],
    size_dist: new Fixed(1700),
    ia_dist: new Exponential(0.00005),
    ia_dist_fn: [](Generator *g) -> double {return (g->generate());},
    // ia_dist: new Fixed(1),
    count_message: (size_t)(count * 1.0),
    base_id: id,
    addr_index: 0,
  };

  clientTxWorker(&incast_arg);

  if (isVictim)
        rte_eal_wait_lcore(victim_tx_lcore_id);

  finish = PerfUtils::Cycles::rdtsc();
  // Wait until other threads are done
  rte_eal_wait_lcore(poll_lcore_id);
  rte_eal_wait_lcore(rx_lcore_id);

  // Taking care of the shared memory
  munmap(bufs, umem_size);
  shm_unlink(SHM_PATH);
  return num_failed_pkt;
}

int main(int argc, char* argv[])
{
  // sleep(15);
  // std::cout << "Hope debuger is connected" << std::endl;
  std::map<std::string, docopt::value> args =
    docopt::docopt(USAGE, {argv + 1, argv + argc},
        true,                 // show help if requested
        "Homa System Test");  // version string

  // Read in args.
  int numTests = args["<count>"].asLong();
  int numServers = args["--servers"].asLong();
  int numBytes = args["--size"].asLong();
  int verboseLevel = args["--verbose"].asLong();
  double packetLossRate = atof(args["--lossRate"].asString().c_str());
  int barrier_count = args["--barriers"].asLong();
  int param_id = args["--id"].asLong();
  int delay = args["--delay"].asLong();
  isVictim = args["--victim"].asBool();
  if (isVictim) {
    std::cout << "victim flow is enabled" << std::endl;
  }

  // level of verboseness
  bool printSummary = false;
  if (verboseLevel > 0) {
    printSummary = true;
    Homa::Debug::setLogPolicy(Homa::Debug::logPolicyFromString("ERROR"));
  }
  if (verboseLevel > 1) {
    Homa::Debug::setLogPolicy(Homa::Debug::logPolicyFromString("WARNING"));
  }
  if (verboseLevel > 2) {
    _PRINT_CLIENT_ = true;
    Homa::Debug::setLogPolicy(Homa::Debug::logPolicyFromString("NOTICE"));
  }
  if (verboseLevel > 3) {
    _PRINT_SERVER_ = true;
    Homa::Debug::setLogPolicy(Homa::Debug::logPolicyFromString("VERBOSE"));
  }

  // Here I start parsing DPDK specific parameters
  int dpdk_extra_count = 0;
  char ** dpdk_extra_params;
  uint64_t pkt_length = 64;


  if(args["--tx-pkt-length"]) {
    pkt_length = args["--tx-pkt-length"].asLong();
  }

  bool isVirtioHostPort = args["--vhost-port"].asBool();
  std::string iface = args["--iface"].asString();
  std::string vhost_conf;
  std::string vhost_ip;
  std::string vhost_mac;
  std::string server_ip_string;


  if (isVirtioHostPort) {
    vhost_conf = iface;
    dpdk_extra_count += 2;
    dpdk_extra_params = (char**)malloc(sizeof(char*) * (dpdk_extra_count + 1));
    dpdk_extra_params[0] = strdup("homa");
    dpdk_extra_params[1] = strdup(vhost_conf.c_str());
    dpdk_extra_params[2] = NULL;

    // IP and MAC
    vhost_ip = args["--vhost-port-mac"].asString();
    vhost_mac = args["--vhost-port-ip"].asString();
  }

  std::vector<std::string> param_list = args["--dpdk-extra"].asStringList();
  int extra_size = param_list.size();
  if (extra_size > 0) {
    dpdk_extra_count += extra_size;
    dpdk_extra_params = (char**)realloc(dpdk_extra_params,
        (dpdk_extra_count + 1) * sizeof(char*));
    for (int i = 2; i < dpdk_extra_count; i++) {
      dpdk_extra_params[i] = strdup(param_list[i - 2].c_str());
      // std::cout << dpdk_extra_params[i] << std::endl;
    }
    dpdk_extra_params[dpdk_extra_count] = NULL;
  }
  // Parameter parsing is done
  addresses.emplace_back(Homa::IpAddress::fromString("192.168.1.1"));
  if (isVictim) {
    addresses.emplace_back(Homa::IpAddress::fromString("192.168.1.2"));
  }

  uint64_t start = PerfUtils::Cycles::rdtsc();
  int numFails = clientMain(numTests, numBytes, addresses, vhost_ip,
      vhost_mac, dpdk_extra_count, dpdk_extra_params, barrier_count, param_id,
      delay);
  uint64_t end = PerfUtils::Cycles::rdtsc();
  uint64_t duration = PerfUtils::Cycles::toNanoseconds(end - start);
  double duration_s = PerfUtils::Cycles::toSeconds(end - start);

  if(numFails < 0) {
    std::cout << "Client failed" << std::endl;
    return 1;
  }

  if (printSummary) {
    std::cout << numTests << " Messages tested: " << numTests - numFails
      << " completed, " << numFails << " failed" << std::endl;
    Homa::Perf::Stats stats;
    Homa::Perf::getStats(&stats);
    std::cout << "active_cycles " << stats.active_cycles << std::endl;
    std::cout << "idle_cycles " << stats.idle_cycles << std::endl;
    std::cout << "allocated_rx_messages " << stats.allocated_rx_messages << std::endl;
    std::cout << "received_rx_messages " << stats.received_rx_messages << std::endl;
    std::cout << "delivered_rx_messages " << stats.delivered_rx_messages << std::endl;
    std::cout << "destroyed_rx_messages " << stats.destroyed_rx_messages << std::endl;
    std::cout << "allocated_tx_messages " << stats.allocated_tx_messages << std::endl;
    std::cout << "released_tx_messages " << stats.released_tx_messages << std::endl;
    std::cout << "destroyed_tx_messages " << stats.destroyed_tx_messages << std::endl;
    std::cout << "tx_bytes " << stats.tx_bytes << std::endl;
    std::cout << "rx_bytes " << stats.rx_bytes << std::endl;
    std::cout << "tx_data_pkts " << stats.tx_data_pkts << std::endl;
    std::cout << "rx_data_pkts " << stats.rx_data_pkts << std::endl;
    std::cout << "tx_grant_pkts " << stats.tx_grant_pkts << std::endl;
    std::cout << "rx_grant_pkts " << stats.rx_grant_pkts << std::endl;
    std::cout << "tx_done_pkts " << stats.tx_done_pkts << std::endl;
    std::cout << "rx_done_pkts " << stats.rx_done_pkts << std::endl;
    std::cout << "tx_resend_pkts " << stats.tx_resend_pkts << std::endl;
    std::cout << "rx_resend_pkts " << stats.rx_resend_pkts << std::endl;
    std::cout << "tx_busy_pkts " << stats.tx_busy_pkts << std::endl;
    std::cout << "rx_busy_pkts " << stats.rx_busy_pkts << std::endl;
    std::cout << "tx_ping_pkts " << stats.tx_ping_pkts << std::endl;
    std::cout << "rx_ping_pkts " << stats.rx_ping_pkts << std::endl;
    std::cout << "tx_unknown_pkts " << stats.tx_unknown_pkts << std::endl;
    std::cout << "rx_unknown_pkts " << stats.rx_unknown_pkts << std::endl;
    std::cout << "tx_error_pkts " << stats.tx_error_pkts << std::endl;
    std::cout << "rx_error_pkts " << stats.rx_error_pkts << std::endl;

    std::cout << "Duration ns " << duration << std::endl;
    std::cout << "Duration s " << duration_s << std::endl;

    std::cout << "rx tput " << stats.rx_bytes * 8 / double(duration) << std::endl;
    std::cout << "tx tput " << stats.tx_bytes * 8 / double(duration) << std::endl;

  }

  return 0;
}
