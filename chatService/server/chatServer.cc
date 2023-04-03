#include "serviceImplementations.h"

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <netdb.h>

const int g_backlogSize = 50;

// #define PORT 8080

std::vector<std::string> serverAddresses;

void RunServer(std::string ip_addr, int port) {
    // open log file for server
    ChatServiceImpl service(g_logFile);

    ServerBuilder builder;
    std::string server_addr = ip_addr+":"+std::to_string(port);
    // serverAddresses.push_back(server_addr);

    bool inputtingServers = true;
    while (inputtingServers) {
        std::string other_server;
        std::cout << "Input address of a server, or input 'y' to finish: ";
        std::cin >> other_server;
        if (other_server == "y") {
            inputtingServers = false;
        }
        serverAddresses.push_back(other_server);
    }

    builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_addr << std::endl;

    // Before waiting for requests, start thread that connects to other servers
    server->Wait();
    std::cout << "Wait finished?" << std::endl;
}

int main (int argc, char const* argv[]) {
    if (argc > 2) {
        std::cout << "The only optional argument is a CSV file with information to start the server with." << std::endl;
        return -1;
    }

    if (argc == 2) {
        // verify valid CSV file
        std::string historyFile = argv[1];
        if (historyFile.substr(historyFile.find_last_of(".")+1) != "csv") {
            std::cout << "File must be a CSV file" << std::endl;
            return -1;
        }

        // populate data structures using file
        // TODO: handle how we deal with matching fields in CSV file
    }
 
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
    while (noPort) {
        std::cout << "Please input port number (8080, 8081 or 8082) for server to use: ";
        std::cin >> port;
        if (port >= 8080 && port <= 8082) {
            noPort = false;
        }
    }
    RunServer(IP, port);

    return 0;
}