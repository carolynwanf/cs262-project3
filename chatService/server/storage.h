#include "../chatService.pb.h"
#include "../globals.h"

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <map>
#include <tuple>
#include <thread>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <sstream>
#include <algorithm>

// namespaces used
using chatservice::ChatMessage;
using chatservice::Notification;

// TODO: change file name
// TODO: make it so that client tries to connect to others when it receives a not ok message
std::string g_pendingLogFile = "pendingLog";
std::string g_committedLogFile = "committedLog";
std::string g_csvFields = "message_type,username1,username2,password,message_content,messagesseen,leader,clockVal";
std::string g_nullString = "NULL";
std::string g_ElectionString = "olive";


const int g_numberOfServers = 3;

struct CurrentConversation {
    std::string username;
    int messagesSentStartIndex;
    int messagesSentEndIndex;
};

std::map<std::string, CurrentConversation> currentConversationsDict;
std::mutex currentConversationsDictMutex;

// Key: user with active conversations, Value: map from users to number of notifications they have
struct ConversationsDictionary {
    std::unordered_map<std::string, std::unordered_map<std::string, int> > conversations;
    std::mutex notificationsMutex;

    // increment new messages
    void newNotification(std::string senderUsername, std::string recipientUsername) {
        notificationsMutex.lock();
        conversations[recipientUsername][senderUsername]++;
        notificationsMutex.unlock();
    }  

    // decrement seen messages
    void notificationSeen(char senderUsername[g_UsernameLimit], std::string recipientUsername) {
        notificationsMutex.lock();
       conversations[recipientUsername][senderUsername]--;
        notificationsMutex.unlock();
    }

    std::vector<std::pair<char [g_UsernameLimit], char> > getNotifications(std::string recipientUsername) {
        std::vector<std::pair<char [g_UsernameLimit], char> > allNotifications;

        for (auto const& pair : conversations[recipientUsername]) {
            if (pair.second > 0) {
                std::pair<char [g_UsernameLimit], char> notificationItem;
                strcpy(notificationItem.first, pair.first.c_str());
                notificationItem.second = pair.second;

                allNotifications.push_back(notificationItem);
            }
        }

        return allNotifications;
    }


};

ConversationsDictionary conversationsDictionary;

// Messages dictionary key, consists of two usernames.
struct UserPair {
    std::string smallerUsername; // lexicographically smaller username
    std::string largerUsername; // lexicographically larger username

    // Initializes user pair using lexicographic ordering
    UserPair (std::string username1, std::string username2) {

        int compResult = username1.compare(username2);

        if (compResult >= 0) {
            smallerUsername = username2;
            largerUsername = username1;
        } else {
            smallerUsername = username1;
            largerUsername = username2;
        }
    }

    friend bool operator== (const UserPair& pair1, const UserPair& pair2);
};

bool operator== (const UserPair& pair1, const UserPair& pair2) {
    return (pair1.smallerUsername == pair2.smallerUsername) && (pair1.largerUsername == pair2.largerUsername);
}

// A single messsage from a the message dictionary value vector
struct StoredMessage {
    std::string senderUsername;
    bool isRead;
    std::string messageContent;

    StoredMessage (std::string username, bool read, std::string content) {
        senderUsername = username;
        isRead = read;
        messageContent = content;
    }
};

// Wrapping for the return value of getStoredMessages in the StoredMessages struct
struct GetStoredMessagesReturnValue {
    int lastMessageIndex;
    int firstMessageIndex;
    std::vector<ChatMessage> messageList;
};

// A list of stored messages
struct StoredMessages {
    std::vector<StoredMessage> messageList;
    std::mutex messageMutex;

    // Adding a new message onto the messageList
    void addMessage(std::string senderUsername, std::string recipientUsername, std::string message) {
        messageMutex.lock();

        StoredMessage newMessage(senderUsername, false, message);
        messageList.push_back(newMessage);

        // Increment unread messages for recipient 
        conversationsDictionary.newNotification(senderUsername, recipientUsername);
        messageMutex.unlock();
    }

