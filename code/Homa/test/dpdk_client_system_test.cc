/* Copyright (c) 2019-2020, Stanford University
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above * copyright notice and this permission notice appear in all copies.  * * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <Homa/Debug.h>
// #include <Homa/Drivers/Fake/FakeDriver.h>
#include <Homa/Drivers/DPDK/DpdkDriver.h>
#include <Homa/Homa.h>
#include <unistd.h>
#include <Cycles.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <sys/time.h>


#include "StringUtil.h"
#include "docopt.h"
#include "Output.h"
#include "Perf.h"

static const char USAGE[] = R"(Homa System Test.

    Usage:
        system_test <count> [-v | -vv | -vvv | -vvvv] [--dpdk-extra=<arg>]... [options]
        system_test (-h | --help)
        system_test --version

    Options:
        -h --help       Show this screen.
        --version       Show version.
        -v --verbose    Show verbose output.
        --servers=<n>   Number of virtual servers [default: 1].
        --size=<n>      Number of bytes to send as a payload [default: 10].
        --lossRate=<f>  Rate at which packets are lost [default: 0.0].
        --vhost-port          Vhost port config which should fill the iface if added.
        --slow-down=CYCLES    Cycles to wait before receiving the packet batch [default: 0].
        --tx-pkt-length=SIZE  Sender packet length [default 64].
        --rx-pkt-length=SIZE  Receiver packet length [default 64].
        --vhost-port-ip=IP    Vhost port ip, this is highly useful for vhost port.
        --vhost-port-mac=MAC  Vhost port mac address, this is highly useful for vhost port.
	--iface=IFACE	      Interface for the vhost port mostly
)";

bool _PRINT_CLIENT_ = false;
bool _PRINT_SERVER_ = false;

struct MessageHeader {
    uint64_t id;
    uint64_t length;
} __attribute__((packed));

struct Node {
    explicit Node(uint64_t id, std::string ip, std::string mac, int dpdk_param_size, char **dpdk_params)
        : id(id)
        // , driver("ens3f0")
        // , driver("ens3f0", )
        , driver("ens3f0", ip.c_str(), mac.c_str(), dpdk_param_size, dpdk_params)
        , transport(Homa::Transport::create(&driver, id))
        , thread()
        , run(false)
    {}

    const uint64_t id;
    // Homa::Drivers::Fake::FakeDriver driver;
    Homa::Drivers::DPDK::DpdkDriver driver;
    Homa::Transport* transport;
    std::thread thread;
    std::atomic<bool> run;
};

// void
// serverMain(Node* server, std::vector<Homa::IpAddress> addresses)
// {
//     while (true) {
//         if (server->run.load() == false) {
//             break;
//         }
// 
//         Homa::unique_ptr<Homa::InMessage> message =
//             server->transport->receive();
// 
//         if (message) {
//             MessageHeader header;
//             message->get(0, &header, sizeof(MessageHeader));
//             char buf[header.length];
//             message->get(sizeof(MessageHeader), &buf, header.length);
// 
//             if (_PRINT_SERVER_) {
//                 std::cout << "  -> Server " << server->id
//                           << " (opId: " << header.id << ")" << std::endl;
//             }
//             message->acknowledge();
//         }
//         server->transport->poll();
//     }
// }

/**
 * @return
 *      Number of Op that failed.
 */
int
clientMain(int count, int size, std::vector<Homa::IpAddress> addresses,
		std::string ip, std::string mac, int
		dpdk_param_size, char **dpdk_params)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> randAddr(0, addresses.size() - 1);
    std::uniform_int_distribution<> randAddrID(0, 100);
    std::uniform_int_distribution<char> randData(0);

    // uint64_t nextId = 0;
    int numFailed = 0;

    std::vector<Output::Latency> times;
    uint64_t start;
    uint64_t stop;

    // struct timeval current_time;
    // struct timespec tps;
    // struct timespec tpe;

    // uint64_t duration_timespec_us;

    // uint64_t nextId = 0;// randAddrID(gen) * count;
    uint64_t nextId = randAddrID(gen) * count;
    int id = randAddrID(gen);
    Node client(id, ip, mac, dpdk_param_size, dpdk_params);
    std::cout << "ID: " << id << std::endl;

    for (int i = 0; i < count; ++i) {
        uint64_t id = nextId++;
        char payload[size];
        for (int i = 0; i < size; ++i) {
            payload[i] = randData(gen);
        }

        Homa::IpAddress destAddress = addresses[randAddr(gen)];

        Homa::unique_ptr<Homa::OutMessage> message = client.transport->alloc(0);
        {
            MessageHeader header;
            header.id = id;
            header.length = size;
            message->append(&header, sizeof(MessageHeader));
            message->append(payload, size);
            if (_PRINT_CLIENT_) {
                std::cout << "Client -> (opId: " << header.id << ")"
                          << std::endl;
            }
        }
        start = PerfUtils::Cycles::rdtsc();
	//clock_gettime(CLOCK_MONOTONIC, &tps);
        // message->send(Homa::SocketAddress{destAddress, 60001}, Homa::OutMessage::Options::NO_RETRY);
        message->send(Homa::SocketAddress{destAddress, 60001});

        while (1) {
            Homa::OutMessage::Status status = message->getStatus();
            if (status == Homa::OutMessage::Status::COMPLETED) {
		stop = PerfUtils::Cycles::rdtsc();
	 	//clock_gettime(CLOCK_MONOTONIC, &tpe);
                times.emplace_back(PerfUtils::Cycles::toSeconds(stop - start));
		// duration_timespec_us = (tpe.tv_sec - tps.tv_sec) * 1000000000L + (tpe.tv_nsec - tps.tv_nsec)*1L;
                // times.emplace_back(duration_timespec_us);
                break;
            } else if (status == Homa::OutMessage::Status::FAILED) {
                numFailed++;
                break;
	    }
            client.transport->poll();
        }
    }

    std::cout << Output::basicHeader() << std::endl;
    std::cout << Output::basic(times, "Homa Transport Testing") << std::endl;

    return numFailed;
}

int
main(int argc, char* argv[])
{
    std::cout << "before client starts" << std::endl;
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
        dpdk_extra_count+=2;
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


    // Homa::Drivers::Fake::FakeNetworkConfig::setPacketLossRate(packetLossRate);

    // uint64_t nextServerId = 101;
    std::vector<Homa::IpAddress> addresses;
    // std::vector<Node*> servers;
    // for (int i = 0; i < numServers; ++i) {
    //     Node* server = new Node(nextServerId++);
    //     addresses.emplace_back(server->driver.getLocalAddress());
    //     servers.push_back(server);
    // }

    // for (auto it = servers.begin(); it != servers.end(); ++it) {
    //     Node* server = *it;
    //     server->run = true;
    //     server->thread = std::move(std::thread(&serverMain, server, addresses));
    // }
    //
    addresses.emplace_back(Homa::IpAddress::fromString("192.168.1.1"));

    uint64_t start = PerfUtils::Cycles::rdtsc();
    int numFails = clientMain(numTests, numBytes, addresses, vhost_ip,
		    vhost_mac, dpdk_extra_count, dpdk_extra_params);
    uint64_t end = PerfUtils::Cycles::rdtsc();
    uint64_t duration = PerfUtils::Cycles::toNanoseconds(end - start);
    double duration_s = PerfUtils::Cycles::toSeconds(end - start);
    // for (auto it = servers.begin(); it != servers.end(); ++it) {
    //     Node* server = *it;
    //     server->run = false;
    //     server->thread.join();
    //     delete server;
    // }

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

    return numFails;
}
