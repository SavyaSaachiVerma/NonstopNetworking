#include "common.h"
#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <signal.h>
#include "vector.h"
  
/*		Function Prototypes			*/
void accept_connections(struct epoll_event *e);
void handle_data(struct epoll_event *e);
void handle_new_event(struct epoll_event *e);
void parseHeader( struct epoll_event *e);
void parse_put_get_delete_header(struct epoll_event *e);
void parse_put_size(struct epoll_event *e);
void parse_put_data(struct epoll_event *e);
void parse_delete(struct epoll_event *e);
void parse_list(struct epoll_event *e);
void parse_get(struct epoll_event *e);
void create_response(struct epoll_event *e);
ssize_t write_all_to_socket(int socket, const char *buffer, size_t count);
ssize_t read_all_from_socket(int socket, char *buffer, size_t count);



/*		Global Variables			*/
vector* listEntries;
int epoll_fd;
int sock_fd;
char* temp_directory;
enum connection_state {NEW, VERB_PARSED, HEADER_PARSED, SIZE_PARSED, DATA_PARSED, RESPONSE};
//enum command {PUT, GET, LIST, DELETE, V_UNKNOWN};

typedef struct server_event{
int fd;
char dataBuff[1024];
char headerBuff[1024];
char fileName[256];
char verbBuff[12];
int fileDescriptor;
char sizeBuff[8];
size_t size;
int sizeOffset;
int dataOffset;
int verbOffset;
int headerOffset;
int done;
enum connection_state state;
verb Verb; 
} server_event;

void intHandler (int sig){
	
//	printf("Shutting Down Sever\n");
	close(epoll_fd);
	shutdown(sock_fd, SHUT_RDWR);
	close(sock_fd);
	size_t numFiles=vector_size(listEntries);
        for(size_t i=0; i<numFiles; i++){
                char* file=vector_get(listEntries, i);
		remove(file);
	}
	chdir("..");
	rmdir(temp_directory);
	exit(1);
}

int main(int argc, char** argv)
{
	
	signal(SIGPIPE, SIG_IGN); 							//Ignoring the SIGPIPE Signal will handle elsewhere

	struct sigaction sig;
    	sig.sa_handler = intHandler;
    	sigaction(SIGINT, &sig, NULL);

	listEntries=string_vector_create();

	int s;
	char* port=argv[1];

	//struct epoll_event *events;

    	// Create the socket as a nonblocking socket
    	sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    	struct addrinfo hints, *result = NULL;
    	memset(&hints, 0, sizeof(struct addrinfo));
    	hints.ai_family = AF_INET;
    	hints.ai_socktype = SOCK_STREAM;
   	hints.ai_flags = AI_PASSIVE;

    	s = getaddrinfo(NULL, port, &hints, &result);
    	if (s != 0) {
        	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        	exit(1);
    	}

    	int optval = 1;
    	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    	if ( bind(sock_fd, result->ai_addr, result->ai_addrlen) != 0 )
    	{
        	perror("bind()");
        	exit(1);
    	}

    	if ( listen(sock_fd, 10) != 0 )
    	{
        	perror("listen()");
        	exit(1);
    	}

	//setup epoll
 	epoll_fd = epoll_create(1);
	if(epoll_fd == -1)
    	{
        	perror("epoll_create()");
        	exit(1);
    	}

	struct epoll_event event;
	event.data.fd = sock_fd;
	event.events = EPOLLIN | EPOLLET;
	//Add the sever socket to the epoll
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event))
	{
        	perror("epoll_ctl()");
        	exit(1);
	}
	
	char template[]="XXXXXX";
	temp_directory = mkdtemp (template);
	print_temp_directory(temp_directory);
	
	if( chdir(temp_directory) == -1){
		printf("chdir error"); 
	}

	// Event loop
	while(1) {
		struct epoll_event new_event;

		if(epoll_wait(epoll_fd, &new_event, 1, -1) > 0)
		{
			//Probably check for errors

			// New Connection Ready
			if(sock_fd == new_event.data.fd)
				accept_connections(&new_event);
			else
				handle_data(&new_event);
		}
	}
    return 0;
}

