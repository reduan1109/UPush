//TODO: optimize imports
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <signal.h>

#define MSG_BUFF_SIZE 1400
#define NICK_SIZE 20
#define HEARTBEAT 3

struct nick_list
{
    char nick[NICK_SIZE];       //Nickname of client
    char ip[INET_ADDRSTRLEN];   //IP of client
    unsigned short port;        //Port of client //TODO: make ushort
    long cur_seq;               //Expected message sequence.
    long last_seen;             //Timestamp used for timeouts
    short retries;              //Number of retries
    short in_flight_flag;       //Flag, is a message in flight? 
    short updated_flag;         //Flag, is the information updated? (for timeout lookup) 
    short blocked_flag;         //Flag, is the person blocked?
    struct msg_list* msgs;      //Message queue
    struct nick_list* next_nick;//Nextpointer to next client in list
}__attribute__((packed));

struct msg_list
{
    char msg[MSG_BUFF_SIZE];    //The message
    struct msg_list* next_msg;  //Next pointer
}__attribute__((packed));

//See common.c
extern char* reg;
extern char* ack_ok;
extern char* look;
extern char* look_fail;
extern char* look_succ;
extern char* txt_msg;
extern char* ack_wn;
extern char* ack_wf;

extern void check_perror (int ret, char* msg);
extern void check_null (void* ret, char* msg);
extern void check_error (int ret, char* msg);

extern void remove_spaces(char* s);
extern void get_string(char* buffer, size_t size);
extern int check_ascii(char* s);
extern int check_format(char* s);

extern void print_nick_list(struct nick_list* nl);
extern void print_msg_list(struct msg_list* msgs);

extern void* free_nick_list(struct nick_list* nl);
extern void* free_msg_list(struct msg_list* msgs);

extern struct nick_list* create_nick_list(char* nick, char* ip, unsigned short port);
extern struct nick_list* add_to_nick_list(struct nick_list* new_nick, struct nick_list* nl);
extern struct nick_list* find_nick(char* nick, struct nick_list* nl);
extern struct nick_list* find_address(char* ip, int port, struct nick_list* nl);
extern struct nick_list* remove_nick(char* nick, struct nick_list* nl);

extern struct msg_list* create_msg_list(char* msg);
extern struct msg_list* add_to_msg_list(struct msg_list* new_msg, struct msg_list* start);
extern struct msg_list* pop_msg(struct msg_list** start);
#endif