#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <time.h>

#include "send_packet.h"

static float loss_probability = 0.0f;

void set_loss_probability( float x )
{
    loss_probability = x;
}

ssize_t send_packet( int sock, void* buffer, size_t size, int flags, struct sockaddr* addr, socklen_t addrlen )
{
    srand48(time(0));
    float rnd = drand48();


    if( rnd < loss_probability)
    {
        //fprintf(stderr, "Dropped: %s\n", (char*)buffer);
        return size;
    }

    //fprintf(stderr, "Sent: %s\n", (char*)buffer);

    return sendto( sock,
                   buffer,
                   size,
                   flags,
                   addr,
                   addrlen );
}