void accept_connections(struct epoll_event *e)
{
        while(1)
        {
                struct sockaddr_in new_addr;
                socklen_t new_len = sizeof(new_addr);
                int new_fd = accept(e->data.fd, (struct sockaddr*) &new_addr, &new_len);

                if(new_fd == -1)
                {
                        // All pending connections handled
                        if(errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                        else
                        {
                                perror("accept");
                                exit(1);
                        }
                }

/*
                char *connected_ip= inet_ntoa(new_addr.sin_addr);
                int port = ntohs(new_addr.sin_port);
        	printf("Accepted Connection %s port %d\n", connected_ip, port);
*/

        	int flags = fcntl(new_fd, F_GETFL, 0);
        	fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
			
		server_event  *new_event= malloc(sizeof(server_event));
		new_event->fd=new_fd;
		new_event->state=NEW;
		new_event->Verb=V_UNKNOWN;
		new_event->headerOffset=0;
		new_event->dataOffset=0;
		new_event->verbOffset=0;
		new_event->size=0;
		new_event->sizeOffset=0;
		new_event->done=0;
		new_event->fileDescriptor=0;

        	//Connection to epoll
        	struct epoll_event event;
        	event.data.ptr = (void*)new_event;
        	event.events = EPOLLIN | EPOLLET;
        	if(epoll_ctl (epoll_fd, EPOLL_CTL_ADD, new_fd, &event) == -1)
        	{
                	perror("accept epoll_ctl");
                	exit(1);
        	}
        }
}

void handle_data(struct epoll_event *e)
{
	server_event* event=(server_event*)(e->data.ptr);
	
       	if(event->state == NEW){
		handle_new_event(e);
	}
	if(event->state == VERB_PARSED){
		parseHeader(e);		
	}
	if(event->state == HEADER_PARSED && event->Verb == PUT){
		parse_put_size(e);
	}
	if(event->state == SIZE_PARSED && (event->Verb == PUT || event->Verb == GET) ){
		parse_put_data(e);
        }
	if(event->state == DATA_PARSED && event->Verb != PUT){
		create_response(e); 
        }

}

void handle_new_event(struct epoll_event *e){
		
	server_event* event=(server_event*)e->data.ptr;
	char* header=event->verbBuff;										//Pointing to the location where it needs to start
	int offset=event->verbOffset;
	header+=offset;	
	
	char buff[1];
	buff[0]='\0';
	
	while(buff[0] != ' ' && buff[0] != '\n'){								//list has \n
        	ssize_t bytesread= read_all_from_socket(event->fd, buff, 1);					//reading bytes one by one to read the verb
		if(bytesread == -2){										//in case EAGAIN occurs
			event->headerOffset=offset;								// points to the space wher next byte has to be stored
			return;											//handle EAGAIN or EWOULDBLOCK
        	}
		else{
			*header=buff[0];
			++header;
			++offset;
		}
	}
	
//	event->headerOffset=0;											//storing the offset in headerBuff

	--header;												//removing read space
	*header='\0';
			
//	printf("Verb: %s\n", header);								//we have read the entire verb
	
	event->state=VERB_PARSED;
}

void parseHeader( struct epoll_event *e){

	server_event* event=(server_event*)(e->data.ptr);
	char* header=event->verbBuff;

        if( strcmp(header,"PUT") == 0 ){
                event->Verb=PUT;
                parse_put_get_delete_header(e);
//		event->state=HEADER_PARSED;
        }
        else if( strcmp(header,"GET") == 0 ){
                event->Verb=GET;
                parse_put_get_delete_header(e);
//		event->state=DATA_PARSED;
//		event->state=HEADER_PARSED;  
      }
        else if( strcmp(header,"DELETE") == 0 ){
                event->Verb=DELETE;
                parse_put_get_delete_header(e);
//		event->state=DATA_PARSED;
		
        }
        else if( strcmp(header,"LIST") == 0 ){
                event->Verb=LIST;
		event->state=DATA_PARSED;
        }
        else{
                char* buff="ERROR\n";
//		printf("VERB\n");
//		printf("%s\n",header);
                write_all_to_socket(event->fd, buff, strlen(buff));
                write_all_to_socket(event->fd, err_bad_request, strlen(err_bad_request));				//ERR!! MALFORMED REQUEST
	        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
	        shutdown(event->fd, SHUT_RDWR);
        	close(event->fd);

                return;                   
	}

	
}
	
void parse_put_get_delete_header(struct epoll_event *e){
//	printf("header \n");	

	server_event* event=(server_event*)(e->data.ptr);
	strcpy(event->headerBuff,"");
	char* header=event->headerBuff;  
	int offset=event->headerOffset;
        header+=offset;
	
	char buff[1]="";

	while(buff[0] != '\n' && offset < 256){
 
	       	ssize_t bytesread= read_all_from_socket(event->fd, buff, 1);   
//		printf("Byte Read: %c\n", buff[0]);				                                 //reading bytes one by one to read the verb
                if(bytesread == -2 ){
                        event->headerOffset=offset;                                                             // points to the space wher next byte has to be stored
                        return;                                                                                 //handle EAGAIN or EWOULDBLOCK
                }
                else{
                        *header=buff[0];
                        ++header;
                        ++offset;
                }
        }

	if( offset >= 256 ){											//filename too long
		write_all_to_socket(event->fd, err_bad_request, strlen(err_bad_request));			//ERR!! MALFORMED REQUEST
//		printf("FILENAME SIZE\n");
	        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
	        shutdown(event->fd, SHUT_RDWR);
        	close(event->fd);
		return;
	}
	
	--header;
	*header='\0';
//	printf("Header: %s %s\n", header - event->headerOffset, event->headerBuff);
	event->headerOffset=0;
	strcpy(event->fileName,event->headerBuff);								//store the filename read
//	printf("%s\n",event->fileName);
	if(event->Verb == PUT){
		event->state=HEADER_PARSED;
	}
	else if(event->Verb == GET){
		event->state=DATA_PARSED;
	}
	else if(event->Verb == DELETE){
		event->state=DATA_PARSED;
	}
}

void parse_put_size(struct epoll_event *e){
	
//	printf("size \n");

	server_event* event=(server_event*)(e->data.ptr);	
	vector_push_back( listEntries, (void*)(event->fileName));						//Logging the file name to list of fn's
	ssize_t totbytesread=event->sizeOffset;
//	printf("TOTBYTESREAD= %zd\n", totbytesread);
	char* header=event->sizeBuff+totbytesread;
	char buff[1];
	buff[0]='\0';
	ssize_t bytesread=0;

 	while(totbytesread<8){
		bytesread=read_all_from_socket(event->fd, buff, 1);								//next 8 bytes 
		if(bytesread == -2){	
			event->sizeOffset=totbytesread;
			return;
		}
		else{
			*header=buff[0];
			++header;
			++totbytesread;
		}
	}
//	printf("size read:- %zu \n", event->size);	
	memcpy(&(event->size), event->sizeBuff, 8);
		
//		event->size =*((size_t*)buff);
//	size_t *test = malloc(sizeof(size_t));
//	memcpy(test, buff, 8);
//	printf("Size: %d\n", (int)test);
//	printf("size read:-%zu\n", event->size);

	event->state=SIZE_PARSED;
//	printf("%s\n",event->fileName);
       	event->fileDescriptor = open(event->fileName ,O_CREAT | O_TRUNC | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);		//open file to write data

}

void parse_put_data(struct epoll_event *e){

	server_event* event=(server_event*)e->data.ptr;
	size_t size=event->size;
	char* buff=event->dataBuff;
//	int fildes = open(event->fileName ,O_CREAT | O_TRUNC | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
	ssize_t bytes=0;
	ssize_t bytesread=0;
	ssize_t totbytesread=event->dataOffset;

	while((size_t)totbytesread < size){
		bytesread=read_all_from_socket(event->fd, buff, 1024);
//		printf("br : %zd\n",bytesread);		
		if(bytesread == 0){							//when the client has shut down from his side
	                char* buff="ERROR\n";
        	        write_all_to_socket(event->fd, buff, strlen(buff));
			write_all_to_socket(event->fd, err_bad_file_size, strlen(err_bad_file_size));  		//ERR!! TOO LITTLE DATA
//			printf("TOO LITTLE \n");
	        	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
        		shutdown(event->fd, SHUT_RDWR);
	        	close(event->fd);
			remove(event->fileName);
			return;
		}
		 else if(bytesread == -2){                                               //server isnt able to read any byte and gets blocked
                        event->dataOffset=totbytesread;
                        return;
                }
		else if(bytesread < 0){							//when the server reads some bites but then it blocks
			bytes= 0 - bytesread;
			write(event->fileDescriptor, buff, bytes);
			totbytesread+=bytes;
			event->dataOffset=totbytesread;
			return;
		}	
		else{									//server reads the desired number of bytes
			write(event->fileDescriptor, buff, bytesread);
			totbytesread+=bytesread;
		}

	} 

	if((size_t)totbytesread > size){
                char* buff="ERROR\n";
                write_all_to_socket(event->fd, buff, strlen(buff));
		write_all_to_socket( event->fd, err_bad_file_size, strlen(err_bad_file_size) );				//ERR!! TOO MUCH DATA
//		printf("TOO MUCH\n");
//		printf("ToT:- %zd\n",totbytesread);
	        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
        	shutdown(event->fd, SHUT_RDWR);
        	close(event->fd);
		remove(event->fileName);
		return;
	}

	event->state=DATA_PARSED;

	char* buf="OK\n";
	write_all_to_socket(event->fd, buf, 3);					//response of PUT
	
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
  	shutdown(event->fd, SHUT_RDWR);
      	close(event->fd);


}

void create_response(struct epoll_event *e){
	server_event* event=(server_event*)e->data.ptr;
	
	if(event->Verb == GET)
		parse_get(e);
	else if (event->Verb == DELETE)
		parse_delete(e);
	else if (event->Verb == LIST)
		parse_list(e);
	
	return;
}



void parse_list(struct epoll_event *e){

	size_t numFiles=vector_size(listEntries);
	size_t sizeOfList=0;	

	for(size_t i=0; i<numFiles; i++){
		char* file=vector_get(listEntries, i);
		sizeOfList+=(strlen(file)+1);								//+1 for \n
	}
	--sizeOfList;											//last file doesnt have a \n

	char* buff=malloc(sizeOfList+8+10);

	char* str="OK\n";
	
	memcpy(buff, str, 3);

	memcpy(buff+3, (char*)&sizeOfList, 8); 
	 
	size_t siz=0;
	for( size_t i=0; i<numFiles; i++){
		char* file=vector_get(listEntries, i);
		memcpy(buff+11+siz, file, strlen(file));
		siz+=strlen(file);
		memcpy(buff+11+siz, "\n", 1);
		siz+=1;
	}
	
	memcpy(buff+10+siz,"\0",1); 
	server_event* event =(server_event*)(e->data.ptr);	
	ssize_t byteswritten=0;
	ssize_t totbyteswritten=event->dataOffset;
	ssize_t bytes=0;	
	
	size_t bytesToWrite=sizeOfList+11-totbyteswritten;
	
	
	byteswritten= write_all_to_socket( event->fd, buff+totbyteswritten, bytesToWrite);

	if(byteswritten == -3){								//EPIPE the client closes the socket
		free(buff);
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
		close(event->fd);
		return;
	}
	else if (byteswritten == -2){							// tries to write but cnt even write 1 byte due to blocking			

                e->events=EPOLLIN | EPOLLET | EPOLLOUT;                                 //modifying the event so that we are informed when we can write
                 e->data.ptr=e->data.ptr;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event->fd, e);

		free(buff);
                return;

	}

	if(byteswritten < 0){								//in case we get EAGAIN
		bytes= 0 - byteswritten;
		totbyteswritten+= bytes;
		event->dataOffset=totbyteswritten;
										
		e->events=EPOLLIN | EPOLLET | EPOLLOUT;					//modifying the event so that we are informed when we can write
		e->data.ptr=e->data.ptr;
		epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event->fd, e);

		free(buff);
		return;
	}
	
	free(buff);									//if we have been successfully able to write the required bytes
       	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
	shutdown(event->fd, SHUT_RDWR);
	close(event->fd);
}	

