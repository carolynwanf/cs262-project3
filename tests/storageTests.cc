#include <gtest/gtest.h>
#include "../chatService/server/storage.h"
#include <unordered_set>

// Tests for the data structures

std::vector<std::string> usernameQuery (UserTrie trie, std::string query) {
  try {
    std::vector<std::string> users = trie.returnUsersWithPrefix(query);
    return users;
  } catch (std::runtime_error &e) {
    return std::vector<std::string> {};
  }
}

void dummyIncrement(int Z, ConversationsDictionary* testConversationsDictionary, char sender[g_UsernameLimit], char recipient[g_UsernameLimit]) {
  for (int i = 0; i < Z; i++) {
      testConversationsDictionary->newNotification(sender, recipient);
  }

  std::cerr << "incremented\n";

};
 void dummyDecrement(int Z, ConversationsDictionary* testConversationsDictionary, char sender[g_UsernameLimit], char recipient[g_UsernameLimit]) {
  for (int i = 0; i < Z; i++) {
      testConversationsDictionary->notificationSeen(sender, recipient);
  }

  std::cerr << "decremented\n";

};

TEST(ConversationsDictionaryTest, SingleThreadModifications) {
  char sender[g_UsernameLimit] = "carolyn";
  char recipient[g_UsernameLimit] = "victor";

  ConversationsDictionary testConversationsDictionary;

  // Incrementing
  dummyIncrement(1, &testConversationsDictionary, sender, recipient);

  EXPECT_EQ(testConversationsDictionary.conversations[recipient][sender], 1);

  dummyIncrement(20, &testConversationsDictionary, sender, recipient);


  EXPECT_EQ(testConversationsDictionary.conversations[recipient][sender], 21);

  // Decrementing
  dummyDecrement(5, &testConversationsDictionary, sender, recipient);

  EXPECT_EQ(testConversationsDictionary.conversations[recipient][sender], 16);

}

TEST(ConversationsDictionaryTest, MultiThreadedModifications) {
  char sender[g_UsernameLimit] = "carolyn";
  char recipient[g_UsernameLimit] = "victor";

  ConversationsDictionary testConversationsDictionary;

  std::thread t1(dummyIncrement,1, &testConversationsDictionary, sender, recipient);
  std::thread t2(dummyDecrement,1, &testConversationsDictionary, sender, recipient);

  t1.join();
  t2.join();

  EXPECT_EQ(testConversationsDictionary.conversations[recipient][sender], 0);
}

TEST(ConversationsDictionaryText, GetNotifications) {
  char username1[g_UsernameLimit] = "carolyn";
  char username2[g_UsernameLimit] = "victor";
  char message[g_MessageLimit] = "Hello";

  UserPair testpair(username1, username2);
  std::unordered_map<UserPair, StoredMessages> testMessagesDictionary;

  testMessagesDictionary[testpair].addMessage(username1, username2, message);
  testMessagesDictionary[testpair].addMessage(username1, username2, message);
  testMessagesDictionary[testpair].addMessage(username2, username1, message);
  testMessagesDictionary[testpair].addMessage(username2, username1, message);
  testMessagesDictionary[testpair].addMessage(username2, username1, message);

  std::vector<std::pair<char [g_UsernameLimit], char> > username1Notifications = conversationsDictionary.getNotifications(username1);

  EXPECT_EQ(std::string(username1Notifications[0].first), std::string(username2));
  EXPECT_EQ(username1Notifications[0].second, 3);

  std::vector<std::pair<char [g_UsernameLimit], char> > username2Notifications = conversationsDictionary.getNotifications(username2);

  EXPECT_EQ(std::string(username2Notifications[0].first), std::string(username1));
  EXPECT_EQ(username2Notifications[0].second, 2);

}

TEST(MessagesDictionaryTest, UserPairExpectations) {
  char username1[g_UsernameLimit] = "CaRolyn";
  char username2[g_UsernameLimit] = "VictOr";

  // Arguments in alphabetical order.
  UserPair testpair1(username1, username2);

  // Expect smallerUsername to be Carolyn.
  EXPECT_EQ(testpair1.smallerUsername, username1);
  
  // Expect largerUsername to be Victor.
  EXPECT_EQ(testpair1.largerUsername, username2);

  // Arguments in reverse alphabetical order.
  UserPair testpair2(username2, username1);

  // Expect smallerUsername to be Carolyn.
  EXPECT_EQ(testpair2.smallerUsername, username1);
  
  // Expect largerUsername to be Carolyn.
  EXPECT_EQ(testpair2.largerUsername, username2);

  char username3[g_UsernameLimit] = "1fdgaslkf";
  char username4[g_UsernameLimit] = "3kjdsfUHFD!";

  // Arguments contain non-alphabetic characters.
  UserPair testpair3(username3, username4);

  // Expect smallerUsername to be 1fdgaslkf.
  EXPECT_EQ(testpair3.smallerUsername, username3);
  
  // Expect largerUsername to be 3kjdsfUHFD!
  EXPECT_EQ(testpair3.largerUsername, username4);
}

