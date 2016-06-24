#include <stdio.h> 
#include <stdlib.h> // Needed for rand() and RAND_MAX
#include <math.h> // Needed for log()
#include <time.h>
#include <stdarg.h>
#include "arrival.h"
//----- Constants -------------------------------------------------------------
#define PKT_limit 1000000 // Simulation time
#define SIM_TIME  1000000000000
#define quiet 1
#define debug 0
#define printdist 1
// #define ivy
//#define SIM_TIME 1000000 // Simulation time
#ifdef ivy
#define server_count 4 //no of servers
#else
#define server_count 1 //no of servers
#endif
#define q_len PKT_limit
#define event_count 3
#define latency_bound 100000
#define alpha 1.0


typedef struct
{
    double time_arrived;
	double time_started;
	double service_time;
	double time_finished;
	int handled;
}Pkt;

typedef struct
{
    int index;
	int cur_pkt_index;
    double time_arrived;
	double time_finished;
	// double time_vacation_end;
	int state; // 0 idle 1 busy
	int preempted; //0 no 1 yes
	double job_left;
}Server;

typedef struct
{
    double time_arrived;
	// double time_finished;
	double time_vacation_end;
	int state; // 0 idle 1 busy
}Package;


//----- Function prototypes ---------------------------------------------------
double expntl(double x); // Generate exponential RV with mean x
int generate_iat(int count, double* cdf);
int read_and_scale_dist(double target_mean);
void read_dist(double* avg_service_time);
int rand_int(int b);
void hist(int* hist_array, Pkt* pkts, int* nfp, int* nnp);
void hist_double(int* hist_array, double* input, int* nfp, int* nnp);

void error( const char* format, ... ) {
    if(debug==1){
		va_list args;
		fprintf( stderr, "Error: " );
		va_start( args, format );
		vfprintf( stderr, format, args );
		va_end( args );
		fprintf( stderr, "\n" );
	}
}

// ivybridge
#ifdef ivy
double freq[13]={3.4,3.3,3.2,3,2.9,2.8,2.6,2.4,2.2,2.1,2,1.7,1.6};
double voltage[13]={1.070800781,1.045776367,1.025756836,0.990722656,0.975708008,0.955688477,0.955688477,0.955688477,0.950683594,0.950683594,0.950683594,0.945678711,0.945678711};
// Pa = 0.708255*v+1.474418*v^2*f
double Pa_dyn=1.474418, Pa_static=0.708255;
double S_dyn=1.0747, S_static=0.9002;

#else
// xeonphi
double freq[15]={2.7,2.5,2.4,2.3,2.2,2.1,2,1.9,1.8,1.7,1.6,1.5,1.4,1.3,1.2};
double voltage[15]={0.990722656,0.970703125,0.960693359,0.955688477,0.945678711,0.940673828,0.935668945,0.92565918,0.920654297,0.910644531,0.900634766,0.895629883,0.890625,0.885620117,0.875610352};
// Pa = 0.2261*v+1.2208*v^2*f
double Pa_dyn=1.2208, Pa_static=0.2261;
double S_dyn=4.8233, S_static=2.6099;

#endif

int C_state_wakeup_latency[4]={0,1,59,89};
int C_state_exit_latency[4]={0,1,20,22};
double C_state_power[4]={0,1.2,0.13,0};


double* service_length;
double* service_cdf;
double* arrival_length;
double* arrival_cdf;
int service_count,arrival_count;



double server_pkt_arrival_time[server_count]={0};
double server_pkt_depart_time[server_count]={0};

int server_idle_counter[server_count]={0};
int server_busy_counter[server_count]={0};
int package_idle_counter=0;
int package_wakeup_counter=0;
int server_wakeup_counter[server_count]={0};
int server_pkts_counter[server_count]={0};

int queue_en_ptr=0;
int queue_de_ptr=0;
int pkt_index=0;
int queued_pkt=0;
// double accu_arrival_time[server_count]={0};

int pick;
// int map[server_count]={0};
int check_package=0;
//Pkt pkts[server_count*SIM_TIME];

