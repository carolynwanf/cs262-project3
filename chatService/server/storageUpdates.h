#include "storage.h"
#include <fstream>

// Functions for updating storage structures based on logs
// No mutexes because these operations are done sequentially

// Updates user trie with created account and also active users set
int tryCreateAccount(std::string username, std::string password) {
    // User already exists
    std::cout << "Trying to create account" << std::endl;
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
int tryLogin(std::string username, std::string password) {
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
int tryLogout(std::string username) {
    int status = 0;     // User currently active
    if (activeUsers.find(username) == activeUsers.end()) {
        status = 1;     // User was not active
    } else {
        activeUsers.erase(username);
    }

    return status;
}

// Update messages dictionary
int trySendMessage(std::string sender, std::string recipient, std::string content) {
    int status = 0;
    bool senderExists = userTrie.userExists(sender);
    bool recipientExists = userTrie.userExists(recipient);

    if (senderExists && recipientExists) {
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
int tryDeleteAccount(std::string username) {
    int status = 0; // Account successfully deleted
    // Flag user account as deleted in trie
    userTrie_mutex.lock();
    try {
        userTrie.deleteUser(username);
    } catch (std::runtime_error &e) {
        status = 1;     // Account not deleted
    }
    userTrie_mutex.unlock();

    currentConversationsDictMutex.lock();
    currentConversationsDict.erase(username);
    currentConversationsDictMutex.unlock();

    return status;
}

int tryMessagesSeen(std::string clientusername, std::string otherusername, int messagesseen) {
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

std::vector<ChatMessage> tryQueryMessages(std::string clientusername, std::string otherusername) {
    // Get stored messages depending on if the client has the conversation open
    UserPair userPair(clientusername, otherusername);
    int lastMessageDeliveredIndex = -1;
    currentConversationsDictMutex.lock();
    CurrentConversation currentConversation = currentConversationsDict[clientusername];
    if (currentConversation.username == otherusername && messagesDictionary[userPair].messageList.size() > g_MessageLimit) {
        lastMessageDeliveredIndex = currentConversation.messagesSentStartIndex;
    } else {
        currentConversation.username = otherusername;
    }

    if (currentConversation.messagesSentStartIndex == 0) {
        lastMessageDeliveredIndex = -1;
    }

    GetStoredMessagesReturnValue returnVal = messagesDictionary[userPair].getStoredMessages(clientusername, lastMessageDeliveredIndex);

    // Update current conversation information
    currentConversation.messagesSentStartIndex = returnVal.firstMessageIndex;
    currentConversation.messagesSentEndIndex = returnVal.lastMessageIndex;
    currentConversationsDict[clientusername] = currentConversation;
    currentConversationsDictMutex.unlock();

    return returnVal.messageList;
}

void parseLine(std::vector<std::string> line) {
    // 
    if (line.size() < 8) {
        std::cout<<"would have seg faulted " << std::to_string(line.size()) <<std::endl;
        return;
    }
    int operation = stoi(line[0]);

    switch (operation) {
        case CREATE_ACCOUNT:
            tryCreateAccount(line[1], line[3]);
            break;
        case LOGIN:
            tryLogin(line[1], line[3]);
            break;
        case LOGOUT:
            tryLogout(line[1]);
            break;
        case SEND_MESSAGE:
            trySendMessage(line[1], line[2], line[4]);
            break;
        case QUERY_MESSAGES:
            tryQueryMessages(line[1], line[2]);
            break;
        case DELETE_ACCOUNT:
            tryDeleteAccount(line[1]);
            break;
        case MESSAGES_SEEN:
            tryMessagesSeen(line[1], line[2], stoi(line[5]));
            break; 
        default:
            std::cout << "unrecognized operation" << std::endl;
    }

}

void writeToLogs(std::ofstream& logWriter, int operation, std::string username1 = "NULL", std::string username2= "NULL", std::string password = "NULL", std::string messageContent = "NULL", std::string messagesSeen = "NULL", std::string leader = "NULL", int clockVal = 0) {

    // Check if operation was valid
    if (operation == CREATE_ACCOUNT || operation == LOGIN || operation == LOGOUT || operation == SEND_MESSAGE || operation == QUERY_MESSAGES || operation == MESSAGES_SEEN || operation == DELETE_ACCOUNT) {
        logWriter << std::to_string(operation) << "," << username1 << "," << username2 << "," << password << "," << messageContent << "," << messagesSeen << "," << leader << "," << std::to_string(clockVal) << std::endl;
    } else {
        std::cout << "unrecognized operation in write to logs" << std::endl;
    }

}

void readFile (std::vector<std::vector<std::string>>* content, std::string historyFile) {
    std::vector<std::string> row;
    std::string line, word;

    std::fstream file;
    file.open(historyFile, std::ios::in);
    file.seekg(0, file.beg);
    if (file.is_open()) {
        while (getline(file, line)) {
            row.clear();

            std::stringstream str(line);

            while (getline(str, word, ',')) {
                row.push_back(word);
            }
            content->push_back(row);
        }
    }
    else {
        std::cout<<"Could not open " << historyFile << std::endl;
    }

    file.close();
}

std::vector<std::string> moveToCommit(std::string filename, std::ofstream& writer) {
    std::vector<std::vector<std::string>> content;

    // Read everything out of pending file
    readFile(&content, filename);

    std::ofstream pendingLogOverWriter;

    // write everything back except the last line
    pendingLogOverWriter.open(filename, std::fstream::trunc);

    pendingLogOverWriter << g_csvFields << std::endl;

    for (int i=1; i<content.size()-1; i++) {
        writeToLogs(pendingLogOverWriter, stoi(content[i][0]), content[i][1], content[i][2], content[i][3], content[i][4], content[i][5], content[i][6], stoi(content[i][7]));
    }

    // Commit last line
    int lastIdx = content.size() - 1;
    writeToLogs(writer, stoi(content[lastIdx][0]), content[lastIdx][1], content[lastIdx][2], content[lastIdx][3], content[lastIdx][4], content[lastIdx][5], content[lastIdx][6], stoi(content[lastIdx][7]));

    pendingLogOverWriter.close();
    
    return content[lastIdx];
}

struct OperationClass {
    int clockVal;
    int opCode;
    std::string username1;
    std::string username2;
    std::string password;
    std::string message_content;
    std::string messagesseen;
    std::string leader;
};

struct opCompare {
    bool operator()(const OperationClass& a, const OperationClass& b) {
        return a.clockVal > b.clockVal;
    }
};

bool compareOperations(const OperationClass& a, const OperationClass& b) {
    return a.clockVal < b.clockVal;
}

void sortOperations(std::vector<OperationClass> &operationsList) {
    std::sort(operationsList.begin(), operationsList.end(), compareOperations);
}