void parse_delete(struct epoll_event *e){
        server_event* event=(server_event*)e->data.ptr;
	if( access(event->fileName , F_OK ) != -1 ){								//if file exists

		remove(event->fileName);								//removing the file from directory
	
		size_t numFiles=vector_size(listEntries);						//removing file from the list 
		for(size_t i=0; i<numFiles; i++){
        	        char* file=vector_get(listEntries, i);
                	if(strcmp(file, event->fileName) == 0){
				vector_erase(listEntries, i);
				break;
			}
        	}
	
		char* buff="OK\n";
		write_all_to_socket(event->fd, buff, strlen(buff));					//response
	}
	else{
		char* buff="ERROR\n";
		write_all_to_socket(event->fd, buff, strlen(buff));
		write_all_to_socket(event->fd, err_no_such_file, strlen(err_no_such_file));
	        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
	        shutdown(event->fd, SHUT_RDWR);
        	close(event->fd);
		return;		
	}
	
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
	shutdown(event->fd, SHUT_RDWR);
        close(event->fd);


}

void parse_get(struct epoll_event *e){
	server_event* event=(server_event*)e->data.ptr;

	if( access( event->fileName, F_OK ) != -1 ){                                                              //if file exists
		struct stat fileInfo;
		size_t fileSize=0;

		if( stat(event->fileName, &fileInfo) == -1){							//Getting the File size
				perror("stat: ");
				exit(1);
			}
		else{
			fileSize=(size_t)(fileInfo.st_size);
		}
		
		FILE* localFile=fopen(event->fileName,"r+");							//file from which data has to be read
		int fildes= fileno(localFile);		
		fseek(localFile, event->dataOffset, SEEK_SET);							//Pointer file pointer to the right location
		
		if(event->done == 0){
			char buff[12]="OK\n";
			memcpy(buff+3, (char*)&fileSize, 8);	
			write_all_to_socket(event->fd, buff, 11);							//writing OK\n[size]=11 by
			event->done= 1;
		}
		
		ssize_t totbyteswritten=event->dataOffset;
		ssize_t byteswritten=0;		
		ssize_t bytesread=0;
		ssize_t bytes=0;
	
		while((size_t)totbyteswritten < fileSize ){
			
			bytesread=read(fildes, event->dataBuff, 1024);

			byteswritten=write_all_to_socket(event->fd, event->dataBuff, bytesread);
			
			if(byteswritten == -2){
			  	e->events=EPOLLIN | EPOLLET | EPOLLOUT;                                 //modifying the event so that we are informed when we can write
	        	        e->data.ptr=e->data.ptr;
        	        	epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event->fd, e);

				fclose(localFile);
				return;
			}
			else if(byteswritten == -3 ){
		                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
        		        close(event->fd);
		
				fclose(localFile);
	                	return;		
			}
			else if(byteswritten < 0){
	        	        bytes= 0 - byteswritten;
        	        	totbyteswritten+= bytes;
	                	event->dataOffset=totbyteswritten;
	
        		        e->events=EPOLLIN | EPOLLET | EPOLLOUT;                                 //modifying the event so that we are informed when we can write
                		e->data.ptr=e->data.ptr;
                		epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event->fd, e);
		
				fclose(localFile);
                		return;
			}	
			else{
				totbyteswritten+=byteswritten;
			}
			
		}
	    	
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
		fclose(localFile);
		shutdown(event->fd, SHUT_RDWR);
		close(event->fd);
	}
	else{
                char* buff="ERROR\n";
                write_all_to_socket(event->fd, buff, strlen(buff));
		write_all_to_socket(event->fd, err_no_such_file, strlen(err_no_such_file));
	        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
	        shutdown(event->fd, SHUT_RDWR);
        	close(event->fd);

	}
}

