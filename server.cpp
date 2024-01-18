#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define PORT 8080 // define the port
#define BUFFER_SIZE 1024 // define the buffer size (1KB)
#define SERVER_FOLDER "./" // select cwd as the server folder
#define SYNC_INTERVAL 30 // define the sync interval

bool fileExists(const std::string& filename) {  // check if the file exists
    struct stat buffer; 
    return (stat(filename.c_str(), &buffer) == 0); // stat() returns 0 if the file exists
}

void receiveFile(int clientSocket, const std::string& filename) {
    std::ofstream file(SERVER_FOLDER + filename, std::ios::binary); // open the file in binary mode

    if (file.is_open()) {   // if the file is open
        // Receive file size
        std::streampos fileSize; // streampos is a type that represents the current position of the file pointer
        int bytesRead = recv(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0); // receive the file size

        if (bytesRead != sizeof(fileSize)) {
            std::cerr << "Error receiving file size for: " << filename << std::endl; // if the file size is not received correctly
            file.close();
            return; // return from the function
        }


        char buffer[BUFFER_SIZE]; // create a buffer of size 1KB
        while (fileSize > 0) { // while the file size is greater than 0
            bytesRead = recv(clientSocket, buffer, std::min(sizeof(buffer), static_cast<size_t>(fileSize)), 0); // receive the file content, min() is used to avoid buffer overflow and static_cast is used to convert fileSize to size_t

            if (bytesRead <= 0) { // if the file content is not received correctly
                std::cerr << "Error receiving file content for: " << filename << std::endl;
                file.close();
                return;
            }

            file.write(buffer, bytesRead); // write the file content to the file on the server side
            fileSize -= bytesRead; // decrease the file size by the number of bytes read
        }

        file.close(); // close the file

        // Send acknowledgment to the client
        send(clientSocket, "success", 7, 0); // send the acknowledgment to the client that the file was received successfully

        std::cout << "Received file: " << filename << std::endl;
    } else {
        std::cerr << "Error opening file for writing: " << SERVER_FOLDER + filename << std::endl;
        // Inform the client about the error
        send(clientSocket, nullptr, 0, 0); // send the error message to the client
    }
}

void synchronizeFiles(int clientSocket) { // synchronize the files
    while (true) { 
        // Receive file name from the client
        char buffer[BUFFER_SIZE]; // create a buffer of size 1KB
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0); // receive the file name from the client

        // if the file name is not received correctly or the buffer is empty (end of file list)
        if (bytesRead <= 0 || buffer[0] == '\0') {
            break;  // End of file list
        }

        std::string filename(buffer); // convert the buffer to a string to get the file name 


        if (!fileExists(SERVER_FOLDER + filename)) {
            send(clientSocket, "SEND", 4, 0); // send the message to the client to send the file
            receiveFile(clientSocket, filename); // receive the file from the client
        } else {
            // TODO: Check if the file on the client side is newer than the one on the server side
            send(clientSocket, "SKIP", 4, 0); // send the message to the client to skip the file if the file already exists
        }
    }
}

int main() {
    int serverSocket, clientSocket; // create the server and client sockets
    struct sockaddr_in serverAddr, clientAddr; // create the server and client address structures
    socklen_t addrLen = sizeof(clientAddr); // get the size of the client address structure

    // creating socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0); 
    if (serverSocket == -1) {
        std::cerr << "Error creating socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    // setup server address structure
    serverAddr.sin_family = AF_INET; // set the address family to IPv4
    serverAddr.sin_addr.s_addr = INADDR_ANY; // set the IP address to the localhost, INADDR_ANY allows to bind to any address
    serverAddr.sin_port = htons(PORT); // set the port number

    // binding the socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) { 
        std::cerr << "Error binding socket" << std::endl;
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    // listen for incoming connections
    if (listen(serverSocket, 5) == -1) { // 5 is the maximum size of the queue, if listen() returns -1, it means that the queue is full or the socket is not listening
        std::cerr << "Error listening for connections" << std::endl;
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    while (true) {
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen); // accept the connection from the client
        if (clientSocket == -1) { // check if connection is not accepted
            std::cerr << "Error accepting connection" << std::endl;
            close(serverSocket);
            exit(EXIT_FAILURE);
        }

        char clientIP[INET_ADDRSTRLEN]; // create a buffer to store the client IP address
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN); // convert the client IP address to a string
        std::cout << "Connection accepted from " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl; // ntohs() converts the port number to host byte order


        synchronizeFiles(clientSocket); // synchronize the files


        close(clientSocket); // close the client socket

        // Sleep for SYNC_INTERVAL seconds before the next synchronization
        std::cout << "Waiting for the next synchronization in " << SYNC_INTERVAL << " seconds..." << std::endl;
        sleep(SYNC_INTERVAL);
    }

    // Close the server socket
    close(serverSocket);

    return 0;
}
