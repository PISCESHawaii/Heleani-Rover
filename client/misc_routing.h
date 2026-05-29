//
// Created by joseph on 5/27/26.
//

#ifndef HELELANIROVER_MISC_ROUTING_H
#define HELELANIROVER_MISC_ROUTING_H
#include <iostream>

#include "saucer/smartview.hpp"
#include "libstrophe_cpp.h"
#include "xmpp_iq.h"


void initialize_telemetry_listener(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client
);

void log_server_details(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client
);

void fetch_rover_options(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::function<
        void(
            std::string video_url,
            std::vector<std::pair<std::string, std::string> > commands
        )> callback
);

void send_command(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::string command_id
);

#endif //HELELANIROVER_MISC_ROUTING_H
