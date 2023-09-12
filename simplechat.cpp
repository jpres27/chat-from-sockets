// simplechat.cpp
//
// a server for running the simplechat messenging system

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#define PORT "9025"   // Port we're listening on

const unsigned int MAX_BUF_LEN = 4096;

// Obtain appropriately casted sock address, IPv4 or IPv6
void* get_sock_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Obtain listener socket and return it
int get_listener_socket(void)
{
    int listener;     // Listening socket descriptor
    int yes=1;        // For setsockopt() SO_REUSEADDR, below
    int rv;

    struct addrinfo hints, *ai, *p;

    // Get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // bind to the host the server is running on for now, can later be dropped
                                 // and the IP address we want to connect to would be placed in for the first
                                 // argument to getaddrinfo()
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        std::cout << "getaddrinfo() failed";
        exit(1);

    }
    
    // Loop through the returned linked list of addrinfo structs until we can bind one
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        std::cout << "socket() returned fd: [" << listener << "]\n";
        if (listener < 0) { 
            continue;
        }
        
        // Set socket options, mainly for allowing reuse of address
        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0 ) {
            std::cout << "Sock option setting failed\n";
        }

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            std::cout << "bind() failed on fd: [" << listener << "]\n";
            close(listener);
            continue;
        }

        break;
    }

    freeaddrinfo(ai); // Free no longer needed addrinfo struct

    // If we got here, it means we didn't get bound
    if (p == NULL) {
        return -1;
    }

    // Listen
    if (listen(listener, 10) == -1) {
        return -1;
    }

    return listener;
}

pollfd pfdsetup(int newfd, pollfd & newpollfd) {
    newpollfd.fd = newfd;      
    newpollfd.events = POLLIN; // Report ready to read on incoming connection
    newpollfd.revents = 0;
    return newpollfd;
}

std::string getMsg(int socket) {
    std::cout << "Calling getMsg()\n";
    std::vector<char> rbuf(MAX_BUF_LEN);
    std::cout << "Initialized rbuf vector to MAX_BUF_LEN size\n";
    std::string rmsg;
    std::cout << "Declared string rmsg\n";
    unsigned int rbytes = 0;
    rbytes = recv(socket, rbuf.data(), rbuf.size(), 0);
    std::cout << "Called recv(), rbytes value is " << rbytes << "\n";
        if (rbytes <= 0) {
            if (rbytes == 0) {
                std::cout << "pollserver: socket %d hung up\n" + socket;
            } else {
            perror("recv");
            }
            close(socket);
            socket = -1;
        } else {
            std::cout << "Beginning append process from buffer to rmsg\n";
            rmsg.append(rbuf.cbegin(), rbuf.cend());
        }
    std::cout << "Returning rmsg\n";
    return rmsg;
}

void sendMsg(int socket, const std::string& msg) {
    std::cout << "Calling sendMsg()\n";
    if (send(socket, msg.c_str(), msg.size(), 0) == -1) {
        perror("send");
    }
}


int main(void) {
    struct sockaddr_storage remoteaddr; // Client address
    int listener, newfd, sender_fd, dest_fd, pollcount;
    socklen_t addrlen;
    pollfd firstpfd, nextpfd;
    char remoteIP[INET6_ADDRSTRLEN];
    std::string msg;
    std::vector<pollfd> pfds;

    listener = get_listener_socket();
    std:: cout << "Listener socket is fd: [" << listener << "]\n";

    if (listener == -1) {
        std::cout << errno;
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    pfds.push_back(pfdsetup(listener, firstpfd));
    std::cout << "Put listener fd into pollfd struct and pushed onto pfds vector\n";

    for(;;) {

        if (pfds.empty()) {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (auto i : pfds) {
            pollcount = poll(pfds.data(), pfds.size(), -1);
            std::cout << "Calling poll() on pfds\n";
            if (pollcount == -1) {
                perror("poll");
                exit(1);
            }
            // Check for an fd that is ready to read
            if (i.revents & POLLIN) { // We got one!!
                std::cout << "sockfd [" << i.fd << "] is ready to read\n";
                if (i.fd == listener) {
                    // If ready to read, accept()
                    std::cout << "sockfd " << i.fd << " is listener\n";

                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);
                    std::cout << "Assigned accept()ed sockfd " << i.fd <<  " to newfd variable\n";
                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        pfds.push_back(pfdsetup(newfd, nextpfd));
                        std::cout << "Added " << i.fd << " to pfds vector\n";


                        printf("pollserver: new connection from %s on "
                            "socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_sock_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
                    }
                } else {
                    // If not the listener, we're just a regular client
                        std::cout << "sockfd is client, entering else branch for msg broadcast\n";
                        msg = getMsg(i.fd);
                        std::cout << "Retrieved message: " << msg << "\n";
                        sender_fd = i.fd;

                        for(auto j : pfds) {
                            // Send to everyone!
                            std::cout << "broadcasting msg\n";
                            if (j.fd != -1) {
                                dest_fd = j.fd;

                                // Except the listener and ourselves
                                if (dest_fd != listener && dest_fd != sender_fd) {
                                    sendMsg(dest_fd, msg);
                                }
                            }
                        }
                    }
                } // END handle data from client
            } // END got ready-to-read from poll()
    } // END for(;;)--and you thought it would never end!
    
    return 0;
}