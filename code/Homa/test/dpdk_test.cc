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

static const char USAGE[] = R"(DPDK Driver Test.

    Usage:
        dpdk_test [options] --iface=<iface> (--server | <server_ip>) [--dpdk-extra=<arg>]...

    Options:
        -h --help           Show this screen.
        --version           Show version.
        --timetrace         Enable TimeTrace output [default: false].
	--vhost-port        Vhost port config which should fill the iface if added
)";

// --extra-dpdk PARAM  Extra DPDK parameters
int
main(int argc, char* argv[])
{

    int dpdk_extra_count = 0;
    char ** dpdk_extra_params;
    std::map<std::string, docopt::value> args =
        docopt::docopt(USAGE, {argv + 1, argv + argc},
                       true,                 // show help if requested
                       "DPDK Driver Test");  // version string

    // for (auto const& arg : args) {
    //     std::cout << arg.first << " " <<  arg.second << std::endl;
    // }

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
    std::string server_ip_string;
    if (!isServer) {
        server_ip_string = args["<server_ip>"].asString();
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

    std::cout << dpdk_extra_count << std::endl;
    for(int i = 0; i <= dpdk_extra_count; i++) {
        std::cout << i << " " << dpdk_extra_params[i] << std::endl;
    }
    Homa::Drivers::DPDK::DpdkDriver driver(iface.c_str(), dpdk_extra_count,
                                           dpdk_extra_params);
    if (isServer) {
        std::cout << Homa::IpAddress::toString(driver.getLocalAddress())
                  << std::endl;
        while (true) {
            Homa::Driver::Packet* incoming[10];
            Homa::IpAddress srcAddrs[10];
            uint32_t receivedPackets;
            do {
                receivedPackets = driver.receivePackets(10, incoming, srcAddrs);
            } while (receivedPackets == 0);
            Homa::Driver::Packet* pong = driver.allocPacket();
            pong->length = 100;
            driver.sendPacket(pong, srcAddrs[0], 0);
            driver.releasePackets(incoming, receivedPackets);
            driver.releasePackets(&pong, 1);
        }
    } else {
        Homa::IpAddress server_ip =
            Homa::IpAddress::fromString(server_ip_string.c_str());
        std::vector<Output::Latency> times;
        for (int i = 0; i < 100000; ++i) {
            uint64_t start = PerfUtils::Cycles::rdtsc();
            PerfUtils::TimeTrace::record(start, "START");
            Homa::Driver::Packet* ping = driver.allocPacket();
            PerfUtils::TimeTrace::record("allocPacket");
            ping->length = 100;
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
                PerfUtils::TimeTrace::record("receivePackets");
            } while (receivedPackets == 0);
            driver.releasePackets(incoming, receivedPackets);
            PerfUtils::TimeTrace::record("releasePacket");
            uint64_t stop = PerfUtils::Cycles::rdtsc();
            times.emplace_back(PerfUtils::Cycles::toSeconds(stop - start));
        }
        if (args["--timetrace"].asBool()) {
            PerfUtils::TimeTrace::print();
        }
        std::cout << Output::basicHeader() << std::endl;
        std::cout << Output::basic(times, "DpdkDriver Ping-Pong") << std::endl;
    }

    return 0;
}
