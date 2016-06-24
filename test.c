#include <stdio.h> 
#include <stdlib.h> // Needed for rand() and RAND_MAX
#include <math.h> // Needed for log()
#include <time.h>

int **out;

int main(){
	
	int **out = malloc(5 * sizeof *out);
	for(int kk=0;kk<5;kk++){
		out[kk] = malloc(2 * sizeof *out[kk]);		
	}
	int a=0;
	for(int kk=0;kk<5;kk++){
		for(int jj=0;jj<2;jj++){
			out[kk][jj] = a;
			a++;
		}
	}
	for(int kk=0;kk<5;kk++){
		for(int jj=0;jj<2;jj++){
			printf("%d\n",out[kk][jj]);
		}
	}
}