Server server[server_count];
Package package;
/********************** Main program******************************/
int main(int argc, char **argv){
	/****************/
	/*initialization*/
	/****************/
	int i,j,kk,jj;
	Pkt *pkts = malloc(server_count*PKT_limit * sizeof(Pkt));
	for(kk=0;kk<server_count*PKT_limit;kk++){
		pkts[kk].time_arrived=0;
		pkts[kk].time_started=-1;
		pkts[kk].service_time=0;
		pkts[kk].time_finished=-1;
		pkts[kk].handled=-1;
	}
	
	double **server_idle_time = malloc(server_count * sizeof *server_idle_time);
	double **server_busy_time = malloc(server_count * sizeof *server_busy_time); //[server_count][q_len]={0}
	int *queue = malloc(PKT_limit * sizeof(int));
	for(kk=0;kk<server_count;kk++){
		server_idle_time[kk] = malloc(q_len * sizeof *server_idle_time[kk]);
		server_busy_time[kk] = malloc(q_len * sizeof *server_busy_time[kk]);
		for(jj=0;jj<q_len;jj++){
			server_idle_time[kk][jj]=-1;
			server_busy_time[kk][jj]=-1;
		}
	}
	
	double *package_idle_time = malloc(q_len * sizeof(double));
	for(jj=0;jj<q_len;jj++){
			package_idle_time[jj]=-1;
	}
	
	
	srand(time(NULL));
	/****************/
	/*input argument*/
	/****************/
		
	if(argc<6) {
		printf("use: [freq] [load] [core count] [C-state] [latency constraint] [(opt)package wake up latency]\n");
		return 0;
	}
	
	int select_f=atoi(argv[1]); // selected frequency
	if(select_f<0 || select_f>12) {
		printf("choose P-state between P0 ~ P12\n");
		return 0;
	}
	double p=atof(argv[2]);  // traffic load
	int m=atoi(argv[3]);    // how many active cores
	m=server_count; // do not use core parking
	int C_state = atoi(argv[4]); // which C-state a core will enter during idle (C1,C2,C3 -> C1,C3,C6)
	if(C_state<1 || C_state>3) {
		printf("choose C-state between C1 ~ C3\n");
		return 0;
	}
	C_state=0;
	int LC=atoi(argv[5]);  //latency constraint
	
	//printf("start main loop\n");
		
	double time = 0.0; // Simulation time stamp
	double event[event_count]={0,SIM_TIME,SIM_TIME}; // event (arrival, departure, wakeup)
	double next_event;
	
	/****************/
	/***read trace***/
	/****************/
	
	double average_service_time;
	read_dist(&average_service_time);
	double Ta=average_service_time/(server_count*p);
	int ret=read_and_scale_dist(Ta); // ret == -1 // use exponential
	
	
	double Ts_dvfs=average_service_time*freq[0]/freq[select_f]*alpha+average_service_time*(1-alpha);
	double Ts_nf_dvfs=0.000015*freq[0]/freq[select_f]*alpha+0.000015*(1-alpha);
	if(Ts_dvfs/Ta/m>1){
		printf("the selected P-state will overload the server\n");
		return 0;
	}
	
	
	double Pa = Pa_static*voltage[select_f]+Pa_dyn*voltage[select_f]*voltage[select_f]*freq[select_f]; // core power
	C_state_power[0]=Pa;
	double S = S_static*voltage[select_f]+S_dyn*voltage[select_f]*voltage[select_f]*freq[select_f];  // uncore power
	double wake_up_latency=C_state_wakeup_latency[C_state];
	double exit_latency=C_state_exit_latency[C_state];
	// double package_wakeup_latency=atof(argv[6]);
	// double wake_up_latency=atof(argv[6]);
	// double exit_latency=wake_up_latency*0.5;
	// double package_wakeup_latency=wake_up_latency*2;
	// double wake_up_latency=89;
	// double exit_latency=22;
	double package_wakeup_latency;
	double Pc;
	if(argc==6){
		package_wakeup_latency=100;
		Pc=C_state_power[3];
	}else{
		package_wakeup_latency=atof(argv[6]);
		Pc=C_state_power[3];
	}
	if(!quiet){
		printf("system load %f\n",Ts_dvfs/Ta/m);
		printf("# of core %d\n",m);
		printf("selected frequency/voltage %f/%f\n",freq[select_f],voltage[select_f]);
		printf("mean service time %f\nmean arrival time %f\n",Ts_dvfs*1000000,Ta*1000000);
		printf("Pa %f, S %f\n",Pa,S);
		printf("Pc %f\n",Pc);
	}	
	
	for(i=0;i<m;i++){
		server[i].index=i;
		server[i].cur_pkt_index=-1;
		server[i].time_arrived=0;
		server[i].time_finished=-1;
		server[i].state=0;
		server[i].preempted=0;
		// server[i].time_vacation_end=-1;
		server[i].job_left=-1;
		server_pkts_counter[i]=0;
	}
	package.time_arrived=-1;
	// package.time_finished=-1;
	package.time_vacation_end=-1;
	package.state=0;
	
	int next_event_index=-1;
	double pkt_service_time=0;
	
	/**********************/
	/*Main simulation loop*/
	/**********************/
	
	while (pkt_index < PKT_limit){
		// find the next event
		// fprintf(stderr,"%f %f %f\n",event[0],event[1],event[2]);
		error("queue length %d\n",queued_pkt);
		next_event=SIM_TIME+1;
		next_event_index=-1;
		for(i=0;i<event_count;i++){
			if(event[i]<next_event){
				next_event=event[i];
				next_event_index=i;
			}
		}
		if(next_event_index<0 || next_event>SIM_TIME) {
			printf("wrong event time %f\n",next_event);
			return 0;
		} else{
			//printf("event %d\n",next_event_index);
		}
		if (next_event == event[0]){ // *** Event #1 (arrival)
			time = event[0]; 
			
			// insert pkt into queue
			pkts[pkt_index].time_arrived=time;
			pkt_service_time=service_length[generate_iat(service_count,service_cdf)]*1000000;
			pkts[pkt_index].service_time=pkt_service_time*freq[0]/freq[select_f]*alpha+pkt_service_time*(1-alpha);
			if(pkts[pkt_index].service_time==0) printf("pkt serviec time 0!!!\n");
			pkts[pkt_index].time_finished=-1;
			
			queue[queue_en_ptr]=pkt_index;
			pkt_index++;
			queue_en_ptr++;
			queued_pkt++;
						
			/*determin when the core need to wake up*/
			if(package.state==0){ // package is currently sleeping
				// no core is busy, but some might be preempted
				int preempted_server=0;
				for(i=0;i<m;i++){
					preempted_server+=server[i].preempted;
				}
				error("%10.6f\tpreempted cores %d  queued pkts %d\n",time,preempted_server,queued_pkt);
				if((queued_pkt + preempted_server)== m){ // all cores can be on
					if((time+package_wakeup_latency)<package.time_vacation_end || package.time_vacation_end<0 ){
						// update the package wake-up time
						package.time_vacation_end=time+package_wakeup_latency;
						error("wake up package at %f\n",package.time_vacation_end);
					}
				}
				error("%10.6f\tpkt %d arrives and inserted into queue\n",time,pkt_index-1);	
			} else { // package is currently active
				int busy_server=0;
				for(i=0;i<m;i++){
					busy_server+=server[i].state;
				}
				if(busy_server==m){ // all cores busy, packet should be queued
					// sanity check
					int preempted_server=0;
					for(i=0;i<m;i++){
						preempted_server+=server[i].preempted;
					}
					if(preempted_server!=0) {
						printf("preempted cores not 0 when all cores are busy\n");
						return 0;
					}
				error("%10.6f\tpkt %d arrives and inserted into queue\n",time,pkt_index-1);	
				} else{ // not all cores are busy, this should be caused by the packet
					    // allowable stall time is exceeded
					// sanity check
					if(queued_pkt!=1){
						printf("when package are on without all cores busy, there should be only 1 pkt in the queue\n");
						return 0;
					}
					// the arriving packet should be immediately assigned to one idle core
					
					int assigned_server=-1;
					for(i=0;i<m;i++){
						if(server[i].state==0){
							assigned_server=i;
							break;
						}
					}
					if(assigned_server<0){
						printf("find no idle core when not all cores are busy\n");
						return 0;
					}
					server_idle_time[assigned_server][server_idle_counter[assigned_server]]=time-server[assigned_server].time_arrived+exit_latency;
					// server_idle_time[assigned_server][server_idle_counter[assigned_server]]=time-server[assigned_server].time_arrived;
					server_idle_counter[assigned_server]++;
					server_wakeup_counter[assigned_server]++;
					
					server[assigned_server].state=1;
					server[assigned_server].cur_pkt_index=queue[queue_de_ptr];
					pkts[server[assigned_server].cur_pkt_index].handled=assigned_server;
					pkts[server[assigned_server].cur_pkt_index].time_started=time;
					queue_de_ptr++;
					server[assigned_server].time_arrived=time+exit_latency;
					server[assigned_server].time_finished=time+exit_latency+pkts[server[assigned_server].cur_pkt_index].service_time;
					// server[assigned_server].time_arrived=time;
					// server[assigned_server].time_finished=time+pkts[server[assigned_server].cur_pkt_index].service_time;
					
					//pkts[server[pick].cur_pkt_index].time_finished=server[pick].time_finished;
					queued_pkt--;
					error("%10.6f\tpkt %d arrives and assigned to core %d\n",time,pkt_index,assigned_server);	
				}
			}
			// update events
			if(ret==0)
				event[0] = time + arrival_length[generate_iat(arrival_count,arrival_cdf)]*1000000; // next pkt arrival time
			else
				event[0] = time + expntl(Ta)*1000000; // next pkt arrival time
			
			event[1] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_finished<event[1] && server[i].time_finished>0)
					event[1]=server[i].time_finished;
			}
			event[2] = SIM_TIME+1;
			if(package.time_vacation_end>0)
				event[2]=package.time_vacation_end;
			

		}
		else if (next_event == event[1]){ // *** Event #2 (departure)
			
			time = event[1];
			
			//sanity check
			if(package.state==0){
				printf("packet depart when package is sleep\n");
				return 0;
			}
			// find which core has finished pkts
			pick=-1;
			for(i=0;i<m;i++){
				if(server[i].time_finished==event[1]){
					pick=i;
					break;
				}
			}
			if(pick<0) {
				printf("departure event error\n"); 
				return 0;
			}
			
			//printf("pkt %d departed from server %d at time %f\n",server[pick].cur_pkt_index,pick,time);
			pkts[server[pick].cur_pkt_index].time_finished=time;
			server_pkts_counter[pick]++;
			error("%10.6f\tpkt %d depart from core %d\n",time,server[pick].cur_pkt_index,pick);
			
			if(queued_pkt>0){ // there are pkts in the queue, immediately assign to the server
				server[pick].state=1;
				server[pick].cur_pkt_index=queue[queue_de_ptr];
				pkts[server[pick].cur_pkt_index].handled=pick;
				pkts[server[pick].cur_pkt_index].time_started=time;
				queue_de_ptr++;
				server[pick].time_finished=time+pkts[server[pick].cur_pkt_index].service_time;
				// server[pick].time_vacation_end=-1;
				queued_pkt--;
				error("%10.6f\tpkt %d assigned to core %d\n",time,server[pick].cur_pkt_index,pick);
			} else{ // no queue pkts, server check if it can go to sleep
				error("%10.6f\tno queued pkts\n",time);
				server_busy_time[pick][server_busy_counter[pick]]=time-server[pick].time_arrived;
				server_busy_counter[pick]++;
				server[pick].state=0;
				server[pick].cur_pkt_index=-1;
				server[pick].time_arrived=time;
				server[pick].time_finished=-1;
				// server[pick].time_vacation_end=-1;
				double longest_stall_time=0;
				
				for(i=0;i<m;i++){
					if(server[i].cur_pkt_index>0){
						if(pkts[server[i].cur_pkt_index].time_started<0){
							printf("pkt %d start time error\n",server[i].cur_pkt_index);
							return 0;
						}
						if((time-pkts[server[i].cur_pkt_index].time_started)>longest_stall_time){
							longest_stall_time=time-pkts[server[i].cur_pkt_index].time_started;
							error("%10.6f\tlongest stall time is %f for core %d\n",time,longest_stall_time,i);
						}
					}
				}
				error("%10.6f\tlongest stall time is %f\n",time,longest_stall_time);
				if(longest_stall_time<(LC-package_wakeup_latency)){ // the longest stall pkts are shorter than stall time limit, so we preempt the current req processings
					for(i=0;i<m;i++){
						if(server[i].state==1){ // preempt the busy cores
							if(time-server[i].time_arrived<0){
								printf("busy period less than 0: core %d start %f end %f\n",i,server[i].time_arrived,time);
								return 0;
							}
							server_busy_time[i][server_busy_counter[i]]=time-server[i].time_arrived;
							server_busy_counter[i]++;
							server[i].time_arrived=time;
							server[i].state=0;
							server[i].preempted=1;
							server[i].job_left=server[i].time_finished-time;
							server[i].time_finished=-1;
							// sanity check
							if(server[i].job_left<0){
								printf("preempt a core with job left less than 0\n");
								return 0;
							}
							error("%10.6f\tpreempt core %d\n",time,i);
						}
					}
					package.state=0;
					package.time_arrived=time;
					package.time_vacation_end=time+LC-longest_stall_time;
					
				}
				//printf("%d th busy period of server %d\t%f\n",server_busy_counter[pick],pick,server_busy_time[pick][server_busy_counter[pick]]);
				
			}
			
			// update event
			
			event[1] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_finished<event[1] && server[i].time_finished>0)
					event[1]=server[i].time_finished;
			}
			event[2] = SIM_TIME+1;
			if(package.time_vacation_end>0)
				event[2]=package.time_vacation_end;

		} 
		else if (next_event == event[2]){ // *** Event #3 (vacation ends)
			// package will wake up at 1) the stall time limit hit
			//						   2) an incoming packet makes all core can be busy 
			
			time = event[2];
			error("package woken up %f\n",time);
			// sanity check
			if(package.state!=0){
				printf("package wake up while it is awake\n");
				return 0;
			}
			package.time_vacation_end=-1;
			package_idle_time[package_idle_counter]=time-package.time_arrived;
			package_idle_counter++;
			package_wakeup_counter++;
			package.time_arrived=-1;
			package.state=1;
			// resume the preempted cores
			int preempted_server=0;
			for(i=0;i<m;i++){
				if(server[i].preempted==1){
					// sanity check
					if(server[i].state!=0){
						printf("cores is not idle when it is preempted\n");
						return 0;
					}
					error("%10.6f\tresume core %d\n",time,i);
					// server_idle_time[i][server_idle_counter[i]]=time-server[i].time_arrived+C_state_exit_latency[C_state];
					server_idle_time[i][server_idle_counter[i]]=time-server[i].time_arrived;
					server_idle_counter[i]++;
					server_wakeup_counter[i]++;
					server[i].preempted=0;
					server[i].state=1;
					server[i].time_arrived=time+exit_latency;
					// server[i].time_arrived=time;
					// sanity check
					if(server[i].job_left<0){
						printf("cores return from preemption with job left less than 0\n");
						return 0;
					}
					if(server[i].cur_pkt_index<0){
						printf("cores return from preemption with invalid pkt index\n");
						return 0;
					}
					server[i].time_finished=time+server[i].job_left+exit_latency;
					// server[i].time_finished=time+server[i].job_left;
					server[i].job_left=-1;
					preempted_server++;
				}
				
			}
			
			int idle_servers=m-preempted_server; // # of idle cores
			
			int assigned_server=-1;
			for(i=0;i<idle_servers;i++){
				if(queued_pkt>0){
					assigned_server=-1;
					for(j=0;j<m;j++){
						if(server[j].state==0){
							assigned_server=j;
							break;
						}
					}
					if(assigned_server<0){
						printf("find no idle core when not all cores are busy after wake up\n");
						return 0;
					}
					// server_idle_time[assigned_server][server_idle_counter[assigned_server]]=time-server[assigned_server].time_arrived+C_state_exit_latency[C_state];
					server_idle_time[assigned_server][server_idle_counter[assigned_server]]=time-server[assigned_server].time_arrived;
					server_idle_counter[assigned_server]++;
					server_wakeup_counter[assigned_server]++;
					
					server[assigned_server].state=1;
					server[assigned_server].cur_pkt_index=queue[queue_de_ptr];
					pkts[server[assigned_server].cur_pkt_index].handled=assigned_server;
					pkts[server[assigned_server].cur_pkt_index].time_started=time;
					queue_de_ptr++;
					server[assigned_server].time_arrived=time+exit_latency;
					server[assigned_server].time_finished=time+exit_latency+pkts[server[assigned_server].cur_pkt_index].service_time;
					// server[assigned_server].time_arrived=time;
					// server[assigned_server].time_finished=time+pkts[server[assigned_server].cur_pkt_index].service_time;
					
					queued_pkt--;
					error("%10.6f\tassign pkt %d to core %d and finish at %f\n",server[assigned_server].cur_pkt_index,assigned_server,time,server[assigned_server].time_finished);
				}
			
			}
			
			// update event			
			event[1] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_finished<event[1] && server[i].time_finished>0)
					event[1]=server[i].time_finished;
			}
			event[2] = SIM_TIME+1;
			if(package.time_vacation_end>0)
				event[2]=package.time_vacation_end;
		
		}else { 
		// preserve
		}
	}
	
	/*******************************/
	/*Main simulation loop finished*/
	/*******************************/
	
	// wrap up the busy/idle period counter
	for (i=0;i<m;i++){
		if(server[i].state==0){
			server_idle_time[i][server_idle_counter[i]]=time-server[i].time_arrived;
			// server_idle_counter[i]++;
		}else{
			server_busy_time[i][server_busy_counter[i]]=time-server[i].time_arrived;
			// server_busy_counter[i]++;
		}
		
	}
	if(package.time_arrived>0){
		package.time_vacation_end=time;
		package_idle_time[package_idle_counter]=time-package.time_arrived;
		package.time_arrived=-1;
		// package_idle_counter++;
	}
	
	/*******************************/
	/*post processing the statistic*/
	/*******************************/
	
	double avg_busy[server_count]={0};
	double avg_idle[server_count]={0};
	double avg_package_idle=0;
	for(i=0;i<q_len;i++){
		for(j=0;j<m;j++){
			//printf("server %d, %d th busy period : %f\n",j,i,server_busy_time[j][i]);
			if(i<server_busy_counter[j] && server_busy_time[j][i]>0) {
				//printf("server %d, %d th busy period : %f\n",j,i,server_busy_time[j][i]);
				avg_busy[j]=avg_busy[j]+server_busy_time[j][i];
			} else{
				if(i<server_busy_counter[j] && server_busy_time[j][i]<0){
					printf("something wrong with the busy period counter\n");
					printf("core %d, %d th busy period : %f\n",j,i,server_busy_time[j][i]);
				}
			}
			if(i<server_idle_counter[j] && server_idle_time[j][i]>0){
				
				avg_idle[j]=avg_idle[j]+server_idle_time[j][i];
			} else{
				if(i<server_idle_counter[j] && server_idle_time[j][i]<0)
					printf("something wrong with the idle period counter\n");
			}
			
		}
		if(i<package_idle_counter && package_idle_time[i]>0) {
			if(package_idle_time[i]<package_wakeup_latency){
				printf("package idle time less than package_wakeup_latency\n");
			}
			avg_package_idle=avg_package_idle+package_idle_time[i];
		}else{
			if(i<package_idle_counter && package_idle_time[i]<0){
				printf("something wrong with the package idle period counter\n");
			}
		}
		
	}
	
	double busy_ratio[server_count]={0};
	double idle_ratio[server_count]={0};
	double wakeup_ratio[server_count]={0};
	double overall_busy=0,overall_idle=0,overall_wakeup=0;
	double overall_busy_ratio=0,overall_idle_ratio=0,overall_wakeup_ratio=0;
	
	for(j=0;j<m;j++){
		busy_ratio[j]=avg_busy[j]/(avg_busy[j]+avg_idle[j]);
		idle_ratio[j]=(avg_idle[j]-server_wakeup_counter[j]*wake_up_latency)/(avg_busy[j]+avg_idle[j]);
		wakeup_ratio[j]=server_wakeup_counter[j]*wake_up_latency/(avg_busy[j]+avg_idle[j]);
		overall_busy+=avg_busy[j];
		overall_idle+=(avg_idle[j]-server_wakeup_counter[j]*wake_up_latency);
		overall_wakeup+=server_wakeup_counter[j]*wake_up_latency;
		
		avg_busy[j]=avg_busy[j]/server_busy_counter[j];
		avg_idle[j]=avg_idle[j]/server_idle_counter[j];
	}
	overall_busy_ratio=overall_busy/(overall_busy+overall_idle+overall_wakeup);
	overall_idle_ratio=overall_idle/(overall_busy+overall_idle+overall_wakeup);
	overall_wakeup_ratio=overall_wakeup/(overall_busy+overall_idle+overall_wakeup);
	
	int pkt_processed=0,pkt_in_server=0,pkt_in_queue=0;
	for(j=0;j<m;j++){
		pkt_processed+=server_pkts_counter[j];
		pkt_in_server+=server[j].state;
		pkt_in_server+=server[j].preempted;
	}
	pkt_in_queue=queued_pkt;
	if(!quiet){
		printf("\n*********Result*********\n\n");
		printf("total pkts arrived %d\n",pkt_index);
		printf("total pkts processed %d\n",pkt_processed);
		printf("total pkts in queue %d\n",pkt_in_queue);
		printf("total pkts in servers %d\n",pkt_in_server);
	}
	
	double latency[server_count]={0};
	double overall_latency=0;
	double avg_wakeup=0;
	for(j=0;j<pkt_index;j++){
		if(pkts[j].time_finished>0){
			if(pkts[j].time_finished-pkts[j].time_arrived<0)
				printf("pkt process latency error\n");
			else
				latency[pkts[j].handled]+=pkts[j].time_finished-pkts[j].time_arrived;
		}
	}
	for(j=0;j<m;j++){
		avg_wakeup+=server_wakeup_counter[j];
		overall_latency+=latency[j];
		latency[j]=latency[j]/server_pkts_counter[j];
	}
	double package_idle=0;
	for(j=0;j<package_idle_counter;j++){
		package_idle=package_idle+package_idle_time[j];
	}
	
	double package_idle_ratio=package_idle/time;
	double package_wakeup_ratio=package_wakeup_latency*(package_wakeup_counter)/time;
	if(!quiet){
		printf("total time %f\n",time);
		printf("average latency %f\n",overall_latency/pkt_processed);
		printf("average busy ratio %f\n",overall_busy_ratio);
		printf("average idle ratio %f\n",overall_idle_ratio);
		printf("average wakeup ratio %f\n",overall_wakeup_ratio);
		printf("average wakeup count %f\n",avg_wakeup/m);
		printf("average core power %f\n",(Pa*(overall_busy_ratio+overall_wakeup_ratio)+Pc*overall_idle_ratio)); 
		printf("average active power %f\n",Pa*overall_busy_ratio);
		printf("average state transition power %f\n",Pa*overall_wakeup_ratio);
		printf("average idle power %f\n",Pc*overall_idle_ratio);
		printf("average package idle ratio %f\n",package_idle_ratio);
		printf("average package wakeup ratio %f\n",package_wakeup_ratio);
	}
		
	if(0){	
		printf("\n************per core***********\n\n");
		printf("busy period count\t");
		for(j=0;j<m;j++){
			printf("%d\t",server_busy_counter[j]);
		}
		printf("\n");
		printf("wake up count\t");
		for(j=0;j<m;j++){
			printf("%d\t",server_wakeup_counter[j]);
		}
		printf("\n");
		for(j=0;j<m;j++){
			printf("core %d: pkts : %d\t busy ratio: %f\t idle ratio: %f\t wakeup ratio: %f\tavg latency : %f\n",j,server_pkts_counter[j],busy_ratio[j],idle_ratio[j],wakeup_ratio[j],latency[j]);
			//printf("server %d: pkts : %d\tavg busy : %f\t avg idle : %f\t avg latency : %f\n",j,server_pkts_counter[j],avg_busy[j],avg_idle[j],latency[j]);
		}
	}
	
	// latency dist
	
	int *hist_array = malloc((latency_bound+1) * sizeof(int));
	for (j=0;j<latency_bound+1;j++)
		hist_array[j]=0;
	int nfp,nnp;
	hist(hist_array,pkts,&nfp,&nnp);
	// printf("%d\t%.2f\t%f\t%f\t%f\t%f\t%d\t%d\t%d\tC%d\n",m,p,overall_package_idle/time,overall_package_idle/package_idle_counter,(Pa*(overall_busy_ratio+overall_wakeup_ratio)+Pc*overall_idle_ratio)+S*(1-overall_package_idle/time),overall_latency/pkt_processed,nfp,nnp,LC,C_state);
	printf("%d\t%.2f\t%f\t%f\t%f\t%d\t%d\t%d\tC%d\n",m,p,(Pa*(overall_busy_ratio+overall_wakeup_ratio)+Pc*(overall_idle_ratio))*server_count,S*(1-package_idle_ratio)+2*S*package_wakeup_ratio,overall_latency/pkt_processed,nfp,nnp,LC,C_state);
	for (j=0;j<latency_bound+1;j++)
		hist_array[j]=0;
	// hist_double(hist_array, package_idle_time, &nfp,&nnp);
	double *flatten_array = malloc((server_count*q_len) * sizeof(double));
	int counter=0;
	for (j=0;j<latency_bound+1;j++)
		hist_array[j]=0;
	// for(kk=0;kk<server_count;kk++){
		// for(jj=0;jj<q_len;jj++){
			// flatten_array[counter]=server_idle_time[kk][jj];
			// counter++;
		// }
	// }
	// hist_double(hist_array, server_idle_time[0], &nfp,&nnp);
	
	


}

