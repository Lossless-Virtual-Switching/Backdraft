/* Copyright (c) 2020, Stanford University
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <iostream>
#include <vector>

#include <Homa/Drivers/DPDK/DpdkDriver.h>
#include <Cycles.h>
#include <TimeTrace.h>
#include <docopt.h>

#include "Output.h"

#define G 1000000000

static const char USAGE[] = R"(DPDK Driver Test.

    Usage:
        dpdk_test [options] --iface=<iface> (--server | <server_ip>) [--dpdk-extra=<arg>]...

    Options:
        -h --help           Show this screen.
        --version           Show version.
        --timetrace         Enable TimeTrace output [default: false].
	--vhost-port        Vhost port config which should fill the iface if added.
	--slow-down=CYCLES  Cycles to wait before receiving the packet batch [default: 0].
	--tx-pkt-length=SIZE Sender packet length [default 64].
	--rx-pkt-length=SIZE Receiver packet length [default 64].
)";

// --extra-dpdk PARAM  Extra DPDK parameters
int
main(int argc, char* argv[])
{

    int dpdk_extra_count = 0;
    char ** dpdk_extra_params;
    uint64_t pkt_length=64;
    // Homa::Drivers::DPDK::DpdkDriver driver;
    std::map<std::string, docopt::value> args =
        docopt::docopt(USAGE, {argv + 1, argv + argc},
                       true,                 // show help if requested
                       "DPDK Driver Test");  // version string

    for (auto const& arg : args) {
        std::cout << arg.first << " " <<  arg.second << std::endl;
    }

    if(args["--tx-pkt-length"]) {
        pkt_length = args["--tx-pkt-length"].asLong();
    }

    bool isVirtioHostPort = args["--vhost-port"].asBool();
    std::string vhost_conf;
    std::string iface = args["--iface"].asString();
    if (isVirtioHostPort) {
        vhost_conf = iface;
        dpdk_extra_count+=2;
        dpdk_extra_params = (char**)malloc(sizeof(char*) * (dpdk_extra_count + 1));
        dpdk_extra_params[0] = strdup("homa");
        dpdk_extra_params[1] = strdup(vhost_conf.c_str());
        dpdk_extra_params[2] = NULL;
    }

    bool isServer = args["--server"].asBool();
    uint64_t cycles_to_wait;
    std::string server_ip_string;
    if (!isServer) {
        server_ip_string = args["<server_ip>"].asString();
    } else {
        cycles_to_wait = args["--slow-down"].asLong();
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

    // std::cout << dpdk_extra_count << std::endl;
    // for(int i = 0; i <= dpdk_extra_count; i++) {
    //     std::cout << i << " " << dpdk_extra_params[i] << std::endl;
    // }
    std::cout << "testing" << std::endl;

    // I have to ask a question here.
    // if(isVirtioHostPort)
    //     driver = Homa::Drivers::DPDK::DpdkDriver::DpdkDriver(iface.c_str(), dpdk_extra_count, dpdk_extra_params);
    // else
    //     driver = Homa::Drivers::DPDK::DpdkDriver::DpdkDriver(iface.c_str());
    
    Homa::Drivers::DPDK::DpdkDriver driver(iface.c_str(), dpdk_extra_count, dpdk_extra_params);
    
    // Homa::Drivers::DPDK::DpdkDriver driver(iface.c_str());

    if (isServer) {
        std::cout << Homa::IpAddress::toString(driver.getLocalAddress())
                  << std::endl;

        while (true) {
            Homa::Driver::Packet* incoming[10];
            Homa::IpAddress srcAddrs[10];
            uint32_t receivedPackets;
            uint64_t start, now;
            do {
                receivedPackets = driver.receivePackets(10, incoming, srcAddrs);

		// std::cout << receivedPackets << std::endl;
		// if(total_pkts_processed >= 100000)
		// 	goto end;

                while (now - start < cycles_to_wait) {
                    now = PerfUtils::Cycles::rdtsc();
                }
            } while (receivedPackets == 0);
            Homa::Driver::Packet* pong = driver.allocPacket();
            pong->length = pkt_length;
            driver.sendPacket(pong, srcAddrs[0], 0);
            driver.releasePackets(incoming, receivedPackets);
            driver.releasePackets(&pong, 1);
        }
    } else {
   	uint64_t start = PerfUtils::Cycles::rdtsc();
        Homa::IpAddress server_ip =
            Homa::IpAddress::fromString(server_ip_string.c_str());
        std::vector<Output::Latency> times;
	uint64_t total_rx_bytes = 0;
        for (int i = 0; i < 100000; ++i) {
            uint64_t start = PerfUtils::Cycles::rdtsc();
            PerfUtils::TimeTrace::record(start, "START");
            Homa::Driver::Packet* ping = driver.allocPacket();
            PerfUtils::TimeTrace::record("allocPacket");
            ping->length = pkt_length;
            PerfUtils::TimeTrace::record("set ping args");
            driver.sendPacket(ping, server_ip, 0);
            PerfUtils::TimeTrace::record("sendPacket");
            driver.releasePackets(&ping, 1);
            PerfUtils::TimeTrace::record("releasePacket");
            Homa::Driver::Packet* incoming[10];
            Homa::IpAddress srcAddrs[10];
            uint32_t receivedPackets;
            do {
                receivedPackets = driver.receivePackets(10, incoming, srcAddrs);
		if(receivedPackets)
		total_rx_bytes += receivedPackets * 1000;
                PerfUtils::TimeTrace::record("receivePackets");
            } while (receivedPackets == 0);
            driver.releasePackets(incoming, receivedPackets);
            PerfUtils::TimeTrace::record("releasePacket");
            uint64_t stop = PerfUtils::Cycles::rdtsc();
            times.emplace_back(PerfUtils::Cycles::toSeconds(stop - start));
        }
	uint64_t end = PerfUtils::Cycles::rdtsc();

	uint64_t diff_s = PerfUtils::Cycles::toSeconds(end - start);
	uint64_t diff_us = PerfUtils::Cycles::toMicroseconds(end - start);
	uint64_t diff_ns = PerfUtils::Cycles::toNanoseconds(end - start);
	double throughput_rx = total_rx_bytes * 8 / (double)diff_ns;
	double throughput_tx = 100000 * 8 * 64 / (double)diff_ns;


        if (args["--timetrace"].asBool()) {
            PerfUtils::TimeTrace::print();
        }

	std::cout << Output::basicHeader() << std::endl;
        std::cout << Output::basic(times, "DpdkDriver Ping-Pong") << std::endl;
	std::cout << "Total Time seconds " << diff_s << std::endl;
	std::cout << "Total Time microseconds " << diff_us << std::endl;
	std::cout << "Total Time nanoseconds " << diff_ns << std::endl;
	std::cout << "Total receive bytes " <<  total_rx_bytes << std::endl;
	std::cout << "RX Throughput " << throughput_rx << " Gbps"   << std::endl;
	std::cout << "TX Throughput " << throughput_tx << " Gbps"   << std::endl;
	std::cout << "Transfer Throughput " << throughput_tx + throughput_rx << " Gbps"   << std::endl;

    }

    return 0;
}