TEST(MessagesDictionaryDict, StoringAndReadingMessages) {
  char username1[g_UsernameLimit] = "carolyn";
  char username2[g_UsernameLimit] = "victor";
  char message[g_MessageLimit] = "Hello, Victor!";

  StoredMessages testStoredMessages;

  // Adding messages
  testStoredMessages.addMessage(username1, username2, message);

  // Messages are added correctly
  EXPECT_EQ(testStoredMessages.messageList[0].senderUsername, username1);
  EXPECT_EQ(testStoredMessages.messageList[0].isRead, false);
  EXPECT_EQ(testStoredMessages.messageList[0].messageContent, message);

  // Unread messages incremented for recipient
  EXPECT_EQ(conversationsDictionary.conversations[username2][username1], 1);

  // Setting a message as read
  testStoredMessages.setRead(0, 0, username2);

  EXPECT_EQ(testStoredMessages.messageList[0].isRead, true);
  EXPECT_EQ(conversationsDictionary.conversations[username1][username2], 0);
}

// TEST(MessagesDictionaryDict, ConsectiveReads) {
//   char username1[g_UsernameLimit] = "carolyn";
//   char username2[g_UsernameLimit] = "victor";
//   char message[g_MessageLimit] = "Hello, Victor!";

//   StoredMessages testStoredMessages;

//   for (char i = 0; i<30 ; i++) {
//     char newMessage[g_MessageLimit];
//     newMessage[0] = i;
//     testStoredMessages.addMessage(username1, username2, newMessage);
//   }

  



// }

TEST(MessagesDictionaryDict, GettingMessages) {
  char username1[g_UsernameLimit] = "carolyn";
  char username2[g_UsernameLimit] = "victor";
  char message[g_MessageLimit] = "Hello, Victor!";

  StoredMessages testStoredMessages;

  // Getting less than g_MessageQueryLimit messages on initial query
  for (int i = 0; i < 10; i++) {
    testStoredMessages.addMessage(username1, username2, message);
  }
  
  GetStoredMessagesReturnValue test1 = testStoredMessages.getStoredMessages(username2, -1);

  EXPECT_EQ(test1.messageList.size(), 10);
  EXPECT_EQ(test1.firstMessageIndex, 0);
  EXPECT_EQ(test1.lastMessageIndex, 9);
  
  // Unread messages incremented for recipient
  EXPECT_EQ(conversationsDictionary.conversations[username2][username1], 10);

  // Getting g_MessageQueryLimit messages on initial query
  for (int i = 0; i < g_MessageQueryLimit; i++) {
    testStoredMessages.addMessage(username1, username2, message);
  }
  
  GetStoredMessagesReturnValue test2 = testStoredMessages.getStoredMessages(username2, -1);

  EXPECT_EQ(test2.messageList.size(), g_MessageQueryLimit);
  EXPECT_EQ(test2.firstMessageIndex, 10);
  EXPECT_EQ(test2.lastMessageIndex, 10+g_MessageQueryLimit-1);

  // Unread messages incremented for recipient
  EXPECT_EQ(conversationsDictionary.conversations[username2][username1], 30);

  // Getting messages on second query
  GetStoredMessagesReturnValue test3 = testStoredMessages.getStoredMessages(username2, test2.firstMessageIndex);

  EXPECT_EQ(test3.messageList.size(), 10);
  EXPECT_EQ(test3.firstMessageIndex, 0);
  EXPECT_EQ(test3.lastMessageIndex, 9);

  GetStoredMessagesReturnValue test4  = testStoredMessages.getStoredMessages(username2, test3.firstMessageIndex);

  EXPECT_EQ(test3.messageList.size(), 10);
  EXPECT_EQ(test3.firstMessageIndex, 0);
  EXPECT_EQ(test3.lastMessageIndex, 9);

}


TEST(UsernameTrieStorage, AddingUsernames) {
  UserTrie usernameTrie;
  std::string user1 = "Victor";
  std::string user2 = "Carolyn";
  std::string user3 = "Carlos";
  std::string user4 = "Vicky";

  usernameTrie.addUsername(user1, "password");
  usernameTrie.addUsername(user2, "password");
  usernameTrie.addUsername(user3, "password");
  usernameTrie.addUsername(user4, "password");

  std::vector<std::string> query1 = usernameTrie.returnUsersWithPrefix("Victor");
  std::vector<std::string> query1_expects {"Victor"};

  std::vector<std::string> query2 = usernameTrie.returnUsersWithPrefix("Carolyn");
  std::vector<std::string> query2_expects {"Carolyn"};

  std::vector<std::string> query3 = usernameTrie.returnUsersWithPrefix("Carlos");
  std::vector<std::string> query3_expects {"Carlos"};

  std::vector<std::string> query4 = usernameTrie.returnUsersWithPrefix("Vicky");
  std::vector<std::string> query4_expects {"Vicky"};

  EXPECT_EQ(query1, query1_expects);
}