/************************************************************************
Function to generate exponentially distributed RVs 
Input: x (mean value of distribution) 
Output: Returns with exponential RV 
*************************************************************************/
double expntl(double x)
{
double z; // Uniform random number from 0 to 1


// Pull a uniform RV (0 < z < 1)
do
{
z = ((double) rand() / RAND_MAX);
}
while ((z == 0) || (z == 1));

return(-x * log(z));
}


int rand_int(int b)
{
    //The basic function here is like (rand() % range)+ Lower_bound

        return((rand() % (b)));
    
}
int generate_iat(int count, double* cdf){
	int i;
		
	double a = (double)rand()/(double)RAND_MAX;
	for(i=0;i<count;i++){
		if(cdf[i]>a){
			return i;
		}
	}
	return count;
}

void read_dist(double* avg_service_time){
	FILE *trace_file;
	
	trace_file = fopen(FILE_SERVICE, "r");
	// printf("fopend\n");
	int line_count=0;
	double unit;
	fscanf(trace_file, "%d%lf", &line_count, &unit);
	// printf("%d\t%f\n",line_count,unit);
	service_count=line_count;
	// double* service_length;
// double* service_cdf;
	service_length = malloc(line_count*sizeof(double));
	service_cdf = malloc(line_count*sizeof(double));
	double *pdf = malloc(line_count*sizeof(double));
	double a,b;
	int drop=0;
	int read_counter=0;
	while (fscanf(trace_file, "%lf%lf", &a,&b) != EOF) {
		service_length[read_counter]=a;
		service_cdf[read_counter]=b;
		// printf("%f\t%f\n",service_length[read_counter],service_cdf[read_counter]);
		read_counter++;
		
	}
	fclose(trace_file);
	
	double average=0;
	double check=0;
	for(int i=0;i<line_count;i++){
		if(i==0) 
			pdf[i]=service_cdf[i];
		else 
			pdf[i]=service_cdf[i]-service_cdf[i-1];
		check=check+pdf[i];
		average=average+service_length[i]*pdf[i];
		// printf("%lf\n",pdf[i]);
	}
	*avg_service_time=average;
	
	
}


