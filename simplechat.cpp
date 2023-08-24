#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdexcept>

#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#define PORT "9034"   // Port we're listening on

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
        throw std::runtime_error("getaddrinfo() failed");
        exit(1);

    }
    
    // Loop through the returned linked list of addrinfo structs until we can bind one
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
        
        // Set socket options, mainly for allowing reuse of address
        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0 ) {
            throw std::runtime_error("Sock option setting failed");
        }

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
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
    return newpollfd;
}


int main(void)
{
    int listener;     // Listening sockfd

    int newfd;        // Newly accept()ed sockfd
    struct sockaddr_storage remoteaddr; // Client address
    socklen_t addrlen;

    char buf[256];    // Buffer for client data

    char remoteIP[INET6_ADDRSTRLEN];

    // Setup vector of poll fds
    std::vector<pollfd> pfds;

    // Set up and get a listening socket
    listener = get_listener_socket();

    if (listener == -1) {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }
    pollfd firstpfd;
    pfds.push_back(pfdsetup(listener, firstpfd));


    // Main loop
    for(;;) {

        if (pfds.empty()) {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (auto i : pfds) {
            poll(pfds.data(), pfds.size(), -1);

            // Check for an fd that is ready to read
            if (i.revents & POLLIN) { // We got one!!

                if (i.fd == listener) {
                    // If ready to read, accept()

                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        pollfd nextpfd;
                        pfds.push_back(pfdsetup(newfd, nextpfd));


                        printf("pollserver: new connection from %s on "
                            "socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_sock_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
                    }
                } else {
                    // If not the listener, we're just a regular client
                    int nbytes = recv(i.fd, buf, sizeof buf, 0);

                    int sender_fd = i.fd;

                    if (nbytes <= 0) {
                        // Got error or connection closed by client
                        if (nbytes == 0) {
                            // Connection closed
                            printf("pollserver: socket %d hung up\n", sender_fd);
                        } else {
                            perror("recv");
                        }

                        close(i.fd); // Bye

                    } else {
                        // We got some good data from a client

                        for(auto j : pfds) {
                            // Send to everyone!
                            int dest_fd = j.fd;

                            // Except the listener and ourselves
                            if (dest_fd != listener && dest_fd != sender_fd) {
                                if (send(dest_fd, buf, nbytes, 0) == -1) {
                                    perror("send");
                                }
                            }
                        }
                    }
                } // END handle data from client
            } // END got ready-to-read from poll()
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!
    
    return 0;
}