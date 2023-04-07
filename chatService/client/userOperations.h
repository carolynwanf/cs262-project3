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

std::string loggedInErrorMsg(std::string operationAttempted) {
    return "User must be logged in to perform " + operationAttempted;
}


struct ChatServiceClient {
    private:
        std::unique_ptr<ChatService::Stub> stub_;
        // Boolean determining whether the user has logged in
        bool USER_LOGGED_IN = false;
        std::string clientUsername;

    public:
        ChatServiceClient(std::shared_ptr<Channel> channel) {
            stub_ = ChatService::NewStub(channel);
        }

        void createAccount(std::string username, std::string password) {
            if (USER_LOGGED_IN) {
                throw std::runtime_error("Cannot create account if already logged in.");
            }

            ClientContext context;
            CreateAccountMessage message;
            message.set_username(username);
            message.set_password(password);

            CreateAccountReply reply;
            Status status = stub_->CreateAccount(&context, message, &reply);
            if (status.ok()) {

                if (reply.createaccountsuccess()) {
                    std::cout << "Welcome " << username << "!" << std::endl;
                    USER_LOGGED_IN = true;
                    clientUsername = username;
                } else {
                    std::cout << "Create account returned, but failed" << std::endl;
                    std::cout << reply.errormsg() << std::endl;
                }
            } else {
                std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            }
        }

        void login(std::string username, std::string password) {
            if (USER_LOGGED_IN) {
                throw std::runtime_error("Cannot log in if already logged in.");
            }

            ClientContext context;
            LoginMessage message;
            message.set_username(username);
            message.set_password(password);

            LoginReply reply;
            Status status = stub_->Login(&context, message, &reply);
            if (status.ok()) {
                if (reply.loginsuccess()) {
                    std::cout << "Welcome " << username << "!" << std::endl;
                    USER_LOGGED_IN = true;
                    clientUsername = username;
                } else {
                    std::cout << reply.errormsg() << std::endl;
                }
            } else {
                std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            }

        }


        // ALL FUNCTIONS BELOW HERE REQUIRE USER TO BE LOGGED IN
        void logout() {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("logout"));
            }

            ClientContext context;
            LogoutMessage message;
            message.set_username(clientUsername);
            LogoutReply reply;
            Status status = stub_->Logout(&context, message, &reply);
            if (status.ok()) {
                std::cout << "Goodbye!" << std::endl;
            } else {
                std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            }

            USER_LOGGED_IN = false;
        }

        void listUsers(std::string prefix) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("list_users"));
            }

            ClientContext context;
            QueryUsersMessage message;
            message.set_username(prefix);

            User user;
            
            std::unique_ptr<ClientReader<User>> reader(stub_->ListUsers(&context, message));
            std::cout << "Found Following Users:" << std::endl;
            while (reader->Read(&user)) {
                std::cout << user.username() << std::endl;
            }
            Status status = reader->Finish();
            if (!status.ok()) {
                std::cout << "list_users failed." << std::endl;
                std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            }
        }

        void sendMessage(std::string recipient, std::string message_content) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("send_message"));
            }

            ClientContext context;
            ChatMessage message;
            message.set_msgcontent(message_content);
            message.set_recipientusername(recipient);
            message.set_senderusername(clientUsername);
            
            SendMessageReply reply;
            Status status = stub_->SendMessage(&context, message, &reply);
            if (status.ok()) {
                if (reply.messagesent()) {
                    std::cout << "Message sent to " << recipient << "!" << std::endl;
                } else {
                    std::cout << reply.errormsg() << std::endl;
                }
            } else {
                std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            }
        }

        void queryNotifications() {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("query_notifications"));
            }
            ClientContext context;
            QueryNotificationsMessage message;
            message.set_user(clientUsername);

            Notification notification;
            
            std::unique_ptr<ClientReader<Notification>> reader(stub_->QueryNotifications(&context, message));
            while (reader->Read(&notification)) {
                std::cout << notification.user() << ": " << std::to_string(notification.numberofnotifications()) << std::endl;
            }
            Status status = reader->Finish();
            if (!status.ok()) {
                std::cout << "query_notifications failed." << std::endl;
            }
        }


        void queryMessages(std::string username) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("query_messages"));
            }
            ClientContext context;
            QueryMessagesMessage message;
            message.set_otherusername(username);
            message.set_clientusername(clientUsername);

            ChatMessage msg;
            int messagesRead = 0;
            std::unique_ptr<ClientReader<ChatMessage>> reader(stub_->QueryMessages(&context, message));
            while (reader->Read(&msg)) {
                std::cout << msg.senderusername() << ": " << msg.msgcontent() << std::endl;
                messagesRead++;
            }
            Status status = reader->Finish();
            if (!status.ok()) {
                std::cout << "query_messages failed." << std::endl;
                // return;
            }

            ClientContext context2;
            MessagesSeenMessage message2;
            message2.set_messagesseen(messagesRead);
            message2.set_clientusername(clientUsername);
            message2.set_otherusername(username);
            MessagesSeenReply server_reply;
            status = stub_->MessagesSeen(&context2, message2, &server_reply);
            if (!status.ok()) {
                std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            }
        }


        void deleteAccount(std::string username, std::string password) {
            if (!USER_LOGGED_IN) {
                throw std::runtime_error(loggedInErrorMsg("delete_account"));
            }

            ClientContext context;
            DeleteAccountMessage message;
            message.set_username(username);
            message.set_password(password);
            DeleteAccountReply reply;

            Status status = stub_->DeleteAccount(&context, message, &reply);
            if (status.ok()) {
                if (reply.deletedaccount()) {
                    std::cout << "Account deleted, goobye!" << std::endl;
                    USER_LOGGED_IN = false;
                } else {
                    std::cout << reply.errormsg() << std::endl;
                }
            } else {
                std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            }
        }

        // handle server messages
        void refresh() {
            // If user not logged in there's nothing to refresh
            if (!USER_LOGGED_IN) {
                return;
            }

            ClientContext context;
            RefreshRequest request;
            request.set_clientusername(clientUsername);
            RefreshResponse reply;

            Status status = stub_->RefreshClient(&context, request, &reply);

            if (!status.ok()) {
                std::cout << "Refresh failed" << std::endl;
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