int read_and_scale_dist(double target_mean){
	if(FILE_ARRIVAL==0){// use exponential{
		return -1;
	}
	FILE *trace_file;
	
	trace_file = fopen(FILE_ARRIVAL, "r");
	int line_count=0;
	double unit;
	fscanf(trace_file, "%d%lf", &line_count, &unit);
	// printf("%d\t%f\n",line_count,unit);
	arrival_count=line_count;
	// printf("%d\n",line_count);
	double *length = malloc(line_count*sizeof(double));
	double *cdf = malloc(line_count*sizeof(double));
	double *pdf = malloc(line_count*sizeof(double));
	double a,b;
	int drop=0;
	int read_counter=0;
	while (fscanf(trace_file, "%lf%lf", &a,&b) != EOF) {
		length[read_counter]=a;
		cdf[read_counter]=b;
		read_counter++;
	}
	fclose(trace_file);
	
	double average=0;
	double check=0;
	for(int i=0;i<line_count;i++){
		if(i==0) 
			pdf[i]=cdf[i];
		else 
			pdf[i]=cdf[i]-cdf[i-1];
		check=check+pdf[i];
		average=average+length[i]*pdf[i];
		// printf("%lf\n",pdf[i]);
	}
	// printf("check: %f\n",check);
	// printf("avg: %f\n",average);
	// printf("target: %f\tavg: %f\n",target_mean,average);
	double scaliing=target_mean/average;
	for(int i=0;i<line_count;i++){
		length[i]=length[i]*scaliing;
		// printf("%f\n",length[i]);
	}
	int max_new_bin=ceil((line_count-1)*scaliing);
	// printf("original: %f\ntarget: %f\nscaling: %f\nnew max bin: %d\n",average,target_mean,scaliing,max_new_bin);
	if(scaliing<1){
		// printf("scaling < 1, future work\n");
		return -1;
	}
	
	// do scaling and interpolate
	// double* arrival_length;
// double* arrival_cdf;
	arrival_length = malloc(max_new_bin*sizeof(double));
	double *new_pdf = malloc(max_new_bin*sizeof(double));
	arrival_cdf = malloc(max_new_bin*sizeof(double));
	arrival_length[0]=0;
	new_pdf[0]=pdf[0];
	int old_ptr=1;
	double distance_prev=0, distance_next=0;
	for(int i=1;i<max_new_bin;i++){
		arrival_length[i]=i*unit;
		distance_prev=arrival_length[i]-length[old_ptr-1];
		distance_next=length[old_ptr]-arrival_length[i];
		// printf("%d\t%f\t%f\t%f\n",i,new_length[i],length[old_ptr-1],length[old_ptr]);
		// printf("%f\t%f\n",distance_prev,pdf[old_ptr-1]);
		// printf("%f\t%f\n",distance_next,pdf[old_ptr]);
		if(old_ptr>=line_count) 
			new_pdf[i]=0;
		else
			new_pdf[i]=(pdf[old_ptr-1]*distance_next+pdf[old_ptr]*distance_prev)/(distance_prev+distance_next);
		// printf("%f\n",new_pdf[i]);
		if(arrival_length[i]+unit>length[old_ptr])
			old_ptr++;
		
	}
	int nf=0,nn=0;
	double new_average=0;
	check=0;
	for(int i=0;i<max_new_bin;i++){
		
		new_pdf[i]=new_pdf[i]/scaliing;
		
		check=check+new_pdf[i];
		new_average=new_average+new_pdf[i]*arrival_length[i];
		if(i==0) 
			arrival_cdf[i]=new_pdf[i];
		else 
			arrival_cdf[i]=arrival_cdf[i-1]+new_pdf[i];
		// printf("%f\t%f\t%f\n",new_length[i],new_pdf[i],new_cdf[i]);
		// printf("%d\t%f\n",i,cdf[i]);
		// if(new_cdf[i]>0.95 && nf < 1) {
			// printf("95th %d\n",i);
			
			// nf=i;
		// }
		// if(new_cdf[i]>0.99 && nn < 1) {
			// printf("99th %d\n",i);
			
			// nn=i;
		// }	
	}
	return 0;
	// printf("check: %f\n",check);
	// printf("avg: %f\n",new_average);
	// printf("95th: %f\n",nf*unit);
	// printf("99th: %f\n",nn*unit);
}

