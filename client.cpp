#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h> // for sleep()
#include <sys/socket.h> // for socket(), connect(), send(), recv()
#include <netinet/in.h> // for sockaddr_in structure
#include <dirent.h> // for opendir(), readdir(), closedir()
#include <sys/stat.h> // for stat() 
#include <arpa/inet.h> // for inet_addr()

#define PORT 8080 // define the port
#define BUFFER_SIZE 1024 // define the buffer size (1KB)
#define CLIENT_FOLDER "./" // select cwd as the client folder
#define SYNC_INTERVAL 10 // define the sync interval

bool fileExists(const std::string& filename) { // check if the file exists
    struct stat buffer; 
    return (stat(filename.c_str(), &buffer) == 0); 
}

time_t getFileLastModifiedTime(const std::string& filename) { // get the last modified time of the file
    struct stat buffer;
    stat(filename.c_str(), &buffer);
    return buffer.st_mtime;
}

void sendFile(int serverSocket, const std::string& filename) {
    std::ifstream file(CLIENT_FOLDER + filename, std::ios::binary); // open the file in binary mode

    if (file.is_open()) { // if the file is open

        file.seekg(0, std::ios::end); // set the file pointer to the end of the file
        std::streampos fileSize = file.tellg(); // get the current position of the file pointer to get the file size by subtracting the beginning of the file pointer from the end of the file pointer
        file.seekg(0, std::ios::beg); // seek back to the beginning of the file

        send(serverSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0); // send the file size to the server

        char buffer[BUFFER_SIZE]; 
        while (!file.eof()) { // while the end of the file is not reached
            file.read(buffer, sizeof(buffer)); // read the file content into the buffer 
            send(serverSocket, buffer, file.gcount(), 0);  // send the file content to the server in chunks, gcount() returns the number of characters extracted by the last unformatted input operation
        }

        file.close();

        char ackBuffer[BUFFER_SIZE]; 
        int bytesRead = recv(serverSocket, ackBuffer, sizeof(ackBuffer), 0); // receive the acknowledgment from the server that the file was received successfully
        if (bytesRead <= 0 || strncmp(ackBuffer, "success", 7) != 0) { // check if the acknowledgment is not received correctly
            std::cerr << "Error receiving acknowledgment for: " << filename << std::endl;
        }

        std::cout << "Sent file: " << filename << std::endl;
    } else {
        std::cerr << "Error opening file: " << filename << std::endl;
        send(serverSocket, nullptr, 0, 0);
    }
}

void synchronizeFiles(int serverSocket) {
    DIR* dir; // pointer to the directory
    struct dirent* ent; // pointer to the directory entry (file) 

    if ((dir = opendir(CLIENT_FOLDER)) != nullptr) { // open the client folder and check if it is not null (nullptr)
        while ((ent = readdir(dir)) != nullptr) { // read the directory entries one by one and check if it is not null (nullptr)
            if ((ent->d_type == DT_REG) && (strcmp(ent->d_name, "client") != 0)) {  // regular file and not "client"
                std::string filename(ent->d_name); // get the filename from the directory entry
                send(serverSocket, filename.c_str(), filename.size() + 1, 0); // send the filename to the server
                char okBuffer[BUFFER_SIZE]; // create a buffer of size 1KB
                int bytesRead = recv(serverSocket, okBuffer, sizeof(okBuffer), 0); // receive the acknowledgment from the server
                if (bytesRead <= 0 || strncmp(okBuffer, "OK", 2) != 0) { // check if the acknowledgment is not received correctly
                    std::cerr << "Error receiving acknowledgment for: " << filename << std::endl;
                    continue;
                }
                time_t lastModifiedTime = getFileLastModifiedTime(CLIENT_FOLDER + filename); // get the last modified time of the file
                send(serverSocket, reinterpret_cast<char*>(&lastModifiedTime), sizeof(lastModifiedTime), 0); // send the last modified time of the file to the server
                // we now wait for the server to tell us if it wants the file or not (SEND or SKIP)
                char ackBuffer[BUFFER_SIZE];
                bytesRead = recv(serverSocket, ackBuffer, sizeof(ackBuffer), 0); // receive the acknowledgment from the server
                if (bytesRead <= 0 || strncmp(ackBuffer, "SEND", 4) == 0) { // check if the acknowledgment is not received correctly or if the server wants the file
                    sendFile(serverSocket, filename); // send the file to the server
                }
            }
        }

        // after sending all the files, send an empty message to the server to tell that the file list is complete
        send(serverSocket, "OVR", 3, 0);

        closedir(dir);
    } else {
        std::cerr << "Error opening client folder: " << CLIENT_FOLDER << std::endl;
    }
}

int main() {
    
    int clientSocket; // define the client socket
    struct sockaddr_in serverAddr; // define the server address structure
    clientSocket = socket(AF_INET, SOCK_STREAM, 0); // create a TCP socket, AF_INET for IPv4, SOCK_STREAM for TCP, 0 for the protocol

    // check if the socket was created successfully
    if (clientSocket == -1) { 
        std::cerr << "Error creating socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    // set the server address structure
    serverAddr.sin_family = AF_INET; // set the address family to IPv4
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // set the IP address to localhost
    serverAddr.sin_port = htons(PORT); // set the port number to the one defined above

    // connect to the server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Error connecting to server" << std::endl;
        close(clientSocket);
        exit(EXIT_FAILURE);
    }

    std::cout << "Connected to server" << std::endl;

    while (true) { 
        synchronizeFiles(clientSocket); // synchronize the files
        std::cout << "Waiting for the next synchronization in " << SYNC_INTERVAL << " seconds..." << std::endl;
        sleep(SYNC_INTERVAL); // sleepzZz for SYNC_INTERVAL seconds
    }

    // Close the client socket
    close(clientSocket);

    return 0;
}
