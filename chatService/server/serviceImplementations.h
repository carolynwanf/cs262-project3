#include "../chatService.grpc.pb.h"
#include "storageUpdates.h"

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

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using chatservice::ChatService;
// Messages
using chatservice::CreateAccountMessage;
using chatservice::LoginMessage;
using chatservice::LogoutMessage;
using chatservice::QueryUsersMessage;
using chatservice::ChatMessage;
using chatservice::QueryNotificationsMessage;
using chatservice::QueryMessagesMessage;
using chatservice::DeleteAccountMessage;
using chatservice::MessagesSeenMessage;
using chatservice::HeartBeatRequest;
using chatservice::LeaderElectionProposal;
using chatservice::CandidateValue;
using chatservice::CommitRequest;
// Replies
using chatservice::CreateAccountReply;
using chatservice::LoginReply;
using chatservice::LogoutReply;
using chatservice::User;
using chatservice::SendMessageReply;
using chatservice::Notification;
using chatservice::DeleteAccountReply;
// using chatservice::NewMessageReply;
using chatservice::RefreshRequest;
using chatservice::RefreshResponse;
using chatservice::MessagesSeenReply;
using chatservice::HeartBeatResponse;
using chatservice::LeaderElectionProposalResponse;
using chatservice::LeaderElectionResponse;
using chatservice::CommitResponse;

struct LeaderValues {
    bool isLeader = false;
    int leaderidx = -1;
    std::string leaderAddress = "";

    LeaderValues() {}
};

struct ElectionValues {
    int numberOfCandidatesReceived = 0;
    int maxLeaderElectionVal = -1;
    std::string currLeaderCandidateAddr;

    ElectionValues() {}
};

class ChatServiceImpl final : public chatservice::ChatService::Service {
    private:
        // This might be where we store the conversations open per user or something
        std::ofstream pendingLogWriter;
        std::ofstream commitLogWriter;

        // for reading pending logs
        std::fstream pendingFile;

        // For interserver communication
        std::mutex connectionMutex;
        std::vector<std::unique_ptr<ChatService::Stub>> connections;
        std::unordered_map<std::string, int> addressToConnectionIdx;
        std::string myAddress;

        // Commented out the global versions in storage.h
        std::mutex leaderMutex;
        LeaderValues leaderVals;

        std::mutex leaderElectionValuesMutex;
        ElectionValues electionVals;

    public:
        explicit ChatServiceImpl(std::string pendingFileName, std::string commitFileName) {
            // open CSV files in append mode
            pendingLogWriter.open(pendingFileName, std::fstream::app);
            commitLogWriter.open(commitFileName, std::fstream::app);

            // open pendingLogFile in read mode
            pendingFile.open(pendingFileName, std::ios::in);
        }

        // Called when CSV file was found to be empty (i.e. a new log file) so we define the CSV fields
        void addFields() {
            pendingLogWriter << g_csvFields << std::endl;
            commitLogWriter << g_csvFields << std::endl;
        }

        void addMyAddress(std::string addr) {
            myAddress = addr;
            std::cout << "My address is " << myAddress << std::endl;
        }

        int numberOfConnections() {
            int numberOfConnections;
            connectionMutex.lock();
            numberOfConnections = connections.size();
            connectionMutex.unlock();

            return numberOfConnections;            
        }

