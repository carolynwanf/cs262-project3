#include "serviceImplementations.h"

#include <cstdlib>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using chatservice::ChatService;

// void serverThread(const std::vector<std::string> serverAddresses, std::string myAddress) {
    // ServerServerConnection serverConnections(myAddress);
void serverThread(const std::vector<std::string> serverAddresses) {
    for (std::string server_addr : serverAddresses) {
        g_Service.addConnection(server_addr);
    }

    if (g_Service.numberOfConnections() == 0) {
        std::cout << "Setting as leader as there are no other servers up" << std::endl;
        g_Service.setAsLeader();
    }

    srand(time(NULL));
    while (true) {
        // sleep
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_Service.isLeader()) {
            continue;
        }

        // heart beat
        if (!g_Service.heartbeat()) {
            // if heartbeat fails
            //      propose / execute leader election
            if (g_Service.proposeLeaderElection()) {
                g_Service.leaderElection();
            }
        }

    }
}