TEST(UsernameTrieStorage, QueryingUsernames) {
  UserTrie usernameTrie;
  std::string user1 = "Victor";
  std::string user2 = "Carolyn";
  std::string user3 = "Carlos";
  std::string user4 = "Vicky";

  usernameTrie.addUsername(user1, "password");
  usernameTrie.addUsername(user2, "password");
  usernameTrie.addUsername(user3, "password");
  usernameTrie.addUsername(user4, "password");

  EXPECT_EQ(usernameQuery(usernameTrie, "victor"), std::vector<std::string> {});
  EXPECT_EQ(usernameQuery(usernameTrie, "Caroline"), std::vector<std::string> {});
  EXPECT_EQ(usernameQuery(usernameTrie, "Diego"), std::vector<std::string> {});
  EXPECT_EQ(usernameQuery(usernameTrie, "a"), std::vector<std::string> {});
  EXPECT_EQ(usernameQuery(usernameTrie, "b"), std::vector<std::string> {});
  EXPECT_EQ(usernameQuery(usernameTrie, "A"), std::vector<std::string> {});
  EXPECT_EQ(usernameQuery(usernameTrie, "B"), std::vector<std::string> {});

  std::unordered_set<std::string> test{"Vicky", "Victor"};
  for (std::string user : usernameQuery(usernameTrie, "V")) {
    EXPECT_EQ(test.count(user), 1);
  }

  std::unordered_set<std::string> test2{"Carlos", "Carolyn"};
  for (std::string user : usernameQuery(usernameTrie, "C")) {
    EXPECT_EQ(test2.count(user), 1);
  }

  std::unordered_set<std::string> test3{"Carlos", "Carolyn", "Vicky", "Victor"};
  for (std::string user : usernameQuery(usernameTrie, "")) {
    EXPECT_EQ(test3.count(user), 1);
  }
}

TEST(UsernameTrieStorage, PasswordStorage) {
  UserTrie usernameTrie;
  std::string user1 = "Victor";
  std::string user2 = "Carolyn";
  std::string user3 = "Carlos";
  std::string user4 = "Vicky";

  usernameTrie.addUsername(user1, "password1");
  usernameTrie.addUsername(user2, "password2");
  usernameTrie.addUsername(user3, "password3");
  usernameTrie.addUsername(user4, "password4");

  EXPECT_EQ(usernameTrie.verifyUser("Victor", "password1"), true);
  EXPECT_EQ(usernameTrie.verifyUser("Victor", "notpassword"), false);
  EXPECT_EQ(usernameTrie.verifyUser("Carlos", "password3"), true);
  EXPECT_EQ(usernameTrie.verifyUser("Carlos", "somethingelse"), false);
  EXPECT_EQ(usernameTrie.verifyUser("NoUser", "somethingelse"), false);
  EXPECT_EQ(usernameTrie.verifyUser("Vic", "password1"), false);
  EXPECT_EQ(usernameTrie.verifyUser("Vic", "password4"), false);
}

TEST(UsernameTrieStorage, UserExists) {
  UserTrie usernameTrie;
  std::string user1 = "Victor";
  std::string user2 = "Carolyn";
  std::string user3 = "Carlos";
  std::string user4 = "Vicky";

  usernameTrie.addUsername(user1, "password1");
  usernameTrie.addUsername(user2, "password2");
  usernameTrie.addUsername(user3, "password3");
  usernameTrie.addUsername(user4, "password4");

  EXPECT_EQ(usernameTrie.userExists("Victor"), true);
  EXPECT_EQ(usernameTrie.userExists("Vic"), false);
  EXPECT_EQ(usernameTrie.userExists("Carolyn"), true);
  EXPECT_EQ(usernameTrie.userExists("Taco"), false);
}

TEST(UsernameTrieStorage, DeleteUser) {
  UserTrie usernameTrie;
  std::string user1 = "Victor";
  std::string user2 = "Carolyn";
  std::string user3 = "Carlos";
  std::string user4 = "Vicky";

  usernameTrie.addUsername(user1, "password1");
  usernameTrie.addUsername(user2, "password2");
  usernameTrie.addUsername(user3, "password3");
  usernameTrie.addUsername(user4, "password4");

  usernameTrie.deleteUser("Victor");
  usernameTrie.deleteUser("Carlos");

  EXPECT_EQ(usernameTrie.userExists("Victor"), false);
  EXPECT_EQ(usernameTrie.userExists("Carlos"), false);
  EXPECT_EQ(usernameTrie.userExists("Carolyn"), true);
}


int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}