#include <stdio.h> 
#include <stdlib.h> // Needed for rand() and RAND_MAX
#include <math.h> // Needed for log()
#include <time.h>
#include <stdarg.h>
#include "arrival.h"
#include <string.h>
//----- Constants -------------------------------------------------------------
#define PKT_limit 100000 // Simulation time
#define SIM_TIME  1000000000000
#define quiet 1
#define debug 0
#define printdist 0
#define read_req_trace
// #define ivy
//#define SIM_TIME 1000000 // Simulation time
#ifdef ivy
#define server_count 4 //no of servers
#else
#define server_count 12 //no of servers
#endif
#define q_len PKT_limit
#define event_count 3
#define latency_bound 200000
#define alpha 1.0
#define package_sleep 0
#define idle_history 8
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
	double time_vacation_end;
	int state; // 0 idle 1 busy
	int sleep_state; //-1:busy 0-2 C-state 1-3
	int P_state;
	double time_P_state;
}Server;

typedef struct
{
    int index;
	int cur_pkt_index;
    double time_arrived;
	double time_finished;
	int state; // 0 idle 1 busy
}Package;

#ifdef read_req_trace
int req_assigned_core[PKT_limit];
double req_arrival_time[PKT_limit];
double req_service_time[PKT_limit];

#endif

//----- Function prototypes ---------------------------------------------------
double expntl(double x); // Generate exponential RV with mean x
int generate_iat(int count, double* cdf);
int read_and_scale_dist(double target_mean);
void read_dist(double* avg_service_time);
int rand_int(int b);
void hist(int* hist_array, Pkt* pkts, int* nfp, int* nnp);
void convolve(double* A,int A_len,double* B,int B_len,double* output);
int pdf_to_tail(double* input, int length);
void compute_conditional_pdf(double condition,double* ret_pdf, int* ret_length);

