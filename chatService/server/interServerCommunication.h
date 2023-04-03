#include "serviceImplementations.h"

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


struct ServerServerConnection {
    private:
        std::vector<std::unique_ptr<ChatService::Stub>> connections;
        int leaderIdx = -1;
        // std::unique_ptr<ChatService::Stub> stub_;

    public:
        // ServerServerConnection(std::shared_ptr<Channel> channel) {
        //     stub_ = ChatService::NewStub(channel);
        // }

        ServerServerConnection() {}

        void addConnection(std::string server_address) {
            // TODO: do we want to put this in a loop to keep trying until it works?
            auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
            std::unique_ptr<ChatService::Stub> stub_ = ChatService::NewStub(channel);
            connections.push_back(stub_);
        }

        bool heartbeat() {
            // TODO: call heartbeat RPC with stub of leader
            if (leaderIdx != -1) {
                ClientContext context;
                HeartBeatRequest message;
                HeartBeatResponse reponse;
                Status status = connections[leaderIdx]->HeartBeat(&context, message, &reponse);

                if (status.ok()) {
                    return true;
                }
                else {
                    return false;
                }
            }
            else {
                return false;
            }
        }

        bool proposeLeaderElection() {
            // TODO: call propose leader election for each stub in vector
            
        }

        bool leaderElection() {
            // TODO: generate random number and broadcast it to all other connections
        }
};

std::vector<int> serverPorts {8080, 8081, 8082};
std::unordered_map<std::string, ServerServerConnection> connectionsDict;


void serverThread(const std::vector<std::string> serverAddresses) {
    ServerServerConnection serverConnections;
    for (std::string server_addr : serverAddresses) {
        serverConnections.addConnection(server_addr);
    }

}

// perform heartbeat, if fails then propose leader election
void performHeartbeat() {

}