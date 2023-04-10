#include "../globals.h"
#include "../chatService.grpc.pb.h"

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
using chatservice::CreateAccountMessage;
using chatservice::LoginMessage;
using chatservice::LogoutMessage;
using chatservice::QueryUsersMessage;
using chatservice::ChatMessage;
using chatservice::QueryNotificationsMessage;
using chatservice::QueryMessagesMessage;
using chatservice::DeleteAccountMessage;
using chatservice::MessagesSeenMessage;
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


// Boolean determining whether program is still running
bool g_ProgramRunning = true;
std::string g_ElectionString = "olive";

std::string loggedInErrorMsg(std::string operationAttempted) {
    return "User must be logged in to perform " + operationAttempted;
}


struct ChatServiceClient {
    private:
        std::unique_ptr<ChatService::Stub> stub_;       // Current client stub
        
        bool USER_LOGGED_IN = false;                    // Boolean determining whether the user has logged in
        std::string clientUsername;                     // Username associated with client

        std::string currentIP;                          // IP address the client is currently connected to

        std::vector<std::string> serverAddresses;       // List of all potential server IP addresses the client can connect to

    public:
        ChatServiceClient() {}

        // Adds server addresses
        void addServerAddress(std::string addr) {
            serverAddresses.push_back(addr);
        }

        // Changes stub to new one connected to the given address
        void changeStub(std::string address) {
            auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());