    // Setting a subset of messages as read given the username of the reader
    void setRead(int startingIndex, int endingIndex, std::string readerUsername) {
        messageMutex.lock();
        for (int i = startingIndex; i < endingIndex + 1; i++) {
            if (messageList[i].senderUsername != readerUsername) {
                messageList[i].isRead = true;
                conversationsDictionary.notificationSeen(const_cast<char*>(messageList[i].senderUsername.c_str()), readerUsername);
            }
        }
        messageMutex.unlock();

    }

    // Returning the messages a user queries
    GetStoredMessagesReturnValue getStoredMessages(std::string readerUsername, int lastMessageDeliveredIndex) {
        // assert(lastMessageDeliveredIndex!=0);
        messageMutex.lock();

        // Keep track of the last unread message for that user
        GetStoredMessagesReturnValue returnValue;
        int currNumberOfMessages = messageList.size();
        int firstMessageIndex; 
        int lastMessageIndex; 

        // Calculate which messages need to be returned
        if (lastMessageDeliveredIndex == -1) {
            // If no previous messages were delivered
            firstMessageIndex = std::max(currNumberOfMessages - int(g_MessageQueryLimit), 0);
            lastMessageIndex = std::min(firstMessageIndex + int(g_MessageQueryLimit), currNumberOfMessages -1);
        } else {
            // If there were previous consecutive queries
            lastMessageIndex = lastMessageDeliveredIndex - 1;
            firstMessageIndex = std::max(lastMessageIndex - int(g_MessageQueryLimit), 0);
        }

        returnValue.firstMessageIndex = firstMessageIndex;
        returnValue.lastMessageIndex = lastMessageIndex;


        // Grab relevant messages
        for (int i = firstMessageIndex; i < lastMessageIndex+1; i++) {
            ChatMessage newItem;
            newItem.set_senderusername(messageList[i].senderUsername);
            newItem.set_msgcontent(messageList[i].messageContent);

            returnValue.messageList.push_back(newItem);
        }

        messageMutex.unlock();

        return returnValue;
    }

};


template<>
struct std::hash<UserPair>
{
    size_t operator()(const UserPair& pair) const
    {
        std::string concat = pair.smallerUsername + pair.largerUsername;
        return std::hash<std::string>{}(concat);
    }
};

std::unordered_map<UserPair, StoredMessages> messagesDictionary;

std::mutex activeUser_mutex;
std::unordered_set<std::string> activeUsers;


struct CharNode {
    char character;
    std::unordered_map<char, CharNode*> children;
    bool isTerminal;

    CharNode(char c, bool b) {
        character = c;
        isTerminal = b;
    }

    friend struct UserTrie;
};

std::unordered_map<CharNode*, std::string> userPasswordMap;


struct UserTrie {
    private:
        std::unordered_map<char, CharNode*> roots;

    public:
        // add new username to trie; return whether username was added successfully or not
        //      If username could not be added throws invalid_argument exception
        void addUsername(std::string username, std::string password) {
            if (!validString(username)) {
                std::string errorMsg = "Username '" + username + "' is invalid. Must be alphanumeric and at least 1 character.";
                throw std::invalid_argument(errorMsg);
            }

            std::pair<CharNode*, int> nodeIdxPair = findLongestMatchingPrefix(username);
            CharNode* currNode = nodeIdxPair.first;

            if (currNode == nullptr) {
                currNode = new CharNode(username[0], false);
                roots[username[0]] = currNode;
                nodeIdxPair.second = 0;
            }
            else if (currNode->isTerminal) {
                std::cout << "Username '" << username << "' has already been taken." << std::endl;
            }

            for (int idx = nodeIdxPair.second+1; idx < username.size(); idx++) {
                char c = username[idx];
                CharNode* newChild = new CharNode(c, false);
                currNode->children[c] = newChild;
                currNode = newChild;
            }

            currNode->isTerminal = true;
            userPasswordMap[currNode] = password;
        }

