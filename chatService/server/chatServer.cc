#include "interServerCommunication.h"

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <netdb.h>

const int g_backlogSize = 50;

// #define PORT 8080

std::vector<std::string> serverAddresses;

void RunServer(std::string server_addr) {
    ServerBuilder builder;
    builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&g_Service);
    std::unique_ptr<Server> server(builder.BuildAndStart());

    // Before waiting for requests, start thread that connects to other servers
    server->Wait();
}

int main (int argc, char const* argv[]) {
 
    // For getting host IP address we followed tutorial found here: 
    //      https://www.tutorialspoint.com/how-to-get-the-ip-address-of-local-computer-using-c-cplusplus
    char host[256];
    char *IP;
    hostent *host_entry;
    int hostname;
    hostname = gethostname(host, sizeof(host)); //find the host name
    host_entry = gethostbyname(host); //find host information
    IP = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0])); //Convert into IP string
    int port;
    bool noPort = true;

    // Get port number from user
    while (noPort) {
        std::cout << "Please input port number (8080, 8081 or 8082) for server to use: ";
        std::cin >> port;
        if (port >= 8080 && port <= 8082) {
            noPort = false;
        }
    }

    // Prints server's IP
    std::string server_addr = std::string(IP)+":"+std::to_string(port);
    std::cout << "Server listening on " << server_addr << std::endl;
    g_Service.initialize(server_addr);

    // Gets other server addresses from user
    while (true) {
        std::string other_server;
        std::cout << "Input address of a server, or input 'y' to finish: ";
        std::cin >> other_server;
        if (other_server == "y") {
            break;
        }
        serverAddresses.push_back(other_server);
    }

    // Start inter-server communication thread
    std::thread serverCommunicationThread(serverThread, serverAddresses);
    serverCommunicationThread.detach();

    RunServer(server_addr);

    return 0;
}