#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h> // for sleep()
#include <sys/socket.h>//  for socket(), bind(), listen(), accept(), send(), recv()
#include <netinet/in.h> // for sockaddr_in structure
#include <dirent.h> // for opendir(), readdir(), closedir()
#include <sys/stat.h> // for stat()
#include <arpa/inet.h> // for inet_ntop()
#include <algorithm> // for std::find()
#include <vector> // for std::vector
#include <fstream>  // for std::ofstream
#include <ctime>  

#define PORT 8080 // define the port
#define BUFFER_SIZE 1024 // define the buffer size (1KB)
#define SERVER_FOLDER "./" // select cwd as the server folder
#define SYNC_INTERVAL 10 // define the sync interval

bool fileExists(const std::string& filename) {  // check if the file exists
    struct stat buffer; 
    return (stat(filename.c_str(), &buffer) == 0); // stat() returns 0 if the file exists
}

time_t getFileLastModifiedTime(const std::string& filename) { // get the last modified time of the file
    struct stat buffer; // create a buffer to store the file information
    stat(filename.c_str(), &buffer); // get the file information and store it in the buffer
    return buffer.st_mtime; // return the last modified time
}

void receiveFile(int clientSocket, const std::string& filename) {
    std::ofstream file(SERVER_FOLDER + filename, std::ios::binary); // open the file in binary mode

    if (file.is_open()) {   // if the file is open
        // Receive file size
        std::streampos fileSize; // streampos is a type that represents the current position of the file pointer
        int bytesRead = recv(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0); // receive the number of bytes in the file from the client (reinterpret_cast is used to convert fileSize to char*)

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

void synchronizeFiles(int clientSocket) {

    // vector to store received filenames from local directory
    std::vector<std::string> files;

    // Receive missing files and update existing ones
    while (true) { 
        // Receive file name from the client
        char buffer[BUFFER_SIZE]; // create a buffer of size 1KB
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0); // receive the file name from the client

        // if the file name is not received correctly or the buffer is empty (end of file list)
        if (bytesRead <= 0 || strncmp(buffer, "OVR", 3) == 0) {
            break;  // End of file list
        }
        std::string filename(buffer); // convert the buffer to a string to get the file name 
        files.push_back(filename); // add the file name to the vector of files to compare later on the server side (to delete files that are no longer existing on client side)
        send(clientSocket, "OK", 2, 0); // send the acknowledgment to the client that the file name was received successfully

        time_t lastModifiedTime; // create a variable to store the last modified time
        std::memset(buffer, 0, sizeof(buffer)); // reset the buffer
        bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0); // receive the last modified time from the client
        if (bytesRead != sizeof(lastModifiedTime)) { // if the last modified time is not received correctly
            std::cerr << "Error receiving last modified time for: " << filename << std::endl;
            continue; // continue to the next file
        }
        
        std::memcpy(&lastModifiedTime, buffer, sizeof(lastModifiedTime)); // copy the last modified time from the buffer to the variable

        if (!fileExists(SERVER_FOLDER + filename) || lastModifiedTime > getFileLastModifiedTime(SERVER_FOLDER + filename)) {
            std::ofstream changelog(SERVER_FOLDER "changelog.txt", std::ios::app); // open the changelog file in append mode (to append new logs to the end of the file)
            bool existing = fileExists(SERVER_FOLDER + filename); // check if the file already exists before receiving it, to know if we need to log an update or a receive
            send(clientSocket, "SEND", 4, 0); // send the message to the client to send the file
            receiveFile(clientSocket, filename); // receive the file from the client
            time_t now = time(0); // get the current time to log it
            if (existing){ // if the file already existed before receiving, we log an update
                changelog <<"["<< now << "] -" << " [-UPDATED-] - "<< filename << " from " << getFileLastModifiedTime(SERVER_FOLDER + filename) << " to " << lastModifiedTime << std::endl;
            } else {
                changelog <<"["<< now << "] -" << "[x] RECEIVED - "<< filename << std::endl; // if the file didn't exist before receiving, we log a receive
            }
            changelog.close(); // close the changelog file
        } else {
            send(clientSocket, "SKIP", 4, 0); // send the message to the client to skip the file if the file already exists
        }

    }

    // compare client names to files on remote directory, to delete files that are no longer existing on client side
    DIR* dir; // pointer to the directory
    struct dirent* ent; // pointer to the directory entry (file)
    // check all files in server directory to see if they are in the vector, if not, we delete
    if ((dir = opendir(SERVER_FOLDER)) != nullptr) { // open the server folder and check if it is not null (nullptr)
        while ((ent = readdir(dir)) != nullptr) { // read the directory entries one by one and check if it is not null (nullptr)
            if ((ent->d_type == DT_REG) && (strcmp(ent->d_name, "server") != 0) && (strcmp(ent->d_name, "changelog.txt") != 0)) {  // regular file and not "server" or "changelog.txt"
                std::string filename(ent->d_name); // get the filename from the directory entry
                if (std::find(files.begin(), files.end(), filename) == files.end()) { // if the file is not found in the vector
                    std::ofstream changelog(SERVER_FOLDER "changelog.txt", std::ios::app); // open the changelog file in append mode (to append new logs to the end of the file)
                    time_t now = time(0);
                    changelog <<"["<< now << "] -" <<  " [=DELETED=] X " << filename << std::endl;
                    std::cout <<"["<< now << "] -" <<  " [=DELETED=] X " << filename << std::endl;
                    remove((SERVER_FOLDER + filename).c_str()); // delete the file
                    changelog.close(); // close the changelog file
                }
            }
        }
        closedir(dir);
    } else {
        std::cerr << "Error opening server folder: " << SERVER_FOLDER << std::endl;
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
    clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen); // accept the connection from the client
    if (clientSocket == -1) { // check if connection is not accepted
        std::cerr << "Error accepting connection" << std::endl;
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    char clientIP[INET_ADDRSTRLEN]; // create a buffer to store the client IP address, inet_addrstrlen is the maximum length of the IP address
    inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN); // convert the client IP address to a string, sin_addr is the IP address in the client address structure
    std::cout << "Connection accepted from " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl; // ntohs() converts the port number to host byte order

    while (true) {
        synchronizeFiles(clientSocket); // synchronize the files


        // Sleep for SYNC_INTERVAL seconds before the next synchronization
        std::cout << "Waiting for the next synchronization in " << SYNC_INTERVAL << " seconds..." << std::endl;
        sleep(SYNC_INTERVAL);
    }

    close(clientSocket);

    // Close the server socket
    close(serverSocket);

    return 0;
}
