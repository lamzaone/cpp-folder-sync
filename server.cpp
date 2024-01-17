#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define SERVER_FOLDER "./"
#define SYNC_INTERVAL 30

bool fileExists(const std::string& filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

void receiveFile(int clientSocket, const std::string& filename) {
    std::ofstream file(SERVER_FOLDER + filename, std::ios::binary);

    if (file.is_open()) {
        // Receive file size
        std::streampos fileSize;
        int bytesRead = recv(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);

        if (bytesRead != sizeof(fileSize)) {
            std::cerr << "Error receiving file size for: " << filename << std::endl;
            file.close();
            return;
        }

        // Receive file content
        char buffer[BUFFER_SIZE];
        while (fileSize > 0) {
            bytesRead = recv(clientSocket, buffer, std::min(sizeof(buffer), static_cast<size_t>(fileSize)), 0);

            if (bytesRead <= 0) {
                std::cerr << "Error receiving file content for: " << filename << std::endl;
                file.close();
                return;
            }

            file.write(buffer, bytesRead);
            fileSize -= bytesRead;
        }

        file.close();

        // Send acknowledgment to the client
        send(clientSocket, "ACK", 3, 0);

        std::cout << "Received file: " << filename << std::endl;
    } else {
        std::cerr << "Error opening file for writing: " << SERVER_FOLDER + filename << std::endl;
        // Inform the client about the error
        send(clientSocket, nullptr, 0, 0);
    }
}

void synchronizeFiles(int clientSocket) {
    while (true) {
        // Receive file name from the client
        char buffer[BUFFER_SIZE];
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytesRead <= 0 || buffer[0] == '\0') {
            break;  // End of file list
        }

        std::string filename(buffer);

        // Check if the file exists or needs to be updated
        if (!fileExists(SERVER_FOLDER + filename)) {
            // File does not exist on the server, request the client to send it
            send(clientSocket, "SEND", 4, 0);
            receiveFile(clientSocket, filename);
        } else {
            // File already exists on the server, check if it needs an update
            // (you can add more sophisticated logic here, e.g., by comparing timestamps)
            send(clientSocket, "SKIP", 4, 0);
        }
    }
}

int main() {
    int serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Error creating socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Setup server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    // Bind the socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Error binding socket" << std::endl;
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(serverSocket, 5) == -1) {
        std::cerr << "Error listening for connections" << std::endl;
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << PORT << std::endl;

       while (true) {
        // Accept a connection
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == -1) {
            std::cerr << "Error accepting connection" << std::endl;
            close(serverSocket);
            exit(EXIT_FAILURE);
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
        std::cout << "Connection accepted from " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;

        // Synchronize files with the connected client
        synchronizeFiles(clientSocket);

        // Close the client socket
        close(clientSocket);

        // Sleep for SYNC_INTERVAL seconds before the next synchronization
        std::cout << "Waiting for the next synchronization in " << SYNC_INTERVAL << " seconds..." << std::endl;
        sleep(SYNC_INTERVAL);
    }

    // Close the server socket
    close(serverSocket);

    return 0;
}
