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
// Messages

// Replies


// struct ServerServerConnection {
//     private:
//         std::vector<std::unique_ptr<ChatService::Stub>> connections;
//         std::unordered_map<std::string, int> addressToConnectionIdx;
//         int leaderIdx = -1;
//         std::string myAddress;
//         // std::unique_ptr<ChatService::Stub> stub_;

//     public:
//         // ServerServerConnection(std::shared_ptr<Channel> channel) {
//         //     stub_ = ChatService::NewStub(channel);
//         // }

//         ServerServerConnection(std::string address) {
//             myAddress = address;
//         }

//         void addConnection(std::string server_address) {
//             // TODO: do we want to put this in a loop to keep trying until it works?
//             auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
//             std::unique_ptr<ChatService::Stub> stub_ = ChatService::NewStub(channel);
//             connections.push_back(stub_);
//             addressToConnectionIdx[server_address] = connections.size()-1;
//         }

//         bool heartbeat() {
//             if (leaderIdx != -1) {
//                 ClientContext context;
//                 HeartBeatRequest message;
//                 HeartBeatResponse reponse;
//                 Status status = connections[leaderIdx]->HeartBeat(&context, message, &reponse);

//                 if (status.ok()) {
//                     return true;
//                 }
//                 else {
//                     return false;
//                 }
//             }
//             else {
//                 return false;
//             }
//         }

//         bool proposeLeaderElection() {
//             // TODO: call propose leader election for each stub in vector
//             LeaderElectionProposal message;

//             for (int i = 0; i < connections.size(); i++) {
//                 ClientContext context;
//                 LeaderElectionProposalResponse reply;
//                 Status status = connections[i]->ProposeLeaderElection(&context, message, &reply);
//                 if (status.ok()) {
//                     if (reply.accept()) {
//                         return true;
//                     }
//                     else {
//                         return false;
//                     }
//                 }
//                 else {
//                     // TODO: might want to throw an exception here instead but we know how
//                     //      much Carolyn loves exceptions
//                     return false;
//                 }
//             }
//         }

//         void leaderElection() {
//             int candidateValue = rand();
//             g_leaderElectionValuesMutex.lock();
//             g_currLeaderCandidateAddr = myAddress;
//             g_maxLeaderElectionVal = candidateValue;
//             g_leaderElectionValuesMutex.unlock();

//             CandidateValue message;
//             message.set_number(candidateValue);

//             for (int i = 0; i < connections.size(); i++) {
//                 ClientContext context;
//                 LeaderElectionResponse reply;
//                 Status status =  connections[i]->LeaderElection(&context, message, &reply);
//             }

//             // wait until we've received leader election values of all other servers
//             bool waitingForLeaderElection = true;
//             while (waitingForLeaderElection) {
//                 g_leaderElectionValuesMutex.lock();
//                 if (g_numberOfCandidatesReceived == g_numberOfServers - 1) {
//                     waitingForLeaderElection = false;
//                     g_numberOfCandidatesReceived = 0;
//                 }
//                 g_leaderElectionValuesMutex.unlock();
//             }

//             // select new leader
//             if (g_currLeaderCandidateAddr == myAddress) {
//                 g_isLeaderMutex.lock();
//                 g_isLeader = true;
//                 g_isLeaderMutex.unlock();
//                 leaderIdx = -1;
//             } 
//             else {
//                 leaderIdx = addressToConnectionIdx[g_currLeaderCandidateAddr];
//             }

//             // reset necessary values
//             g_leaderElectionValuesMutex.lock();
//             g_currLeaderCandidateAddr = "";
//             g_maxLeaderElectionVal = -1;
//             g_leaderElectionValuesMutex.unlock();
//         }
// };

// void serverThread(const std::vector<std::string> serverAddresses, std::string myAddress) {
    // ServerServerConnection serverConnections(myAddress);
void serverThread(const std::vector<std::string> serverAddresses) {
    
    // Sleep before continuing
    for (std::string server_addr : serverAddresses) {
        g_Service.addConnection(server_addr);
    }

    if (g_Service.numberOfConnections() == 0) {
        std::cout << "Setting as leader as there are no other servers up" << std::endl;
        g_Service.setAsLeader();
    }

    // TODO: set seed
    while (true) {
        if (g_Service.isLeader()) {
            continue;
        }
        // sleep
        std::this_thread::sleep_for(std::chrono::seconds(1));

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