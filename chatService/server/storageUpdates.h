#include "storage.h"

// Functions for updating storage structures based on logs
// No mutexes because these operations are done sequentially

// Updates user trie with created account and also active users set
int createAccount(std::string username, std::string password) {
    // User already exists
    int status = 0;
    if (userTrie.userExists(username)) {
        status = 1;
    // User doesn't already exist
    } else {
        // Update storage with new user
        userTrie_mutex.lock();
        userTrie.addUsername(username, password);
        userTrie_mutex.unlock();

        activeUser_mutex.lock();
        activeUsers.insert(username);
        activeUser_mutex.unlock();
    }

    return status;
}

// Updates active users with username 
int login(std::string username, std::string password) {
    int status = 0;
    // Check for existing user and verify password
    userTrie_mutex.lock();
    bool verified = userTrie.verifyUser(username, password);
    userTrie_mutex.unlock();
    
    if (verified) {
        activeUsers.insert(username);
    } else {
        status = 1;    // Account not able to be verified
    }

    return status;
}

// Removes username from active users
int logout(std::string username) {
    int status = 0;     // User currently active
    if (activeUsers.find(username) == activeUsers.end()) {
        status = 1;     // User was not active
    } else {
        activeUsers.erase(username);
    }

    return status;
}

// Update messages dictionary
int sendMessage(std::string sender, std::string recipient, std::string content) {
    int status = 0;
    bool userExists = userTrie.userExists(recipient);

    if (userExists) {
        // Add message to messages dictionary
        UserPair userPair(sender, recipient);
        messagesDictionary[userPair].addMessage(sender, recipient, content);

        // Adds queued operations for active user
        activeUser_mutex.lock();
        if (activeUsers.find(recipient) != activeUsers.end()) {
            queuedOperations_mutex.lock();
            Notification note;
            note.set_user(sender);
            queuedOperationsDictionary[recipient].push_back(note);
            queuedOperations_mutex.unlock();
        }
        activeUser_mutex.unlock();

    } else {
        status = 1;     // User does not exist
    }
    return status;
}

// Remove account from userTrie
int deleteAccount(std::string username) {
    int status = 0; // Account successfully deleted
    // Flag user account as deleted in trie
    userTrie_mutex.lock();
    try {
        userTrie.deleteUser(username);
    } catch (std::runtime_error &e) {
        int status = 1;     // Account not deleted
    }
    userTrie_mutex.unlock();

    currentConversationsDictMutex.lock();
    currentConversationsDict.erase(username);
    currentConversationsDictMutex.unlock();

    return status;
}

int messagesSeen(std::string clientusername, std::string otherusername, int messagesseen) {
    int status = 0;     // Valid query
    UserPair userPair(clientusername, otherusername);
    currentConversationsDictMutex.lock();
    int startIdx = currentConversationsDict[clientusername].messagesSentStartIndex;
    currentConversationsDictMutex.unlock();

    if (messagesDictionary.find(userPair) != messagesDictionary.end()) {
            messagesDictionary[userPair].setRead(startIdx,
                                            startIdx+messagesseen - 1, clientusername);
    } else {
        status = 1;     // No existing coneration between user pairs
    }

    return status;
}

std::vector<ChatMessage> queryMessages(std::string clientusername, std::string otherusername) {
    // Get stored messages depending on if the client has the conversation open
    UserPair userPair(clientusername, otherusername);
    int lastMessageDeliveredIndex = -1;
    currentConversationsDictMutex.lock();
    CurrentConversation currentConversation = currentConversationsDict[clientusername];
    if (currentConversation.username == otherusername) {
        lastMessageDeliveredIndex = currentConversation.messagesSentStartIndex;
    } else {
        currentConversation.username = otherusername;
    }

    GetStoredMessagesReturnValue returnVal = messagesDictionary[userPair].getStoredMessages(clientusername, lastMessageDeliveredIndex);

    // Update current conversation information
    currentConversation.messagesSentStartIndex = returnVal.firstMessageIndex;
    currentConversation.messagesSentEndIndex = returnVal.lastMessageIndex;
    currentConversationsDict[clientusername] = currentConversation;
    currentConversationsDictMutex.unlock();

    return returnVal.messageList;
}



