load_step=0.1;
load=0.1;
loadbound=1.0;
while [ "$(echo "${load} < ${loadbound}" | bc)" -eq 1 ];
do
	./GGk_sleepscale $load 12 1 $1;
	load=$(echo "$load+$load_step" | bc);
done
