/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <string>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <algorithm>
#include <pthread.h>
#include <fcntl.h>
#include <map>
#include <time.h>
#include <assert.h>

#include "http-request.h"
#include "http-response.h"

#define MAX_BUF_SIZE 2048
#define MAX_THREAD 20
#define PROXY_PORT "14893"
#define SERVER_PORT 24894
#define REMOTE_SERVER "24894"
#define REMOTE_SERVER2 "34889"

using namespace std;
int thread_count;
//int socket_id;
//local proxy cache
typedef map<string,string> webCache;

//pthread structure to use

typedef struct
{
// pthread_mutex_t* mutexA; 
 pthread_mutex_t* mutex; //mutex to block other threads
 int cfd; //client file descriptor
 webCache* wcache; //each thread has a cache
 const char* sport;

} proxy_thread_t;

/** detects whether the associated date is "expired" or not
*/
bool isExpired(string date,string exp_date)
{
	string e_date;
	string day2,mon2,year2,h2,m2,s2;
	string nmon2;
	long int datenum,edatenum;
	
	day2 = exp_date.substr(5,2);
	mon2 = exp_date.substr(8,3);
	year2 = exp_date.substr(11,5);
	h2 = exp_date.substr(17,2);
	m2 = exp_date.substr(20,2);
	s2 = exp_date.substr(23,2);
	
	if (mon2 == "Jan")
	{nmon2 = "01";}
	else if (mon2 == "Feb")
	{nmon2 = "02";}
	else if (mon2 == "Mar")
	{nmon2 = "03";}
	else if (mon2 == "Apr")
	{nmon2 = "04";}
	else if (mon2 == "May")
	{nmon2 = "05";}
	else if (mon2 == "Jun")
	{nmon2 = "06";}
	else if (mon2 == "Jul")
	{nmon2 = "07";}
	else if (mon2 == "Aug")
	{nmon2 = "08";}
	else if (mon2 == "Sep")
	{nmon2 = "09";}
	else if (mon2 == "Oct")
	{nmon2 = "10";}
	else if (mon2 == "Nov")
	{nmon2 = "11";}
	else if (mon2 == "Dec")
	{nmon2 = "12";}
	
	e_date = year2+nmon2+day2+h2+m2+s2+'\0';
	

	time_t now = time(0);
	char ltime[15];
	string loctime;
	if (strftime(ltime,sizeof(ltime),"%Y%m%d%H%M%S",localtime(&now)) == 0)
	{ assert(0);}
	loctime.append(ltime);

	
	datenum = atol(loctime.c_str());
	edatenum = atol(e_date.c_str());
	
	printf("local time: %ld expire time: %ld \n",datenum,edatenum); 	
	//long int ctime = (long int) time(NULL);
	if (datenum > edatenum)
	{ return true;}
	else
	{ return false;}
}


