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
#define CLIENT_FOLDER "./"
#define SYNC_INTERVAL 30

bool fileExists(const std::string& filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

void sendFile(int serverSocket, const std::string& filename) {
    std::ifstream file(CLIENT_FOLDER + filename, std::ios::binary);

    if (file.is_open()) {
        // Send file size
        file.seekg(0, std::ios::end);
        std::streampos fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        send(serverSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);

        // Send file content
        char buffer[BUFFER_SIZE];
        while (!file.eof()) {
            file.read(buffer, sizeof(buffer));
            send(serverSocket, buffer, file.gcount(), 0);
        }

        file.close();

        // Wait for acknowledgment from the server
        char ackBuffer[BUFFER_SIZE];
        int bytesRead = recv(serverSocket, ackBuffer, sizeof(ackBuffer), 0);
        if (bytesRead <= 0 || strncmp(ackBuffer, "ACK", 3) != 0) {
            std::cerr << "Error receiving acknowledgment for: " << filename << std::endl;
        }

        std::cout << "Sent file: " << filename << std::endl;
    } else {
        std::cerr << "Error opening file: " << filename << std::endl;
        // Inform the server about the error
        send(serverSocket, nullptr, 0, 0);
    }
}

void synchronizeFiles(int serverSocket) {
    DIR* dir;
    struct dirent* ent;

    if ((dir = opendir(CLIENT_FOLDER)) != nullptr) {
        // Send the list of files in the client folder
        while ((ent = readdir(dir)) != nullptr) {
            if ((ent->d_type == DT_REG) && (strcmp(ent->d_name, "client") != 0)) {  // Regular file and not "client"
                std::string filename(ent->d_name);
                send(serverSocket, filename.c_str(), filename.size() + 1, 0);

                // Wait for the server to check if the file needs to be sent
                char ackBuffer[BUFFER_SIZE];
                int bytesRead = recv(serverSocket, ackBuffer, sizeof(ackBuffer), 0);
                if (bytesRead <= 0 || strncmp(ackBuffer, "SEND", 4) == 0) {
                    sendFile(serverSocket, filename);
                }
            }
        }

        // Signal the end of the file list
        send(serverSocket, nullptr, 0, 0);

        closedir(dir);
    } else {
        std::cerr << "Error opening client folder: " << CLIENT_FOLDER << std::endl;
    }
}

int main() {
    int clientSocket;
    struct sockaddr_in serverAddr;

    // Create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        std::cerr << "Error creating socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Setup server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Replace with the server's IP address
    serverAddr.sin_port = htons(PORT);

    // Connect to the server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Error connecting to server" << std::endl;
        close(clientSocket);
        exit(EXIT_FAILURE);
    }

    std::cout << "Connected to server" << std::endl;

    while (true) {
        // Synchronize files with the server
        synchronizeFiles(clientSocket);

        // Sleep for SYNC_INTERVAL seconds before the next synchronization
        std::cout << "Waiting for the next synchronization in " << SYNC_INTERVAL << " seconds..." << std::endl;
        sleep(SYNC_INTERVAL);
    }

    // Close the client socket
    close(clientSocket);

    return 0;
}