        Status CreateAccount(ServerContext* context, const CreateAccountMessage* create_account_message, 
                            CreateAccountReply* server_reply) {

            std::string username = create_account_message->username();
            std::string password = create_account_message->password();
            writeToLogs(pendingLogWriter, CREATE_ACCOUNT, username, g_nullString, password);
            
             // If master, talk to replicas, if not master just return ok after writing to pending
            if (leaderVals.isLeader) {

                std::vector<int> droppedConnections;

                // Tell replicas to write to pending
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CreateAccountReply reply;
                    Status status = connections[i]->CreateAccount(&context, *create_account_message, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(i);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    connections.erase(connections.begin() + droppedConnections[i]);
                }

                // Commit 
                writeToLogs(commitLogWriter, CREATE_ACCOUNT, username, g_nullString, password);

                // Tell replicas to commit
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = connections[i]->Commit(&context, request, &reply);
                }

                // Add to storage
                int createAccountStatus = createAccount(username, password);

                std::cout << "this is create account status " << std::to_string(createAccountStatus) << std::endl; 

                // Update error messages and reply based on account creation status
                if (createAccountStatus == 1) {
                    std::string errorMsg = "Username '" + create_account_message->username() + "' already exists.";
                    server_reply->set_errormsg(errorMsg);
                    server_reply->set_createaccountsuccess(false);
                } else {
                    server_reply->set_createaccountsuccess(true);
                }



            } 