int socketAccept(int fd_temp)
{
   int socketID;
   struct sockaddr_in req_addr; //address of requesting peer
   socklen_t addr_len = sizeof(req_addr); // size of requesting address
   socketID = accept(fd_temp,(struct sockaddr *) &req_addr, &addr_len);
   if (socketID == -1 && (errno == EINTR || errno == EAGAIN)) //if invalid socket ID or no data available right now, try again later
   { return -1;} 
	
 
   return socketID;

}
/**
Create a new server listener on a port
*/
int serverListen(const char* port)
{

 	struct addrinfo addrInfo,*addr_ptr;
	int sock_fd;
	int yes = 1;
	//load up address structs
	memset(&addrInfo,0,sizeof(addrInfo));
	addrInfo.ai_family = AF_UNSPEC;
	addrInfo.ai_flags = AI_PASSIVE;
 	addrInfo.ai_socktype = SOCK_STREAM;
	
	int addr_status = getaddrinfo(NULL,port,&addrInfo,&addr_ptr);
	if (addr_status != 0)
	{ return -1;}

	struct addrinfo *ptr = addr_ptr;
	//loop through results
	while (ptr != NULL)
	{
		//create the socket
		sock_fd = socket(addr_ptr->ai_family,addr_ptr->ai_socktype,addr_ptr->ai_protocol);
		if (sock_fd < 0)
		{ 
			ptr = ptr->ai_next;
			continue;
		}
		//set the socket options
		int opt_status = setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int));
		if (opt_status == -1)
		{ exit(1);}
		//bind the socket to the specified port
		int bind_status = bind(sock_fd,addr_ptr->ai_addr,addr_ptr->ai_addrlen);
		if (bind_status != 0)
		{ 
			close(sock_fd);
		  	ptr = ptr->ai_next;
		  	continue;
		}
		//break upon first success
		break;
	
	}
	//bind failed
	if (ptr == NULL)
	{ return -1;}
	freeaddrinfo(addr_ptr);
	//begin listening,max 100 connections
	if (listen(sock_fd,100) == -1)
	{ exit(1);}

	return sock_fd;
}
/**
Connect the client to the host with the associated port number
*/
int client_connect(const char* hostname,const char* port)
{
 	struct addrinfo addrInfo,*addr_ptr;
	int sock_fd;
	
	memset(&addrInfo,0,sizeof(addrInfo));
	addrInfo.ai_family = AF_UNSPEC;
 	addrInfo.ai_socktype = SOCK_STREAM;
	//get info about the port 
	int addr_status = getaddrinfo(hostname,port,&addrInfo,&addr_ptr);
	if (addr_status != 0)
	{ return -1;}
	
	struct addrinfo *ptr = addr_ptr;
	//establish socket connection
	while (ptr != NULL)
	{
		//accept socket
		sock_fd = socket(addr_ptr->ai_family,addr_ptr->ai_socktype,addr_ptr->ai_protocol);
		if (sock_fd < 0)
		{
			ptr = ptr->ai_next;
			continue;
		}
		//make the connection
		if (connect(sock_fd,ptr->ai_addr,ptr->ai_addrlen) < 0)
		{
			 ptr = ptr->ai_next;
			 close(sock_fd);
		 	 continue;
		}
		//bind the first one then break
		break;
	}
	//if it failed - just return
	if (ptr == NULL)
	{ return -1;}
	//if all things succeeded return a new socket
	freeaddrinfo(addr_ptr);
	return sock_fd;

}

/**
Get response from the client
*/
int getResponse(int socket_id,string &resp)
{
	int nbytes;
	//infinite loop until we get what we want or until an error occurs.
	while (1)
	{
		char temp;	
		nbytes = recv(socket_id,&temp,1,0);
		if (memmem(resp.c_str(),resp.length(),"\r\n\r\n",4) != NULL)
		{
			//append to the response
			resp.append(&temp,nbytes);
			HttpResponse temp_resp;
			temp_resp.ParseResponse(resp.c_str(),resp.length());
			string rsize = temp_resp.FindHeader("Content-Length");
			int sizeInt = atoi(rsize.c_str());
			char *temp_buf2 = new char[sizeInt];
			//try again with the Content-length
			nbytes = recv(socket_id,temp_buf2,sizeInt,0);
			resp.append(temp_buf2,nbytes);
			free(temp_buf2);
			return 0;

		}
		//if there is an error
		if (nbytes < 0)
		{
			perror("The following receiving error occured");
			return -1;
		}
		
		if (nbytes == 0)
		{ return -1;}
		
		resp.append(&temp,nbytes);
	}
	return -1;
}