            stub_ = ChatService::NewStub(channel);
            currentIP = address;
        }

        // Calls the stub's CreateAccount RPC and handles surrounding logic
        void createAccount(std::string username, std::string password) {
            if (USER_LOGGED_IN) {
                throw std::runtime_error("Cannot create account if already logged in.");
            }

            ClientContext context;
            CreateAccountMessage message;
            message.set_username(username);
            message.set_password(password);

            // Attempts to create an account
            CreateAccountReply reply;
            Status status = stub_->CreateAccount(&context, message, &reply);

            // If the operation returns with status ok
            if (status.ok()) {
                
                // If the account was created successfully and there is not another server that is the leader, create the account
                if (reply.createaccountsuccess() && !reply.has_leader()) {
                    std::cout << "Welcome " << username << "!" << std::endl;
                    USER_LOGGED_IN = true;
                    clientUsername = username;

                } else if (reply.has_leader()) {
                    // If we contacted a server and it's not in the middle of an election, connect to the real leader and try again
                    if (reply.leader() != g_ElectionString) {
                        changeStub(reply.leader());
                    }

                    // If the server is in the middle of an election, try again
                    createAccount(username, password);
                    return;
                
                } else {
                    std::cout << "Create account returned, but failed" << std::endl;
                    std::cout << reply.errormsg() << std::endl;
                }
            } else {
                // If the status is not OK, we assume the server has gone down 

                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    createAccount(username, password);
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            }
        }

        // Calls the stub's Login RPC and handles surrounding logic
        void login(std::string username, std::string password) {
            if (USER_LOGGED_IN) {
                throw std::runtime_error("Cannot log in if already logged in.");
            }

            ClientContext context;
            LoginMessage message;
            message.set_username(username);
            message.set_password(password);

            // Attempts to login
            LoginReply reply;
            Status status = stub_->Login(&context, message, &reply);

            // If the operation returns with status ok
            if (status.ok() && !reply.has_leader()) {
                if (reply.loginsuccess()) {
                    std::cout << "Welcome " << username << "!" << std::endl;
                    USER_LOGGED_IN = true;
                    clientUsername = username;
                } else {
                    std::cout << reply.errormsg() << std::endl;
                }
            } else if (reply.has_leader()) {
                    // If we contacted a server and it's not in the middle of an election, connect to the real leader and try again
                    if (reply.leader() != g_ElectionString) {
                        changeStub(reply.leader());
                    }

                    // If the server is in the middle of an election, try again
                    login(username, password);
                    return;
                
            } else {
                // If the status is not OK, we assume the server has gone down 

                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    login(username, password);
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            }

        }


        // ALL FUNCTIONS BELOW HERE REQUIRE USER TO BE LOGGED IN
        // Calls the stub's Logout RPC and handles surrounding logic
        void logout() {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("logout"));
            }

            ClientContext context;
            LogoutMessage message;
            message.set_username(clientUsername);

            // Attempts to logout
            LogoutReply reply;
            Status status = stub_->Logout(&context, message, &reply);

            // If the operation returns with status ok and I contacted the leader
            if (status.ok() && !reply.has_leader()) {
                std::cout << "Goodbye!" << std::endl;
                USER_LOGGED_IN = false;
            } else if (reply.has_leader()) {
                    // If we contacted a replica and it's not in the middle of an election, connect to the real leader
                    if (reply.leader() != g_ElectionString) {
                        changeStub(reply.leader());
                    }

                    // Attempt to logout again
                    logout();
                    return;
            } else {
                // If the connection dropped 

                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    logout();
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } 
        }

        // Calls the stub's ListUsers RPC and handles surrounding logic
        void listUsers(std::string prefix) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("list_users"));
            }

            ClientContext context;
            QueryUsersMessage message;
            message.set_username(prefix);

            User user;
            
            // Attempt to list users
            std::unique_ptr<ClientReader<User>> reader(stub_->ListUsers(&context, message));
            std::cout << "Found Following Users:" << std::endl;

            // Read the useres from the stream
            while (reader->Read(&user)) {
                if (user.has_leader()) {
                    // If we contacted a replica and it's not in the middle of an election, contact the real leader
                    if (user.leader() != g_ElectionString) {
                        changeStub(user.leader());
                    }

                    // List users again
                    listUsers(prefix);
                    return;

                } else {
                    // If we contacted the leader, print what it said
                    std::cout << user.username() << std::endl;
                }
            }

            Status status = reader->Finish();

            // If the connection was dropped
            if (!status.ok()) { 
                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    listUsers(prefix);
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } 
        }

        // Calls the stub's SendMessage RPC and handles surrounding logic
        void sendMessage(std::string recipient, std::string message_content) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("send_message"));
            }

            ClientContext context;
            ChatMessage message;
            message.set_msgcontent(message_content);
            message.set_recipientusername(recipient);
            message.set_senderusername(clientUsername);
            
            // Attempts to send a message
            SendMessageReply reply;
            Status status = stub_->SendMessage(&context, message, &reply);

            // If the request wen through and I contaced the leader
            if (status.ok() && !reply.has_leader()) {
                // If the message was sent properly
                if (reply.messagesent()) {
                    std::cout << "Message sent to " << recipient << "!" << std::endl;
                } else {
                    std::cout << reply.errormsg() << std::endl;
                }

            // If I contacted someone other than the leader
            } else if (reply.has_leader()) {
                    // If the replica we contacted was not in the middle of an election, connect to the real leader
                    if (reply.leader() != g_ElectionString) {
                        changeStub(reply.leader());
                    }

                    // Send message again
                    sendMessage(recipient, message_content);
                    return;

            // If the connection dropped 
            } else {
                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    sendMessage(recipient, message_content);
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } 
        }

        // Calls the stub's QueryNoticiations RPC and handles surrounding logic
        void queryNotifications() {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("query_notifications"));
            }
            ClientContext context;
            QueryNotificationsMessage message;
            message.set_user(clientUsername);

            // Attempts to query notifications
            Notification notification;
            std::unique_ptr<ClientReader<Notification>> reader(stub_->QueryNotifications(&context, message));

            // Reads notifications from the stream
            while (reader->Read(&notification)) {
                if (notification.has_leader()) {
                    // If we contacted a replica and it's not in the middle of an election, change stub 
                    if (notification.leader() != g_ElectionString) {
                        changeStub(notification.leader());
                    }

                    // Query notifications again
                    queryNotifications();
                    return;
                
                } else {
                    std::cout << notification.user() << ": " << std::to_string(notification.numberofnotifications()) << std::endl;
                }
            }
            Status status = reader->Finish();

            // If the connection dropped
            if (!status.ok()) {
                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    queryNotifications();
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } 
        }

        // Calls the stub's QueryMessages RPC and handles surrounding logic
        void queryMessages(std::string username) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("query_messages"));
            }
            ClientContext context;
            QueryMessagesMessage message;
            message.set_otherusername(username);
            message.set_clientusername(clientUsername);

            // Attempts to query messages
            ChatMessage msg;
            int messagesRead = 0;
            std::unique_ptr<ClientReader<ChatMessage>> reader(stub_->QueryMessages(&context, message));

            // Reads messages from the stream
            while (reader->Read(&msg)) {
                if (msg.has_leader()) {
                    // If we contacted a replica and it's not in the middle of an election, connect to the real leader
                    if (msg.leader() != g_ElectionString) {
                        changeStub(msg.leader());
                    }

                    // Query messages again
                    queryMessages(username);
                    return;
                
                }
                std::cout << msg.senderusername() << ": " << msg.msgcontent() << std::endl;
                messagesRead++;
            }

            Status status = reader->Finish();

            // If the connection dropped
            if (!status.ok()) {
                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    queryMessages(username);
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } 

            ClientContext context2;
            MessagesSeenMessage message2;
            message2.set_messagesseen(messagesRead);
            message2.set_clientusername(clientUsername);
            message2.set_otherusername(username);

            // Tell the server that the messages were seen
            MessagesSeenReply server_reply;
            status = stub_->MessagesSeen(&context2, message2, &server_reply);

            // If the connection was dropped
            if (!status.ok()) {
                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    stub_->MessagesSeen(&context2, message2, &server_reply);
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } 
        }

        // Calls the stub's DeleteAccount RPC and handles surrounding logic
        void deleteAccount(std::string username, std::string password) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("delete_account"));
            }

            ClientContext context;
            DeleteAccountMessage message;
            message.set_username(username);
            message.set_password(password);
            DeleteAccountReply reply;

            // Attempts to delete account
            Status status = stub_->DeleteAccount(&context, message, &reply);

            // If the server is still up and is actually the leader
            if (status.ok() && !reply.has_leader()) {
                if (reply.deletedaccount()) {
                    std::cout << "Account deleted, goobye!" << std::endl;
                    USER_LOGGED_IN = false;
                } else {
                    std::cout << reply.errormsg() << std::endl;
                }

            // If the contacted replica is not the leader
            } else if (reply.has_leader()) {
                    // If the contacted replica is not in the middle of an election, connect to the real leader
                    if (reply.leader() != g_ElectionString) {
                        changeStub(reply.leader());
                    }

                    // Delete account again
                    deleteAccount(username, password);
                    return;
                
            }  else {
                // Delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // Reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    deleteAccount(username, password);
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } 
        }

        // Handle server messages
        void refresh() {
            // If user not logged in there's nothing to refresh
            if (!USER_LOGGED_IN) {
                return;
            }

            ClientContext context;
            RefreshRequest request;
            request.set_clientusername(clientUsername);
            RefreshResponse reply;

            // Attempt to refresh client
            Status status = stub_->RefreshClient(&context, request, &reply);

            if (!status.ok() && !reply.has_leader()) {
                std::cout << "Refresh failed" << std::endl;
            } else if (reply.has_leader()) {
                    // If we contacted a replica and it's not in the middle of an election, change stub 
                    if (reply.leader() != g_ElectionString) {
                        changeStub(reply.leader());
                    }

                    // Refresh again
                    refresh();
                    return;
                
            } else if (!status.ok()) {
                // delete current IP address from vector
                std::vector<std::string>::iterator it = std::find(serverAddresses.begin(), serverAddresses.end(), currentIP);
                if (it != serverAddresses.end()) {
                    serverAddresses.erase(it);
                }

                // reset stub to IP address at first index if it exists
                if (serverAddresses.size() > 0) {
                    changeStub(serverAddresses[0]);
                    std::cout << "Changing connection to server at " << serverAddresses[0] << std::endl;
                    refresh();
                    return;
                } else {
                    // All servers are down rip
                    std::cout << "All servers are down, try again later" << std::endl;
                }

            } else {
                if (reply.forcelogout()) {
                    std::cout << "Logged in on another device. Ending session here." << std::endl;
                    USER_LOGGED_IN = false;
                    return;
                }

                for (int idx=0; idx < reply.notifications_size(); idx++) {
                    const Notification note = reply.notifications(idx);
                    std::cout << "New message from " << note.user() << std::endl;
                }
            }
        }
};