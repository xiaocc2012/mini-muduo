#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

int main()
{
    int sfd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sfd == -1) 
    {   
        fprintf(stderr, "socket create error"); 
        exit(EXIT_FAILURE); 
    }   

    //reuse
    int reuse = 1;  
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); 

    struct sockaddr_in addr; 
    bzero(&addr, sizeof(addr)); 

    addr.sin_family = AF_INET; 
    addr.sin_port = htons(8888); 
    addr.sin_addr.s_addr = htonl(INADDR_ANY); 

    int ret = bind(sfd, (struct sockaddr*)&addr, sizeof(addr)); 

    ret = listen(sfd, 10); 

    const int max_size = 10; 
    int efd = epoll_create(max_size);

    struct epoll_event event; 
    event.events = EPOLLIN; 
    event.data.fd = sfd; 

    epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event); 

    struct epoll_event array[max_size]; 
    bzero(array, sizeof(array)); 

    while (true)
    {   
        int num = epoll_wait(efd, array, max_size, 10); 
        if (num <= 0)   continue;

        for (int i = 0; i != num; ++i)
        {
            struct epoll_event* ev = &array[i];
            int fd = ev->data.fd;
            if (fd == sfd)
            {
                if (ev->events & EPOLLIN)
                {
                    // main socket fd
                    struct sockaddr_in client_addr;
                    bzero(&client_addr, sizeof(client_addr));

                    socklen_t len = sizeof(client_addr);
                    int nfd = accept(sfd, (struct sockaddr*)(&client_addr), &len);
                    if (nfd != -1)
                    {
                        //non-block
                        int flags = fcntl(nfd, F_GETFL, 0);
                        flags |= O_NONBLOCK;
                        fcntl(nfd, F_SETFL, flags);

                        //add to epoll
                        struct epoll_event event;
                        event.events = EPOLLIN | EPOLLOUT;
                        event.data.fd = nfd;

                        epoll_ctl(efd, EPOLL_CTL_ADD, nfd, &event);

                        char buff[32] = {0};
                        inet_ntop(AF_INET, &client_addr.sin_addr, buff, sizeof(buff));

                        printf("client connect: ip:%s port:%d\n", buff, ntohs(client_addr.sin_port));
                    }
                }
            }
            else
            {
                bool error = false;

                if (ev->events & EPOLLIN)
                {
                    char buff[64] = {0};
                    int n = read(fd, buff, sizeof(buff));
                    if (n == -1)
                    {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                        {
                            error = true;
                        }
                    }
                    else if (n == 0)
                    {
                        error = true;
                    }
                    else
                    {
                        write(fd, buff, sizeof(buff));
                    }
                }

                if (ev->events & EPOLLERR)
                {
                    error = true;
                }

                if (error)
                {
                    struct epoll_event event;
                    event.events = EPOLLIN | EPOLLOUT;
                    event.data.fd = 0;

                    epoll_ctl(efd, EPOLL_CTL_DEL, fd, &event);

                    close(fd);
                }
            }
        }
    }

    close(sfd);
    close(efd);

    return 0;
}
