#include "takeInput.h"

#include <iomanip>
#include <cstring>

int main (void) {
    std::string server_addr;
    ChatServiceClient client;
    while (true) {
        std::cout << "Input Address of Server (IP_ADDR:PORT_NUMBER), if done enter 'y'\n";
        std::cin >> server_addr;
        if (server_addr == "y") {
            break;
        }
        client.addServerAddress(server_addr);
        // Creates connection to last address added
        client.changeStub(server_addr);
    }


    // Main loop for user
    char userInput[g_InputLimit];
    std::cin.ignore();
    while(g_ProgramRunning) {
        if (!takeInput(userInput)) {
            continue;
        }

        ParsedInput input;

        try {
            input = parseInput(userInput);
        } catch (std::invalid_argument &e) {
            std::cout << e.what() << std::endl;
            printUsage();
        }


        // Process input
        switch (input.operation)
        {
            case CREATE_ACCOUNT:
                {
                    if (input.arguments.size() != 2) {
                        std::cout << "Incorrect number of arguments for create_account." << std::endl;
                        printUsage();
                        break;
                    }
                    std::string username = input.arguments[0];
                    std::string password = input.arguments[1];
                    try {
                        validateField("username", username, g_UsernameLimit);
                        validateField("password", password, g_PasswordLimit);
                        client.createAccount(username, password);
                    } catch (std::invalid_argument &e) {
                        std::cout << e.what() << std::endl;
                    } catch (std::runtime_error &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                break;

            case LOGIN:
                {
                    if (input.arguments.size() != 2) {
                        std::cout << "Incorrect number of arguments for login." << std::endl;
                        printUsage();
                        break;
                    }

                    std::string username = input.arguments[0];
                    std::string password = input.arguments[1];
                    try {
                        validateField("username", username, g_UsernameLimit);
                        validateField("passoword", password, g_PasswordLimit);
                        client.login(username, password);
                    } catch (std::invalid_argument &e) {
                        std::cout << e.what() << std::endl;
                    } catch (std::runtime_error &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                break;

            case LOGOUT:
                {
                    try {
                        client.logout();
                    } catch (std::runtime_error &e) {
                        e.what();
                    }
                }
                break;
            
            case LIST_USERS:
                {
                    if (input.arguments.size() > 1) {
                        std::cout << "Incorrect number of arguments for list_users." << std::endl;
                        printUsage();
                        break;
                    }
                    
                    try {
                        std::string prefix;
                        if (input.arguments.size() == 1) {
                            prefix = input.arguments[0];
                            validateField("username", prefix, g_UsernameLimit);
                        }
                        client.listUsers(prefix);
                    } catch (std::invalid_argument &e) {
                        std::cout << e.what() << std::endl;
                    } catch (std::runtime_error &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                break;
            
            case SEND_MESSAGE:
                {
                    if (input.arguments.size() != 2) {
                        std::cout << "Incorrect number of arguments for send_message." << std::endl;
                        printUsage();
                        break;
                    }

                    std::string username = input.arguments[0];
                    std::string msg_content = input.arguments[1];

                    try {
                        validateField("username", username, g_UsernameLimit);
                        client.sendMessage(username, msg_content);
                    } catch (std::invalid_argument &e) {
                        std::cout << e.what() << std::endl;
                    } catch (std::runtime_error &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                break;
            
            case QUERY_NOTIFICATIONS:
                {
                    if (input.arguments.size() != 0) {
                        std::cout << "Incorrect number of arguments for query_notifications." << std::endl;
                        printUsage();
                        break;
                    }

                    try {
                        client.queryNotifications();
                    } catch (std::runtime_error &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                break;

            case QUERY_MESSAGES:
                {
                    if (input.arguments.size() != 1) {
                        std::cout << "Incorrect number of arguments for query_messages." << std::endl;
                        printUsage();
                        break;
                    }

                    std::string username = input.arguments[0];

                    try {
                        validateField("username", username, g_UsernameLimit);
                        client.queryMessages(username);
                    } catch (std::invalid_argument &e) {
                        std::cout << e.what() << std::endl;
                    } catch (std::runtime_error &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                break;
            
            case DELETE_ACCOUNT:
                {
                    if (input.arguments.size() != 2) {
                        std::cout << "Incorrect number of arguments for delete_account." << std::endl;
                        printUsage();
                        break;
                    }

                    std::string username = input.arguments[0];
                    std::string password = input.arguments[1];

                    try {
                        validateField("username", username, g_UsernameLimit);
                        validateField("password", password, g_PasswordLimit);
                        client.deleteAccount(username, password);
                    } catch (std::invalid_argument &e) {
                        std::cout << e.what() << std::endl;
                    } catch (std::runtime_error &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                break;

            case HELP:
                printUsage();
                break;

            default:
                break;
        }

        client.refresh();
     }

    return 0;
}
