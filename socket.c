/*
** Copyright (C) 2014 Eneo Tecnologia S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "socket.h"
#include "global_config.h"
#include "util.h"

#include <librd/rdlog.h>
#include <jansson.h>

#include <ev.h>

#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_NUM_THREADS 256

struct udp_thread_info{
	pthread_mutex_t listenfd_mutex;
	int listenfd;
};

#define N2KAFKA_TCP "tcp"
#define N2KAFKA_UDP "udp"

#define CONFIG_NUM_THREADS "threads"

enum thread_mode{
	#define STR_MODE_THREAD_PER_CONNECTION "thread_per_connection"
	MODE_THREAD_PER_CONNECTION,
	#define STR_MODE_SELECT "select"
	MODE_SELECT,
	#define STR_MODE_POLL "poll"
	MODE_POLL,
	#define STR_MODE_EPOLL "epoll"
	MODE_EPOLL,
	MODE_INVALID
};

static enum thread_mode thread_mode_str(const char *mode_str) {
	if(NULL==mode_str || 0==strcmp(STR_MODE_THREAD_PER_CONNECTION,mode_str))
		return MODE_THREAD_PER_CONNECTION;
	if(0 == strcmp(STR_MODE_SELECT,mode_str))
		return MODE_SELECT;
	if(0 == strcmp(STR_MODE_POLL,mode_str))
		return MODE_POLL;
	if(0 == strcmp(STR_MODE_EPOLL,mode_str))
		return MODE_EPOLL;
	return MODE_INVALID;
}

#define READ_BUFFER_SIZE 4096
static const struct timeval READ_SELECT_TIMEVAL  = {.tv_sec = 20,.tv_usec = 0};
static const struct timeval WRITE_SELECT_TIMEVAL = {.tv_sec = 5,.tv_usec = 0};
#define ERROR_BUFFER_SIZE 256
static __thread char errbuf[ERROR_BUFFER_SIZE];

static int do_shutdown = 0;

static int createListenSocket(const char *proto,uint16_t listen_port) {
	int listenfd = 0;
	if (NULL == proto) {
		rdlog(LOG_ERR,"Can't create listen socket: No protocol given");
		return 0;
	}

	if (0 == strcmp(N2KAFKA_UDP,proto)) {
		listenfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK,0);
	} else if (0 == strcmp(N2KAFKA_TCP,proto)) {
		listenfd = socket(AF_INET,SOCK_STREAM,0);
	} else {
		rdlog(LOG_ERR,"Can't create socket: Unknown type");
		return 0;	
	}

	if(listenfd==-1){
		rdlog(LOG_ERR,"Error creating socket: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
		return 0;
	}

	const int so_reuseaddr_value = 1;
	const int setsockopt_ret = setsockopt(listenfd,SOL_SOCKET, SO_REUSEADDR,&so_reuseaddr_value,sizeof(so_reuseaddr_value));
	if(setsockopt_ret < 0){
		rdlog(LOG_WARNING,"Error setting socket option: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
	}

	struct sockaddr_in server_addr;
	memset(&server_addr,0,sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
	assert(listen_port > 0);
	server_addr.sin_port=htons(listen_port);

	const int bind_ret = bind(listenfd,(struct sockaddr *)&server_addr,sizeof(server_addr));
	if(bind_ret == -1){
		rdlog(LOG_ERR,"Error binding socket: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
		close(listenfd);
		return -1;
	}
	
	if(0 == strcmp(N2KAFKA_TCP,proto)) {
		const int listen_ret = listen(listenfd,SOMAXCONN);
		if(listen_ret == -1){
			rdlog(LOG_ERR,"Error in listen: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
			close(listenfd);
			return -1;
		}
	}
	
	rdlog(LOG_INFO,"Listening socket created successfuly");
	return listenfd;
}

static int createListenSocketMutex(pthread_mutex_t *mutex){
	const int init_returned = pthread_mutex_init(mutex,NULL);
	if(init_returned!=0)
		rdlog(LOG_ERR,"Error creating mutex: ");
	return init_returned;
}

static void set_nonblock_flag(int fd){
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void set_keepalive_opt(int fd){
	int i=1;
	const int sso_rc = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&i, sizeof(int));
	if(sso_rc == -1)
		rdbg("Can't set SO_KEEPALIVE option\n");
}

static uint16_t get_port(const struct sockaddr_in *sa){
	return ntohs(sa->sin_port);
	
}

static void print_accepted_connection_log(const struct sockaddr_in *sa){
	char str[sizeof(INET_ADDRSTRLEN)];
	inet_ntop(AF_INET, &(sa->sin_addr), str, INET_ADDRSTRLEN);

	rdlog(LOG_INFO,"Accepted connection from %s:%d",str,get_port(sa));
}

static int select_socket(int listenfd,struct timeval *tv){
	fd_set listenfd_set;

	FD_ZERO(&listenfd_set);
	FD_SET(listenfd,&listenfd_set);
	return select(listenfd+1,&listenfd_set,NULL,NULL,tv);
}

static int write_select_socket(int writefd,struct timeval *tv){
	fd_set writefd_set;
	FD_ZERO(&writefd_set);
	FD_SET(writefd,&writefd_set);
	return select(writefd+1,NULL,&writefd_set,NULL,tv);
}

static int receive_from_socket(int fd,struct sockaddr_in6 *addr,char *buffer,const size_t buffer_size){
	socklen_t socklen = (socklen_t)sizeof(*addr);
	return recvfrom(fd,buffer,buffer_size,MSG_DONTWAIT,(struct sockaddr *)addr,&socklen);
}

static void process_data_received_from_socket(char *buffer,const size_t recv_result){
	if(unlikely(global_config.debug))
		rdlog(LOG_DEBUG,"received %zu data: %.*s\n",recv_result,(int)recv_result,buffer);

	if(unlikely(only_stdout_output()))
		free(buffer);
	else
		send_to_kafka(buffer,recv_result,RD_KAFKA_MSG_F_FREE);
}

static int send_to_socket(int fd,const char *data,size_t len){
	struct timeval tv = WRITE_SELECT_TIMEVAL;
	const int select_result = write_select_socket(fd,&tv);
	if(select_result > 0){
		return write(fd,data,len);
	}else if(select_result == 0){
		rdlog(LOG_ERR,"Socket not ready for writing in %ld.%6ld. Closing.\n",
			WRITE_SELECT_TIMEVAL.tv_sec,WRITE_SELECT_TIMEVAL.tv_usec);
		return select_result;
	}else{
		rdlog(LOG_ERR,"Error writing to socket: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
		return select_result;
	}
}

struct connection_private {
	int first_response_sent;
};

static void close_socket_and_stop_watcher(struct ev_loop *loop,struct ev_io *watcher){
	ev_io_stop(loop,watcher);

	close(watcher->fd);
	free(watcher->data); /* Connection data */
	free(watcher);
}