void error( const char* format, ... ) {
    if(debug==1){
		va_list args;
		// fprintf( stderr, "Error: " );
		va_start( args, format );
		vfprintf( stderr, format, args );
		va_end( args );
		// fprintf( stderr, "\n" );
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
int C_state_EBT[4]={0,1,156,300};
double C_state_power[4]={0,1.2,0.13,0};


double* service_length;
double* service_cdf;
double* service_pdf;
double service_tail;
double* arrival_length;
double* arrival_cdf;
int service_count,arrival_count;



double server_pkt_arrival_time[server_count]={0};
double server_pkt_depart_time[server_count]={0};

int server_idle_counter[server_count]={0};
int server_busy_counter[server_count]={0};
int server_busy_P_state_counter[server_count]={0};
int package_idle_counter=0;
int server_wakeup_counter[server_count]={0};
int server_pkts_counter[server_count]={0};

int queue_en_ptr[server_count]={0};
int queue_de_ptr[server_count]={0};
int pkt_index=0;
int queued_pkt[server_count]={0};
// for menu governor
double **past_idle; 
int past_idle_ptr[server_count]={0};
// double accu_arrival_time[server_count]={0};

int pick;
// int map[server_count]={0};
int check_package=0;
//Pkt pkts[server_count*SIM_TIME];

Server server[server_count];
Package package;
/********************** Main program******************************/
int main(int argc, char **argv){
	srand(time(NULL));
	/****************/
	/*input argument*/
	/****************/
		
	if(argc<5) {
		printf("use: [freq] [load] [core count] [latency constraint] [DVFS latency]\n");
		return 0;
	}
	
	int select_f=atoi(argv[1]); // selected frequency
	if(select_f<0 || select_f>15) {
		printf("choose P-state between P0 ~ P14\n");
		return 0;
	}
	double p=atof(argv[2]);  // traffic load
	int m=atoi(argv[3]);    // how many active cores
	m=server_count; // do not use core parking
	
	int LC=atoi(argv[4]);  //latency constraint
	int DVFS_latency;
	if(argc==5)
		DVFS_latency=10;
	else
		DVFS_latency=atoi(argv[6]);
	
	/****************/
	/*initialization*/
	/****************/
	int i,j,kk,jj;
	Pkt *pkts = malloc(server_count*PKT_limit * sizeof(Pkt));
	for(kk=0;kk<server_count*PKT_limit;kk++){
		pkts[kk].time_arrived=-1;
		pkts[kk].time_started=-1;
		pkts[kk].service_time=0;
		pkts[kk].time_finished=-1;
		pkts[kk].handled=-1;
	}
	
	double **server_idle_time = malloc(server_count * sizeof *server_idle_time);
	double **server_busy_time = malloc(server_count * sizeof *server_busy_time); //[server_count][q_len]={0}
	double **server_busy_P_state = malloc(server_count * sizeof *server_busy_P_state);
	int **server_busy_P_state_index = malloc(server_count * sizeof *server_busy_P_state_index);
	int **queue = malloc(server_count * sizeof *queue);
	for(kk=0;kk<server_count;kk++){
		server_idle_time[kk] = malloc(q_len * sizeof *server_idle_time[kk]);
		server_busy_time[kk] = malloc(q_len * sizeof *server_busy_time[kk]);
		server_busy_P_state[kk] = malloc(q_len * sizeof *server_busy_P_state[kk]);
		server_busy_P_state_index[kk] = malloc(q_len * sizeof *server_busy_P_state_index[kk]);
		queue[kk] = malloc(PKT_limit * sizeof *queue[kk]);
		for(jj=0;jj<q_len;jj++){
			server_idle_time[kk][jj]=-1;
			server_busy_time[kk][jj]=-1;
			server_busy_P_state[kk][jj]=-1;
			server_busy_P_state_index[kk][jj]=-1;
		}
		for(jj=0;jj<PKT_limit;jj++){
			queue[kk][jj]=-1;
		}
	}
	
	// for menu governor
	double ***server_per_state_idle_time = malloc(server_count * sizeof **server_per_state_idle_time);
	for(kk=0;kk<server_count;kk++){
		server_per_state_idle_time[kk]=malloc(3 * sizeof **server_per_state_idle_time[kk]);
			for(jj=0;jj<3;jj++){
				server_per_state_idle_time[kk][jj]= malloc(q_len * sizeof *server_per_state_idle_time[kk][jj]);
				for(i=0;i<q_len;i++){
					server_per_state_idle_time[kk][jj][i]=-1;
				}
			}
	}
	int **server_per_state_idle_counter = malloc(server_count * sizeof *server_per_state_idle_counter);
	for(kk=0;kk<server_count;kk++){
		server_per_state_idle_counter[kk]=malloc(3 * sizeof *server_per_state_idle_counter[kk]);
		for(jj=0;jj<3;jj++){
			server_per_state_idle_counter[kk][jj]= 0;
		}
	}
	
	past_idle = malloc(server_count * sizeof *past_idle);
	for(kk=0;kk<server_count;kk++){
		past_idle[kk] = malloc(idle_history * sizeof *past_idle[kk]);
		for(jj=0;jj<idle_history;jj++){
			past_idle[kk][jj] = 0;
		}
	}
	
	double *package_idle_time = malloc(q_len * sizeof(double));
	for(jj=0;jj<q_len;jj++){
			package_idle_time[jj]=-1;
	}
	
	
	
	//printf("start main loop\n");
		
	double time = 0.0; // Simulation time stamp
	double event[event_count]={0,SIM_TIME,SIM_TIME}; // event (arrival, departure, wakeup)
	double next_event;
	
	/****************/
	/***read trace***/
	/****************/
#ifdef read_req_trace
	FILE *req_trace_file;
	char trace_file_location[100];
	char trace_file_name[10];
	int read_counter=0;
	sprintf(trace_file_name, "/%.1f", p);
	strcpy(trace_file_location, req_trace_path);
	strcat(trace_file_location, trace_file_name);
	// printf("%s\n",trace_file_location);
	req_trace_file = fopen(trace_file_location, "r");
	
	while (fscanf(req_trace_file, "%d%lf%lf", &req_assigned_core[read_counter]
		,&req_arrival_time[read_counter],&req_service_time[read_counter]) != EOF) {
		// printf("%d\t%f\t%f\n",req_assigned_core[read_counter],req_arrival_time[read_counter],req_service_time[read_counter]);
		read_counter++;
		if(read_counter>PKT_limit)
			break;
		
	}
	fclose(req_trace_file);
#endif	
	double average_service_time;
	read_dist(&average_service_time);
	double Ta=average_service_time/(server_count*p);
	int ret=read_and_scale_dist(Ta); // ret == -1 // use exponential
	
	
	double Ts_dvfs=average_service_time*freq[0]/freq[select_f]*alpha+average_service_time*(1-alpha);
	
	// printf("service dist tail is %f\n",service_tail*1e-6);
	double Ts_nf_dvfs=service_tail*1e-6*freq[0]/freq[select_f]*alpha+service_tail*1e-6*(1-alpha);
	if(Ts_dvfs/Ta/m>1){
		printf("the selected P-state will overload the server\n");
		return 0;
	}
	
	
	double Pa = Pa_static*voltage[select_f]+Pa_dyn*voltage[select_f]*voltage[select_f]*freq[select_f]; // core power
	double Pmax = Pa_static*voltage[0]+Pa_dyn*voltage[0]*voltage[0]*freq[0]; // C-state transition power
	C_state_power[0]=Pa;
	double S = S_static*voltage[select_f]+S_dyn*voltage[select_f]*voltage[select_f]*freq[select_f];  // uncore power
	
	double package_wakeup_latency;
	if(argc==6){
		package_wakeup_latency=100;
		
	}else{
		package_wakeup_latency=atof(argv[6]);
		
	}
	if(!quiet){
		printf("system load %f\n",Ts_dvfs/Ta/m);
		printf("# of core %d\n",m);
		printf("selected frequency/voltage %f/%f\n",freq[select_f],voltage[select_f]);
		printf("mean service time %f\nmean arrival time %f\n",Ts_dvfs*1000000,Ta*1000000);
		printf("Pa %f, S %f\n",Pa,S);
	}	
	
	for(i=0;i<m;i++){
		server[i].index=i;
		server[i].cur_pkt_index=-1;
		server[i].time_arrived=0;
		server[i].time_finished=-1;
		server[i].state=0;
		server[i].sleep_state=0;
		server[i].time_vacation_end=-1;
		server[i].P_state=0;
		server[i].time_P_state=-1;
		server_pkts_counter[i]=0;
	}
	package.time_arrived=-1;
	package.time_finished=-1;
	
	int next_event_index=-1;
	double pkt_service_time=0;
	
	/**********************/
	/*Main simulation loop*/
	/**********************/
	
	while (pkt_index < PKT_limit){
		// find the next event
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
			// find out which core handle this arrival
#ifdef read_req_trace // read from trace
			int assigned_server=req_assigned_core[pkt_index];
#else
			int assigned_server=rand_int(m); // randomly assign one core to handle the request
#endif			
			// insert pkt into queue
			pkts[pkt_index].time_arrived=time;
#ifdef read_req_trace // read from trace
			pkt_service_time=req_service_time[pkt_index];
#else
			pkt_service_time=service_length[generate_iat(service_count,service_cdf)]*1000000;
#endif
			pkts[pkt_index].service_time=pkt_service_time*freq[0]/freq[select_f]*alpha+pkt_service_time*(1-alpha);
			if(pkts[pkt_index].service_time==0) printf("pkt serviec time 0!!!\n");
			pkts[pkt_index].time_finished=-1;
			pkts[pkt_index].handled=assigned_server;
			queue[assigned_server][queue_en_ptr[assigned_server]]=pkt_index;
			pkt_index++;
			queue_en_ptr[assigned_server]++;
			queued_pkt[assigned_server]++;
						
			/*determin when the core need to wake up*/
			if(server[assigned_server].state==0){ //core idle
				//sanity check
				if(server[assigned_server].sleep_state<0 || server[assigned_server].sleep_state>2){
					printf("server sleep state error\n");
					return 0;
				}
				int server_c_state=server[assigned_server].sleep_state+1;
				if(server[assigned_server].time_vacation_end==-1){
					if(time-server[assigned_server].time_arrived>=(C_state_wakeup_latency[server_c_state]-C_state_exit_latency[server_c_state])){
						//transit into sleep state done
						server[assigned_server].time_vacation_end=time+C_state_exit_latency[server_c_state];
					} else {
						//transit into sleep state not done
						error("req arrives before entering sleep state %d %f\n",assigned_server,time-server[assigned_server].time_arrived);
						server[assigned_server].time_vacation_end=server[assigned_server].time_arrived+C_state_wakeup_latency[server_c_state];
					}
				}
				
				
			}else{
				if(queued_pkt[assigned_server]<1) {
					error("error\n");
					return 0;
				}
				//determine the frequency
				// since the core just complete a request, we dont need to consider the conditional pdf
				int P_state_config=14,P_state_config_temp=14;
				double freq_temp;
				double pkt_arrive_time_temp;
				error("%d pkts in the queue of core %d\n",queued_pkt[pick],pick);
				pkt_arrive_time_temp=pkts[server[assigned_server].cur_pkt_index].time_arrived;
				double condition=time-pkts[server[assigned_server].cur_pkt_index].time_started;
				if(condition<=0){
					printf("pkt condition calculation error %f\n",condition);
					return 0;
				}
				double *cond_pdf=malloc(service_count * sizeof(double));
				int ret_length;
				compute_conditional_pdf(condition,cond_pdf, &ret_length);
				int tail=pdf_to_tail(cond_pdf, ret_length);
				error("tail after condition is %d\n",tail);
				// printf("tail after condition is %d\n",tail);
				freq_temp=tail/(LC-DVFS_latency-(time-pkt_arrive_time_temp));
				for(i=0;i<queued_pkt[assigned_server];i++){
					pkt_arrive_time_temp=pkts[queue[assigned_server][queue_de_ptr[assigned_server]+i]].time_arrived;
					// sanity check
					if(pkt_arrive_time_temp<0){
						error("pkt arrive time error\n");
						return 0;
					}
					freq_temp=(tail+Ts_nf_dvfs*1e6*(i-1))/(LC-DVFS_latency-(time-pkt_arrive_time_temp));
					error("arrive:core %d pick freq %f\n",assigned_server,freq_temp);
					for(j=0;j<15;j++){
						if(freq_temp<0){
							P_state_config_temp=0;
							break;
						}else{
							if(freq[j]<freq_temp){
								P_state_config_temp=j-1;
								break;
							}
						}
					}
					error("arrive:core %d pick P-state %d\n",assigned_server,P_state_config_temp);
					if(P_state_config_temp<0)
						P_state_config_temp=0;
					
					if(P_state_config_temp<P_state_config)
						P_state_config=P_state_config_temp;
				}
				int DVFS_changed=0;
				if(server[assigned_server].P_state!=P_state_config)
					DVFS_changed=1;
				double cur_remain=(server[assigned_server].time_finished-time);
				double adjusted_remain=cur_remain*freq[server[assigned_server].P_state]/freq[P_state_config]*alpha+cur_remain*(1-alpha);
				if(cur_remain<=0 || adjusted_remain<=0){
					printf("pkt remain time calculation error %f:%f\n",cur_remain,adjusted_remain);
					return 0;
				}
				error("ori remain %f, adj remain %f\n",cur_remain,adjusted_remain);
				server[assigned_server].time_finished=time+adjusted_remain+DVFS_changed*DVFS_latency;
				error("%10.6f\tselect P_state %d for core %d\n",time,P_state_config,assigned_server);
				if(server[assigned_server].P_state!=P_state_config){
					if(server[assigned_server].time_P_state==-1){
						printf("arrive:P state counter error\n");
						return 0;
					}
					server_busy_P_state[assigned_server][server_busy_P_state_counter[assigned_server]]=time-server[assigned_server].time_P_state;
					server_busy_P_state_index[assigned_server][server_busy_P_state_counter[assigned_server]]=server[assigned_server].P_state;
					server_busy_P_state_counter[assigned_server]++;
					server[assigned_server].time_P_state=time;
				}
				server[assigned_server].P_state=P_state_config;
				
			}
			
			// update events
#ifdef read_req_trace // read from trace
			event[0] = req_arrival_time[pkt_index]; // read from trace
#else
			if(ret==0)
				event[0] = time + arrival_length[generate_iat(arrival_count,arrival_cdf)]*1000000; // next pkt arrival time
			else
				event[0] = time + expntl(Ta)*1000000; // next pkt arrival time
#endif		
			event[1] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_finished<event[1] && server[i].time_finished>0)
					event[1]=server[i].time_finished;
			}
			event[2] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_vacation_end<event[2] && server[i].time_vacation_end>0)
					event[2]=server[i].time_vacation_end;
			}
			

		}
		else if (next_event == event[1]){ // *** Event #2 (departure)
			
			time = event[1];
			
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
			
			if(queued_pkt[pick]>0){ // there are pkts in the queue, immediately assign to the server
				//determine the frequency
				// since the core just complete a request, we dont need to consider the conditional pdf
				int P_state_config=14,P_state_config_temp=14;
				double freq_temp;
				double pkt_arrive_time_temp;
				error("%d pkts in the queue of core %d\n",queued_pkt[pick],pick);
				for(i=0;i<queued_pkt[pick];i++){
					pkt_arrive_time_temp=pkts[queue[pick][queue_de_ptr[pick]+i]].time_arrived;
					// sanity check
					if(pkt_arrive_time_temp<0){
						error("pkt arrive time error\n");
						return 0;
					}
					freq_temp=Ts_nf_dvfs*1e6*(i+1)/(LC-DVFS_latency-(time-pkt_arrive_time_temp));
					error("depart:core %d pick freq %f\n",pick,freq_temp);
					for(j=0;j<15;j++){
						if(freq_temp<0){
							P_state_config_temp=0;
							break;
						}else{
							if(freq[j]<freq_temp){
								P_state_config_temp=j-1;
								break;
							}
						}
					}
					error("depart:core %d pick P-state %d\n",pick,P_state_config_temp);
					if(P_state_config_temp<0)
						P_state_config_temp=0;
						
					if(P_state_config_temp<P_state_config)
						P_state_config=P_state_config_temp;
				}
				
				error("%10.6f\tselect P_state %d for core %d\n",time,P_state_config,pick);
				if(server[pick].P_state!=P_state_config){
					if(server[pick].time_P_state==-1){
						printf("depart:P state counter error\n");
						return 0;
					}
					server_busy_P_state[pick][server_busy_P_state_counter[pick]]=time-server[pick].time_P_state;
					server_busy_P_state_index[pick][server_busy_P_state_counter[pick]]=server[pick].P_state;
					server_busy_P_state_counter[pick]++;
					server[pick].time_P_state=time;
				}
				int DVFS_changed=0;
				if(server[pick].P_state!=P_state_config)
					DVFS_changed=1;
				server[pick].P_state=P_state_config;
				
				server[pick].state=1;
				server[pick].cur_pkt_index=queue[pick][queue_de_ptr[pick]];
				pkts[server[pick].cur_pkt_index].time_started=time;
				if(pkts[server[pick].cur_pkt_index].handled!=pick){
					printf("pkt handle does not match\n");
					return 0;
				}
				
				queue_de_ptr[pick]++;
				server[pick].time_finished=time+DVFS_changed*DVFS_latency+pkts[server[pick].cur_pkt_index].service_time*freq[0]/freq[server[pick].P_state]*alpha+pkts[server[pick].cur_pkt_index].service_time*(1-alpha);
				server[pick].time_vacation_end=-1;
				queued_pkt[pick]--;
			} else{ // no queue pkts, server goes to vacation
				server_busy_time[pick][server_busy_counter[pick]]=time-server[pick].time_arrived;
				if(time-server[pick].time_arrived==0){
					printf("!!!time %f\n",time);
				}
				//printf("%d th busy period of server %d\t%f\n",server_busy_counter[pick],pick,server_busy_time[pick][server_busy_counter[pick]]);
				server_busy_counter[pick]++;
				server[pick].state=0;
				server[pick].cur_pkt_index=-1;
				server[pick].time_arrived=time;
				server[pick].time_finished=-1;
				server[pick].time_vacation_end=-1;
				// predict the idle period and select corresponding C-state
				double temp_idle=0;
				int temp_idle_count=0;
				for(i=0;i<8;i++){
					if(past_idle[pick][i]>0){
						temp_idle+=past_idle[pick][i];
						temp_idle_count++;
					}
				}
				temp_idle=temp_idle/temp_idle_count;
				server[pick].sleep_state=0;
				for(i=3;i>0;i--){
					if(temp_idle>C_state_EBT[i]){
						server[pick].sleep_state=i-1;
						break;
					}
						
				}
				server_busy_P_state[pick][server_busy_P_state_counter[pick]]=time-server[pick].time_P_state;
				server_busy_P_state_index[pick][server_busy_P_state_counter[pick]]=server[pick].P_state;
				server_busy_P_state_counter[pick]++;
				server[pick].time_P_state=-1;
				
				// check the package state
				check_package=0;
				for(i=0;i<m;i++){
					check_package=check_package+server[i].state;
				}
				if(check_package==0){ // package idle
					package.time_arrived=time;
				}
			}
			
			// update event
			
			event[1] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_finished<event[1] && server[i].time_finished>0)
					event[1]=server[i].time_finished;
			}
			event[2] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_vacation_end<event[2] && server[i].time_vacation_end>0)
					event[2]=server[i].time_vacation_end;
			}

		} 
		else if (next_event == event[2]){ // *** Event #3 (vacation ends)
			time = event[2];
			
			// find which core has to wake up
			pick=-1;
			for(i=0;i<m;i++){
				if(server[i].time_vacation_end==event[2]){
					pick=i;
					break;
				}
			}
			if(pick<0) {
				printf("wake-up event error\n"); 
				return 0;
			}
			
			//printf("server %d wakeup at time %f\n",pick,time);
			server_idle_time[pick][server_idle_counter[pick]]=time-server[pick].time_arrived;
			// per-sleep-state statistic
			server_per_state_idle_time[pick][server[pick].sleep_state][server_per_state_idle_counter[pick][server[pick].sleep_state]]=time-server[pick].time_arrived;
			server_per_state_idle_counter[pick][server[pick].sleep_state]++;
			// menu governor
			past_idle[pick][past_idle_ptr[pick]]=time-server[pick].time_arrived;
			past_idle_ptr[pick]=(past_idle_ptr[pick]+1)%idle_history;
			server_idle_counter[pick]++;
			server_wakeup_counter[pick]++;
			
			if(queued_pkt[pick]>0){ // there are pkts in the queue, immediately assign to the server
				//determine the frequency
				// since the core is woken up from idle state, we dont need to consider the conditional pdf
				int P_state_config=14,P_state_config_temp=14;
				double freq_temp;
				double pkt_arrive_time_temp;
				error("%d pkts in the queue of core %d\n",queued_pkt[pick],pick);
				for(i=0;i<queued_pkt[pick];i++){
					pkt_arrive_time_temp=pkts[queue[pick][queue_de_ptr[pick]+i]].time_arrived;
					// sanity check
					if(pkt_arrive_time_temp<0){
						error("pkt arrive time error\n");
						return 0;
					}
					freq_temp=Ts_nf_dvfs*1e6*(i+1)/(LC-DVFS_latency-(time-pkt_arrive_time_temp));
					error("wake-up:core %d pick freq %f\n",pick,freq_temp);
					for(j=0;j<15;j++){
						if(freq_temp<0){
							P_state_config_temp=0;
							break;
						}else{
							if(freq[j]<freq_temp){
								P_state_config_temp=j-1;
								break;
							}
						}
					}
					error("wake-up:core %d pick P-state %f\n",pick,P_state_config_temp);
					if(P_state_config_temp<0)
						P_state_config_temp=0;
					if(P_state_config_temp<P_state_config)
						P_state_config=P_state_config_temp;
				}
				int DVFS_changed=0;
				if(server[pick].P_state!=P_state_config)
					DVFS_changed=1;
				error("%10.6f\tselect P_state %d for core %d\n",time,P_state_config,pick);
				if(server[pick].time_P_state!=-1){
					printf("P state counter error\n");
					return 0;
				}
				server[pick].time_P_state=time;
				
				server[pick].P_state=P_state_config;
				server[pick].state=1;
				server[pick].cur_pkt_index=queue[pick][queue_de_ptr[pick]];
				
				if(pkts[server[pick].cur_pkt_index].handled!=pick){
					printf("pkt handle does not match\n");
					return 0;
				}
				queue_de_ptr[pick]++;
				server[pick].time_arrived=time;
				pkts[server[pick].cur_pkt_index].time_started=time;
				server[pick].time_finished=time+DVFS_changed*DVFS_latency+pkts[server[pick].cur_pkt_index].service_time*freq[0]/freq[server[pick].P_state]*alpha+pkts[server[pick].cur_pkt_index].service_time*(1-alpha);
				// if(pkts[server[pick].cur_pkt_index].service_time==0) printf("pkt serviec time 0\n");
				server[pick].time_vacation_end=-1;
				queued_pkt[pick]--;
				// check package state
				if(package.time_arrived>0){
					package.time_finished=time;
					package_idle_time[package_idle_counter]=package.time_finished-package.time_arrived;
					package.time_arrived=-1;
					package_idle_counter++;
				}
			} else{ // no queue pkts, server goes to vacation (should not happen)
				error("core wake up and finds no pkt in the queue\n");
				return 0;
			}
			
			// update event			
			event[1] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_finished<event[1] && server[i].time_finished>0)
					event[1]=server[i].time_finished;
			}
			event[2] = SIM_TIME+1;
			for(i=0;i<m;i++){
				if(server[i].time_vacation_end<event[2] && server[i].time_vacation_end>0)
					event[2]=server[i].time_vacation_end;
			}
		
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
			server_idle_counter[i]++;
			server_per_state_idle_time[i][server[i].sleep_state][server_per_state_idle_counter[i][server[i].sleep_state]]=time-server[i].time_arrived;
			server_per_state_idle_counter[i][server[i].sleep_state]++;
		}else{
			server_busy_time[i][server_busy_counter[i]]=time-server[i].time_arrived;
			server_busy_counter[i]++;
			server_busy_P_state[i][server_busy_P_state_counter[i]]=time-server[i].time_P_state;
			server_busy_P_state_index[i][server_busy_P_state_counter[i]]=server[pick].P_state;
		}
		
	}
	if(package.time_arrived>0){
		package.time_finished=time;
		package_idle_time[package_idle_counter]=package.time_finished-package.time_arrived;
		package.time_arrived=-1;
		package_idle_counter++;
	}
	
	/*******************************/
	/*post processing the statistic*/
	/*******************************/
	
	// P_states
	double **P_state_time = malloc(server_count * sizeof *server_idle_time);
	for(kk=0;kk<server_count;kk++){
		P_state_time[kk] = malloc(15 * sizeof *P_state_time[kk]);
		for(jj=0;jj<15;jj++){
			P_state_time[kk][jj]=0;
		}
	}
	server_busy_P_state[pick][server_busy_P_state_counter[pick]]=time-server[pick].time_P_state;
	server_busy_P_state_index[pick][server_busy_P_state_counter[pick]]=server[pick].P_state;
				
	for(i=0;i<q_len;i++){
		for(j=0;j<m;j++){
			//printf("server %d, %d th busy period : %f\n",j,i,server_busy_time[j][i]);
			if(i<server_busy_P_state_counter[j] && server_busy_P_state[j][i]>0) {
				//printf("server %d, %d th busy period : %f\n",j,i,server_busy_time[j][i]);
				P_state_time[j][server_busy_P_state_index[j][i]]+=server_busy_P_state[j][i];
			} else{
				if(i<server_busy_P_state_counter[j] && server_busy_P_state[j][i]<0){
					printf("something wrong with the P_state counter\n");
					printf("core %d, %d th busy period : %f\n",j,i,server_busy_P_state[j][i]);
				}
			}
			
		}
		
	}
	double total_P_state_time[server_count]={0};
	double total_P_state_ratio[server_count]={0};
	for(i=0;i<server_count;i++){
		for(j=0;j<15;j++){
			total_P_state_time[i]+=P_state_time[i][j];
		}
		total_P_state_ratio[i]=total_P_state_time[i]/time;
		// printf("%f\n",total_P_state_ratio[i]);
	}
	double per_P_state_time[15]={0};
	for(i=0;i<server_count;i++){
		for(j=0;j<15;j++){
			per_P_state_time[j]+=P_state_time[i][j];
		}
	}
	double overall_P_state_time=0;
	for(j=0;j<15;j++){
		if(!quiet){
			printf("P_state %d\t%f\t%f\n",j,per_P_state_time[j]/time/m,(Pa_static*voltage[j]+Pa_dyn*voltage[j]*voltage[j]*freq[j])*per_P_state_time[j]);
		}
		overall_P_state_time+=per_P_state_time[j];
	}
	if(!quiet){
		printf("total P_state\t%f\n",overall_P_state_time/time/m);
	}
	
	double total_P_state_energy[server_count]={0};
	double total_P_state_power[server_count]={0};
	for(i=0;i<server_count;i++){
		for(j=0;j<15;j++){
			total_P_state_energy[i]+=(Pa_static*voltage[j]+Pa_dyn*voltage[j]*voltage[j]*freq[j])*P_state_time[i][j];
		}
		total_P_state_power[i]=total_P_state_energy[i]/total_P_state_time[i];
	}
	double overall_P_state_power=0;
	for(i=0;i<server_count;i++){
		overall_P_state_power+=total_P_state_power[i];
	}
	
	double idle_period_energy[4]={0};
	double avg_busy[server_count]={0};
	double avg_idle[server_count]={0};
	double **avg_idle_per_state = malloc(server_count * sizeof *avg_idle_per_state);
	for(kk=0;kk<server_count;kk++){
		avg_idle_per_state[kk]=malloc(3 * sizeof *avg_idle_per_state[kk]);
		for(jj=0;jj<3;jj++){
			avg_idle_per_state[kk][jj]= 0;
		}
	}
	double avg_wakeup[server_count]={0};
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
			
		}
		
	}
	for(j=0;j<m;j++){
		for(kk=0;kk<3;kk++){
			for(i=0;i<q_len;i++){
				if(i<server_per_state_idle_counter[j][kk] && server_per_state_idle_time[j][kk][i]>0){
					if(server_per_state_idle_time[j][kk][i]-C_state_wakeup_latency[kk+1]*1.0<-0.0001 && i<server_per_state_idle_counter[j][kk]-1){
						printf("idle period error %d\t%d\t%f\t%d\n",i,server_per_state_idle_counter[j][kk],server_per_state_idle_time[j][kk][i],C_state_wakeup_latency[kk+1]);
						// return 0;
					}
					avg_idle_per_state[j][kk]=avg_idle_per_state[j][kk]+server_per_state_idle_time[j][kk][i]-C_state_wakeup_latency[kk+1];
					avg_wakeup[j]+=C_state_wakeup_latency[kk+1];
				}else{
					if(i<server_per_state_idle_counter[j][kk] && server_per_state_idle_time[j][kk][i]<0)
						printf("something wrong with the idle period counter\n");
				}
			}
		}
	}
	double Pidle[server_count]={0};
	for(j=0;j<m;j++){
		for(kk=0;kk<3;kk++){
			avg_idle[j]+=avg_idle_per_state[j][kk];
			Pidle[j]+=avg_idle_per_state[j][kk]*C_state_power[kk+1];
		}
		if(avg_idle[j]==0)
			Pidle[j]=0;
		else
			Pidle[j]=Pidle[j]/avg_idle[j];
	}
	
	double busy_ratio[server_count]={0};
	double idle_ratio[server_count]={0};
	double wakeup_ratio[server_count]={0};
	double overall_busy=0,overall_idle=0,overall_wakeup=0;
	double overall_busy_ratio=0,overall_idle_ratio=0,overall_wakeup_ratio=0;
	double overall_idle_power=0;
	
	for(j=0;j<m;j++){
		busy_ratio[j]=avg_busy[j]/(avg_busy[j]+avg_idle[j]+avg_wakeup[j]);
		idle_ratio[j]=avg_idle[j]/(avg_busy[j]+avg_idle[j]+avg_wakeup[j]);
		wakeup_ratio[j]=avg_wakeup[j]/(avg_busy[j]+avg_idle[j]+avg_wakeup[j]);
		overall_busy+=avg_busy[j];
		overall_idle+=avg_idle[j];
		overall_wakeup+=avg_wakeup[j];
		overall_idle_power+=Pidle[j]*avg_idle[j];
		
		avg_busy[j]=avg_busy[j]/server_busy_counter[j];
		avg_idle[j]=avg_idle[j]/server_idle_counter[j];
	}
	overall_busy_ratio=overall_busy/(overall_busy+overall_idle+overall_wakeup);
	overall_idle_ratio=overall_idle/(overall_busy+overall_idle+overall_wakeup);
	overall_wakeup_ratio=overall_wakeup/(overall_busy+overall_idle+overall_wakeup);
	overall_idle_power=overall_idle_power/overall_idle;
	int pkt_processed=0,pkt_in_server=0,pkt_in_queue=0;
	for(j=0;j<m;j++){
		pkt_processed+=server_pkts_counter[j];
		pkt_in_queue+=queued_pkt[j];
		pkt_in_server+=server[j].state;
	}
	if(!quiet){
		printf("\n*********Result*********\n\n");
		printf("total pkts arrived %d\n",pkt_index);
		printf("total pkts processed %d\n",pkt_processed);
		printf("total pkts in queue %d\n",pkt_in_queue);
		printf("total pkts in servers %d\n",pkt_in_server);
	}
	
	double latency[server_count]={0};
	double overall_latency=0;
	double avg_wakeup_count=0;
	for(j=0;j<pkt_index;j++){
		if(pkts[j].time_finished>0){
			if(pkts[j].time_finished-pkts[j].time_arrived<0)
				printf("pkt process latency error\n");
			else
				latency[pkts[j].handled]+=pkts[j].time_finished-pkts[j].time_arrived;
		}
	}
	for(j=0;j<m;j++){
		avg_wakeup_count+=server_wakeup_counter[j];
		overall_latency+=latency[j];
		latency[j]=latency[j]/server_pkts_counter[j];
	}
	double overall_package_idle=0,overall_package_transistion=0;
	if(package_sleep > 0){ // package sleep enabled
	
		for(j=0;j<package_idle_counter;j++){
			if(package_idle_time[j] < package_wakeup_latency){
				// printf("package idle time %d error %f\n",j,package_idle_time[j]);
				// return 0;
			}else{
				overall_package_idle=overall_package_idle+(package_idle_time[j]-package_wakeup_latency);
				overall_package_transistion+=package_wakeup_latency;
			}
		}
	}
	//printf("package idle ratio %f\n",overall_package_idle/time);
	if(!quiet){
		printf("total time %f\n",time);
		printf("average latency %f\n",overall_latency/pkt_processed);
		printf("average busy ratio %f\n",overall_busy_ratio);
		printf("average idle ratio %f\n",overall_idle_ratio);
		printf("average wakeup ratio %f\n",overall_wakeup_ratio);
		printf("average wakeup count %f\n",avg_wakeup_count/m);
		printf("average core power %f\n",overall_idle_power*overall_idle_ratio+Pmax*overall_wakeup_ratio+overall_P_state_power/m*overall_busy_ratio); 
		printf("average active power %f\n",overall_P_state_power/m*overall_busy_ratio);
		printf("average state transition power %f\n",Pmax*overall_wakeup_ratio);
		printf("average idle power %f\n",overall_idle_power*overall_idle_ratio);
		if(package_sleep > 0){ // package sleep enabled
			printf("average package power %f\n",S*(1-overall_package_idle/time));
			printf("average package transition power %f\n",S*(overall_package_transistion/time));
		}
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
	// printf("%d\t%.2f\t%f\t%f\t%f\t%d\t%d\t%d\tC%d\n",m,p,Pa*overall_wakeup_ratio*m+overall_P_state_power*overall_busy_ratio+Pc*overall_idle_ratio*m,S*(1-overall_package_idle/time),overall_latency/pkt_processed,nfp,nnp,LC,C_state);
	printf("%d\t%.2f\t%f\t%f\t%f\t%d\t%d\t%d\n",m,p,(overall_idle_power*overall_idle_ratio+Pmax*overall_wakeup_ratio+overall_P_state_power/m*overall_busy_ratio)*m,S*(1-overall_package_idle/time),overall_latency/pkt_processed,nfp,nnp,LC);
	// for (j=0;j<latency_bound+1;j++)
		// hist_array[j]=0;
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
	service_pdf = malloc(line_count*sizeof(double));
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
			service_pdf[i]=service_cdf[i];
		else 
			service_pdf[i]=service_cdf[i]-service_cdf[i-1];
		check=check+service_pdf[i];
		average=average+service_length[i]*service_pdf[i];
		// printf("%lf\n",pdf[i]);
	}
	*avg_service_time=average;
	service_tail=pdf_to_tail(service_pdf,line_count)*1e6*unit;
	
	
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

