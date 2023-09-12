// simplechatclient.cpp
//
// a client to connect to the simplechat server

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>

#define PORT "9025" // the port client will be connecting to 

const unsigned int MAX_BUF_LEN = 4096;
int sockfd;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
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
                exit(1);
            } 
            else if (errno != EWOULDBLOCK) {
                perror("recv");
                close(socket);
                socket = -1;
            }
        } else {
            std::cout << "Beginning append process from buffer to rmsg\n";
            rmsg.append(rbuf.cbegin(), rbuf.cend());
        }
    std::cout << "Returning rmsg\n";
    return rmsg;
}

void sendMsg(int socket, const std::string& msg) {
    std::cout << "sendMsg() called\n";
    if (send(socket, msg.c_str(), msg.size(), 0) < 0) {
        if (errno != EWOULDBLOCK) {
        perror("send");
        }
    }
}

bool enableSocketNonBlocking(int fd) {
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return false;
    else return true;
}

int main(void) {
    struct addrinfo hints, *servinfo, *p;
    int rv, pollcount;
    char s[INET6_ADDRSTRLEN];
    std::string receive, send;
    pollfd inputpfd, serverpfd;
    std::vector<pollfd> pfds;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo("127.0.0.1", PORT, &hints, &servinfo)) != 0) { //connecting to localhost for now
        std::cout << "getaddrinfo: %s\n";
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
    std::cout << "client: connecting to " << s << "\n";

    freeaddrinfo(servinfo); // all done with this struct

    if (enableSocketNonBlocking(sockfd) == false) {
        std::cout << "Setting sockfd to nonblock failed. Error: " << errno << "\n";
    }
    //if (enableSocketNonBlocking(STDIN_FILENO) == false) {
    //    std::cout << "Setting stdin to nonblock failed. Error: " << errno << "\n";
    //}
    //if (enableSocketNonBlocking(STDOUT_FILENO) == false) {
    //    std::cout << "Setting stdout to nonblock failed. Error: " << errno << "\n";
    //}

    inputpfd.fd = STDIN_FILENO;
    inputpfd.events = POLLIN;
    inputpfd.revents = 0;

    serverpfd.fd = sockfd;
    serverpfd.events = POLLIN;
    serverpfd.revents = 0;

    pfds.push_back(inputpfd);
    pfds.push_back(serverpfd);

    while(1) {
        if (pfds.empty()) {
            perror("poll");
            exit(1);
        }

        for (auto i : pfds) {
            pollcount = poll(pfds.data(), pfds.size(), 5000);
            std::cout << "Calling poll() on pfds\n";
            if (pollcount == -1) {
                perror("poll");
                exit(1);
            }
            if (i.revents & POLLIN) {
                receive = getMsg(sockfd);
                std::cout << receive << "\n";
            } 
            if (i.revents & POLLIN) {
                std::cout << "sockfd [" << i.fd << "] is ready to read\n";
                std::cout << "polled socket is STDIN_FILENO\n";
                std::getline(std::cin, send);
                std::cout << "Message to be sent: " << send << "\n";
                sendMsg(sockfd, send);
            } 
        }
    }


        
    receive = getMsg(sockfd);
    std::cout << receive << "\n";
    
    close(sockfd);
    std::cout << "Client closing connection\n";
    return 0;
}