load_step=0.1;
load=0.1;
loadbound=1.0;
while [ "$(echo "${load} < ${loadbound}" | bc)" -eq 1 ];
do
	./GGk_default 0 $load 12; 
	load=$(echo "$load+$load_step" | bc);
done
