#include <gtest/gtest.h>
#include "../chatService/server/storageUpdates.h"

TEST(StorageUpdates, CreatingAnAccount) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Making a new account
    int createAccountStatus = tryCreateAccount(username, password);

    EXPECT_EQ(createAccountStatus, 0);

    // Making another account with the same username and same password
    createAccountStatus = tryCreateAccount(username, password);

    EXPECT_EQ(createAccountStatus, 1);

    // Making another account with the same username and a different password
    createAccountStatus = tryCreateAccount(username, anotherpassword);

    EXPECT_EQ(createAccountStatus, 1);

    // Making another account with a different username and the same original password
    createAccountStatus = tryCreateAccount(anotherusername, password);

    EXPECT_EQ(createAccountStatus, 0);
    
}

TEST(StorageUpdates, LoggingIn) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Logging in without account being made
    int loginStatus = tryLogin(username, password);

    EXPECT_EQ(loginStatus, 1);

    // Making a new account
    tryCreateAccount(username, password);

    // Logging into account that was just made
    loginStatus = tryLogin(username, password);

    EXPECT_EQ(loginStatus, 0);

    // Logging into an account that has already been logged into 
    loginStatus = tryLogin(username, password);

    EXPECT_EQ(loginStatus, 0);

    // Logging into an account with the incorrect password
    loginStatus = tryLogin(username, anotherpassword);

    EXPECT_EQ(loginStatus, 1);
    
}

TEST(StorageUpdates, LoggingOut) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Creating Carolyn's account
    tryCreateAccount(username, password);

    // Logging in Carolyn
    tryLogin(username, password);

    // Logging out user who was active
    int logoutStatus = tryLogout(username);

    EXPECT_EQ(logoutStatus, 0);

    // The rest of these operations don't happen irl, but also don't do anything when they do happen
    // Logging out user who was active again
    logoutStatus = tryLogout(username);

    EXPECT_EQ(logoutStatus, 1);

    // Logging out user who was not active and who doesn't have an account
    logoutStatus = tryLogout(anotherusername);

}

TEST(StorageUpdates, SendingMessages) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";
    std::string message = "hello";

    // Send message from account that doesn't exist to account that doesn't exist
    int sendMessageStatus = trySendMessage(username, anotherusername, message);

    EXPECT_EQ(sendMessageStatus, 1);

    tryCreateAccount(username, password);

    // Send message from account that exists to account that doesn't exist
    sendMessageStatus = trySendMessage(username, anotherusername, message);

    EXPECT_EQ(sendMessageStatus, 1);

    // Send message from account that doesn't exist to account that exists
    sendMessageStatus = trySendMessage(anotherusername, username, message);

    EXPECT_EQ(sendMessageStatus, 1);

    // Send message from account that exists to account that exists
    tryCreateAccount(anotherusername, anotherpassword);
    sendMessageStatus = trySendMessage(anotherusername, username, message);

    EXPECT_EQ(sendMessageStatus, 0);

}

TEST(StorageUpdates, DeletingAnAccount) {
    UserTrie usernameTrie;
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";

    // Deleting account that doesn't exist
    int deleteAccountStatus = tryDeleteAccount(password);

    EXPECT_EQ(deleteAccountStatus, 1);

    // Deleting account that does exist
    tryCreateAccount(anotherusername, anotherpassword);
    deleteAccountStatus = tryDeleteAccount(anotherusername);

    EXPECT_EQ(deleteAccountStatus, 0);

}

TEST(StorageUpdates, SeeingMessages) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";
    std::string message = "hello";

    // Both users don't exist
    int messagesSeenStatus = tryMessagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // First user doesn't exist 
    tryCreateAccount(username, password);
    messagesSeenStatus = tryMessagesSeen(anotherusername, username, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // Second user doesn't exist
    messagesSeenStatus = tryMessagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // Both users exist, no conversation between users
    tryCreateAccount(anotherusername, anotherpassword);

    messagesSeenStatus = tryMessagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 1);

    // Both users exist, existing conversation between users
    trySendMessage(username, anotherusername, message);
    
    messagesSeenStatus = tryMessagesSeen(username, anotherusername, 0);

    EXPECT_EQ(messagesSeenStatus, 0);

}

TEST(StorageUpdates, messagesQueried) {
    std::string username = "carolyn";
    std::string password = "password";
    std::string anotherusername = "victor";
    std::string anotherpassword = "anotherpassword";
    std::string message = "hello";

    // Both users don't exist
    std::vector<ChatMessage> messageQueriedStatus = tryQueryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // First user doesn't exist 
    tryCreateAccount(username, password);
    messageQueriedStatus = tryQueryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // Second user doesn't exist
    messageQueriedStatus = tryQueryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // Both users exist, no conversation between users
    tryCreateAccount(anotherusername, anotherpassword);

    messageQueriedStatus = tryQueryMessages(username, anotherusername);

    EXPECT_EQ(messageQueriedStatus.size(), 0);

    // Both users exist, existing conversation between users
    int sendMessageStatus = trySendMessage(username, anotherusername, message);

    EXPECT_EQ(sendMessageStatus, 0);
    
    messageQueriedStatus = tryQueryMessages(anotherusername, username);

    EXPECT_EQ(messageQueriedStatus.size(), 1);

}

