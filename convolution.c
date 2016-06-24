#include <stdio.h> 
#include <stdlib.h> // Needed for rand() and RAND_MAX
#include <math.h> // Needed for log()
#include <time.h>

#define FILE_SERVICE "/home/chou/datacenter/trace/memcached_service_cdf_v3.log"

double* service_length;
double* service_cdf;
double* service_pdf;
int service_count;
double **out;
void read_dist(void){
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
	// double *pdf = malloc(line_count*sizeof(double));
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
	// *avg_service_time=average;
	
	
}

void Convolve(double* input,int inputLength,double* filter,int filterLength,double* output)
{
	int i,j;
	unsigned int lengthOfOutput = inputLength + filterLength - 1;
	// output = malloc(lengthOfOutput*sizeof(double));
 
	for(i = 0; i < lengthOfOutput; i++){
		output[i] = 0;
	}
 
	for(i = 0; i < inputLength; ++i){
		for(j = 0; j < filterLength; ++j){
			output[i+j] += input[i] * filter[j];
		}
	}
}

int pdf_to_nfth(double* input, int length){
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

int main(){
	double a[6]={0,0.1,0.2,0.3,0.2,0.2};
	double b[12]={0,0.05,0.1,0.15,0.2,0.25,0.3,0.25,0.2,0.2,0.2,0.2};
	
	double c[20]={0};
	double temp;
	for(int i=0;i<12;i++){
		temp+=b[i];
	}
	for(int i=0;i<12;i++){
		b[i]=b[i]/temp;
	}
	Convolve(a,6,a,6,c);
	for(int i=0;i<20;i++){
		printf("%d\t%f\n",i,c[i]);
	}
	// read_dist();
	// double **out = malloc(20 * sizeof *out);
	// for(int kk=0;kk<20;kk++){
		// out[kk] = malloc(20*service_count * sizeof *out[kk]);
		
	// }
	// int nf;
	// nf=pdf_to_nfth(service_pdf,service_count);
	// printf("%d\n",nf);
	// Convolve(service_pdf,service_count,service_pdf,service_count,out[0]);
	// nf=pdf_to_nfth(out[0],2*service_count-1);
	// printf("%d\n",nf);
	// for(int i=0;i<18;i++){
		// Convolve(out[i],(i+2)*service_count-(i+1),service_pdf,service_count,out[i+1]);
		// nf=pdf_to_nfth(out[i+1],(i+3)*service_count-(i+2));
		// printf("%d\n",nf);
	// }
	
	// for(int i=0;i<service_count;i++){
		// printf("%d\t%.9f\n",i,service_pdf[i]);
	// }
	// double *cond_pdf=malloc(service_count * sizeof(double));
	// int ret_length;
	// compute_conditional_pdf(10,cond_pdf, &ret_length);
	// for(int i=0;i<ret_length;i++){
		// printf("%d\t%.9f\n",i,cond_pdf[i]);
	// }
	// printf("here");



}

