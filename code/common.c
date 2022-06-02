#include "common.h"

//Pullout regex to another file?
char* ack_ok = "ACK %ld OK";                    
char* reg = "PKT %ld REG %s";                   
char* look = "PKT %ld LOOKUP %s";               
char* look_fail = "ACK %ld NOT FOUND";          
char* look_succ = "ACK %ld NICK %s %s PORT %d"; 
char* txt_msg = "PKT %ld FROM %s TO %s MSG %s";                
char* ack_wn = "ACK %ld WRONG NAME";        
char* ack_wf = "ACK %ld WRONG FORMAT";      

//Removes whitespace characters from nullterminated buffer using isspace()
void remove_spaces(char* str)
{
    char* tmp = str;
    while ((*str++ = *tmp++))
        while (isspace(*tmp)) 
            ++tmp;   
}

//Checks if printable ascii. 1 on success, -1 on failure
int check_ascii(char* str)
{
    if (str == NULL) return -1;
    for (int i = 0; str[i] != 0; i++)
        if (str[i] < 32 || str[i] > 126)
            return -1;
    return 1;
}

//Prints errno using perror(msg) if ret == -1 and exits with EXIT_FAILURE
void check_perror (int ret, char* msg)
{
    if (ret == -1)
    {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

//Prints msg if ret == NULL and exits with EXIT_FAILURE
void check_null (void* ret, char* msg)
{
   if (ret == NULL)
    {
        fprintf(stderr, msg);
        exit(EXIT_FAILURE);
    } 
}

//Prints msg if ret == -1 and exits with EXIT_FAILURE
void check_error (int ret, char* msg)
{
   if (ret == -1)
    {
        fprintf(stderr, msg);
        exit(EXIT_FAILURE);
    } 
}

//Hentet fra: Cbra-2021-04-13
void get_string(char* buffer, size_t size)
{
    char c;
    fgets(buffer, size, stdin);
    if(buffer[strlen(buffer) - 1] == '\n')
        buffer[strlen(buffer) - 1] = 0;
    else
        while((c = getchar()) != '\n' && c != EOF);
}

//Creates and instantiates  nick_list struct
struct nick_list* create_nick_list(char* nick, char* ip, unsigned short port)
{
    struct nick_list* output = malloc(sizeof(struct nick_list));
    check_null(output, "malloc create_nick_list() failed");

    strncpy(output -> nick, nick, NICK_SIZE);    
    strcpy(output -> ip, ip);
    output -> port = port;
    output -> last_seen = 0;
    output -> in_flight_flag = 0;
    output -> blocked_flag = 0;
    output -> retries = 0;
    output -> updated_flag = 0;
    output -> cur_seq = 0; 
    output -> msgs = NULL;
    output -> next_nick = NULL;

    return output;
}

//Creates and instantiates a msg_list struct
struct nick_list* add_to_nick_list(struct nick_list* new_nick, struct nick_list* nl)
{
    if (nl == NULL) return new_nick;

    struct nick_list* prev = NULL;
    struct nick_list* tmp = nl;

    while (tmp != NULL)
    {
        if (strcmp(tmp -> nick, new_nick -> nick) == 0) 
        {
            strcpy(tmp -> ip, new_nick -> ip);
                   tmp -> port = new_nick -> port;
                   tmp -> last_seen = new_nick -> last_seen;
            free_nick_list(new_nick);
            return nl;
        }
        prev = tmp;
        tmp = tmp -> next_nick;
    }

    prev -> next_nick = new_nick;

    return nl;
}

//Finds the nick struct if its in the list using the nickname
struct nick_list* find_nick(char* nick, struct nick_list* nl)
{
    if (nl == NULL || nick == NULL) return NULL;

    struct nick_list* tmp = nl;

    while (tmp != NULL) 
    {
        if (strcmp(tmp -> nick, nick) == 0) return tmp;
        
        tmp = tmp -> next_nick;
    }

    return NULL;
}

//Finds the nick struct if its in the list using the ip and port
struct nick_list* find_address(char* ip, int port, struct nick_list* nl)
{
    if (nl == NULL) return NULL;

    struct nick_list* tmp = nl;

    while (tmp != NULL) 
    {
        if (strcmp(tmp -> ip, ip) == 0 && tmp -> port == port) return tmp;
        
        tmp = tmp -> next_nick;
    }

    return NULL;
}


//Removes and frees the nick from the list
struct nick_list* remove_nick(char* nick, struct nick_list* nl)
{
    if (nl == NULL) return NULL;

    struct nick_list* previous = NULL;
    struct nick_list* current = nl;

    while (current != NULL) 
    {
        if (strcmp(current -> nick, nick) == 0)
        {
            //Is first nick
            if(current == nl)
            {
                nl = nl -> next_nick;
                free_msg_list(current->msgs);
                free(current);
                current = nl;
                return nl;
            }
            else
            {
                previous -> next_nick = current -> next_nick;
                free_msg_list(current->msgs);
                free(current);
                current = previous -> next_nick;
                return nl;
            }
        }
        else
        { 
            previous = current;
            current = current -> next_nick;
        }
    }

    return nl;
}

//Frees the struct list
void* free_nick_list(struct nick_list* nl)
{
    struct nick_list* tmp;

    while (nl != NULL)
    {
        tmp = nl;
        nl = nl -> next_nick;
        free_msg_list(tmp -> msgs);
        free(tmp);
    }
    
    return NULL;
}

//Creates and instantiates a msg_list struct
struct msg_list* create_msg_list(char* msg)
{
    struct msg_list* output = malloc(sizeof(struct msg_list));
    check_null(output, "malloc create_msg_list() failed");

    strcpy(output -> msg, msg);
    output -> next_msg = NULL;

    return output;
}

//Adds to the msg list struct
struct msg_list* add_to_msg_list(struct msg_list* new_msg, struct msg_list* start)
{
    if (start == NULL) return new_msg;

    struct msg_list* prev = NULL;
    struct msg_list* tmp = start;

    while(tmp  != NULL)
    {
        prev = tmp;
        tmp = tmp -> next_msg;
    }
    
    prev -> next_msg = new_msg;
    return start;
}

//Pops a msg off the struct
struct msg_list* pop_msg(struct msg_list** start)
{
    struct msg_list* tmp = *start;
    if (tmp)
    {
        *start = tmp -> next_msg;
        tmp -> next_msg = NULL;
    }

    return tmp;
}

//Print nick_list struct
void print_nick_list(struct nick_list* nl)
{
    if (nl == NULL)
    {
        printf("Empty :)\n");
        return;
    }
    

    struct nick_list* tmp_nl = nl;
    while (tmp_nl != NULL) 
    {
        fprintf(stderr, "%s | %s | %i \n",
        tmp_nl -> nick, tmp_nl -> ip, tmp_nl -> port);
        tmp_nl = tmp_nl -> next_nick;
    }
}

//Print msg_list struct
void print_msg_list(struct msg_list* msgs)
{
    if (msgs == NULL)
    {
        printf("Empty :)\n");
        return;
    } else {
        fprintf(stderr, "-=MSGS=-\n");
    }

    int i = 0;
    struct msg_list* tmp_msg = msgs;
    while (tmp_msg != NULL) 
    {
        fprintf(stderr, "%d. \"%s\"\n", i, tmp_msg->msg);
        tmp_msg = tmp_msg -> next_msg;
        i++;
    }
}

//Frees the struct list
void* free_msg_list(struct msg_list* msgs)
{ 
    struct msg_list* tmp = NULL;
    while (msgs != NULL)
    {
        tmp = msgs;
        msgs = msgs -> next_msg;
        free(tmp);
    }

    return msgs;
}