ssize_t read_all_from_socket(int socket, char *buffer, size_t count) {
		    
	ssize_t bytesread;
	ssize_t totbytesread=0;	
	errno=0;

	while(count){
		bytesread=read(socket, buffer, count);

		if(bytesread == 0)
			return totbytesread;

		if(bytesread == -1 && ( errno == EAGAIN || errno == EWOULDBLOCK )){
			if(totbytesread == 0)
				return -2;
			else
                        	return -totbytesread;
		}
		if( bytesread != -1){
			count -=bytesread;
			buffer += bytesread;
			totbytesread += bytesread;
		}	
	}

    return totbytesread;
}

ssize_t write_all_to_socket(int socket, const char *buffer, size_t count) {
    ssize_t byteswritten;
    ssize_t totbyteswritten=0;
    errno=0;

	while(count){
		byteswritten=write(socket, buffer, count);

		if(byteswritten == 0)
			return totbyteswritten;		

		if(byteswritten == -1 && ( errno == EAGAIN || errno == EWOULDBLOCK )){
        		if(totbyteswritten == 0)
		  		return -2;
			else
				return -totbyteswritten;
		}
		if(byteswritten == -1 && errno == EPIPE)
			return -3;
		
		if(byteswritten != -1){
			count -= byteswritten;
			buffer +=byteswritten;
			totbyteswritten+=byteswritten;
		}
	}
	
    return totbyteswritten; 
}