void hist(int* hist_array, Pkt* pkts, int* nfp, int* nnp){
	int j;
	double latency;
	int latency_int;
	double latency_pdf[latency_bound+1];
	double latency_cdf[latency_bound+1]={0};
	int pkt_count=0;
	int nf=0,nn=0,temp=0;
	for(j=0;j<PKT_limit;j++){
		if(pkts[j].time_finished>0){
			latency=pkts[j].time_finished-pkts[j].time_arrived;
			if(latency<0)
				printf("wrong_hist\n");
			if(latency>latency_bound) latency=latency_bound;
			latency_int=(int)round(latency);
			pkt_count++;
			hist_array[latency_int]++;
		}
	}
	for(j=0;j<latency_bound+1;j++){
		latency_pdf[j]=hist_array[j]*1.0/pkt_count;
		if(j==0) latency_cdf[j]=latency_pdf[j];
		else latency_cdf[j]=latency_cdf[j-1]+latency_pdf[j];
		if(latency_cdf[j]>0.95 && nf < 1) {
			// printf("95th latency %d\n",j);
			*nfp=j;
			nf=1;
		}
		if(latency_cdf[j]>0.99 && nn < 1) {
			// printf("99th latency %d\n",j);
			*nnp=j;
			nn=1;
		}
		if(printdist){
			temp=temp+hist_array[j];
			if(temp<pkt_count){
				printf("%d\t%f\t%f\n",j,latency_pdf[j],latency_cdf[j]);
			} else if(temp==pkt_count){
				printf("%d\t%f\t%f\n",j,latency_pdf[j],latency_cdf[j]);
				printf("95th %d\t99th %d\n",*nfp,*nnp);
				return;
			}
		}
		//printf("%d\t%f\t%f\n",j,latency_cdf[j],latency_cdf[j]);
	}

}