void convolve(double* A,int A_len,double* B,int B_len,double* output)
{
	int i,j;
	unsigned int lengthOfOutput = A_len + B_len - 1;
	// output = malloc(lengthOfOutput*sizeof(double));
 
	for(i = 0; i < lengthOfOutput; i++){
		output[i] = 0;
	}
 
	for(i = 0; i < A_len; ++i){
		for(j = 0; j < B_len; ++j){
			output[i+j] += A[i] * B[j];
		}
	}
}

int pdf_to_tail(double* input, int length){
	double temp=0;
	for(int i=0;i<length;i++){
		temp=temp+input[i];
		// printf("%f\n",temp);
		if(temp>0.95) {
			return i;
		
		}
	}

}

void compute_conditional_pdf(double condition,double* ret_pdf, int* ret_length){
	for(int i=0;i<service_count;i++){
		ret_pdf[i]=0;
	}
	int counter=0;
	int cond_ceil=ceil(condition);
	double scale=0;
	for(int i=cond_ceil;i<service_count;i++){
		scale+=service_pdf[i];
		ret_pdf[counter]=service_pdf[i];
		counter++;
	}
	for(int i=0;i<counter;i++){
		ret_pdf[i]=ret_pdf[i]*1/scale;
	}
	*ret_length=service_count-cond_ceil;


}