            return Status::OK;
        }


        Status Login(ServerContext* context, const LoginMessage* login_message, LoginReply* server_reply) {
            std::string username = login_message->username();
            std::string password = login_message->password();
            writeToLogs(pendingLogWriter, LOGIN, username, g_nullString, password);

            // If master, talk to replicas, if not master just return ok after writing to pending
            if (leaderVals.isLeader) {
                std::vector<int> droppedConnections;

                // Get consensus 
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    LoginReply reply;
                    Status status = connections[i]->Login(&context, *login_message, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(i);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    connections.erase(connections.begin() + droppedConnections[i]);
                }

                // Commit if you get consensus
                writeToLogs(commitLogWriter, LOGIN, username, g_nullString, password);

                // Tell replicas to commit
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = connections[i]->Commit(&context, request, &reply);
                }

                // Add to storage
                // Check for existing user and verify password

                int loginStatus = login(username, password);
                
                if (loginStatus == 0) {
                    server_reply->set_loginsuccess(true);
                } else {
                    server_reply->set_loginsuccess(false);
                    server_reply->set_errormsg("Incorrect username or password.");
                }

            } 

            return Status::OK;
        }


        Status Logout(ServerContext* context, const LogoutMessage* logout_message, LogoutReply* server_reply) {
            writeToLogs(pendingLogWriter, LOGOUT, logout_message->username());

            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                
                std::vector<int> droppedConnections;

                // Get consensus 
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    LogoutReply reply;
                    Status status = connections[i]->Logout(&context, *logout_message, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(i);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    connections.erase(connections.begin() + droppedConnections[i]);
                }

                // Commit if you get consensus
                writeToLogs(commitLogWriter, LOGOUT, logout_message->username());


                // Tell replicas to commit
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = connections[i]->Commit(&context, request, &reply);
                }

                // Add to storage
                int logoutStatus = logout(logout_message->username());

            } 

            return Status::OK;
        }


        Status ListUsers(ServerContext* context, const QueryUsersMessage* query, ServerWriter<User>* writer) {
            std::string prefix = query->username();
            std::vector<std::string> usernames;
            userTrie_mutex.lock();
            try {
                usernames = userTrie.returnUsersWithPrefix(prefix);
            } catch (std::runtime_error &e) {
                std::cout << e.what() << std::endl;
                usernames = {};
            }
            userTrie_mutex.unlock();

            for (std::string username : usernames) {
                User user;
                user.set_username(username);
                writer->Write(user);
            }
            return Status::OK;
        }


        Status SendMessage(ServerContext* context, const ChatMessage* msg, SendMessageReply* server_reply) {
            std::string senderUsername = msg->senderusername();
            std::string recipientUsername = msg->recipientusername();
            std::string messageContent = msg->msgcontent();

            writeToLogs(pendingLogWriter, SEND_MESSAGE, senderUsername, recipientUsername, g_nullString, messageContent);
             
            // check if master, talk to replicas
            if (leaderVals.isLeader) {

                std::vector<int> droppedConnections;

                // Get consensus 
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    SendMessageReply reply;
                    Status status = connections[i]->SendMessage(&context, *msg, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(i);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    connections.erase(connections.begin() + droppedConnections[i]);
                }

                // Commit if you get consensus
            
                writeToLogs(commitLogWriter, SEND_MESSAGE, senderUsername, recipientUsername, g_nullString, messageContent);


                // Tell replicas to commit
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = connections[i]->Commit(&context, request, &reply);
                }

                // Add to storage
                int sendMessageStatus = sendMessage(senderUsername, recipientUsername, messageContent);

                if (sendMessageStatus == 0) {
                    server_reply->set_messagesent(true);
                } else {
                    std::string errormsg = "Tried to send a message to a user that doesn't exist '" + msg->recipientusername() + "'";
                    server_reply->set_errormsg(errormsg);
                }

            } 

            return Status::OK;
        }


        Status QueryNotifications(ServerContext* context, const QueryNotificationsMessage* query, 
                                ServerWriter<Notification>* writer) {

            std::string clientUsername = query->user();
            std::vector<std::pair<char [g_UsernameLimit], char> > notifications = conversationsDictionary.getNotifications(clientUsername);
            
            for (auto notification : notifications) {
                std::cout << "Username: " << notification.first << ", " << std::to_string(notification.second) << " notifications" << std::endl;
                Notification note;
                note.set_numberofnotifications(notification.second);
                note.set_user(notification.first);
                writer->Write(note);
            }
            return Status::OK;
        }


        Status QueryMessages(ServerContext* context, const QueryMessagesMessage* query, 
                            ServerWriter<ChatMessage>* writer) {
            writeToLogs(pendingLogWriter, QUERY_MESSAGES, query->clientusername(), query->otherusername());

            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                
                std::vector<int> droppedConnections;

                // Get consensus 
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    std::unique_ptr<ClientReader<ChatMessage>> reader(connections[i]->QueryMessages(&context, *query));
                    Status status = reader->Finish();
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(i);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    connections.erase(connections.begin() + droppedConnections[i]);
                }

                // Commit if you get consensus
               
                writeToLogs(commitLogWriter, QUERY_MESSAGES, query->clientusername(), query->otherusername());


                // Tell replicas to commit
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = connections[i]->Commit(&context, request, &reply);
                }

                // Add to storage
                std::cout << "Getting messages between '" << query->clientusername() << "' and '"<< query->otherusername() << "'" << std::endl;

                std::vector<ChatMessage> queryMessagesMessageList = queryMessages(query->clientusername(), query->otherusername());

                for (auto message : queryMessagesMessageList) {
                    writer->Write(message);
                }
            } 

            return Status::OK;
        }

        Status DeleteAccount(ServerContext* context, const DeleteAccountMessage* delete_account_message,
                            DeleteAccountReply* server_reply) {
            writeToLogs(pendingLogWriter, DELETE_ACCOUNT, delete_account_message->username());
            
            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                
                std::vector<int> droppedConnections;

                // Get consensus 
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    DeleteAccountReply reply;
                    Status status = connections[i]->DeleteAccount(&context, *delete_account_message, &reply); 
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(i);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    connections.erase(connections.begin() + droppedConnections[i]);
                }

                // Commit if you get consensus
                writeToLogs(commitLogWriter, DELETE_ACCOUNT, delete_account_message->username());

                // Tell replicas to commit
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = connections[i]->Commit(&context, request, &reply);
                }

                // Add to storage
               std::cout << "Deleting account of '" << delete_account_message->username() << "'" << std::endl;
                // Flag user account as deleted in trie
                int deleteAccountStatus = deleteAccount(delete_account_message->username());

                if (deleteAccountStatus == 1) {
                    server_reply->set_deletedaccount(false);
                } else {
                    server_reply->set_deletedaccount(true);
                }

            }
            
            return Status::OK;
        }


        Status MessagesSeen(ServerContext* context, const MessagesSeenMessage* msg, MessagesSeenReply* reply) {
            writeToLogs(pendingLogWriter, MESSAGES_SEEN, msg->clientusername(), msg->otherusername(), g_nullString, g_nullString, std::to_string(msg->messagesseen()));

            // TODO: check if master, talk to replicas
            if (leaderVals.isLeader) {
                
                std::vector<int> droppedConnections;
                
                // Get consensus 
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    MessagesSeenReply reply;
                    Status status = connections[i]->MessagesSeen(&context, *msg, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(i);
                    }
                }
                
                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    connections.erase(connections.begin() + droppedConnections[i]);
                }

                // Commit if you get consensus
                writeToLogs(commitLogWriter, MESSAGES_SEEN, msg->clientusername(), msg->otherusername(), g_nullString, g_nullString, std::to_string(msg->messagesseen()));


                // Tell replicas to commit
                for (int i = 0; i < connections.size(); i++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = connections[i]->Commit(&context, request, &reply);
                }

                // Add to storage
                int messagesSeenStatus = messagesSeen(msg->clientusername(), msg->otherusername(), msg->messagesseen());

            } 

            return Status::OK;
        }

        Status RefreshClient(ServerContext* context, const RefreshRequest* request, RefreshResponse* reply) {
            std::cout << "Refreshing for " << request->clientusername() << std::endl;
            if (queuedOperationsDictionary.find(request->clientusername()) != queuedOperationsDictionary.end()) {
                std::cout << "Running queued operations for '" << request->clientusername() << "'" << std::endl;
                for (Notification note : queuedOperationsDictionary[request->clientusername()]) {
                    Notification* n = reply->add_notifications();
                    n->set_user(note.user());
                }

                queuedOperationsDictionary.erase(request->clientusername());
            }
            return Status::OK;
        }

        Status Commit(ServerContext* context, const CommitRequest* request, CommitResponse* reply) {
            std::string line;
            
            // Add last pending log to committed log
            getline(pendingFile, line);

            commitLogWriter <<  line;

            return Status::OK;

        }

        Status HeartBeat(ServerContext* context, const HeartBeatRequest* request, HeartBeatResponse* reply) {
            leaderMutex.lock();
            reply->set_isleader(leaderVals.isLeader);
            leaderMutex.unlock();
            return Status::OK;
        }

        Status ProposeLeaderElection(ServerContext* context, const LeaderElectionProposal* request, LeaderElectionProposalResponse reply) {
            // TODO: implement leader election proposal RPC
            // Check if I am the leader or if leaderIdx != -1, otherwise we have no leader
            leaderMutex.lock();
            if (leaderVals.leaderidx != -1 || leaderVals.isLeader) {
                reply.set_accept(false);
                reply.set_leader(leaderVals.leaderAddress);
            }
            else {
                reply.set_accept(true);
            }
            leaderMutex.unlock();

            return Status::OK;
        }

        Status LeaderElection(ServerContext* context, const CandidateValue* request, LeaderElectionResponse* reply) {
            // Update leader candidate values
            leaderElectionValuesMutex.lock();
            electionVals.numberOfCandidatesReceived++;
            if (request->number() > electionVals.maxLeaderElectionVal) {
                electionVals.maxLeaderElectionVal = request->number();
                electionVals.currLeaderCandidateAddr = request->address();
            }
            // tie breaker, choose candidate with address that is lexicographically larger
            else if (request->number() == electionVals.maxLeaderElectionVal) {
                if (electionVals.currLeaderCandidateAddr.compare(request->address()) < 0) {
                    return Status::OK;
                }
                else {
                    electionVals.currLeaderCandidateAddr = request->address();
                }
            }
            leaderElectionValuesMutex.unlock();
            return Status::OK;
        }


        // For interserver communication stuff
        void addConnection(std::string server_address) {
            // TODO: do we want to put this in a loop to keep trying until it works?
            auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
            std::unique_ptr<ChatService::Stub> stub_ = ChatService::NewStub(channel);
            connections.push_back(std::move(stub_));
            addressToConnectionIdx[server_address] = connections.size()-1;
        }

        bool heartbeat() {
            std::cout << "Sending heartbeat" << std::endl;
            leaderMutex.lock();
            if (leaderVals.leaderidx != -1) {
                ClientContext context;
                HeartBeatRequest message;
                HeartBeatResponse reponse;
                Status status = connections[leaderVals.leaderidx]->HeartBeat(&context, message, &reponse);

                if (status.ok()) {
                    return true;
                }
                else {
                    // Remove leader from connections vector and from addr_to_idx dict
                    connections.erase(connections.begin()+leaderVals.leaderidx);
                    addressToConnectionIdx.erase(leaderVals.leaderAddress);
                    return false;
                }
            }
            else {
                std::cout << "No leader index" << std::endl;
                return false;
            }
            leaderMutex.unlock();
        }

        bool proposeLeaderElection() {
            // TODO: call propose leader election for each stub in vector
            std::cout << "Proposing leader election" << std::endl;
            LeaderElectionProposal message;

            for (int i = 0; i < connections.size(); i++) {
                ClientContext context;
                LeaderElectionProposalResponse reply;
                Status status = connections[i]->ProposeLeaderElection(&context, message, &reply);
                if (status.ok()) {
                    if (!reply.accept()) {
                        return false;
                    }
                }
                else {
                    // TODO: might want to throw an exception here instead but we know how
                    //      much Carolyn loves exceptions
                    return false;
                }
            }

            std::cout << "Leader eletion proposal accepted" << std::endl;

            return true;
        }

        void leaderElection() {
            std::cout << "Carrying out leader election" << std::endl;
            int candidateValue = rand();

            leaderElectionValuesMutex.lock();
            electionVals.currLeaderCandidateAddr = myAddress;
            electionVals.maxLeaderElectionVal = candidateValue;
            leaderElectionValuesMutex.unlock();

            CandidateValue message;
            message.set_number(candidateValue);

            // send election value to all other servers
            for (int i = 0; i < connections.size(); i++) {
                ClientContext context;
                LeaderElectionResponse reply;
                Status status =  connections[i]->LeaderElection(&context, message, &reply);
            }

            // wait until we've received leader election values of all other servers
            // TODO: we should handle what happens if one of the replicas goes down while waiting for
            //  leader election. If we don't then we'll wait here forever as we'll never receive all
            //  responses
            bool waitingForLeaderElection = true;
            while (waitingForLeaderElection) {
                leaderElectionValuesMutex.lock();
                if (electionVals.numberOfCandidatesReceived >= connections.size()) {
                    waitingForLeaderElection = false;
                }
                leaderElectionValuesMutex.unlock();
            }

            // select new leader
            leaderMutex.lock();
            if (electionVals.currLeaderCandidateAddr == myAddress) {
                leaderVals.isLeader = true;
                leaderVals.leaderidx = -1;
            } 
            else {
                leaderVals.leaderAddress = electionVals.currLeaderCandidateAddr;
                leaderVals.leaderidx = addressToConnectionIdx[leaderVals.leaderAddress];
                leaderVals.isLeader = false;
            }
            leaderMutex.unlock();

            std::cout << "New leader is " << leaderVals.leaderAddress << std::endl;

            leaderElectionValuesMutex.lock();
            electionVals.currLeaderCandidateAddr = "";
            electionVals.maxLeaderElectionVal = -1;
            electionVals.numberOfCandidatesReceived = 0;
            leaderElectionValuesMutex.unlock();
        }

        bool isLeader() {
            leaderMutex.lock();
            bool toReturn = leaderVals.isLeader;
            leaderMutex.unlock();
            
            return toReturn;
        }

        void setAsLeader() {
            leaderMutex.lock();
            leaderVals.isLeader = true;
            leaderMutex.unlock();
        }

        void setNotLeader() {
            leaderMutex.lock();
            leaderVals.isLeader = false;
            leaderMutex.unlock();
        }
};


// open log file for server
ChatServiceImpl g_Service(g_pendingLogFile, g_committedLogFile);