void hist_double(int* hist_array, double* input, int* nfp, int* nnp){
	int j=0;
	double latency;
	int latency_int;
	double latency_pdf[latency_bound+1]={0};
	double latency_cdf[latency_bound+1]={0};
	int pkt_count=0;
	int nf=0,nn=0,temp=0;;
	while(input[j]> 10e-7){
		latency_int=(int)round(input[j]);
		// printf("%d\n",latency_int);
		hist_array[latency_int]++;
		j++;
	}
	pkt_count=j;
	// printf("%d\n",pkt_count);
	for(j=0;j<latency_bound+1;j++){
		latency_pdf[j]=hist_array[j]*1.0/pkt_count;
		if(j==0) latency_cdf[j]=latency_pdf[j];
		else latency_cdf[j]=latency_cdf[j-1]+latency_pdf[j];
		if(latency_cdf[j]>0.95 && nf < 1) {
			// printf("95th latency %d\n",j);
			*nfp=j;
			nf=1;
		}
		if(latency_cdf[j]>0.99 && nn < 1) {
			// printf("99th latency %d\n",j);
			*nnp=j;
			nn=1;
		}
		if(printdist){
			temp=temp+hist_array[j];
			if(temp<pkt_count){
				printf("%d\t%f\t%f\n",j,latency_pdf[j],latency_cdf[j]);
			} else if(temp==pkt_count){
				printf("%d\t%f\t%f\n",j,latency_pdf[j],latency_cdf[j]);
				printf("95th %d\t99th %d\n",*nfp,*nnp);
				return;
			}
		}
	}

}