/**
Another getResponse that uses FD_ISSET  to make sure that the connection isn't reset while we are recv
*/
int getResponse2(int socket_id,string& resp)
{
	fcntl(socket_id,F_SETFL,O_NONBLOCK);
	int nbytes,sel;
	
	while (1)
	{
		struct timeval wait;
		wait.tv_sec = 2;
		wait.tv_usec = 0; 
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(socket_id,&readfds);
		FD_SET(0,&readfds);

		sel = select(socket_id+1,&readfds,NULL,NULL,&wait);
		char temp;	
		if (sel < 0)
		{ continue;}
		
		else if (sel == 0)
		{
			return 1;
		}
		if (FD_ISSET(socket_id,&readfds))
		{
			
			FD_CLR(socket_id,&readfds);	
			nbytes = recv(socket_id,&temp,1,0);	
			if (nbytes < 0)
			{
				perror("The following receiving error occured");
				return -1;
			}
		
			if (nbytes == 0)
			{return 0;}
			
			resp.append(&temp,1);
		
		}
		
		
	
	}
	return -1;
}


//establishing HTTP connection
int http_connection(int fd,pthread_mutex_t *mutex,webCache* cache,const char* sport)
{
	/*
	fd_set waitflag;
	int sel;
	struct timeval waitd = {8,0};
		

	while(1){
	waitd.tv_sec = waitd.tv_usec = 8;
	FD_ZERO(&waitflag);
	FD_SET(fd,&waitflag);
	sel = select(fd+1,&waitflag,NULL,NULL,&waitd);

	if (sel < 0)
	{continue;}


	if (FD_ISSET(fd,&waitflag)){
	*/
	string response,http_buffer,error,host_addr,version,connection_type,hostname,pathname,mod_date,res_buffer;
	char* req_buffer;
	size_t req_length;
	int socket_id;
	//optain http request data format.
	//http data format ends with two sequences of \r\n
	
	while (memmem(http_buffer.c_str(),http_buffer.length(),"\r\n\r\n",4) == NULL)
	{
		
		//receive data and append it to the string.
		char temp_buf[MAX_BUF_SIZE];
		if (recv(fd,temp_buf,sizeof(temp_buf),0) < 0)
		{return -1;}
	
		http_buffer.append(temp_buf);

	}


	//getting http request
	HttpRequest http_req;
	try
	{ http_req.ParseRequest(http_buffer.c_str(),http_buffer.length());}
	catch (ParseException ex)
	{ 
	 	if (strcmp(ex.what(),"Request is not GET") == 0) //request is not get
		{ error = "HTTP/1.1 501 Not Implemented\r\n\r\n";}
		else  //if request is GET and receives a parse exception then it is a bad request.
		{ error = "HTTP/1.1 400 Bad Request\r\n\r\n";}

		if (send(fd,error.c_str(),error.length(),0) == -1)
		{ perror("The following send error occured\n");}
	}
	
	
	//resolving the name
	hostname = http_req.GetHost(); //get host name
	if (hostname.length() == 0)
	{
		hostname = http_req.FindHeader("Host");
	}
        pathname = hostname + http_req.GetPath(); //get full path to the file		
	webCache::iterator it = cache->find(pathname); //find if path is stored in the cache
	if (it != cache->end()) //cache contains the response
	{
		printf("data has been cached before!\n");
		//extract expiration date from cached response
		HttpResponse tempResp;
		tempResp.ParseResponse(it->second.c_str(),it->second.length());
		string exp_date = tempResp.FindHeader("Expires"); //find expiration date		
		mod_date = tempResp.FindHeader("Last-Modified"); //find last modified date
		string date = tempResp.FindHeader("Date"); //date at which response was received
		if (isExpired(date,exp_date)) //the file has been modified!
		{
			printf("\n\nthe file has been modified!\n\n");
			
			http_req.AddHeader("If-Modified-Since",mod_date); //add a new field to the header
		
			req_length = http_req.GetTotalLength();
			req_buffer = (char*)malloc(req_length);
			http_req.FormatRequest(req_buffer); //send request again to make sure it is not modified again
		
	//		printf("requested message format:\n %s \n",req_buffer);
			socket_id = client_connect(hostname.c_str(),REMOTE_SERVER);
			//forward the newly formatted request to the host	
			if (send(socket_id,req_buffer,req_length,0) == -1)
			{
				perror("The following send error occured");
				close(socket_id); //close open file descriptor
				free(req_buffer); //deallocate dynamically allocated variable
				return -1;	
			}	
		//	printf("boo boo!\n");
			//obtain response
			if (getResponse2(socket_id,response) < 0) //cannot obtain response
			{
				close(socket_id);
				free(req_buffer);
				exit(1);
			}		
		//	printf("whywhy!\n");
					
			HttpResponse http_res;
			http_res.ParseResponse(response.c_str(),response.length());
			string statusMsg = http_res.GetStatusMsg();	
		
			printf("status Msg: %s \n",statusMsg.c_str());
			if (!statusMsg.compare("Not Modified")) //if response is not modified, send cached HTTP response to the client
			{
				printf("\n\n\ncached element has not expired yet.\n\n\n");
				response = it->second;
			}
			//otherwise, store the newly obtained HTTP response in the cache
			else
			{
				printf("\n\n\ncached element has expired. fetching latest version!\n\n\n"); 
				pthread_mutex_lock(mutex);
				cache->insert(pair<string,string>(pathname,response));
				pthread_mutex_unlock(mutex);
			}


			if (send(fd,response.c_str(),response.length(),0) == -1)
			{
				perror("The following send error occured");
				return -1;
			}
			printf("latest response: \n %s \n",response.c_str());	

			
			string connection_status = http_req.FindHeader("Connection");
			if (connection_status.compare("close") == 0) //if HTTP request requests closing the connection 
			{
				printf("file descriptor closed\n");
				close(socket_id);
				close(fd);
				return 0;
			}
			
			else
			{

				close(socket_id);
				return 0;	
				//return http_connection(fd,mutex,cache,sport);
				//continue;
			}		
			
		}
		
		else
		{
			//printf("socket_id :%d\n",socket_id);
			//printf("\n\n The file has not been modified!\n\n");
			response = it->second;
			if (send(fd,response.c_str(),response.length(),0) == -1)
			{
				perror("The following send error occured");
				return -1;
			}
			printf("cached response: \n %s \n",response.c_str());	

			
			string connection_status = http_req.FindHeader("Connection");
			if (connection_status.compare("close") == 0) //if HTTP request requests closing the connection 
			{
				printf("file descriptor closed\n");
				close(fd);
				return 0;
			}
		
			/*
			string connection_status = http_req.FindHeader("Connection");
			if (connection_status.compare("close") == 0) //if HTTP request requests closing the connection 
			{
				printf("file descriptor closed\n");
				close(fd);
				return 0;
			}
			
			*/

			
			
			else
			{
					
				return http_connection(fd,mutex,cache,sport);
			}	
			
		
		} 
	
	}
	else //cache does not contain the response. need to send the request to the host 
	{ 
		printf("data has not been cached before\n");
		try
		{req_length = http_req.GetTotalLength();}
		catch (ParseException ex)
		{
			if (!strcmp(ex.what(),"Only GET method is supported"))
			{error = "HTTP/1.1 501 Not Implemented\r\n\r\n";}
		}
		req_buffer = (char*)malloc(req_length);
		
		http_req.FormatRequest(req_buffer);

	//	host_entity = gethostbyname(hostname.c_str()); //resolve true host name		
	//	host_addr = host_entity->h_addr; //host's IP address.
	//	inet_aton(host_addr.c_str(),&inAddr); //optain address in struct in_addr format
	
		socket_id = client_connect(hostname.c_str(),REMOTE_SERVER);

		//printf("socket_id nonpersist: %d \n",socket_id);
		if (send(socket_id,req_buffer,req_length,0) == -1)
		{
			perror("The following send error occured");
			close(socket_id); //close open file descriptor
			free(req_buffer); //deallocate dynamically allocated variable
			return -1;	
		}	
		
		//obtaining response
		//
		if (getResponse2(socket_id,response) < 0) //cannot obtain response
		{
			close(socket_id);
			free(req_buffer);
			exit(1);
		}

		
		printf("response: \n %s \n",response.c_str());	
		//store the HTTP response in the cache
		pthread_mutex_lock(mutex);
		cache->insert(pair<string,string>(pathname,response));
		thread_count++;
		pthread_mutex_unlock(mutex);
		if (send(fd,response.c_str(),response.length(),0) == -1)
		{
			perror("The following send error occured");
			free(req_buffer);
			return -1;
		}		
		printf("response successfully sent!\n");
 	
		string connection_status = http_req.FindHeader("Connection");
		if (connection_status.compare("close") == 0) //if HTTP request requests closing the connection 
		{
			printf("file descriptor closed\n");
			close(socket_id);
			close(fd);
			return 0;
		}
		
		//close(socket_id);
		//return http_connection(fd,mutex,cache,sport);
		
		fd_set tstat;
		struct timeval ttime;
		int vset;
		while (1)
		{
			ttime.tv_sec = ttime.tv_usec = 2;
			FD_ZERO(&tstat);
			FD_SET(socket_id,&tstat);
			vset = select(socket_id+1,&tstat,NULL,NULL,&ttime);
			
			if (vset < 0)
			{ continue;}
			else if (vset == 0)
			{ break;}
		}
		
		close(socket_id);
		return 0;
		//else
		//{ continue;}

	}

}

