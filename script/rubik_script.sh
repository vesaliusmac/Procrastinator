load_step=0.1;
load=0.1;
loadbound=1.0;
while [ "$(echo "${load} < ${loadbound}" | bc)" -eq 1 ];
do
	/home/chou/HPCA2017/GGk_rubik 0 $load 12 $1; 
	load=$(echo "$load+$load_step" | bc);
done