        // Returns a vector of users with given prefix, if none found returns a runtime exception
        std::vector<std::string> returnUsersWithPrefix(std::string usernamePrefix) {
            if (usernamePrefix.size() == 0) {
                // Perform DFS starting at deepest node
                std::vector<std::string> usersFound;
                performDFS(usernamePrefix, nullptr, usersFound);
                std::cout << "Got usernames" << std::endl;
                return usersFound;
            }
            std::pair<CharNode*, int> nodeIdxPair = findLongestMatchingPrefix(usernamePrefix);
            CharNode* deepestNode = nodeIdxPair.first;
            int index = nodeIdxPair.second;

            if (index != usernamePrefix.size()-1) {
                std::string errorMsg = "No usernames found for prefix '" + usernamePrefix + "'";
                throw std::runtime_error(errorMsg);
            }

            // Perform DFS starting at deepest node
            std::vector<std::string> usersFound;
            performDFS(usernamePrefix, deepestNode, usersFound);
            return usersFound;
        }

        std::pair<CharNode*, int> findLongestMatchingPrefix(std::string username) {
            if (roots.find(username[0]) == roots.end()) {
                return std::make_pair(nullptr, -1);
            }

            CharNode* deepestNode = roots[username[0]];

            // Find deepest matching prefix
            int idx = 1;
            while (idx < username.size()) {
                char c = username[idx];
                if ((deepestNode->children).find(c) == (deepestNode->children).end()) {
                    break;
                }
                deepestNode = deepestNode->children[c];
                idx++;
            }

            return std::make_pair(deepestNode, idx-1);
        }


        // Given a substring and current node, append to vector if node ends a username
        void performDFS(std::string substring, CharNode* currNode, std::vector<std::string> &usersFound) {
            if (substring.size() == 0 || currNode == nullptr) {
                for (auto it = roots.begin(); it != roots.end(); ++it) {
                    std::string newSubstr = substring + (*it).first;
                    performDFS(newSubstr, (*it).second, usersFound);
                }
                return;
            }
            if (currNode->isTerminal) {
                usersFound.push_back(substring);
            }
            for (auto it = currNode->children.begin(); it != currNode->children.end(); ++it) {
                std::string newSubstr = substring + (*it).first;
                performDFS(newSubstr, (*it).second, usersFound);
            }
        }

        bool userExists(std::string user) {
            std::pair<CharNode*, int> nodeIdxPair = findLongestMatchingPrefix(user);
            if (nodeIdxPair.first == nullptr || nodeIdxPair.second < user.size()-1 || !nodeIdxPair.first->isTerminal) {
                return false;
            }

            return nodeIdxPair.first->isTerminal;
        }

        bool verifyUser(std::string username, std::string password) {
            std::pair<CharNode*, int> nodeIdxPair = findLongestMatchingPrefix(username);
            if (nodeIdxPair.first == nullptr || nodeIdxPair.second < username.size()-1 || !nodeIdxPair.first->isTerminal) {
                std::cout << "User '" << username << "' not found." << std::endl;
                return false;
            }

            return password == userPasswordMap[nodeIdxPair.first];
        }

        void deleteUser(std::string username) {
            std::pair<CharNode*, int> nodeIdxPair = findLongestMatchingPrefix(username);
            if (nodeIdxPair.first == nullptr || nodeIdxPair.second < username.size()-1 || !nodeIdxPair.first->isTerminal) {
                std::string errorMsg = "User '" + username + "' not found.";
                throw std::runtime_error(errorMsg);
            }

            nodeIdxPair.first->isTerminal = false;
            userPasswordMap.erase(nodeIdxPair.first);
        }
};

std::mutex userTrie_mutex;
UserTrie userTrie;

// Global storage for new messsage operations
std::mutex queuedOperations_mutex;
std::unordered_map<std::string, std::vector<Notification>> queuedOperationsDictionary;
std::unordered_map<int, bool> forceLogoutDictionary;


// Cleaning up session-related storage structures
void cleanup(std::string clientUsername, std::thread::id thread_id, int client_fd) {
    std::cout << "killing thread :" << thread_id << std::endl;
    queuedOperationsDictionary.erase(clientUsername);
    close(client_fd);
}

char threadExitReturnVal [50];