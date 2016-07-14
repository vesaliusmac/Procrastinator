#include <stdio.h> 
#include <stdlib.h> // Needed for rand() and RAND_MAX
#include <math.h> // Needed for log()
#include <time.h>
#include <stdarg.h>
#include "arrival.h"
#include <string.h>



// ivybridge
#ifdef ivy
#define server_count 4 //no of servers
double freq[13]={3.4,3.3,3.2,3,2.9,2.8,2.6,2.4,2.2,2.1,2,1.7,1.6};
double voltage[13]={1.070800781,1.045776367,1.025756836,0.990722656,0.975708008,0.955688477,0.955688477,0.955688477,0.950683594,0.950683594,0.950683594,0.945678711,0.945678711};
// Pa = 0.708255*v+1.474418*v^2*f
double Pa_dyn=1.474418, Pa_static=0.708255;
double S_dyn=1.0747, S_static=0.9002;
#define freq_steps 13
#else
// xeonphi
#define server_count 12 //no of servers
double freq[15]={2.7,2.5,2.4,2.3,2.2,2.1,2,1.9,1.8,1.7,1.6,1.5,1.4,1.3,1.2};
double voltage[15]={0.990722656,0.970703125,0.960693359,0.955688477,0.945678711,0.940673828,0.935668945,0.92565918,0.920654297,0.910644531,0.900634766,0.895629883,0.890625,0.885620117,0.875610352};
// Pa = 0.2261*v+1.2208*v^2*f
double Pa_dyn=1.2208, Pa_static=0.2261;
double S_dyn=4.8233, S_static=2.6099;
#define freq_steps 15
#endif

void main(int argc, char **argv){
	srand(time(NULL));
	/****************/
	/*input argument*/
	/****************/
		
	if(argc<5) {
		printf("use: [load] [core count] [C-state] [latency constraint]\n");
		return;
	}
	
	
	float p=atof(argv[1]);  // traffic load
	int m=atoi(argv[2]);    // how many active cores
	m=server_count; // do not use core parking
	int C_state = atoi(argv[3]); // which C-state a core will enter during idle (C1,C2,C3 -> C1,C3,C6)
	if(C_state<1 || C_state>3) {
		printf("choose C-state between C1 ~ C3\n");
		return;
	}
	int LC=atoi(argv[4]);  //latency constraint
	FILE *fp;
	char command[100];
	char path[1035];
	int a,b,c;
	float d,e,f,g;
	int best_config_c_state=-1;
	int best_config_freq=-1;
	float min_power=9999;
	int best_tail=-1;
	for(int i=1;i<4;i++){	//sweep the c-state
		for(int j=0;j<freq_steps;j++){ // sweep the frequecny
			
			sprintf(command, "./sleepscale_per_config %d %.1f %d %d %d",j,p,m,i,LC);
			fp = popen(command, "r");
			//while (fgets(path, sizeof(path)-1, fp) != NULL) {
			fscanf(fp,"%d%f%f%f%f%d%d",&a,&d,&e,&f,&g,&b,&c);
				// printf("%f\t%d\n",e,b);
				if(b<LC){
					if(e<min_power){
						min_power=e;
						best_config_c_state=i;
						best_config_freq=j;
						best_tail=b;
					}
				}
			
			// }
			/* close */
			pclose(fp);
		}
	}
	if(best_tail>0)
		printf("%.1f\t%d\t%d\t%f\t%d\n",p,best_config_c_state,best_config_freq,min_power,best_tail);
	else{
		sprintf(command, "./sleepscale_per_config 0 %.1f %d 1 %d",p,m,LC);
		fp = popen(command, "r");
		//while (fgets(path, sizeof(path)-1, fp) != NULL) {
		fscanf(fp,"%d%f%f%f%f%d%d",&a,&d,&e,&f,&g,&b,&c);
		pclose(fp);
		printf("nono %.1f\t%d\t%d\t%f\t%d\n",p,1,0,e,b);
		
	}
	

}