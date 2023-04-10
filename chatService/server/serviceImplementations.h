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
using chatservice::PendingLogRequest;
using chatservice::Operation;
// Replies
using chatservice::CreateAccountReply;
using chatservice::LoginReply;
using chatservice::LogoutReply;
using chatservice::User;
using chatservice::SendMessageReply;
using chatservice::Notification;
using chatservice::DeleteAccountReply;
using chatservice::RefreshRequest;
using chatservice::RefreshResponse;
using chatservice::MessagesSeenReply;
using chatservice::HeartBeatResponse;
using chatservice::LeaderElectionProposalResponse;
using chatservice::LeaderElectionResponse;
using chatservice::CommitResponse;
using chatservice::AddToPendingResponse;

bool g_startingUp = true;

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
        std::string pendingFilename;
        std::string commitFilename;

        // for reading logs
        std::ifstream pendingLogReader;

        // For interserver communication
        std::mutex connectionMutex;
        std::unordered_map<std::string, std::unique_ptr<ChatService::Stub>> addressToStub;
        std::string myAddress;

        // Commented out the global versions in storage.h
        std::mutex leaderMutex;
        LeaderValues leaderVals;

        std::mutex leaderElectionValuesMutex;
        ElectionValues electionVals;

        // logical clock
        int clockVal;

    public:
        explicit ChatServiceImpl() {}

        void initialize(std::string addr) {
            myAddress = addr;
            std::cout << "My address is " << myAddress << std::endl;

            pendingFilename = g_pendingLogFile + addr + ".csv";
            commitFilename = g_committedLogFile + addr + ".csv";
            
            // open CSV files in append mode
            pendingLogWriter.open(pendingFilename, std::fstream::app);
            commitLogWriter.open(commitFilename, std::fstream::app);

            // open pendingLogFile in read mode
            pendingLogReader.open(pendingFilename, std::fstream::in);

            // get to the end of the file
            pendingLogReader.seekg(0, pendingLogReader.end);
            if (pendingLogReader.tellg() == 0) {
                addFields();
            }

            clockVal = 0;

        }

        // Getter for pending filename
        std::string getPendingFilename() {
            return pendingFilename;
        }

        // Getter for commit filename
        std::string getCommitFilename() {
            return commitFilename;
        }

        // Called when CSV file was found to be empty (i.e. a new log file) so we define the CSV fields
        void addFields() {
            pendingLogWriter << g_csvFields << std::endl;
            commitLogWriter << g_csvFields << std::endl;
        }

        // Getter for number of connections
        int numberOfConnections() {
            int numberOfConnections;
            connectionMutex.lock();
            numberOfConnections = addressToStub.size();
            connectionMutex.unlock();

            return numberOfConnections;            
        }

        // CreateAccount RPC implementation
        Status CreateAccount(ServerContext* context, const CreateAccountMessage* create_account_message, 
                            CreateAccountReply* server_reply) {

            std::string username = create_account_message->username();
            std::string password = create_account_message->password();

            // Update clock value and write to pending if the message was from the leader
            if (create_account_message->fromleader()) {
                clockVal = std::max(create_account_message->clockval(), clockVal);
                writeToLogs(pendingLogWriter, CREATE_ACCOUNT, username, g_nullString, password, g_nullString, g_nullString, g_nullString, clockVal);
            } 
            
             // If master, talk to replicas, if not master just return ok after writing to pending
            if (leaderVals.isLeader) {
                clockVal++;
                writeToLogs(pendingLogWriter, CREATE_ACCOUNT, username, g_nullString, password, g_nullString, g_nullString, g_nullString, clockVal);
                std::vector<std::string> droppedConnections;

                // Tell replicas to write to pending
                connectionMutex.lock();
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CreateAccountReply reply;
                    CreateAccountMessage new_msg;
                    new_msg.set_fromleader(true);
                    new_msg.set_password(create_account_message->password());
                    new_msg.set_username(create_account_message->username());
                    new_msg.set_clockval(clockVal);
                    Status status = it->second->CreateAccount(&context, new_msg, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(it->first);
                    }
                }

                // Remove dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    addressToStub.erase(droppedConnections[i]);
                }

                // Move last pending line to commit 
                moveToCommit(pendingFilename, commitLogWriter);

                // Tell replicas to commit
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = it->second->Commit(&context, request, &reply);
                }
                connectionMutex.unlock();

                // Add to storage
                int createAccountStatus = tryCreateAccount(username, password);


                // Update error messages and reply based on account creation status
                if (createAccountStatus == 1) {
                    std::string errorMsg = "Username '" + create_account_message->username() + "' already exists.";
                    server_reply->set_errormsg(errorMsg);
                    server_reply->set_createaccountsuccess(false);
                } else {
                    server_reply->set_createaccountsuccess(true);
                }

            } else {
                server_reply->set_leader(leaderVals.leaderAddress);
            }

            return Status::OK;
        }

        // Login RPC implementation
        Status Login(ServerContext* context, const LoginMessage* login_message, LoginReply* server_reply) {
            std::string username = login_message->username();
            std::string password = login_message->password();

            // Update clock value and write to pending if the message was from the leader
            if (login_message->fromleader()) {
                clockVal = std::max(login_message->clockval(), clockVal);
                writeToLogs(pendingLogWriter, LOGIN, username, g_nullString, password, g_nullString, g_nullString, g_nullString, clockVal);
            }

            // If master, talk to replicas, if not master just return ok after writing to pending
            if (leaderVals.isLeader) {
                clockVal++;
                writeToLogs(pendingLogWriter, LOGIN, username, g_nullString, password, g_nullString, g_nullString, g_nullString, clockVal);
                std::vector<std::string> droppedConnections;

                // Get consensus 
                connectionMutex.lock();
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    LoginReply reply;
                    LoginMessage new_msg;
                    new_msg.set_fromleader(true);
                    new_msg.set_password(login_message->password());
                    new_msg.set_username(login_message->username());
                    new_msg.set_clockval(clockVal);
                    Status status = it->second->Login(&context, new_msg, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(it->first);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    addressToStub.erase(droppedConnections[i]);
                }

                // Move last pending line to commit 
                moveToCommit(pendingFilename, commitLogWriter);

                // Tell replicas to commit
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = it->second->Commit(&context, request, &reply);
                }

                connectionMutex.unlock();

                // Add to storage
                int loginStatus = tryLogin(username, password);
                
                if (loginStatus == 0) {
                    server_reply->set_loginsuccess(true);
                } else {
                    server_reply->set_loginsuccess(false);
                    server_reply->set_errormsg("Incorrect username or password.");
                }

            } else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                server_reply->set_leader(leaderVals.leaderAddress);
            } else {
                // if there is no leader, election is going on
                server_reply->set_leader(g_ElectionString);
            }

            return Status::OK;
        }

        // Logout RPC implementation
        Status Logout(ServerContext* context, const LogoutMessage* logout_message, LogoutReply* server_reply) {
            // Update clock value and write to pending if the message was from the leader
            if (logout_message->fromleader()) {
                clockVal = std::max(logout_message->clockval(), clockVal);
                writeToLogs(pendingLogWriter, LOGOUT, logout_message->username(), g_nullString, g_nullString, g_nullString, g_nullString, g_nullString, clockVal);
            }
            
            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                clockVal++;
                writeToLogs(pendingLogWriter, LOGOUT, logout_message->username(), g_nullString, g_nullString, g_nullString, g_nullString, g_nullString, clockVal);
                std::vector<std::string> droppedConnections;

                // Get consensus 
                connectionMutex.lock();
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    LogoutReply reply;
                    LogoutMessage new_msg;
                    new_msg.set_fromleader(true);
                    new_msg.set_username(logout_message->username());
                    new_msg.set_clockval(clockVal);
                    Status status = it->second->Logout(&context, new_msg, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(it->first);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    addressToStub.erase(droppedConnections[i]);
                }

                // Move last pending line to commit 
                moveToCommit(pendingFilename, commitLogWriter);

                // Tell replicas to commit
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = it->second->Commit(&context, request, &reply);
                }

                connectionMutex.unlock();

                // Add to storage
                int logoutStatus = tryLogout(logout_message->username());

            } else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                server_reply->set_leader(leaderVals.leaderAddress);
            } else {
                // if there is no leader, election is going on
                server_reply->set_leader(g_ElectionString);
            }

            return Status::OK;
        }

        // ListUsers RPC implementation
        Status ListUsers(ServerContext* context, const QueryUsersMessage* query, ServerWriter<User>* writer) {
            if (leaderVals.isLeader) {
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
            }  else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                User user;
                user.set_leader(leaderVals.leaderAddress);
                writer->Write(user);
            } else {
                // if there is no leader, election is going on
                User user;
                user.set_leader(g_ElectionString);
                writer->Write(user);
            }


            return Status::OK;
        }

        // SendMessage RPC implementation
        Status SendMessage(ServerContext* context, const ChatMessage* msg, SendMessageReply* server_reply) {
            std::string senderUsername = msg->senderusername();
            std::string recipientUsername = msg->recipientusername();
            std::string messageContent = msg->msgcontent();

            // Update clock value and write to pending if the message was from the leader
            if (msg->fromleader()) {
                clockVal = std::max(msg->clockval(), clockVal);
                writeToLogs(pendingLogWriter, SEND_MESSAGE, senderUsername, recipientUsername, g_nullString, messageContent, g_nullString, g_nullString, clockVal);
            }
             
            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                clockVal++;
                writeToLogs(pendingLogWriter, SEND_MESSAGE, senderUsername, recipientUsername, g_nullString, messageContent, g_nullString, g_nullString, clockVal);
                std::vector<std::string> droppedConnections;

                // Get consensus 
                connectionMutex.lock();
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    SendMessageReply reply;
                    ChatMessage new_msg;
                    new_msg.set_msgcontent(msg->msgcontent());
                    new_msg.set_senderusername(msg->senderusername());
                    new_msg.set_recipientusername(msg->recipientusername());
                    new_msg.set_leader(msg->leader());
                    new_msg.set_fromleader(true);
                    new_msg.set_clockval(clockVal);
                    Status status = it->second->SendMessage(&context, new_msg, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(it->first);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    addressToStub.erase(droppedConnections[i]);
                }

                // Move last pending line to commit 
                moveToCommit(pendingFilename, commitLogWriter);


                // Tell replicas to commit
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = it->second->Commit(&context, request, &reply);
                }
                
                connectionMutex.unlock();

                // Add to storage
                int sendMessageStatus = trySendMessage(senderUsername, recipientUsername, messageContent);

                if (sendMessageStatus == 0) {
                    server_reply->set_messagesent(true);
                } else {
                    std::string errormsg = "Tried to send a message to a user that doesn't exist '" + msg->recipientusername() + "'";
                    server_reply->set_errormsg(errormsg);
                }

            } else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                server_reply->set_leader(leaderVals.leaderAddress);
            } else {
                // if there is no leader, election is going on
                server_reply->set_leader(g_ElectionString);
            }

            return Status::OK;
        }

        // QUeryNotifications RPC implementation
        Status QueryNotifications(ServerContext* context, const QueryNotificationsMessage* query, 
                                ServerWriter<Notification>* writer) {
            // Update clock value and write to pending if the message was from the leader
            if (leaderVals.isLeader) {
                std::string clientUsername = query->user();
                std::vector<std::pair<char [g_UsernameLimit], char> > notifications = conversationsDictionary.getNotifications(clientUsername);
                
                for (auto notification : notifications) {
                    std::cout << "Username: " << notification.first << ", " << std::to_string(notification.second) << " notifications" << std::endl;
                    Notification note;
                    note.set_numberofnotifications(notification.second);
                    note.set_user(notification.first);
                    writer->Write(note);
                }
            } else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                Notification note;
                note.set_leader(leaderVals.leaderAddress);
                writer->Write(note);
            } else {
                // if there is no leader, election is going on
                Notification note;
                note.set_leader(g_ElectionString);
                writer->Write(note);
            }

            return Status::OK;
        }

        // QueryMessages RPC implemetation
        Status QueryMessages(ServerContext* context, const QueryMessagesMessage* query, 
                            ServerWriter<ChatMessage>* writer) {
            // Update clock value and write to pending if the message was from the leader
            if (query->fromleader()) {
                clockVal = std::max(query->clockval(), clockVal);
                writeToLogs(pendingLogWriter, QUERY_MESSAGES, query->clientusername(), query->otherusername(), g_nullString, g_nullString, g_nullString, g_nullString, clockVal);
            }

            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                clockVal++;
                writeToLogs(pendingLogWriter, QUERY_MESSAGES, query->clientusername(), query->otherusername(), g_nullString, g_nullString, g_nullString, g_nullString, clockVal);
                std::vector<std::string> droppedConnections;

                // Get consensus 
                connectionMutex.lock();
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    QueryMessagesMessage new_msg;
                    new_msg.set_clientusername(query->clientusername());
                    new_msg.set_otherusername(query->otherusername());
                    new_msg.set_fromleader(true);
                    new_msg.set_clockval(clockVal);
                    std::unique_ptr<ClientReader<ChatMessage>> reader(it->second->QueryMessages(&context, new_msg));
                    Status status = reader->Finish();
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(it->first);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    std::cout << "Erasing connection to " << droppedConnections[i] << std::endl;
                    addressToStub.erase(droppedConnections[i]);
                }
               
               // Move last pending line to commit 
                moveToCommit(pendingFilename, commitLogWriter);


                // Tell replicas to commit
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = it->second->Commit(&context, request, &reply);
                }

                connectionMutex.unlock();

                // Add to storage
                std::cout << "Getting messages between '" << query->clientusername() << "' and '"<< query->otherusername() << "'" << std::endl;

                std::vector<ChatMessage> queryMessagesMessageList = tryQueryMessages(query->clientusername(), query->otherusername());

                for (auto message : queryMessagesMessageList) {
                    writer->Write(message);
                }
            } else if (leaderVals.leaderidx != -1 && !query->fromleader()) {
                // If there is a leader, but it's not me
                ChatMessage message;
                message.set_leader(leaderVals.leaderAddress);
                writer->Write(message);
            } else if (!query->fromleader()) {
                // if there is no leader, election is going on
                ChatMessage message;
                message.set_leader(g_ElectionString);
                writer->Write(message);
            }
            return Status::OK;
        }

        // DeleteAccount RPC implementation
        Status DeleteAccount(ServerContext* context, const DeleteAccountMessage* delete_account_message,
                            DeleteAccountReply* server_reply) {

            // Update clock value and write to pending if the message was from the leader
            if (delete_account_message->fromleader()) {
                clockVal = std::max(delete_account_message->clockval(), clockVal);
                writeToLogs(pendingLogWriter, DELETE_ACCOUNT, delete_account_message->username(), g_nullString, g_nullString, g_nullString, g_nullString, g_nullString, clockVal);
            }
            
            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                clockVal++;
                writeToLogs(pendingLogWriter, DELETE_ACCOUNT, delete_account_message->username(), g_nullString, g_nullString, g_nullString, g_nullString, g_nullString, clockVal);
                std::vector<std::string> droppedConnections;

                // Get consensus 
                connectionMutex.lock();
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    DeleteAccountReply reply;
                    
                    DeleteAccountMessage new_msg;
                    new_msg.set_username(delete_account_message->username());
                    new_msg.set_password(delete_account_message->password());
                    new_msg.set_fromleader(true);
                    new_msg.set_clockval(clockVal);
                    Status status = it->second->DeleteAccount(&context, new_msg, &reply); 
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(it->first);
                    }
                }

                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    addressToStub.erase(droppedConnections[i]);
                }

                // Move last pending line to commit 
                moveToCommit(pendingFilename, commitLogWriter);

                // Tell replicas to commit
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = it->second->Commit(&context, request, &reply);
                }

                connectionMutex.unlock();

                // Add to storage
               std::cout << "Deleting account of '" << delete_account_message->username() << "'" << std::endl;
                // Flag user account as deleted in trie
                int deleteAccountStatus = tryDeleteAccount(delete_account_message->username());

                if (deleteAccountStatus == 1) {
                    server_reply->set_deletedaccount(false);
                } else {
                    server_reply->set_deletedaccount(true);
                }

            } else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                server_reply->set_leader(leaderVals.leaderAddress);
            } else {
                // if there is no leader, election is going on
                server_reply->set_leader(g_ElectionString);
            }
            
            return Status::OK;
        }

        // MessagesSeen RPC implementation
        Status MessagesSeen(ServerContext* context, const MessagesSeenMessage* msg, MessagesSeenReply* reply) {
            // Update clock value and write to pending if the message was from the leader
            if (msg->fromleader()) {
                clockVal = std::max(msg->clockval(), clockVal);
                writeToLogs(pendingLogWriter, MESSAGES_SEEN, msg->clientusername(), msg->otherusername(), g_nullString, g_nullString, std::to_string(msg->messagesseen()), g_nullString, clockVal);
            }

            // check if master, talk to replicas
            if (leaderVals.isLeader) {
                clockVal++;
                writeToLogs(pendingLogWriter, MESSAGES_SEEN, msg->clientusername(), msg->otherusername(), g_nullString, g_nullString, std::to_string(msg->messagesseen()), g_nullString, clockVal);
                std::vector<std::string> droppedConnections;
                
                // Get consensus 
                connectionMutex.lock();
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    MessagesSeenReply reply;

                    MessagesSeenMessage new_msg;
                    new_msg.set_clientusername(msg->clientusername());
                    new_msg.set_otherusername(msg->otherusername());
                    new_msg.set_messagesseen(msg->messagesseen());
                    new_msg.set_fromleader(true);  
                    new_msg.set_clockval(clockVal);
                    Status status = it->second->MessagesSeen(&context, new_msg, &reply);
                    
                    if (!status.ok()) {
                        droppedConnections.push_back(it->first);
                    }
                }
                
                // Removing dropped connections
                for (int i = 0; i < droppedConnections.size(); i++) {
                    addressToStub.erase(droppedConnections[i]);
                }

                // Move last pending line to commit 
                moveToCommit(pendingFilename, commitLogWriter);


                // Tell replicas to commit
                for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                    ClientContext context;
                    CommitRequest request;
                    CommitResponse reply;
                    Status status = it->second->Commit(&context, request, &reply);
                }

                connectionMutex.unlock();

                // Add to storage
                int messagesSeenStatus = tryMessagesSeen(msg->clientusername(), msg->otherusername(), msg->messagesseen());

            } else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                reply->set_leader(leaderVals.leaderAddress);
            } else {
                // if there is no leader, election is going on
                reply->set_leader(g_ElectionString);
            }

            return Status::OK;
        }

        Status RefreshClient(ServerContext* context, const RefreshRequest* request, RefreshResponse* reply) {
            std::cout << "Refreshing for " << request->clientusername() << std::endl;
            // Update clock value and write to pending if the message was from the leader
            if (leaderVals.isLeader) {
                if (queuedOperationsDictionary.find(request->clientusername()) != queuedOperationsDictionary.end()) {
                    std::cout << "Running queued operations for '" << request->clientusername() << "'" << std::endl;
                    for (Notification note : queuedOperationsDictionary[request->clientusername()]) {
                        Notification* n = reply->add_notifications();
                        n->set_user(note.user());
                    }

                    queuedOperationsDictionary.erase(request->clientusername());
                }
            } else if (leaderVals.leaderidx != -1) {
                // If there is a leader, but it's not me
                reply->set_leader(leaderVals.leaderAddress);
            } else {
                // if there is no leader, election is going on
                reply->set_leader(g_ElectionString);
            }
            return Status::OK;
        }

        // Commit RPC implementation
        Status Commit(ServerContext* context, const CommitRequest* request, CommitResponse* reply) {

            std::vector<std::string> operationToCommit = moveToCommit(pendingFilename, commitLogWriter);

            parseLine(operationToCommit);

            return Status::OK;

        }

        // Heartbeat RPC implementation
        Status HeartBeat(ServerContext* context, const HeartBeatRequest* request, HeartBeatResponse* reply) {
            leaderMutex.lock();
            reply->set_isleader(leaderVals.isLeader);
            leaderMutex.unlock();
            return Status::OK;
        }

        // SuggestLeaderELection RPC implementation
        Status SuggestLeaderElection(ServerContext* context, const LeaderElectionProposal* request, LeaderElectionProposalResponse* reply) {
            // Check if I am the leader or if leaderIdx != -1, otherwise we have no leader
            leaderMutex.lock();
            if (leaderVals.leaderidx != -1 || leaderVals.isLeader) {
                std::cout << "We have a leader, reject leader election" << std::endl;
                reply->set_accept(false);
                reply->set_leader(leaderVals.leaderAddress);
            }
            else {
                std::cout << "Accept leader election" << std::endl;
                reply->set_accept(true);
            }
            leaderMutex.unlock();

            return Status::OK;
        }

        // LeaderElection RPC implementation
        Status LeaderElection(ServerContext* context, const CandidateValue* request, LeaderElectionResponse* reply) {
            // Update leader candidate values
            leaderElectionValuesMutex.lock();
            electionVals.numberOfCandidatesReceived++;
            std::cout << "Current winning value: " << std::to_string(electionVals.maxLeaderElectionVal) << std::endl;
            std::cout << "New value:" << std::to_string(request->number()) << std::endl;
            if (request->number() > electionVals.maxLeaderElectionVal) {
                electionVals.maxLeaderElectionVal = request->number();
                electionVals.currLeaderCandidateAddr = request->address();
            }
            // tie breaker, choose candidate with address that is lexicographically larger
            else if (request->number() == electionVals.maxLeaderElectionVal) {
                if (electionVals.currLeaderCandidateAddr.compare(request->address()) > 0) {
                    electionVals.currLeaderCandidateAddr = request->address();
                }
            }
            leaderElectionValuesMutex.unlock();
            return Status::OK;
        }

        // Write stream of operations to logs
        Status AddToPending(ServerContext* context, ServerReader<Operation>* reader, AddToPendingResponse* response) {
            // delete your own commit logs
            std::cout << "Adding stuff to pending" << std::endl;

            Operation op;
            while (reader->Read(&op)) {
                writeToLogs(commitLogWriter, std::stoi(op.message_type()), op.username1(), op.username2(),
                        op.password(), op.message_content(), op.messagesseen(), op.leader(), std::stoi(op.clockval()));

                std::vector<std::string> row{op.message_type(), op.username1(), op.username2(), op.password(),
                                             op.message_content(), op.messagesseen(), op.leader(), op.clockval()};
                clockVal = std::stoi(op.clockval());
                parseLine(row);
            }
            
            return Status::OK;
        }

        // RequestPendingLog RPC implementation
        Status RequestPendingLog(ServerContext* context, const PendingLogRequest* request, 
                                ServerWriter<Operation>* writer) {
            std::vector<std::vector<std::string>> vectorizedLines;
            std::cout << "Pending logs were requested, reading file" << std::endl;
            readFile(&vectorizedLines, pendingFilename);
            for (int idx = 1; idx < vectorizedLines.size(); idx++) {
                std::vector<std::string> line = vectorizedLines[idx];
                Operation op;
                op.set_clockval(line[7]);
                op.set_message_type(line[0]);
                op.set_username1(line[1]);
                op.set_username2(line[2]);
                op.set_password(line[3]);
                op.set_message_content(line[4]);
                op.set_messagesseen(line[5]);
                op.set_leader(line[6]);
                writer->Write(op);
            }
            
            return Status::OK;
        }

        // For interserver communication stuff
        void addConnection(std::string server_address) {
            auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
            std::unique_ptr<ChatService::Stub> stub_ = ChatService::NewStub(channel);
            std::cout << "Adding connection to " << server_address << std::endl;
            addressToStub[server_address] = std::move(stub_);
        }

        // Heartbeat message from replicas to leader
        bool heartbeat() {
            leaderMutex.lock();
            if (leaderVals.leaderidx != -1) {
                ClientContext context;
                HeartBeatRequest message;
                HeartBeatResponse reponse;
                Status status = addressToStub[leaderVals.leaderAddress]->HeartBeat(&context, message, &reponse);

                if (status.ok()) {
                    leaderMutex.unlock();
                    return true;
                }
                else {
                    // Remove leader from connections vector and from addr_to_idx dict
                    addressToStub.erase(leaderVals.leaderAddress);
                    leaderVals.leaderidx = -1;
                    leaderVals.leaderAddress = "";
                    leaderMutex.unlock();
                    return false;
                }
            }
            else {
                std::cout << "No leader index" << std::endl;
                leaderMutex.unlock();
                return false;
            }
        }

        // Proposes leader election
        bool proposeLeaderElection() {
            std::cout << "Proposing leader election" << std::endl;
            LeaderElectionProposal message;

            for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                ClientContext context;
                LeaderElectionProposalResponse reply;
                std::cout << "Sending proposal to connection " << it->first << std::endl;
                Status status = it->second->SuggestLeaderElection(&context, message, &reply);
                if (status.ok()) {
                    if (!reply.accept()) {
                        std::cout << "Election was rejected" << std::endl;
                        return false;
                    }
                }
                else {
                    // TODO: might want to throw an exception here instead but we know how
                    //      much Carolyn loves exceptions
                    if (!g_startingUp) {
                        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
                        addressToStub.erase(it->first);
                    }
                    return false;
                }
            }

            std::cout << "Leader eletion proposal accepted" << std::endl;

            return true;
        }

        // Conducts leader election
        void leaderElection() {
            int candidateValue;
            if (g_startingUp) {
                commitLogWriter.seekp(0, commitLogWriter.end);
                candidateValue = commitLogWriter.tellp();
                commitLogWriter.seekp(0, commitLogWriter.beg);
            } else {
                candidateValue = rand();
            }

            leaderElectionValuesMutex.lock();
            if (candidateValue > electionVals.maxLeaderElectionVal) {
                electionVals.currLeaderCandidateAddr = myAddress;
                electionVals.maxLeaderElectionVal = candidateValue;
            }
            else if (candidateValue == electionVals.maxLeaderElectionVal) {
                if (electionVals.currLeaderCandidateAddr.compare(myAddress) > 0) {
                    electionVals.currLeaderCandidateAddr = myAddress;
                }
            }
            leaderElectionValuesMutex.unlock();

            CandidateValue message;
            message.set_number(candidateValue);
            message.set_address(myAddress);

            // send election value to all other servers
            for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                ClientContext context;
                LeaderElectionResponse reply;
                Status status =  it->second->LeaderElection(&context, message, &reply);
                if (status.ok()) {
                    continue;
                }
                else {
                    std::cout << status.error_code() << ": " << status.error_message() << std::endl;
                    addressToStub.erase(it->first);
                }
            }

            bool waitingForLeaderElection = true;
            while (waitingForLeaderElection) {
                leaderElectionValuesMutex.lock();
                if (electionVals.numberOfCandidatesReceived >= addressToStub.size()) {
                    waitingForLeaderElection = false;
                }

                leaderElectionValuesMutex.unlock();
            }

            // select new leader
            leaderMutex.lock();
            leaderVals.leaderAddress = electionVals.currLeaderCandidateAddr;
            if (electionVals.currLeaderCandidateAddr == myAddress) {
                leaderVals.isLeader = true;
                leaderVals.leaderidx = -1;
            }
            else {
                leaderVals.isLeader = false;
                leaderVals.leaderidx = 1;
            }
            leaderMutex.unlock();

            std::cout << "New leader is " << leaderVals.leaderAddress << std::endl;

            leaderElectionValuesMutex.lock();
            electionVals.currLeaderCandidateAddr = "";
            electionVals.maxLeaderElectionVal = -1;
            electionVals.numberOfCandidatesReceived = 0;
            leaderElectionValuesMutex.unlock();

            std::cout << "Leader election finished" << std::endl;
            if (!leaderVals.isLeader && g_startingUp) {
                g_startingUp = false;

                // truncate leader
                std::ofstream commitLogOverWriter;
                commitLogOverWriter.open(commitFilename, std::fstream::trunc);
                commitLogOverWriter.close();

                commitLogWriter << g_csvFields << std::endl;
                
            }
        }
        
        // For leader to send commit logs
        void sendLogs(std::string filename) {
        
            // Read committed content
            std::vector<std::vector<std::string>> content;
            readFile(&content, filename);

            if (filename == commitFilename) {
                for (int i = 1; i < content.size(); i++) {
                    parseLine(content[i]);
                    clockVal = std::stoi(content[i][7]);
                }
            }

            // Send to other connections
            std::cout << "Iterating over connections " << std::endl;
            for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                if (it->first != leaderVals.leaderAddress) {
                    ClientContext context;
                    AddToPendingResponse response;
                    std::unique_ptr<ClientWriter<Operation>> writer(it->second->AddToPending(&context, &response));

                    for (int i = 1; i < content.size(); i++) {
                        Operation op;

                        op.set_message_type(content[i][0]);
                        op.set_username1(content[i][1]);
                        op.set_username2(content[i][2]);
                        op.set_password(content[i][3]);
                        op.set_message_content(content[i][4]);
                        op.set_messagesseen(content[i][5]);
                        op.set_leader(content[i][6]);
                        op.set_clockval(content[i][7]);

                        writer->Write(op);

                    }

                    writer->WritesDone();

                    Status status = writer->Finish();
                    
                    if (status.ok()) {
                        std::cout << "RPC succeeded." << std::endl;
                    } else {
                        std::cout << "RPC failed with error code: " << status.error_code() << ", message: " << status.error_message() << std::endl;
                    }
                }
            }
        }
            
        // Gets isLeader
        bool isLeader() {
            leaderMutex.lock();
            bool toReturn = leaderVals.isLeader;
            leaderMutex.unlock();
            
            return toReturn;
        }

        // Sets isLeader to true
        void setAsLeader() {
            leaderMutex.lock();
            leaderVals.isLeader = true;
            leaderMutex.unlock();
        }

        // Sets isLeader to false
        void setNotLeader() {
            leaderMutex.lock();
            leaderVals.isLeader = false;
            leaderMutex.unlock();
        }

        // Sends logs upon request
        void requestLogs(std::vector<OperationClass>& operations) {
            std::cout << "Iterating through connections" << std::endl;
            std::vector<std::string> droppedConnections;
            for (auto it = addressToStub.begin(); it != addressToStub.end(); it++) {
                std::cout << "Requesting pending logs from " << it->first << std::endl;
                ClientContext context;
                PendingLogRequest request;
                Operation op;
                std::unique_ptr<ClientReader<Operation>> reader(it->second->RequestPendingLog(&context, request));
                while (reader->Read(&op)) {
                    OperationClass newOp;
                    newOp.clockVal = std::stoi(op.clockval());
                    newOp.opCode = std::stoi(op.message_type());
                    newOp.username1 = op.username1();
                    newOp.username2 = op.username2();
                    newOp.password = op.password();
                    newOp.message_content = op.message_content();
                    newOp.messagesseen = op.messagesseen();
                    newOp.leader = op.leader();
                    operations.push_back(newOp);
                }

                Status status = reader->Finish();
                if (!status.ok()) {
                    std::cout << "Erasing connection to " << it->first << std::endl;
                    std::cout << status.error_code() << ": " << status.error_message() << std::endl;
                    droppedConnections.push_back(it->first);
                }
            }

            for (std::string addr : droppedConnections) {
                addressToStub.erase(addr);
            }
        }

        // Writes vector of operations into pending
        void writePendingOperations(std::vector<OperationClass> operations) {
            for (OperationClass op : operations) {
                writeToLogs(pendingLogWriter, op.opCode, op.username1, op.username2, op.password,
                            op.message_content, op.messagesseen, op.leader, op.clockVal);
            }
        }

        // Moves every operation from pending into the commit file
        void moveAllPendingToCommit() {
            // Read pending file
            std::vector<std::vector<std::string>> content;
            readFile(&content, pendingFilename);

            // Write to commit file
            for (int i = 1; i < content.size(); i++) {
                writeToLogs(commitLogWriter, stoi(content[i][0]), content[i][1], content[i][2], content[i][3], content[i][4], content[i][5], content[i][6], stoi(content[i][7]));
                parseLine(content[i]);
            }

            // Clear pending log, write first line back
            std::ofstream pendingLogOverWriter;
            pendingLogOverWriter.open(pendingFilename, std::fstream::trunc);

            pendingLogOverWriter << g_csvFields << std::endl;
        }
};


// open log file for server
ChatServiceImpl g_Service;