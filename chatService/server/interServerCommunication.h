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
// using chatservice::PendingLogRequest;

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

        // If leader
        if(g_Service.isLeader()) {
            // Request pending with Request Pending Log
            std::vector<OperationClass> operations;
            g_Service.requestLogs(operations);
            std::vector<std::vector<std::string>> vectorizedLines;
            readFile(&vectorizedLines, g_Service.getPendingFilename());
            for (std::vector<std::string> line : vectorizedLines) {
                OperationClass op;
                op.clockVal = std::stoi(line[7]);
                op.opCode = std::stoi(line[0]);
                op.username1 = line[1];
                op.username2 = line[2];
                op.password = line[3];
                op.message_content = line[4];
                op.messagesseen = line[5];
                op.leader = line[6];
                operations.push_back(op);
            }
            sortOperations(operations);
            g_Service.writePendingOperations(operations);

            // Send commit logs
            std::cout << "sending commit logs" << std::endl;
            g_Service.sendLogs(g_Service.getCommitFilename());

            // Move pending logs to commit
            std::cout << "moving pending logs to commit" << std::endl;
            g_Service.moveAllPendingToCommit();

            // Sending pending logs
            std::cout << "sending pending logs" << std::endl;
            g_Service.sendLogs(g_Service.getPendingFilename());



            
        }



    }
}