void* thread_http_connection(void* thread_param)
{
       proxy_thread_t* temp = (proxy_thread_t *) thread_param;
       http_connection(temp->cfd,temp->mutex,temp->wcache,temp->sport);
//       printf("my guess is that this is where it starts blocking!\n");
       
     //  close(temp->cfd);
       free(thread_param);
  //     printf("my guess is that this is where it starts blocking?\n");
       return NULL;
}

int main (int argc, char *argv[])
{
  int socket_fd = serverListen(PROXY_PORT); 
/*  struct in_addr addr;
  addr.s_addr = INADDR_ANY;
  int socket_fd = socket_establish(&addr,PORTNUM);*/  //first open socket
  if (socket_fd < 0)
  {
	exit(1);
  }
  //int i; 
  //initialize cache and mutexes 
  webCache local_cache; 
  thread_count = 0;
  pthread_mutex_t temp_mutex;
  pthread_mutex_init(&temp_mutex,NULL);
  int cfd;
  fcntl(socket_fd,F_SETFL,O_NONBLOCK);
  int sel;
  fd_set waitset;
  struct timeval tval;
  while(1)
  {
	FD_ZERO(&waitset);
	FD_SET(socket_fd,&waitset);
	tval.tv_sec = tval.tv_usec = 20; //if there is no incoming connection for 15 seconds, time out.
	sel = select(socket_fd+1,&waitset,NULL,NULL,&tval);		

	if (sel == 0) //timeout after 15 seconds of no connection 
	{ return 0;}

	if (sel < 0)
	{continue;}
	
	if (FD_ISSET(socket_fd,&waitset)) {
	cfd = socketAccept(socket_fd); //client file descriptor
	if (cfd < 0) //if connection cannot be accepted at this point, try again later 
	{continue;}
	
//	if (thread_count >= MAX_THREAD)
//	{continue;}
	//initializing thread parameters
	proxy_thread_t *proxy_thread = (proxy_thread_t*)malloc(sizeof(proxy_thread_t));
	proxy_thread->wcache = &local_cache;		
	//proxy_thread->mutex = &mutexes[thread_count];
	proxy_thread->cfd = cfd;	
	proxy_thread->mutex = &temp_mutex;
	proxy_thread->sport = REMOTE_SERVER;
	pthread_t tid; //thread id
	printf("proxy_thread client id: %d \n",cfd);
	pthread_create(&tid,NULL,thread_http_connection,(void*)proxy_thread);
	pthread_detach(tid);
	}

  }	
  return 0;
}


