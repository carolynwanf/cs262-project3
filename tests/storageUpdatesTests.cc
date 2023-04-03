#include <gtest/gtest.h>
#include "../chatService/server/storageUpdates.h"

TEST(StorageUpdates, CreatingAnAccount) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Making a new account
    int createAccountStatus = createAccount(username, password);

    EXPECT_EQ(createAccountStatus, 0);

    // Making another account with the same username and same password
    createAccountStatus = createAccount(username, password);

    EXPECT_EQ(createAccountStatus, 1);

    // Making another account with the same username and a different password
    createAccountStatus = createAccount(username, anotherpassword);

    EXPECT_EQ(createAccountStatus, 1);

    // Making another account with a different username and the same original password
    createAccountStatus = createAccount(anotherusername, password);

    EXPECT_EQ(createAccountStatus, 0);
    
}

TEST(StorageUpdates, LoggingIn) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Logging in without account being made
    int loginStatus = login(username, password);

    EXPECT_EQ(loginStatus, 1);

    // Making a new account
    createAccount(username, password);

    // Logging into account that was just made
    loginStatus = login(username, password);

    EXPECT_EQ(loginStatus, 0);

    // Logging into an account that has already been logged into 
    loginStatus = login(username, password);

    EXPECT_EQ(loginStatus, 0);

    // Logging into an account with the incorrect password
    loginStatus = login(username, anotherpassword);

    EXPECT_EQ(loginStatus, 1);
    
}

TEST(StorageUpdates, LoggingOut) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Creating Carolyn's account
    createAccount(username, password);

    // Logging in Carolyn
    login(username, password);

    // Logging out user who was active
    int logoutStatus = logout(username);

    EXPECT_EQ(logoutStatus, 0);

    // The rest of these operations don't happen irl, but also don't do anything when they do happen
    // Logging out user who was active again
    logoutStatus = logout(username);

    EXPECT_EQ(logoutStatus, 1);

    // Logging out user who was not active and who doesn't have an account
    logoutStatus = logout(anotherusername);

}

TEST(StorageUpdates, SendingMessages) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";
    std::string message = "hello";

    // Send message from account that doesn't exist to account that doesn't exist
    int sendMessageStatus = sendMessage(username, anotherusername, message);

    EXPECT_EQ(sendMessageStatus, 1);

    createAccount(username, password);

    // Send message from account that exists to account that doesn't exist
    sendMessageStatus = sendMessage(username, anotherusername, message);

    EXPECT_EQ(sendMessageStatus, 1);

    // Send message from account that doesn't exist to account that exists
    sendMessageStatus = sendMessage(anotherusername, username, message);

    EXPECT_EQ(sendMessageStatus, 1);

    // Send message from account that exists to account that exists
    createAccount(anotherusername, anotherpassword);
    sendMessageStatus = sendMessage(anotherusername, username, message);

    EXPECT_EQ(sendMessageStatus, 0);

}

TEST(StorageUpdates, DeletingAnAccount) {
    UserTrie usernameTrie;
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Deleting account that doesn't exist
    int deleteAccountStatus = deleteAccount(password);

    EXPECT_EQ(deleteAccountStatus, 1);

    // Deleting account that does exist
    createAccount(anotherusername, anotherpassword);
    deleteAccountStatus = deleteAccount(anotherusername);

    EXPECT_EQ(deleteAccountStatus, 0);

}

TEST(StorageUpdates, SeeingMessages) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";
    std::string message = "hello";

    // Both users don't exist
    int messagesSeenStatus = messagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // First user doesn't exist 
    createAccount(username, password);
    messagesSeenStatus = messagesSeen(anotherusername, username, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // Second user doesn't exist
    messagesSeenStatus = messagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // Both users exist, no conversation between users
    createAccount(anotherusername, anotherpassword);

    messagesSeenStatus = messagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // Both users exist, existing conversation between users
    sendMessage(username, anotherusername, message);
    
    messagesSeenStatus = messagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 0);

}

TEST(StorageUpdates, messagesQueried) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";
    std::string message = "hello";

    // Both users don't exist
    std::vector<ChatMessage> messageQueriedStatus = queryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // First user doesn't exist 
    createAccount(username, password);
    messageQueriedStatus = queryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // Second user doesn't exist
    messageQueriedStatus = queryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // Both users exist, no conversation between users
    createAccount(anotherusername, anotherpassword);

    messageQueriedStatus = queryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // Both users exist, existing conversation between users
    int sendMessageStatus = sendMessage(username, anotherusername, message);

    EXPECT_EQ(sendMessageStatus, 0);
    
    messageQueriedStatus = queryMessages(anotherusername, username);

    EXPECT_EQ(messageQueriedStatus.size(), 1);

}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}