TEST(StorageUpdates, ReadingCSV) {
    std::string historyFile = "../tests/testlog.csv";

    // populate data structures using file
    std::vector<std::vector<std::string>> content;

    readFile(&content, historyFile);

    // Checking a few random values to check that csv was read properly
    EXPECT_EQ(content[1][0], "1");
    EXPECT_EQ(content[1][1], "carolyn");
    EXPECT_EQ(content[6][0], "7");
    EXPECT_EQ(content[6][1], "victor");
}

TEST(StorageUpdates, ParsingLines) {
    std::string user1 = "carolyn";
    std::string user2 = "victor";

    std::string historyFile = "../tests/testlog.csv";

    // populate data structures using file
    std::vector<std::vector<std::string>> content;

    readFile(&content, historyFile);

    for(int i=1; i < content.size(); i++) {
        parseLine(content[i]);
    }


    // After parsing, data structures should reflect
    // Carolyn's account doesn't exist
    EXPECT_EQ(userTrie.userExists(user1), false);

    // Victor's account is active
    EXPECT_NE(activeUsers.find(user2), activeUsers.end());
    
    // Victor's account has password "password"
    EXPECT_EQ(userTrie.verifyUser(user2, "password"), true);

    // Carolyn and Victor have a conversation with the read message "hello"
    UserPair userPair(user1,user2);

    EXPECT_EQ(messagesDictionary[userPair].messageList[0].isRead, true);
    EXPECT_EQ(messagesDictionary[userPair].messageList[0].messageContent, "hello");
    EXPECT_EQ(messagesDictionary[userPair].messageList[0].senderUsername, user1);

}

TEST(StorageUpdates, LogWriting) {
    std::string testFile = "testWriteLog.csv";
    std::string username1 = "carolyn";
    std::string username2 = "victor";
    std::string password = "password";
    std::string messageContent = "hello";
    int messagesSeen = 3;
    int leader = 1;

    std::ofstream logWriter;
    logWriter.open(testFile);
    logWriter << g_csvFields << std::endl;

    // Attempting all valid operations
    writeToLogs(logWriter, CREATE_ACCOUNT, username1, g_nullString, password);
    writeToLogs(logWriter, LOGIN, username1, g_nullString, password);
    writeToLogs(logWriter, LOGOUT, username1);
    writeToLogs(logWriter, SEND_MESSAGE, username1, username2, g_nullString, messageContent);
    writeToLogs(logWriter, QUERY_MESSAGES, username1, username2);
    writeToLogs(logWriter, DELETE_ACCOUNT, username1);
    writeToLogs(logWriter, MESSAGES_SEEN, username1, username2, g_nullString, g_nullString, std::to_string(messagesSeen));
    writeToLogs(logWriter, 100);


    // populate data structures using file
    std::vector<std::vector<std::string>> content;

    readFile(&content, testFile);

    // Expect 8 lines, including the header
    EXPECT_EQ(content.size(), 8);

    // Checking random values
    EXPECT_EQ(content[1][0], std::to_string(CREATE_ACCOUNT));
    EXPECT_EQ(content[1][3], password);
    EXPECT_EQ(content[3][1], username1);
    EXPECT_EQ(content[7][0], std::to_string(MESSAGES_SEEN));
    EXPECT_EQ(content[7][1], username1);
    EXPECT_EQ(content[7][2], username2);
    EXPECT_EQ(content[7][5], std::to_string(messagesSeen));

}

TEST(StorageUpdates, OperationHeap) {
    OperationClass op1;
    OperationClass op2;
    OperationClass op3;
    op1.clockVal = 1;
    op2.clockVal = 2;
    op3.clockVal = 3;

    std::vector<OperationClass> operations {op3, op1, op2};
    sortOperations(operations);
    std::vector<OperationClass> sortedOperations{op1, op2, op3};
    for (int idx = 0; idx < operations.size(); idx++) {
        EXPECT_EQ(sortedOperations[idx].clockVal, operations[idx].clockVal);
    }

    std::vector<OperationClass> alreadySorted {op1, op2, op3};
    sortOperations(alreadySorted);
    for (int idx = 0; idx < operations.size(); idx++) {
        EXPECT_EQ(sortedOperations[idx].clockVal, alreadySorted[idx].clockVal);
    }
}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}