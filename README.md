# How to use
All subsequent steps require that you first download this repository and open the file containing it in your command line, and install gRPC.

## Server
1. From the root directory, run `./build/chatServer`
2. When prompted, choose a port number from 8080, 8081, 8082 for your server
3. Enter the addresses of the rest of the replicas in your service, or press "y" to just use one server
4. Wait until a leader is elected
5. You're good to go!

## Client
1. From the root directory, run `.build/chatClient`
2. When prompted, input the IP addresses of the servers (the server machine(s) will print this to the command line)
3. Press "y" to continue
4. For usage directions, run `help`

## Testing
Note that for this, you need to install [gtest](http://google.github.io/googletest/quickstart-cmake.html).

1. From the root directory, run `cd build && ctest`
2. The results will print to the terminal!