static void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {

	if(EV_ERROR & revents) {
		rdlog(LOG_ERR,"Read callback error: %s",mystrerror(errno,errbuf,
			ERROR_BUFFER_SIZE));
	}

	struct connection_private *connection = (struct connection_private *) watcher->data;
	struct sockaddr_in6 saddr;

	char *buffer = calloc(READ_BUFFER_SIZE,sizeof(char));
	const int recv_result = receive_from_socket(watcher->fd,&saddr,buffer,READ_BUFFER_SIZE);
	if(recv_result > 0){
		process_data_received_from_socket(buffer,(size_t)recv_result);
	}else if(recv_result < 0){
		if(errno == EAGAIN){
			rdbg("Socket not ready. re-trying");
			free(buffer);
		}else{
			rdlog(LOG_ERR,"Recv error: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
			free(buffer);
			close_socket_and_stop_watcher(loop,watcher);
		}
	}else{ /* recv_result == 0 */
		free(buffer);
		close_socket_and_stop_watcher(loop,watcher);
	}

	if(NULL!=global_config.response && !connection->first_response_sent){
		int send_ret = 1;
		rdlog(LOG_DEBUG,"Sending first response...");

		if(global_config.response_len == 0){
			rdlog(LOG_ERR,"Can't send first response: size of response == 0");
			connection->first_response_sent = 1;
		} else {
			send_ret = send_to_socket(watcher->fd,global_config.response,(size_t)global_config.response_len-1);
		}

		if(send_ret <= 0){
			rdlog(LOG_ERR,"Cannot send to socket: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
			close_socket_and_stop_watcher(loop,watcher);
		}
		
		rdlog(LOG_DEBUG,"first response ok");
		connection->first_response_sent = 1;
	}
}

struct socket_listener_private {
	pthread_t main_loop;
	struct ev_loop *event_loop;
	struct ev_async w_async;

	struct {
		char *proto;
		uint16_t listen_port;
		size_t threads;
    	bool tcp_keepalive;
    	enum thread_mode thread_mode;
    } config;

    pthread_t threads[MAX_NUM_THREADS];
    struct ev_loop *event_loops[MAX_NUM_THREADS];
    struct ev_async event_asyncs[MAX_NUM_THREADS];

    size_t accept_current_worker_idx;
};

static void accept_cb(struct ev_loop *loop __attribute__((unused)), 
                      struct ev_io *watcher,int revents){
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	struct socket_listener_private *accept_private = 
		(struct socket_listener_private *)watcher->data;
	int client_sd;
	char buf[512];

	if(EV_ERROR & revents) {
		strerror_r(errno,buf,sizeof(buf));
		rdlog(LOG_ERR,"Invalid event: %s",buf);
		return;
	}

	client_sd = accept(watcher->fd, (struct sockaddr *)&client_addr,
		&client_len);

	if(client_sd < 0) {
		strerror_r(errno,buf,sizeof(buf));
		rdlog(LOG_ERR,"accept error: %s",buf);
		return;
	}

	if(in_addr_list_contains(global_config.blacklist,&client_addr.sin_addr)) {
		if(global_config.debug)
			rdbg("Connection rejected: %s in blacklist",
				inet_ntop(AF_INET,&client_addr,buf,sizeof(buf)));
		close(client_sd);
		return;
	}else if(global_config.debug){
		print_accepted_connection_log((struct sockaddr_in *)&client_addr);
	}

	if(accept_private->config.tcp_keepalive)
		set_keepalive_opt(client_sd);
	set_nonblock_flag(client_sd);

	if(accept_private->config.thread_mode == MODE_THREAD_PER_CONNECTION) {
		rdlog(LOG_ERR,"Mode " STR_MODE_THREAD_PER_CONNECTION "still not implemented");
		exit(-1);
	} else {
		/* Set watcher */
		struct ev_io *w_client = (struct ev_io *) malloc(sizeof(struct ev_io));
		if(unlikely(NULL == w_client)) {
			rdlog(LOG_ERR,"Can't allocate client private data");
		} else {
			const size_t cur_idx = accept_private->accept_current_worker_idx++;
			if(accept_private->accept_current_worker_idx >= accept_private->config.threads)
				accept_private->accept_current_worker_idx = 0;

			rdbg("Sent connection to worker thread %zu",cur_idx);
			
			ev_io_init(w_client, read_cb, client_sd, EV_READ);
			ev_io_start(accept_private->event_loops[cur_idx], w_client);

		}
	}
}

static void async_cb(struct ev_loop *loop, ev_async *w __attribute__((unused)),
	int revents) {

	char buf[512];

	if(EV_ERROR & revents) {
		strerror_r(errno,buf,sizeof(buf));
		rdlog(LOG_ERR,"Invalid event: %s",buf);
		return;
	}

	if(1 == do_shutdown)
		ev_break(loop,EVBREAK_ALL);
}

struct worker_args {
	struct socket_listener_private *accept_private;
	int idx;
};

static void *worker(void *_worker_arg) {
	struct worker_args *worker_args = _worker_arg;

	ev_run(worker_args->accept_private->event_loops[worker_args->idx],0);

	free(worker_args);

	return NULL;
}

static void main_tcp_loop(int listenfd,struct socket_listener_private *priv) {
	priv->event_loop = ev_loop_new(0);
	struct ev_io w_accept = {
		.data = priv,
	};

	ev_io_init((&w_accept),accept_cb,listenfd,EV_READ);
	ev_async_init((&priv->w_async),async_cb);
	ev_io_start(priv->event_loop,&w_accept);
	ev_async_start(priv->event_loop,&priv->w_async);

	size_t i;
	for(i=0;i<priv->config.threads;++i) {
		struct worker_args *args = calloc(1,sizeof(args[0]));
		if(!args) {
			rdlog(LOG_ERR,"Can't allocate worker arg (out of memory?");
			continue;
		}

		args->idx = i;
		args->accept_private = priv;

		priv->event_loops[i] = ev_loop_new(0);
		if(priv->event_loops[i] == NULL){
			rdlog(LOG_ERR,"Can't create even't loop %zu",i);
			free(args);
			continue;
		}

		ev_async_init((&priv->event_asyncs[i]),async_cb);
		ev_async_start(priv->event_loops[i],&priv->event_asyncs[i]);

		pthread_create(&priv->threads[i],NULL,worker,args);
	}

	ev_run(priv->event_loop,0);

	for(i=0;i<priv->config.threads;++i) {
		if(NULL == priv->event_loops[i]) {
			rdlog(LOG_ERR,"Something happened: event loop %zu not found.",i);
			continue;
		}

		ev_async_send(priv->event_loops[i],&priv->event_asyncs[i]);
		pthread_join(priv->threads[i],NULL);

		ev_async_stop(priv->event_loops[i],&priv->event_asyncs[i]);
		ev_loop_destroy(priv->event_loops[i]);
	}

	ev_async_stop(priv->event_loop,&priv->w_async);
	ev_io_stop(priv->event_loop,&w_accept);

	ev_loop_destroy(priv->event_loop);
}

/// @TODO join with TCP
static void *main_consumer_loop_udp(void *_thread_info){
	struct udp_thread_info *thread_info = _thread_info;
	while(!do_shutdown){
		int recv_result = 0;
		struct timeval tv = {.tv_sec = 1,.tv_usec = 0};
		char *buffer = calloc(READ_BUFFER_SIZE,sizeof(char));
		pthread_mutex_lock(&thread_info->listenfd_mutex);
		if(likely(!do_shutdown)){
			int select_result = select_socket(thread_info->listenfd,&tv);
			if(select_result==-1 && errno!=EINTR){ /* NOT INTERRUPTED */
				rdlog(LOG_ERR,"listen select error: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
			}else if(select_result>0){
				struct sockaddr_in6 addr;
				recv_result = receive_from_socket(thread_info->listenfd,&addr,buffer,READ_BUFFER_SIZE);
			}
		}
		pthread_mutex_unlock(&thread_info->listenfd_mutex);

		if(recv_result < 0){
			if(errno == EAGAIN) {
				rdbg("Socket not ready. re-trying");
				free(buffer);
			} else {
				rdlog(LOG_ERR,"Recv error: %s",mystrerror(errno,errbuf,ERROR_BUFFER_SIZE));
				free(buffer);
				break;
			}
		} else {
			process_data_received_from_socket(buffer,(size_t)recv_result);
			
		}
	}

	return NULL;
}

static void main_udp_loop(int listenfd,size_t udp_threads){
	/* Lots of threads listening  and processing*/
	unsigned int i;
	struct udp_thread_info udp_thread_info;
	udp_thread_info.listenfd = listenfd;
	assert(udp_threads>0);
	pthread_t *threads = malloc(sizeof(threads[0])*udp_threads);

	if(0 != createListenSocketMutex(&udp_thread_info.listenfd_mutex))
		exit(-1);

	for(i=0;i<udp_threads;++i)
		pthread_create(&threads[i],NULL,main_consumer_loop_udp,&udp_thread_info);

	for(i=0;i<udp_threads;++i)
		pthread_join(threads[i],NULL);
	
	pthread_mutex_destroy(&udp_thread_info.listenfd_mutex);
	
	free(threads);
}

static void *main_socket_loop(void *_params) {
	struct socket_listener_private *params = _params;

	if( NULL == _params ) {
		rdlog(LOG_ERR,"NULL passed to %s",__FUNCTION__);
		return NULL;
	}
	
	int listenfd = createListenSocket(params->config.proto,params->config.listen_port);
	if(listenfd == -1)
		return NULL;

	/*
	@TODO have to look at ev_set_syserr_cb
	*/

	if( 0 == strcmp(N2KAFKA_UDP,params->config.proto) ){
		main_udp_loop(listenfd,params->config.threads);
	}else{
		main_tcp_loop(listenfd,params);
	}

	rdlog(LOG_INFO,"Closing listening socket.\n");
	close(listenfd);

	return NULL;
}

static void join_listener_socket(void *_private){
	struct socket_listener_private *private = _private;

	do_shutdown = 1;
	ev_async_send (private->event_loop,&private->w_async);
	pthread_join(private->main_loop,NULL);
	free(private);
}

struct listener *create_socket_listener(struct json_t *config,char *err,size_t errsize){
	json_error_t error;
	char *proto;

	struct socket_listener_private *priv = calloc(1,sizeof(*priv));
	if( NULL == priv ) {
		snprintf(err,errsize,"Can't allocate private data (out of memory?)");
		return NULL;
	}

	/* Default */
	priv->config.threads = 1; 
	priv->config.tcp_keepalive = 0;
	priv->config.thread_mode = MODE_EPOLL;
	const char *mode=NULL;

	const int unpack_rc = json_unpack_ex(config,&error,0,
		"{s:s,s:i,s?i,s?b,s?s}",
		"proto",&proto,"port",&priv->config.listen_port,
		"num_threads",&priv->config.threads,"tcp_keepalive",&priv->config.tcp_keepalive,
		"mode",&mode);

	if( unpack_rc != 0 /* Failure */ ) {
		snprintf(err,errsize,"Can't decode listener: %s",error.text);
		free(priv);
		return NULL;
	}

	if( priv->config.threads == 0 ) {
		rdlog(LOG_ERR,"UDP threads has to be > 0. Setting to 1");
		priv->config.threads = 1;
	}

	if( priv->config.threads > MAX_NUM_THREADS ) {
		rdlog(LOG_ERR,"UDP threads has to be < %d. Setting to %d",
			MAX_NUM_THREADS,MAX_NUM_THREADS);
		priv->config.threads = MAX_NUM_THREADS;
	}

	if(mode != NULL) {
		priv->config.thread_mode = thread_mode_str(mode);
	}

	priv->config.proto = strdup(proto);
	if( NULL == priv->config.proto) {
		snprintf(err,errsize,"Error: Can't strdup protocol (out of memory?)");
		free(priv);
		return NULL;
	}

	struct listener *l = calloc(1,sizeof(*l));
	if( NULL == l ) {
		snprintf(err,errsize,"Can't allocate listener (out of memory?)");
		free(priv);
	}

	l->join    = join_listener_socket;
	l->private = priv;

	const int pcreate_rc = pthread_create(&priv->main_loop,NULL,
		main_socket_loop,priv);
	if (pcreate_rc != 0) {
		strerror_r(pcreate_rc,err,errsize);
		free(priv);
		free(l);
		return NULL;
	}

	return l;
}