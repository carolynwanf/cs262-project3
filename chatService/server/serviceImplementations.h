#include "../chatService.grpc.pb.h"
#include "storageUpdates.h"

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
        std::ofstream logWriter;

        // For interserver communication
        std::vector<std::unique_ptr<ChatService::Stub>> connections;
        std::unordered_map<std::string, int> addressToConnectionIdx;
        std::string myAddress;

        // Commented out the global versions in storage.h
        std::mutex leaderMutex;
        LeaderValues leaderVals;

        std::mutex leaderElectionValuesMutex;
        ElectionValues electionVals;

    public:
        explicit ChatServiceImpl(std::string fileName, std::string address) {
            // initialize the CSV log
            logWriter.open(fileName);
            logWriter << g_csvFields << std::endl;
            myAddress = address;
        }

        Status CreateAccount(ServerContext* context, const CreateAccountMessage* create_account_message, 
                            CreateAccountReply* server_reply) {
            // Mutex lock, check for existing users, add user, etc.
            std::string username = create_account_message->username();
            std::string password = create_account_message->password();
            int createAccountStatus = createAccount(username, password);

            // Update error messages and reply based on account creation status
            if (createAccountStatus == 1) {
                std::string errorMsg = "Username '" + create_account_message->username() + "' already exists.";
                server_reply->set_errormsg(errorMsg);
                server_reply->set_createaccountsuccess(false);
            } else {
                server_reply->set_createaccountsuccess(true);
                writeToLogs(logWriter, CREATE_ACCOUNT, username, g_nullString, password);
            }

            return Status::OK;
        }


        Status Login(ServerContext* context, const LoginMessage* login_message, LoginReply* server_reply) {
            // Check for existing user and verify password
            std::string username = login_message->username();
            std::string password = login_message->password();

            int loginStatus = login(username, password);
            
            if (loginStatus == 0) {
                server_reply->set_loginsuccess(true);
                writeToLogs(logWriter, LOGIN, username, g_nullString, password);
            } else {
                server_reply->set_loginsuccess(false);
                server_reply->set_errormsg("Incorrect username or password.");
            }

            return Status::OK;
        }


        Status Logout(ServerContext* context, const LogoutMessage* logout_message, LogoutReply* server_reply) {
            int logoutStatus = logout(logout_message->username());
            if (logoutStatus == 0) {
                writeToLogs(logWriter, LOGOUT, logout_message->username());
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

            int sendMessageStatus = sendMessage(senderUsername, recipientUsername, messageContent);

            if (sendMessageStatus == 0) {
                server_reply->set_messagesent(true);
                writeToLogs(logWriter, SEND_MESSAGE, senderUsername, recipientUsername, g_nullString, messageContent);
            } else {
                std::string errormsg = "Tried to send a message to a user that doesn't exist '" + msg->recipientusername() + "'";
                server_reply->set_errormsg(errormsg);
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
            std::cout << "Getting messages between '" << query->clientusername() << "' and '"<< query->otherusername() << "'" << std::endl;

            std::vector<ChatMessage> queryMessagesMessageList = queryMessages(query->clientusername(), query->otherusername());

            for (auto message : queryMessagesMessageList) {
                writer->Write(message);
            }

            writeToLogs(logWriter, QUERY_MESSAGES, query->clientusername(), query->otherusername());

            return Status::OK;
        }

        Status DeleteAccount(ServerContext* context, const DeleteAccountMessage* delete_account_message,
                            DeleteAccountReply* server_reply) {
            std::cout << "Deleting account of '" << delete_account_message->username() << "'" << std::endl;
            // Flag user account as deleted in trie
            int deleteAccountStatus = deleteAccount(delete_account_message->username());

            if (deleteAccountStatus == 1) {
                server_reply->set_deletedaccount(false);
            } else {
                server_reply->set_deletedaccount(true);
                writeToLogs(logWriter, DELETE_ACCOUNT, delete_account_message->username());
            }
            
            return Status::OK;
        }


        Status MessagesSeen(ServerContext* context, const MessagesSeenMessage* msg, MessagesSeenReply* reply) {
            int messagesSeenStatus = messagesSeen(msg->clientusername(), msg->otherusername(), msg->messagesseen());
            
            writeToLogs(logWriter, MESSAGES_SEEN, msg->clientusername(), msg->otherusername(), g_nullString, g_nullString, std::to_string(msg->messagesseen()));

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

        Status HeartBeat(ServerContext* context, const HeartBeatRequest* request, HeartBeatResponse* reply) {
            leaderMutex.lock();
            reply->set_isleader(isLeader);
            leaderMutex.unlock();
            return Status::OK;
        }

        Status ProposeLeaderElection(ServerContext* context, const LeaderElectionProposal* request, LeaderElectionProposalResponse reply) {
            // TODO: implement leader election proposal RPC
            // Check if I am the leader or if leaderIdx != -1, otherwise we have no leader
            leaderMutex.lock();
            if (leaderIdx != -1 || isLeader) {
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
            // TODO: implement leader election RPC (they basically just send their numbers)
            // Update leader candidate values
            leaderElectionValuesMutex.lock();
            
            leaderElectionValuesMutex.unlock();
        }


        // For interserver communication stuff
        void addConnection(std::string server_address) {
            // TODO: do we want to put this in a loop to keep trying until it works?
            auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
            std::unique_ptr<ChatService::Stub> stub_ = ChatService::NewStub(channel);
            connections.push_back(stub_);
            addressToConnectionIdx[server_address] = connections.size()-1;
        }

        bool heartbeat() {
            leaderMutex.lock();
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
            leaderMutex.unlock();
        }

        bool proposeLeaderElection() {
            // TODO: call propose leader election for each stub in vector
            LeaderElectionProposal message;

            for (int i = 0; i < connections.size(); i++) {
                ClientContext context;
                LeaderElectionProposalResponse reply;
                Status status = connections[i]->ProposeLeaderElection(&context, message, &reply);
                if (status.ok()) {
                    if (reply.accept()) {
                        return true;
                    }
                    else {
                        return false;
                    }
                }
                else {
                    // TODO: might want to throw an exception here instead but we know how
                    //      much Carolyn loves exceptions
                    return false;
                }
            }
        }

        void leaderElection() {
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
            bool waitingForLeaderElection = true;
            while (waitingForLeaderElection) {
                leaderElectionValuesMutex.lock();
                if (electionVals.numberOfCandidatesReceived == g_numberOfServers - 1) {
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

            leaderElectionValuesMutex.lock();
            electionVals.currLeaderCandidateAddr = "";
            electionVals.maxLeaderElectionVal = -1;
            electionVals.numberOfCandidatesReceived = 0;
            leaderElectionValuesMutex.unlock();
        }
};